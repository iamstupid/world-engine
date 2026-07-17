/* RP 工作室：多座席受控角色扮演面板。
   信息隔离由服务端执行（/rp/entries?viewer=…），前端只是如实渲染。 */

const qs = (s) => document.querySelector(s);
const params = new URLSearchParams(location.search);
let SID = params.get("sid");
let VIEWER = null;
let STATE = null;

async function api(path, body, method) {
  const opt = body !== undefined
    ? { method: method || "POST", headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body) }
    : {};
  const r = await fetch(`/api/sessions/${SID}${path}`, opt);
  if (!r.ok) throw new Error(`${path}: ${r.status} ${await r.text()}`);
  return r.json();
}

function setStatus(t) { qs("#status").textContent = t; }

async function ensureInit() {
  if (!SID) { setStatus("缺少 ?sid= 会话参数"); qs("#boot").hidden = false; return false; }
  try { await api("/rp/state", undefined); return true; }
  catch { qs("#boot").hidden = false; return false; }
}

qs("#rp-init-btn").onclick = async () => {
  await api("/rp/init", { use_ai: qs("#use-ai").checked });
  qs("#boot").hidden = true;
  qs("#main").hidden = false;
  refresh();
};

qs("#seat-add").onclick = async () => {
  const name = qs("#seat-name").value.trim();
  if (!name) return;
  await api("/rp/seat", { name, kind: qs("#seat-kind").value,
                          automation: qs("#seat-auto").value });
  qs("#seat-name").value = "";
  refresh();
};

qs("#say-btn").onclick = async () => {
  const text = qs("#say-text").value.trim();
  if (!text || !VIEWER) return;
  await api("/rp/say", { seat: VIEWER, layer: qs("#layer-select").value, text });
  qs("#say-text").value = "";
  refresh();
};

qs("#suggest-btn").onclick = async () => {
  if (!VIEWER) return;
  setStatus("AI 起草中…");
  const draft = await api("/rp/suggest", { seat: VIEWER });
  if (!draft.landed) qs("#say-text").value = draft.text;
  setStatus("");
  refresh();
};

qs("#sc-open-btn").onclick = async () => {
  const cast = qs("#sc-cast").value.split(/[,，]/).map(s => s.trim()).filter(Boolean);
  const body = { place: [parseFloat(qs("#sc-lon").value),
                         parseFloat(qs("#sc-lat").value)],
                 cast, opening: qs("#sc-opening").value };
  const day = parseFloat(qs("#sc-day").value);
  if (!Number.isNaN(day)) body.day = day;
  await api("/rp/scene/open", body);
  refresh();
};

qs("#sc-close-btn").onclick = async () => {
  const events = [];
  const desc = qs("#sc-event").value.trim();
  if (desc) events.push({ desc, visibility: qs("#sc-vis").value });
  await api("/rp/scene/close", {
    elapsed_days: parseFloat(qs("#sc-elapsed").value) || 0.5,
    events, summary: qs("#sc-summary").value });
  qs("#sc-event").value = "";
  refresh();
};

qs("#thread-add").onclick = async () => {
  const desc = qs("#thread-desc").value.trim();
  if (!desc) return;
  await api("/rp/thread", { desc });
  qs("#thread-desc").value = "";
  refresh();
};

qs("#viewer-select").onchange = () => {
  VIEWER = qs("#viewer-select").value;
  render();
  refreshView();
};

function render() {
  if (!STATE) return;
  qs("#clock").textContent = `第 ${STATE.clock.toFixed(1)} 天`;
  qs("#ai-badge").textContent = STATE.ai ? "AI已连接" : "无AI(纯手动)";
  const names = Object.keys(STATE.seats);
  if (!VIEWER && names.length) VIEWER = names[0];
  const sel = qs("#viewer-select");
  sel.innerHTML = names.map(n => `<option ${n === VIEWER ? "selected" : ""}>${n}</option>`).join("");
  qs("#seats-list").innerHTML = names.map(n => {
    const s = STATE.seats[n];
    return `<div class="seat ${n === VIEWER ? "active" : ""}" data-n="${n}">
      ${n} <small>${s.kind}·${s.automation}${s.pending ? `·待收${s.pending}` : ""}</small></div>`;
  }).join("");
  document.querySelectorAll(".seat").forEach(el =>
    el.onclick = () => { VIEWER = el.dataset.n; sel.value = VIEWER; render(); refreshView(); });
  const card = STATE.seats[VIEWER]?.card;
  qs("#statecard").textContent = card
    ? `人设：${card.persona}\n目标：${card.goals}\n情绪：${card.mood}\n已知：${(card.knowledge || []).slice(-5).join("；")}`
    : "";
  const sc = STATE.scene;
  qs("#scene-info").innerHTML = sc
    ? `<b>场景#${sc.id}</b>（第${sc.day}天｜在场：${sc.cast.join("、")}）\n${sc.opening}` +
      (sc.lighting ? `\n<span class="light">☀ ${sc.lighting}</span>` : "") +
      (sc.skyline && sc.skyline.length ? `\n<span class="light">⛰ ${sc.skyline[0]}</span>` : "")
    : "（无开启的场景——用右栏开场）";
  qs("#threads-list").innerHTML = (STATE.threads || []).map(t =>
    `<div class="thread ${t.status === "paid" ? "paid" : ""}">
       <span>#${t.id} ${t.desc}</span>
       ${t.status === "open" ? `<button data-pay="${t.id}">回收</button>` : ""}</div>`).join("");
  document.querySelectorAll("[data-pay]").forEach(el =>
    el.onclick = async () => { await api("/rp/thread", { pay: el.dataset.pay }); refresh(); });
  if (sc) {
    qs("#minimap-img").src = `/api/sessions/${SID}/rp/refpack.png?kind=minimap&_=${sc.id}`;
    qs("#skydome-img").src = `/api/sessions/${SID}/rp/refpack.png?kind=skydome&_=${sc.id}`;
    qs("#minimap-img").hidden = qs("#skydome-img").hidden = false;
  }
}

async function refreshView() {
  if (!VIEWER) return;
  const ent = await api(`/rp/entries?viewer=${encodeURIComponent(VIEWER)}`, undefined);
  qs("#entries").innerHTML = (ent.entries || []).map(e =>
    `<div class="entry ${e.layer}"><span class="who">${e.seat}</span>${e.text}` +
    (e.arbiter_note ? `<span class="note">场记：${e.arbiter_note}</span>` : "") +
    `</div>`).join("");
  const feed = await api(`/rp/feed/${encodeURIComponent(VIEWER)}`, undefined);
  qs("#feed-list").innerHTML = (feed.feed || []).slice(-15).reverse().map(f =>
    `<div class="feed-item">[第${(+f.day).toFixed(1)}天·${f.kind}] ${f.text}</div>`).join("");
}

async function refresh() {
  try {
    STATE = await api("/rp/state", undefined);
    qs("#main").hidden = false;
    qs("#boot").hidden = true;
    render();
    await refreshView();
    setStatus("");
  } catch (e) { setStatus(String(e).slice(0, 120)); }
}

(async () => {
  const ok = await ensureInit();
  if (ok) { qs("#main").hidden = false; refresh(); }
  setInterval(() => { if (!qs("#main").hidden) refresh(); }, 3000);
})();
