/* WorldEngine Studio client (plan M8/M10/M11):
   schema-driven parameter forms, SSE generation progress, layer textures with
   client-side colormapping, three.js globe with vertex displacement, 2D map
   with point query, vector overlays (rivers/settlements/borders/roads),
   uplift paint brush. No build step; three.js vendored. */

"use strict";

const api = {
  async json(url, opts) {
    const r = await fetch(url, opts);
    if (!r.ok) throw new Error(`${url}: ${r.status}`);
    return r.json();
  },
  async layer(sid, name) {
    const r = await fetch(`/api/sessions/${sid}/layer/${name}`);
    if (!r.ok) throw new Error(`layer ${name}: ${r.status}`);
    const dtype = r.headers.get("X-Dtype");
    const w = parseInt(r.headers.get("X-Width"));
    const h = parseInt(r.headers.get("X-Height"));
    const buf = await r.arrayBuffer();
    const arr = dtype === "f32" ? new Float32Array(buf)
      : dtype === "i32" ? new Int32Array(buf) : new Uint8Array(buf);
    return { arr, w, h, dtype };
  },
};

const state = {
  sid: null,
  layers: {},        // name -> {arr,w,h,dtype}
  layerNames: [],
  current: "elevation_eroded_m",
  overlay: "",
  features: null,
  view: "globe",
  brush: { active: false, size: 30, strokes: [] },
};

// ---------------- parameter form ----------------

const UI_DEFAULTS = {
  "world.width": 1024, "world.height": 512,
  "world.physics_grid_frequency": 176,
  "tectonics.grid_frequency": 100,
};

async function buildParamsForm() {
  const schema = await api.json("/api/schema");
  const host = document.getElementById("params");
  host.innerHTML = "";
  for (const [group, fields] of Object.entries(schema.groups)) {
    const details = document.createElement("details");
    details.className = "group";
    details.open = group === "world" || group === "tectonics";
    const summary = document.createElement("summary");
    summary.textContent = group;
    details.appendChild(summary);
    for (const f of fields) {
      const key = `${group}.${f.name}`;
      const row = document.createElement("div");
      row.className = "prow";
      const label = document.createElement("label");
      label.textContent = f.name;
      label.title = `${f.desc} [${f.min} .. ${f.max}]`;
      const input = document.createElement("input");
      input.type = "number";
      input.step = f.type === "int" ? "1" : "any";
      input.value = key in UI_DEFAULTS ? UI_DEFAULTS[key] : f.default;
      input.dataset.key = key;
      input.addEventListener("change", async () => {
        await api.json(`/api/sessions/${state.sid}/params`, {
          method: "PUT", headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ [key]: parseFloat(input.value) }),
        });
      });
      row.appendChild(label);
      row.appendChild(input);
      details.appendChild(row);
    }
    host.appendChild(details);
  }
}

async function pushAllParams() {
  const body = {};
  document.querySelectorAll("#params input").forEach((el) => {
    body[el.dataset.key] = parseFloat(el.value);
  });
  await api.json(`/api/sessions/${state.sid}/params`, {
    method: "PUT", headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
}

// ---------------- colormaps ----------------

function lerp(a, b, t) { return a + (b - a) * t; }

function hypsometric(z) {
  const stops = z < 0
    ? [[-6000, [5, 15, 80]], [-200, [30, 80, 180]], [0, [70, 150, 245]]]
    : [[0, [20, 100, 30]], [200, [50, 170, 60]], [800, [220, 200, 40]],
       [2000, [210, 120, 30]], [4000, [190, 50, 30]], [6000, [140, 60, 170]],
       [9000, [255, 255, 255]]];
  let lo = stops[0], hi = stops[stops.length - 1];
  for (let i = 0; i + 1 < stops.length; i++) {
    if (z >= stops[i][0] && z <= stops[i + 1][0]) { lo = stops[i]; hi = stops[i + 1]; break; }
  }
  const t = Math.min(1, Math.max(0, (z - lo[0]) / Math.max(1e-6, hi[0] - lo[0])));
  return [lerp(lo[1][0], hi[1][0], t), lerp(lo[1][1], hi[1][1], t), lerp(lo[1][2], hi[1][2], t)];
}

const BIOME_COLORS = [
  [40, 60, 120],   // 0 ocean
  [240, 245, 250], // 1 ice
  [180, 200, 210], // 2 tundra
  [70, 110, 70],   // 3 boreal forest
  [110, 160, 80],  // 4 temperate forest
  [160, 190, 90],  // 5 grassland
  [50, 140, 60],   // 6 tropical rainforest
  [140, 170, 70],  // 7 savanna
  [225, 200, 120], // 8 desert
  [130, 130, 135], // 9 alpine
  [90, 150, 140],  // 10 wetland
];

function colormapLayer(name, data) {
  const { arr, w, h } = data;
  const img = new Uint8ClampedArray(w * h * 4);
  let mn = Infinity, mx = -Infinity;
  if (!(name.includes("elevation") || name === "biome_id" || name.endsWith("mask") || name === "crust_type" || name === "plate_id")) {
    for (let i = 0; i < arr.length; i++) { const v = arr[i]; if (v < mn) mn = v; if (v > mx) mx = v; }
  }
  for (let i = 0; i < w * h; i++) {
    let r, g, b;
    const v = arr[i];
    if (name.includes("elevation")) {
      [r, g, b] = hypsometric(v);
    } else if (name === "biome_id") {
      const c = BIOME_COLORS[Math.min(BIOME_COLORS.length - 1, Math.max(0, v | 0))];
      [r, g, b] = c;
    } else if (name === "plate_id") {
      const hcol = ((v * 2654435761) >>> 0) % 360;
      const [rr, gg, bb] = hslToRgb(hcol / 360, 0.55, 0.5);
      r = rr; g = gg; b = bb;
    } else if (name.endsWith("mask") || name === "crust_type") {
      r = g = b = v > 0 ? 235 : 25;
    } else {
      const t = (v - mn) / Math.max(1e-9, mx - mn);
      r = g = b = 255 * Math.sqrt(Math.min(1, Math.max(0, t)));
    }
    img[i * 4] = r; img[i * 4 + 1] = g; img[i * 4 + 2] = b; img[i * 4 + 3] = 255;
  }
  return new ImageData(img, w, h);
}

function hslToRgb(h, s, l) {
  const f = (n) => {
    const k = (n + h * 12) % 12;
    const a = s * Math.min(l, 1 - l);
    return Math.round(255 * (l - a * Math.max(-1, Math.min(k - 3, 9 - k, 1))));
  };
  return [f(0), f(8), f(4)];
}

// ---------------- globe (three.js) ----------------

const globe = { renderer: null, scene: null, camera: null, mesh: null, group: null, texCanvas: null };

function initGlobe() {
  const canvas = document.getElementById("globe-canvas");
  globe.renderer = new THREE.WebGLRenderer({ canvas, antialias: true, preserveDrawingBuffer: true });
  globe.scene = new THREE.Scene();
  globe.scene.background = new THREE.Color(0x0a0c10);
  globe.camera = new THREE.PerspectiveCamera(38, 1, 0.01, 100);
  globe.camera.position.set(0, 0, 3.2);
  globe.group = new THREE.Group();
  globe.scene.add(globe.group);
  const light = new THREE.DirectionalLight(0xffffff, 2.2);
  light.position.set(2, 1.2, 2.5);
  globe.scene.add(light);
  globe.scene.add(new THREE.AmbientLight(0xffffff, 0.55));

  const geo = new THREE.SphereGeometry(1, 256, 128);
  const mat = new THREE.MeshStandardMaterial({ color: 0x3355aa, roughness: 0.95 });
  globe.mesh = new THREE.Mesh(geo, mat);
  globe.group.add(globe.mesh);

  let dragging = false, px = 0, py = 0;
  canvas.addEventListener("pointerdown", (e) => { dragging = true; px = e.clientX; py = e.clientY; });
  window.addEventListener("pointerup", () => { dragging = false; });
  window.addEventListener("pointermove", (e) => {
    if (!dragging) return;
    globe.group.rotation.y += (e.clientX - px) * 0.005;
    globe.group.rotation.x = Math.max(-1.4, Math.min(1.4, globe.group.rotation.x + (e.clientY - py) * 0.005));
    px = e.clientX; py = e.clientY;
  });
  canvas.addEventListener("wheel", (e) => {
    e.preventDefault();
    globe.camera.position.z = Math.max(1.3, Math.min(8, globe.camera.position.z * (1 + e.deltaY * 0.001)));
  }, { passive: false });
  canvas.addEventListener("click", (e) => {
    const rect = canvas.getBoundingClientRect();
    const ndc = new THREE.Vector2(((e.clientX - rect.left) / rect.width) * 2 - 1,
                                  -((e.clientY - rect.top) / rect.height) * 2 + 1);
    const ray = new THREE.Raycaster();
    ray.setFromCamera(ndc, globe.camera);
    const hits = ray.intersectObject(globe.mesh);
    if (hits.length) {
      const p = hits[0].point.clone().applyMatrix4(globe.group.matrixWorld.clone().invert()).normalize();
      const lat = Math.asin(p.y) * 180 / Math.PI;
      const lon = Math.atan2(p.z, p.x) * 180 / Math.PI;
      runQuery(lat, -lon);  // texture wraps opposite handedness
    }
  });

  function resize() {
    const wrap = document.getElementById("viewport");
    globe.renderer.setSize(wrap.clientWidth, wrap.clientHeight, false);
    globe.camera.aspect = wrap.clientWidth / Math.max(1, wrap.clientHeight);
    globe.camera.updateProjectionMatrix();
  }
  window.addEventListener("resize", resize);
  resize();

  (function loop() {
    if (state.view === "globe" && !dragging) globe.group.rotation.y += 0.0012;
    globe.renderer.render(globe.scene, globe.camera);
    requestAnimationFrame(loop);
  })();
}

function updateGlobe() {
  const data = state.layers[state.current];
  if (!data) return;
  const imgData = colormapLayer(state.current, data);
  if (!globe.texCanvas) globe.texCanvas = document.createElement("canvas");
  globe.texCanvas.width = data.w; globe.texCanvas.height = data.h;
  globe.texCanvas.getContext("2d").putImageData(imgData, 0, 0);
  const tex = new THREE.CanvasTexture(globe.texCanvas);
  tex.colorSpace = THREE.SRGBColorSpace;
  globe.mesh.material.map = tex;
  globe.mesh.material.color.set(0xffffff);
  globe.mesh.material.needsUpdate = true;

  const elev = state.layers["elevation_eroded_m"];
  if (elev) {
    const pos = globe.mesh.geometry.attributes.position;
    const uv = globe.mesh.geometry.attributes.uv;
    const v = new THREE.Vector3();
    for (let i = 0; i < pos.count; i++) {
      v.fromBufferAttribute(pos, i).normalize();
      const u = uv.getX(i), w = uv.getY(i);
      const x = Math.min(elev.w - 1, Math.max(0, Math.round(u * (elev.w - 1))));
      const y = Math.min(elev.h - 1, Math.max(0, Math.round((1 - w) * (elev.h - 1))));
      const z = elev.arr[y * elev.w + x];
      const r = 1 + Math.max(0, z) * 6e-6 - (z < 0 ? 1.5e-6 * Math.min(0, -z) : 0);
      pos.setXYZ(i, v.x * r, v.y * r, v.z * r);
    }
    pos.needsUpdate = true;
    globe.mesh.geometry.computeVertexNormals();
  }
}

// ---------------- 2D map ----------------

function updateMap() {
  const data = state.layers[state.current];
  if (!data) return;
  const canvas = document.getElementById("map-canvas");
  canvas.width = data.w; canvas.height = data.h;
  canvas.getContext("2d").putImageData(colormapLayer(state.current, data), 0, 0);
  const wrap = document.getElementById("map-wrap");
  const scale = Math.min(wrap.clientWidth / data.w, wrap.clientHeight / data.h);
  canvas.style.width = `${data.w * scale}px`;
  canvas.style.height = `${data.h * scale}px`;
  canvas.style.left = `${(wrap.clientWidth - data.w * scale) / 2}px`;
  canvas.style.top = `${(wrap.clientHeight - data.h * scale) / 2}px`;
  drawOverlay();
}

function mapLatLon(e) {
  const canvas = document.getElementById("map-canvas");
  const rect = canvas.getBoundingClientRect();
  const fx = (e.clientX - rect.left) / rect.width;
  const fy = (e.clientY - rect.top) / rect.height;
  return { lat: 90 - fy * 180, lon: fx * 360 - 180, fx, fy };
}

function initMap() {
  const canvas = document.getElementById("map-canvas");
  canvas.addEventListener("click", async (e) => {
    if (state.brush.active) return;
    const { lat, lon } = mapLatLon(e);
    runQuery(lat, lon);
  });
  // uplift paint brush (M10)
  let painting = false;
  canvas.addEventListener("pointerdown", (e) => { if (state.brush.active) { painting = true; paintAt(e); } });
  window.addEventListener("pointerup", () => { painting = false; });
  canvas.addEventListener("pointermove", (e) => { if (painting) paintAt(e); });
}

function paintAt(e) {
  const { fx, fy } = mapLatLon(e);
  state.brush.strokes.push({ fx, fy, r: state.brush.size / 800 });
  drawOverlay();
}

function drawOverlay() {
  const data = state.layers[state.current];
  if (!data) return;
  const map = document.getElementById("map-canvas");
  const ov = document.getElementById("overlay-canvas");
  ov.width = 1400; ov.height = 700;
  ov.style.width = map.style.width; ov.style.height = map.style.height;
  ov.style.left = map.style.left; ov.style.top = map.style.top;
  const ctx = ov.getContext("2d");
  ctx.clearRect(0, 0, ov.width, ov.height);
  const W = ov.width, H = ov.height;
  const px = (lon, lat) => [((lon + 180) / 360) * W, ((90 - lat) / 180) * H];

  if (state.features && state.overlay) {
    const feats = state.features.features.filter((f) => f.properties.kind === state.overlay.slice(0, -1) || f.properties.kind === state.overlay);
    for (const f of feats) {
      const g = f.geometry;
      if (g.type === "LineString") {
        ctx.beginPath();
        let first = true, prev = null;
        for (const [lon, lat] of g.coordinates) {
          const [x, y] = px(lon, lat);
          if (!first && prev !== null && Math.abs(x - prev) > W / 2) { first = true; }
          if (first) ctx.moveTo(x, y); else ctx.lineTo(x, y);
          first = false; prev = x;
        }
        if (state.overlay === "rivers") { ctx.strokeStyle = "rgba(80,170,255,0.9)"; ctx.lineWidth = Math.max(0.6, Math.log10((f.properties.discharge_m2 || 1e9) / 1e8)); }
        else if (state.overlay === "borders") { ctx.strokeStyle = "rgba(255,120,120,0.9)"; ctx.lineWidth = 1.4; ctx.setLineDash([5, 3]); }
        else { ctx.strokeStyle = "rgba(230,200,120,0.9)"; ctx.lineWidth = 1; }
        ctx.stroke();
        ctx.setLineDash([]);
      } else if (g.type === "Point") {
        const [x, y] = px(g.coordinates[0], g.coordinates[1]);
        const size = f.properties.rank === 0 ? 5 : f.properties.rank === 1 ? 3.5 : 2.2;
        ctx.fillStyle = f.properties.rank === 0 ? "#ffd75e" : "#ffffff";
        ctx.beginPath(); ctx.arc(x, y, size, 0, Math.PI * 2); ctx.fill();
        if (f.properties.rank <= 1 && f.properties.name) {
          ctx.fillStyle = "rgba(255,255,255,0.95)"; ctx.font = "11px sans-serif";
          ctx.fillText(f.properties.name, x + 6, y + 3);
        }
      }
    }
  }
  // brush strokes preview
  ctx.fillStyle = "rgba(120,220,140,0.25)";
  for (const s of state.brush.strokes) {
    ctx.beginPath(); ctx.arc(s.fx * W, s.fy * H, s.r * W, 0, Math.PI * 2); ctx.fill();
  }
}

// ---------------- query panel ----------------

async function runQuery(lat, lon) {
  if (!state.sid) return;
  try {
    const q = await api.json(`/api/sessions/${state.sid}/query?lat=${lat.toFixed(4)}&lon=${lon.toFixed(4)}`);
    const panel = document.getElementById("query");
    const table = document.getElementById("query-table");
    table.innerHTML = "";
    const keys = ["lat", "lon", "elevation_eroded_m", "temperature_c", "precipitation_mm_yr",
                  "biome_id", "crust_type", "plate_id", "oceanic_age_myr", "flow_accumulation_m2"];
    for (const k of keys) {
      if (!(k in q)) continue;
      const tr = document.createElement("tr");
      tr.innerHTML = `<td>${k}</td><td>${Number(q[k]).toPrecision(5)}</td>`;
      table.appendChild(tr);
    }
    panel.style.display = "block";
  } catch (err) { console.error(err); }
}

// ---------------- generation flow ----------------

async function loadAllLayers() {
  const info = await api.json(`/api/sessions/${state.sid}`);
  state.layerNames = info.layers;
  const select = document.getElementById("layer-select");
  select.innerHTML = "";
  for (const name of info.layers) {
    const opt = document.createElement("option");
    opt.value = name; opt.textContent = name;
    select.appendChild(opt);
  }
  if (info.layers.includes(state.current)) select.value = state.current;
  else state.current = info.layers[0];
  state.layers = {};
  for (const name of info.layers) {
    state.layers[name] = await api.layer(state.sid, name);
  }
  try {
    state.features = await api.json(`/api/sessions/${state.sid}/features`);
  } catch { state.features = null; }
  updateGlobe();
  updateMap();
  document.getElementById("status").textContent =
    `ready — ${info.width}x${info.height}, ${info.layers.length} layers, ${info.features} features`;
  document.body.dataset.ready = "1";
}

async function generate() {
  const btn = document.getElementById("btn-generate");
  btn.disabled = true;
  document.body.dataset.ready = "";
  try {
    await pushAllParams();
    if (state.brush.strokes.length) await uploadPaint();
    await api.json(`/api/sessions/${state.sid}/generate`, { method: "POST" });
    const bar = document.getElementById("progress-bar");
    const status = document.getElementById("status");
    const es = new EventSource(`/api/sessions/${state.sid}/progress`);
    es.onmessage = async (ev) => {
      const e = JSON.parse(ev.data);
      if (e.type === "progress") {
        bar.style.width = `${((e.index - 1 + e.phase) / e.total) * 100}%`;
        status.textContent = `${e.stage} ${(e.phase * 100) | 0}%`;
      } else if (e.type === "done") {
        es.close(); bar.style.width = "100%"; status.textContent = "loading layers…";
        await loadAllLayers();
        btn.disabled = false;
      } else if (e.type === "error") {
        es.close(); status.textContent = `error: ${e.message}`; btn.disabled = false;
      }
    };
  } catch (err) {
    document.getElementById("status").textContent = String(err);
    btn.disabled = false;
  }
}

async function uploadPaint() {
  // rasterize strokes into a coarse f32 equirect field (uplift m/yr bonus)
  const W = 512, H = 256;
  const field = new Float32Array(W * H);
  for (const s of state.brush.strokes) {
    const cx = s.fx * W, cy = s.fy * H, r = s.r * W;
    for (let y = Math.max(0, cy - r | 0); y < Math.min(H, cy + r + 1 | 0); y++) {
      for (let x = Math.max(0, cx - r | 0); x < Math.min(W, cx + r + 1 | 0); x++) {
        const d = Math.hypot(x - cx, y - cy) / r;
        if (d < 1) field[y * W + x] = Math.max(field[y * W + x], 3e-4 * (1 - d * d));
      }
    }
  }
  await fetch(`/api/sessions/${state.sid}/paint/uplift_paint`, {
    method: "POST", headers: { "Content-Type": "application/octet-stream", "X-Width": W, "X-Height": H },
    body: field.buffer,
  });
}

// ---------------- save / load ----------------

async function refreshWorldList() {
  const { worlds } = await api.json("/api/worlds");
  const sel = document.getElementById("world-list");
  sel.innerHTML = "";
  for (const w of worlds) {
    const opt = document.createElement("option");
    opt.value = w; opt.textContent = w;
    sel.appendChild(opt);
  }
}

// ---------------- boot ----------------

async function main() {
  const { id } = await api.json("/api/sessions", { method: "POST" });
  state.sid = id;
  document.body.dataset.sid = id;
  await buildParamsForm();
  await refreshWorldList();
  initGlobe();
  initMap();

  document.getElementById("btn-generate").addEventListener("click", generate);
  document.getElementById("btn-save").addEventListener("click", async () => {
    const name = document.getElementById("world-name").value || "world";
    const r = await api.json(`/api/sessions/${state.sid}/save`, {
      method: "POST", headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ name }),
    });
    document.getElementById("status").textContent = `saved ${r.saved}`;
    await refreshWorldList();
  });
  document.getElementById("btn-load").addEventListener("click", async () => {
    const name = document.getElementById("world-list").value;
    if (!name) return;
    await api.json(`/api/sessions/${state.sid}/load`, {
      method: "POST", headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ name }),
    });
    await loadAllLayers();
  });
  document.getElementById("layer-select").addEventListener("change", (e) => {
    state.current = e.target.value;
    updateGlobe(); updateMap();
  });
  document.getElementById("overlay-select").addEventListener("change", (e) => {
    state.overlay = e.target.value;
    drawOverlay();
  });
  document.getElementById("tab-globe").addEventListener("click", () => switchView("globe"));
  document.getElementById("tab-map").addEventListener("click", () => switchView("map"));
  document.getElementById("brush-toggle").addEventListener("click", () => {
    state.brush.active = !state.brush.active;
    document.getElementById("brush-toggle").classList.toggle("active", state.brush.active);
  });
  document.getElementById("brush-size").addEventListener("input", (e) => {
    state.brush.size = parseInt(e.target.value);
  });
  document.getElementById("brush-panel").classList.add("active");
}

function switchView(v) {
  state.view = v;
  document.getElementById("tab-globe").classList.toggle("active", v === "globe");
  document.getElementById("tab-map").classList.toggle("active", v === "map");
  document.getElementById("globe-canvas").style.display = v === "globe" ? "block" : "none";
  document.getElementById("map-wrap").style.display = v === "map" ? "block" : "none";
  if (v === "map") updateMap();
}

main();
