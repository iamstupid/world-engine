/* World Engine 统一工作台骨架（docs/PIPELINE_DAG_DESIGN.md）。
   Pipeline 面板是原生 React Flow；旧地形/RP 工作室以 iframe 面板进驻
   dock（渐进迁移），后续逐面板原生化。 */

import { DockviewReact, DockviewReadyEvent, IDockviewPanelProps } from "dockview";
import "dockview/dist/styles/dockview.css";
import React, { useEffect, useState } from "react";
import PipelinePanel from "./PipelinePanel";

function Frame(props: { src: string }) {
  return (
    <iframe
      src={props.src}
      style={{ width: "100%", height: "100%", border: 0, background: "#0a0c10" }}
    />
  );
}

const components = {
  pipeline: (p: IDockviewPanelProps<{ sid: string }>) => (
    <PipelinePanel sid={p.params.sid} />
  ),
  studio: () => <Frame src="/" />,
  rp: () => <Frame src="/rp" />,
};

export default function App() {
  const [sid, setSid] = useState<string | null>(null);

  useEffect(() => {
    fetch("/api/sessions", { method: "POST" })
      .then((r) => r.json())
      .then((d) => setSid(d.id));
  }, []);

  const onReady = (event: DockviewReadyEvent) => {
    const wait = () => {
      if (!sidRef.current) return void setTimeout(wait, 100);
      event.api.addPanel({
        id: "pipeline", component: "pipeline", title: "管线节点图",
        params: { sid: sidRef.current },
      });
      event.api.addPanel({
        id: "studio", component: "studio", title: "地形工作室",
        position: { direction: "right" },
      });
      event.api.addPanel({
        id: "rp", component: "rp", title: "RP 工作室",
        position: { referencePanel: "studio", direction: "within" },
      });
      event.api.getPanel("studio")?.api.setActive();
      event.api.getPanel("pipeline")?.api.setActive();
    };
    wait();
  };

  const sidRef = React.useRef<string | null>(null);
  sidRef.current = sid;

  return (
    <div style={{ height: "100%", display: "flex", flexDirection: "column" }}>
      <div style={{ padding: "6px 14px", background: "#1c1f27",
                    borderBottom: "1px solid #2a2e3a", display: "flex",
                    gap: 16, alignItems: "center" }}>
        <b style={{ color: "#8fd0ff" }}>World Engine 工作台</b>
        <span id="session-badge" style={{ color: "#6f7890" }}>
          {sid ? `会话 ${sid}` : "连接中…"}
        </span>
      </div>
      <div style={{ flex: 1, minHeight: 0 }} className="dockview-theme-dark">
        <DockviewReact components={components} onReady={onReady} />
      </div>
    </div>
  );
}
