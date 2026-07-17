"""End-to-end novel demo: generate a world with the full stack, harvest a
geographic/cultural/astronomical fact pack for one town, ask an external
LLM (OpenAI-compatible endpoint) to write a traditional isekai opening
that must respect the facts, then run the novelkit consistency checks on
the protagonist's planned journey.

Usage:  python3 tools/scripts/novel_demo.py [--api http://HOST:8317/v1]
Output: docs/samples/isekai_demo.md
"""

from __future__ import annotations

import argparse
import json
import math
import sys
import time
from pathlib import Path

import numpy as np
import requests

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "build-linux"))
sys.path.insert(0, str(REPO / "tools" / "terrain_server"))

import weterrain  # noqa: E402
import astro  # noqa: E402
import civ  # noqa: E402
import geoquery  # noqa: E402
import novelkit  # noqa: E402
from worldstore import Calendar, WorldStore  # noqa: E402

BIOME_NAMES = {0: "海洋", 1: "冰原", 2: "苔原", 3: "北方针叶林", 4: "温带森林",
               5: "草原", 6: "热带雨林", 7: "稀树草原", 8: "荒漠", 9: "高山",
               10: "湿地"}

MONTHS = [("孟春", 30), ("仲春", 30), ("季春", 30), ("孟夏", 30), ("仲夏", 30),
          ("季夏", 30), ("孟秋", 30), ("仲秋", 30), ("季秋", 30), ("孟冬", 30),
          ("仲冬", 30), ("季冬", 30)]


class Session:
    def __init__(self):
        self.params = {
            "world.seed": 909, "world.width": 768, "world.height": 384,
            "world.physics_grid_frequency": 176,
            "tectonics.grid_frequency": 64,
        }
        self.result = None
        self.features = []
        self.entities = []
        self.store = WorldStore()
        self.geo = None


def build_world():
    print("[demo] generating world ...")
    s = Session()
    p = weterrain.Params()
    for k, v in s.params.items():
        p.set(k, v)
    t0 = time.time()
    s.result = weterrain.generate(p)
    print(f"[demo] terrain in {time.time()-t0:.1f}s")
    civ.generate_civilization(s)
    s.store.set_calendar(Calendar("尘世历", MONTHS, epoch_label="纪元"))
    return s


def pick_town(s, geo):
    setts = s.store.find("settlement")
    rivers = [f for f in s.features if f["kind"] == "river"]

    def river_dist(t):
        best = 999.0
        for r in rivers:
            cs = np.asarray(r["geometry"]["coordinates"])
            d = float(np.abs(cs - np.array([t["lon"], t["lat"]])).sum(axis=1).min())
            best = min(best, d)
        return best

    towns = [t for t in setts if t.get("rank", 9) >= 2]
    towns.sort(key=lambda t: river_dist(t))
    for town in towns:
        capital = next((t for t in setts
                        if t.get("polity_id") == town.get("polity_id")
                        and t.get("rank") == 0), None)
        if capital is None:
            continue
        rh = geo.route((town["lon"], town["lat"]),
                       (capital["lon"], capital["lat"]), "horse")
        rw = geo.route((town["lon"], town["lat"]),
                       (capital["lon"], capital["lat"]), "walk")
        if rh["reachable"] and rw["reachable"] and rh["days"] >= 3:
            return town, capital, rh, rw
    raise RuntimeError("no town with a land route to its capital")


def sky_facts(s, obs, lat, lon, t_day):
    # find a night hour and a day hour near t_day
    night = day = None
    for h in range(48):
        sk = obs.sky(lat, lon, t_day + h / 24.0)
        if sk["period"] == "night" and night is None:
            night = sk
        if sk["period"] == "day" and day is None:
            day = sk
        if night and day:
            break
    facts = {}
    if day:
        facts["白昼"] = {"太阳高度": day["sun"]["alt"]}
        if "companion_star" in day:
            c = day["companion_star"]
            facts["白昼"]["第二颗太阳"] = (
                f"一颗暗红色的伴星日，视星等{c['mag']}，"
                f"{'正挂在天上' if c['up'] else '此刻在地平线下'}")
    if night:
        facts["夜晚"] = {
            "月亮": [{"名字": m["name"], "相位": m["phase_name"],
                      "照明度": m["phase"]} for m in night["moons"]],
            "可见星座": [c["name"] for c in night["constellations"][:4]],
            "亮行星": [p["name"] for p in night["planets"]
                       if p["up"] and p["mag"] < 1.5],
        }
    return facts


def build_fact_pack(s):
    geo = geoquery.GeoIndex(s)
    s.geo = geo
    town, capital, route_horse, route_walk = pick_town(s, geo)
    d = geo.describe(town["lon"], town["lat"])
    news = geo.news_arrival((capital["lon"], capital["lat"]),
                            (town["lon"], town["lat"]))
    view = geo.viewshed(town["lon"], town["lat"])

    q_lat, q_lon = town["lat"], town["lon"]
    pt = {}
    w, h = s.result["width"], s.result["height"]
    x = int(((q_lon + 180.0) / 360.0) * w) % w
    y = min(h - 1, max(0, int(((90.0 - q_lat) / 180.0) * h)))
    for name in ("elevation_eroded_m", "temperature_c", "precipitation_mm_yr",
                 "biome_id"):
        arr = np.frombuffer(s.result["layers"][name]["data"],
                            np.float32 if name != "biome_id" else np.uint8)
        pt[name] = float(arr.reshape(h, w)[y, x])

    rivers = [f for f in s.features if f["kind"] == "river"]
    rbest, rdist = None, 999.0
    for r in rivers:
        cs = np.asarray(r["geometry"]["coordinates"])
        dd = float(np.abs(cs - np.array([q_lon, q_lat])).sum(axis=1).min())
        if dd < rdist:
            rbest, rdist = r, dd

    seed = int(s.params["world.seed"])
    universe, obs = astro.build_universe(seed, 360.0, {
        "home_companion": {"mass_ratio": 0.32, "a_au": 60.0, "e": 0.12},
        "moons": [
            {"name": "澜月", "period_days": 29.5, "phase0": 0.0,
             "inclination_deg": 5.0},
            {"name": "缇月", "period_days": 11.3, "phase0": 2.1,
             "inclination_deg": 8.0}],
    })
    t0 = 137.0  # story opening day
    cal = s.store.calendar

    facts = {
        "开场小镇": {
            "名字": town["name"], "所属政权": d.get("polity"),
            "文化族群": d.get("culture"),
            "坐标": f"东经{q_lon:.1f}° 纬度{q_lat:.1f}°" if q_lon >= 0 else
                    f"西经{-q_lon:.1f}° 纬度{q_lat:.1f}°",
            "生态": BIOME_NAMES.get(int(pt["biome_id"]), "未知"),
            "海拔": f"{pt['elevation_eroded_m']:.0f} 米",
            "年均温": f"{pt['temperature_c']:.1f} °C",
            "年降水": f"{pt['precipitation_mm_yr']:.0f} 毫米",
            "地平线": f"约 {view['horizon_km_median']:.0f} 公里",
        },
        "河流": None if rbest is None else {
            "距镇": f"约 {rdist * 111:.0f} 公里内",
            "入海口宽度": f"约 {rbest['properties'].get('width_m', 0):.0f} 米",
        },
        "首都": {
            "名字": capital["name"],
            "骑马路程": f"{route_horse['days']:.0f} 天" if route_horse["reachable"] else "不可达",
            "步行路程": f"{route_walk['days']:.0f} 天" if route_walk["reachable"] else "不可达",
            "距离": f"{route_horse.get('distance_km', 0):.0f} 公里",
            "首都的消息传到镇上需要": f"{news['days']:.0f} 天" if news["reachable"] else "——",
        },
        "历法与日期": {
            "历名": cal.name, "月份": [m[0] for m in MONTHS],
            "开场日期": cal.format_day(t0), "一年": "360 天，十二月，每月三十天",
        },
        "天空": sky_facts(s, obs, q_lat, q_lon, t0),
    }
    context = {"town": town, "capital": capital,
               "route_horse": route_horse, "route_walk": route_walk,
               "news": news, "t0": t0}
    return facts, context


PROMPT_SYS = ("你是一位中文奇幻小说作者，擅长转生异世界题材。你的文风扎实，"
              "注重风土人情的具体质感——气候的体感、天空的异象、市镇的声音与"
              "气味、距离与时间的真实重量。")

PROMPT_USER = """请写一段传统的转生异世界小说开场（约2500字，简体中文）。

剧情要求：现代都市里的普通人猝然死亡，在异世界的一个小镇醒来（附身于当地一名少年）。
通过他初醒后的所见所闻，展现这个世界的风土人情。结尾处，他下定决心前往王都，
并盘算旅程所需的天数与消息传递的迟缓。

以下是这个世界经过物理模拟得到的【事实档案】。故事必须与这些事实相符，
天象、地理、路程天数不得与档案矛盾；地名人名可按音译自然化用：

{facts}

写作提示：
- 双日与双月是这个世界最醒目的"异世界信号"，请在主角睁眼后不久用感官描写呈现；
- 用年均温、降水和生态推断日常衣食住行的细节（作物、屋顶、集市货品）；
- 骑马{horse_days}天/步行{walk_days}天的王都路程，以及首都消息要{news_days}天
  才到镇上，应成为主角决策的现实约束；
- 不要写成设定说明书，事实要溶进场景和人物对话里。
只输出小说正文。"""


def call_llm(api_base: str, api_key: str, model: str, facts: dict,
             ctx: dict) -> str:
    prompt = PROMPT_USER.format(
        facts=json.dumps(facts, ensure_ascii=False, indent=1),
        horse_days=f"{ctx['route_horse']['days']:.0f}",
        walk_days=f"{ctx['route_walk']['days']:.0f}",
        news_days=f"{ctx['news']['days']:.0f}")
    print(f"[demo] calling {model} at {api_base} ...")
    resp = requests.post(
        f"{api_base}/chat/completions",
        headers={"Authorization": f"Bearer {api_key}",
                 "Content-Type": "application/json"},
        json={"model": model,
              "messages": [{"role": "system", "content": PROMPT_SYS},
                           {"role": "user", "content": prompt}],
              "max_tokens": 8000, "temperature": 0.9},
        timeout=600)
    resp.raise_for_status()
    data = resp.json()
    return data["choices"][0]["message"]["content"], prompt


def consistency_demo(s, ctx) -> list[str]:
    """The novelist-facing loop: the editor checks the protagonist's plan."""
    project = novelkit.Project("异世界转生-样章", s)
    town, capital = ctx["town"], ctx["capital"]
    t0 = ctx["t0"]
    days = ctx["route_horse"]["days"]

    lines = []
    good = project.create_character("林晚舟(合理行程)", town["lon"], town["lat"],
                                    day=t0)
    project.set_location(good, t0 + days * 1.15, capital["lon"], capital["lat"],
                         place=capital["name"])
    v_good = project.check_travel(good)
    lines.append(f"合理行程（第{t0:.0f}天出发，第{t0 + days * 1.15:.0f}天抵达王都）："
                 f"{'通过 ✓' if not v_good else v_good}")

    bad = project.create_character("林晚舟(注水行程)", town["lon"], town["lat"],
                                   day=t0)
    project.set_location(bad, t0 + max(1.0, days * 0.3), capital["lon"],
                         capital["lat"], place=capital["name"])
    v_bad = project.check_travel(bad)
    assert v_bad, "expected a too-fast violation"
    v = v_bad[0]
    lines.append(f"注水行程（{v['elapsed_days']}天赶到，实际骑马需约"
                 f"{v['needed_days']}天）：检查器判定 {v['type']} ✗")

    ev = [{"day": t0, "lon": capital["lon"], "lat": capital["lat"],
           "label": "王都政变",
           "learns": [{"char": good, "day": t0 + 1.0}]}]
    v_news = project.check_knowledge(ev)
    if v_news:
        lines.append(f"消息一致性：主角在第{t0 + 1:.0f}天就『听说』王都政变，"
                     f"但消息最快第{v_news[0]['earliest_day']:.0f}天才能到镇上"
                     f"——判定 {v_news[0]['type']} ✗")
    return lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--api", default="http://172.30.176.1:8317/v1")
    ap.add_argument("--key", default="114514")
    ap.add_argument("--model", default="gpt-5.6-luna")
    args = ap.parse_args()

    s = build_world()
    facts, ctx = build_fact_pack(s)
    print("[demo] fact pack ready:")
    print(json.dumps(facts, ensure_ascii=False, indent=1)[:1200])

    story, prompt = call_llm(args.api, args.key, args.model, facts, ctx)
    checks = consistency_demo(s, ctx)

    out = REPO / "docs" / "samples" / "isekai_demo.md"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(f"""# 转生异世界 · 样章（WorldEngine 全栈驱动）

> 世界由 WorldEngine 生成（seed {s.params['world.seed']}，构造板块 → 测地物理 →
> 气候/生态 → 文明 → 天文全管线）；事实档案取自地理数据库查询
> (describe/route/news/viewshed) 与天文模型 (双日、双月、星座)；正文由
> {args.model} 依据事实档案写成；末尾附 novelkit 一致性检查演示。

## 事实档案（模拟结果，非人工设定）

```json
{json.dumps(facts, ensure_ascii=False, indent=1)}
```

## 样章正文

{story}

## novelkit 一致性检查演示

""" + "\n".join(f"- {line}" for line in checks) + "\n",
                   encoding="utf-8")
    print(f"[demo] written {out}")


if __name__ == "__main__":
    main()
