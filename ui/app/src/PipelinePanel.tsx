/* 管线节点图面板：消费 /api/graph/schema 注册表，硬频率校验，
   校验失败且为频率不匹配时提供一键“插入 resample”修复。 */

import {
  Background, Controls, Edge, Handle, Node, NodeProps, Position,
  ReactFlow, ReactFlowProvider,
} from "@xyflow/react";
import "@xyflow/react/dist/style.css";
import React, { useCallback, useEffect, useMemo, useState } from "react";

type Spec = { nodes: SpecNode[] };
type SpecNode = { id: string; type: string; params?: Record<string, unknown>;
                  inputs?: Record<string, string> };
type Registry = Record<string, { inputs: Record<string, string>;
                                 outputs: string[];
                                 params: Record<string, unknown>; doc: string }>;

function GraphNode({ data }: NodeProps) {
  const d = data as { label: string; type: string; inputs: string[];
                      outputs: string[]; selected?: boolean };
  return (
    <div style={{ background: "#1d212b", border: "1px solid #333a4c",
                  borderRadius: 8, minWidth: 150, fontSize: 12 }}>
      <div style={{ padding: "4px 8px", background: "#232a3c",
                    borderRadius: "8px 8px 0 0", color: "#8fd0ff" }}>
        {d.label} <span style={{ color: "#6f7890" }}>({d.type})</span>
      </div>
      <div style={{ display: "flex", justifyContent: "space-between",
                    padding: "6px 8px" }}>
        <div>
          {d.inputs.map((s, i) => (
            <div key={s} style={{ position: "relative", height: 18 }}>
              <Handle type="target" position={Position.Left} id={s}
                      style={{ top: 9 }} />
              <span style={{ marginLeft: 6, color: "#9aa3b5" }}>{s}</span>
            </div>
          ))}
        </div>
        <div style={{ textAlign: "right" }}>
          {d.outputs.map((s) => (
            <div key={s} style={{ position: "relative", height: 18 }}>
              <span style={{ marginRight: 6, color: "#9aa3b5" }}>{s}</span>
              <Handle type="source" position={Position.Right} id={s}
                      style={{ top: 9 }} />
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}

const nodeTypes = { we: GraphNode };

export default function PipelinePanel({ sid }: { sid: string }) {
  const [registry, setRegistry] = useState<Registry>({});
  const [spec, setSpec] = useState<Spec>({ nodes: [] });
  const [selected, setSelected] = useState<string | null>(null);
  const [status, setStatus] = useState("载入中…");
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    Promise.all([
      fetch("/api/graph/schema").then((r) => r.json()),
      fetch(`/api/sessions/${sid}/graph`).then((r) => r.json()),
    ]).then(([sch, sp]) => {
      setRegistry(sch.nodes);
      setSpec(sp);
      setStatus("就绪");
    });
  }, [sid]);

  const validate = useCallback(async (next: Spec) => {
    const r = await fetch(`/api/sessions/${sid}/graph/validate`, {
      method: "POST", headers: { "Content-Type": "application/json" },
      body: JSON.stringify(next),
    }).then((x) => x.json());
    setError(r.ok ? null : r.error);
    return r.ok;
  }, [sid]);

  // layered auto-layout by topological depth
  const { nodes, edges } = useMemo(() => {
    const depth: Record<string, number> = {};
    const calc = (id: string): number => {
      if (id in depth) return depth[id];
      const n = spec.nodes.find((x) => x.id === id);
      const ups = Object.values(n?.inputs ?? {}).map((r) => r.split(".")[0]);
      depth[id] = ups.length ? Math.max(...ups.map(calc)) + 1 : 0;
      return depth[id];
    };
    spec.nodes.forEach((n) => calc(n.id));
    const perCol: Record<number, number> = {};
    const nodes: Node[] = spec.nodes.map((n) => {
      const col = depth[n.id] ?? 0;
      const row = (perCol[col] = (perCol[col] ?? 0) + 1) - 1;
      const reg = registry[n.type];
      return {
        id: n.id, type: "we",
        position: { x: col * 230 + 20, y: row * 120 + 20 },
        data: { label: n.id, type: n.type,
                inputs: Object.keys(reg?.inputs ?? {}),
                outputs: reg?.outputs ?? [] },
        selected: selected === n.id,
      };
    });
    const edges: Edge[] = [];
    for (const n of spec.nodes) {
      for (const [slot, ref] of Object.entries(n.inputs ?? {})) {
        const [up, out] = ref.split(".");
        edges.push({ id: `${up}.${out}->${n.id}.${slot}`, source: up,
                     sourceHandle: out, target: n.id, targetHandle: slot,
                     animated: true });
      }
    }
    return { nodes, edges };
  }, [spec, registry, selected]);

  const onConnect = useCallback((c: {
    source: string | null; sourceHandle: string | null;
    target: string | null; targetHandle: string | null;
  }) => {
    if (!c.source || !c.target || !c.targetHandle) return;
    const next: Spec = { nodes: spec.nodes.map((n) => n.id === c.target
      ? { ...n, inputs: { ...(n.inputs ?? {}),
          [c.targetHandle!]: `${c.source}.${c.sourceHandle ?? "out"}` } }
      : n) };
    validate(next).then((ok) => { if (ok) setSpec(next); else setSpec(next); });
  }, [spec, validate]);

  const addNode = (type: string) => {
    let k = 1;
    while (spec.nodes.some((n) => n.id === `${type}${k}`)) k += 1;
    const next = { nodes: [...spec.nodes, { id: `${type}${k}`, type,
                                            params: {}, inputs: {} }] };
    setSpec(next);
    setSelected(`${type}${k}`);
  };

  const fixWithResample = async () => {
    // parse "{nid}: 频率不匹配 a@F.. vs b@F.." and rewire b through resample
    const m = error?.match(/^(\S+): 频率不匹配 a@F(\d+) vs b@F(\d+)/);
    if (!m) return;
    const [, nid, fa] = m;
    const node = spec.nodes.find((n) => n.id === nid);
    if (!node?.inputs?.b) return;
    let k = 1;
    while (spec.nodes.some((n) => n.id === `resample${k}`)) k += 1;
    const rid = `resample${k}`;
    const next: Spec = { nodes: [
      ...spec.nodes.map((n) => n.id === nid
        ? { ...n, inputs: { ...n.inputs, b: `${rid}.out` } } : n),
      { id: rid, type: "resample", params: { frequency: Number(fa) },
        inputs: { src: node.inputs.b } },
    ] };
    if (await validate(next)) setSpec(next);
  };

  const run = async () => {
    if (!(await validate(spec))) return;
    setStatus("运行中…");
    await fetch(`/api/sessions/${sid}/graph/run`, {
      method: "POST", headers: { "Content-Type": "application/json" },
      body: JSON.stringify(spec),
    });
    const es = new EventSource(`/api/sessions/${sid}/progress`);
    es.onmessage = (ev) => {
      const e = JSON.parse(ev.data);
      if (e.type === "progress") setStatus(`${e.stage} (${e.index}/${e.total})`);
      if (e.type === "done") { setStatus(`完成，重算 ${e.evaluated.length} 节点`); es.close(); }
      if (e.type === "error") { setStatus("失败"); setError(e.message); es.close(); }
    };
  };

  const sel = spec.nodes.find((n) => n.id === selected);
  const selReg = sel ? registry[sel.type] : null;

  return (
    <div style={{ display: "flex", height: "100%" }}>
      <div style={{ flex: 1, position: "relative" }} data-testid="flow-canvas">
        <ReactFlowProvider>
          <ReactFlow nodes={nodes} edges={edges} nodeTypes={nodeTypes}
                     onConnect={onConnect} fitView
                     onNodeClick={(_, n) => setSelected(n.id)}>
            <Background color="#232734" />
            <Controls />
          </ReactFlow>
        </ReactFlowProvider>
      </div>
      <div style={{ width: 250, borderLeft: "1px solid #2a2e3a",
                    padding: 10, overflowY: "auto", fontSize: 12 }}>
        <div style={{ marginBottom: 8 }}>
          <button onClick={run} style={btn(true)} data-testid="run-graph">
            ▶ 运行图
          </button>
          <span style={{ marginLeft: 8, color: "#9aa3b5" }}>{status}</span>
        </div>
        {error && (
          <div style={{ background: "#3a1d24", padding: 8, borderRadius: 6,
                        marginBottom: 8 }} data-testid="graph-error">
            {error}
            {error.includes("频率不匹配") && (
              <button onClick={fixWithResample} style={{ ...btn(false),
                      display: "block", marginTop: 6 }}
                      data-testid="fix-resample">
                ⚡ 插入 resample 修复
              </button>
            )}
          </div>
        )}
        <div style={{ marginBottom: 8 }}>
          {Object.keys(registry).map((t) => (
            <button key={t} onClick={() => addNode(t)}
                    style={{ ...btn(false), margin: 2 }}>+{t}</button>
          ))}
        </div>
        {sel && selReg && (
          <div data-testid="inspector">
            <b style={{ color: "#8fd0ff" }}>{sel.id}</b>
            <div style={{ color: "#6f7890", margin: "4px 0" }}>{selReg.doc}</div>
            {Object.entries({ ...selReg.params, ...(sel.params ?? {}) })
              .map(([key, val]) => (
              <div key={key} style={{ margin: "4px 0" }}>
                <label style={{ display: "block", color: "#9aa3b5" }}>{key}</label>
                <input
                  defaultValue={String(val)}
                  style={{ width: "100%", background: "#232734",
                           border: "1px solid #333a4c", color: "#d8dbe2",
                           borderRadius: 4, padding: "3px 6px" }}
                  onBlur={(e) => {
                    const raw = e.target.value;
                    const num = Number(raw);
                    const next = { nodes: spec.nodes.map((n) => n.id === sel.id
                      ? { ...n, params: { ...(n.params ?? {}),
                          [key]: Number.isNaN(num) ? raw : num } } : n) };
                    setSpec(next);
                    validate(next);
                  }}
                />
              </div>
            ))}
          </div>
        )}
      </div>
    </div>
  );
}

const btn = (primary: boolean): React.CSSProperties => ({
  background: primary ? "#2b5f9e" : "#2b3040", color: "#d8dbe2",
  border: "1px solid #333a4c", borderRadius: 5, padding: "4px 10px",
  cursor: "pointer", font: "inherit",
});
