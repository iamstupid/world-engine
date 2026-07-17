"""End-to-end RP demo: real world, real multi-model seats, one full scene.

KP (gpt-5.6-sol) opens a rainy-night scene in Craiar; the actor seats
(gpt-5.6-luna as 凯尔, gpt-5.4-mini as 赛芮) play three-layer turns on
auto; movement physics rejects an impossible ride; closing the scene
distributes a public event to an off-stage seat with the real courier
delay. Reference pack renders minimap + skydome PNGs, and the scene art
is generated render-guided via gpt-image-2.

Outputs: docs/samples/rp_demo/ (transcript.jsonl, report.md, PNGs;
scene art goes to worlds/assets/, not committed).
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "build-linux"))
sys.path.insert(0, str(REPO / "tools" / "terrain_server"))
sys.path.insert(0, str(REPO / "tools" / "scripts"))

from novel_demo import build_world, pick_town  # noqa: E402
import astro  # noqa: E402
import geoquery  # noqa: E402
import imagegen  # noqa: E402
import novelkit  # noqa: E402
import refpack  # noqa: E402
import rpkit  # noqa: E402
from llmpool import LLMPool  # noqa: E402

OUT = REPO / "docs" / "samples" / "rp_demo"
ASSETS = REPO / "worlds" / "assets"


def main():
    s = build_world()
    geo = geoquery.GeoIndex(s)
    s.geo = geo
    town, capital, rh, rw = pick_town(s, geo)
    universe, obs = astro.build_universe(909, 360.0, {
        "home_companion": {"mass_ratio": 0.32, "a_au": 60.0, "e": 0.12},
        "moons": [
            {"name": "澜月", "period_days": 29.5, "phase0": 0.0,
             "inclination_deg": 5.0},
            {"name": "缇月", "period_days": 11.3, "phase0": 2.1,
             "inclination_deg": 8.0}],
    })
    pool = LLMPool()
    pool.cfg["roles"]["actor_major"] = {"provider": "local8317",
                                        "model": "gpt-5.6-luna"}
    project = novelkit.Project("rp-demo", s)
    rp = rpkit.Roleplay(
        s, pool=pool, obs=obs, project=project,
        refpack_fn=lambda ss, oo, lon, lat, t:
            refpack.scene_refpack(ss, oo, lon, lat, t))

    rp.add_seat("KP", kind="kp", automation="assist", ai_role="kp")
    rp.add_seat("凯尔", automation="auto", ai_role="actor_major",
                persona="十六岁的雨林镇少年，身体里住着现代都市转生者林晚舟",
                goals="随商队去王都拉托亚，查明自己为何转生",
                mood="外表镇定，内里紧绷",
                place=(town["lon"], town["lat"]))
    rp.add_seat("赛芮", automation="auto", ai_role="actor_minor",
                persona="从王都档案房逃出来的年轻文书，随身带着密封的底簿",
                goals="把密卷送到能保护它的人手里，不暴露身份",
                mood="警觉疲惫",
                place=(town["lon"], town["lat"]))
    rp.add_seat("俄洛", automation="manual", ai_role="actor_minor",
                persona="拉托亚的老学者", goals="研究古碑",
                place=(capital["lon"], capital["lat"]))

    rp.plant_thread("赛芮的密卷来历不明")
    opening = (f"第137天黄昏，{town['name']}镇北货栈。雨刚停，商队明晨出发去"
               f"王都{capital['name']}。火塘边，凯尔在给驮鞍上油，赛芮抱着"
               "行李坐在角落。")
    scene = rp.open_scene((town["lon"], town["lat"]), ["凯尔", "赛芮"],
                          opening, day=137.0)
    print("[rp] scene open; lighting:",
          scene["refpack"]["lighting"]["报告"])

    # real AI turns (auto seats land three layers each)
    for seat in ("凯尔", "赛芮", "凯尔"):
        draft = rp.suggest(seat, max_tokens=2000)
        print(f"[rp] {seat} landed={draft['landed']}")

    # movement physics
    bad = rp.move("凯尔", (capital["lon"], capital["lat"]),
                  claimed_days=2.0)
    print("[rp] impossible ride:", bad["verdict"], bad.get("reason"),
          "needed", bad.get("needed_days"))
    assert bad["verdict"] == "reject"

    # KP narration + settle with a public event
    rp.say("KP", "narration",
           "货栈掌柜进来收灯油钱，顺口说：驿马今天下午到的，王都出事了。")
    settle = rp.close_scene(
        0.5,
        events=[{"desc": "老王病危的急报贴到了镇公所门口", "visibility": "public",
                 "place": [town["lon"], town["lat"]]}],
        deltas={"凯尔": {"knowledge+": "王都局势不稳，商队要抢时间"}},
        summary="出发前夜：急报与不安")
    print("[rp] settle distributed:", settle["distributed"])

    # renders
    OUT.mkdir(parents=True, exist_ok=True)
    pack = refpack.scene_refpack(s, obs, town["lon"], town["lat"], 137.0,
                                 with_png=True)
    (OUT / "minimap.png").write_bytes(pack["minimap_png"])
    (OUT / "skydome.png").write_bytes(pack["skydome_png"])
    print("[rp] refpack PNGs written; skyline:",
          pack["skyline_report"][:2])

    art_path = None
    try:
        gen = imagegen.ImageGen(pool)
        prompt = imagegen.scene_prompt(opening, pack)
        ASSETS.mkdir(parents=True, exist_ok=True)
        art_path = ASSETS / "rp_demo_scene.png"
        gen.generate(prompt, save_to=art_path)
        print(f"[rp] scene art -> {art_path}")
    except Exception as exc:  # noqa: BLE001
        print(f"[rp] scene art skipped: {exc}")

    (OUT / "transcript.jsonl").write_text(rp.transcript_jsonl(),
                                          encoding="utf-8")
    kaier_inner = [e["text"] for e in rp.seats["凯尔"].feed
                   if e["kind"] == "self_inner"]
    sairui_saw = [e["text"] for e in rp.seats["赛芮"].feed
                  if e["kind"].startswith("observed")]
    report = f"""# RP 演武报告（单场景端到端）

- 世界：seed 909；场景地点 {town['name']}（{s.store.calendar.format_day(137)}）
- 座席：KP=gpt-5.6-sol(high) ｜ 凯尔=gpt-5.6-luna(auto) ｜ 赛芮=gpt-5.4-mini(auto)
  ｜ 场记=gpt-5.4-mini ｜ 俄洛=手动(离场，在王都)
- 光照侧写：{scene['refpack']['lighting']['报告']}
- 天际线：{'；'.join(pack['skyline_report'][:2])}

## 物理裁决
- 「两天骑到王都」被世界拒绝：需要约 {bad.get('needed_days')} 天

## 信息隔离与分发
- 凯尔的内心层（其他座席不可见）共 {len(kaier_inner)} 条，例：
  {kaier_inner[-1][:80] if kaier_inner else '—'}
- 赛芮观察到的（无内心层）共 {len(sairui_saw)} 条
- 公共事件分发：{json.dumps(settle['distributed'], ensure_ascii=False)}
  （俄洛在王都，急报按驿路延迟送达，而非即时全知）

## 用量账本
```json
{json.dumps(pool.usage_report(), ensure_ascii=False, indent=1)}
```
"""
    (OUT / "report.md").write_text(report, encoding="utf-8")
    print(f"[rp] report -> {OUT / 'report.md'}")
    return art_path


if __name__ == "__main__":
    main()
