"""Volume 1 generator: drive gpt-5.6-sol (reasoning high) through a
20-chapter short volume bound to the simulated world (seed 909).

Pipeline (checkpointed; rerun skips finished steps):
  --phase outline   sol produces a 20-chapter detailed outline (JSON),
                    machine-prechecked (dates monotone, fords sum to 9,
                    eclipse only in ch19)
  --phase chapters  per-chapter generation with an injected fact pack
                    (date, moon phases, local describe, fords, distance
                    left) + running context; each chapter is validated
                    (length, moon-phase wording, eclipse gating) and
                    rewritten once on violation
  --phase assemble  novelkit final travel check + full-volume markdown
  --phase all       everything in order

Timeline note (fixes a 1-day gap in brief v1): the royal courier reaches
Craiar at noon of 18 仲夏 (sent 14 仲夏, 4.19-day lag), the caravan
leaves the SAME evening to beat the mourning lockdown, so 7 days of hard
riding arrive at the capital gates in the moonless night of 24 仲夏 —
exactly what the simulation gives (both moons near new on day 143-144).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from pathlib import Path

import numpy as np
import requests

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "build-linux"))
sys.path.insert(0, str(REPO / "tools" / "terrain_server"))
sys.path.insert(0, str(REPO / "tools" / "scripts"))

OUT_DIR = REPO / "docs" / "samples" / "vol1"
API = "http://172.30.176.1:8317/v1"
KEY = "114514"
MODEL = "gpt-5.6-sol"
EFFORT = "high"

T0 = 137.0  # 18 仲夏

# chapter plan: (idx, date_label, day_eval(evening), loc_frac 0=town 1=cap,
#                fords_this_chapter, beat)
PLAN = [
    (1, "18仲夏晨", 137.2, 0.0, 0, "转生觉醒：双日之下的克莱亚（样章蓝本重写）"),
    (2, "18仲夏午", 137.45, 0.0, 0, "小镇速写与集市；正午王都急报抵镇（老王病危，14仲夏发出，路上走了4天）"),
    (3, "18仲夏午后", 137.6, 0.0, 0, "商队决定连夜动身抢在国丧封关前入都；主角以帮工身份争得随行；与伊莎仓促告别"),
    (4, "18仲夏黄昏-夜", 137.8, 0.03, 0, "黄昏出发；亮夜行路（澜月半亏、缇月近满）；商队夜行规矩"),
    (5, "19仲夏", 138.8, 0.1, 1, "第1次渡河；渡口的规费与摆渡人；主角第一次真正意义上的『赶路』"),
    (6, "19-20仲夏", 139.6, 0.18, 0, "雨林官道日常；缇月计日的民俗；向导这本『活里程表』"),
    (7, "20仲夏", 140.5, 0.27, 2, "第2、3次渡河；涨水与排队的商队；同行者暗线初伏"),
    (8, "21仲夏", 141.4, 0.33, 0, "爬升垭口：气候突变，第一次凉爽，衣物终于干了；温带林物产"),
    (9, "21仲夏夜", 141.7, 0.36, 0, "垭口夜话：德留尔四城与西北大城佩维克的传说；夜已很暗"),
    (10, "22仲夏", 142.5, 0.5, 3, "下行入盆地：暴雨，第4-6次渡河，一次险情"),
    (11, "23仲夏", 143.3, 0.66, 0, "最湿的盆地段（年降水近2900毫米）；同行者的秘密露出一角"),
    (12, "23仲夏夜-24仲夏晨", 143.6, 0.8, 0, "双月俱暗的前夜；后方有骑手逼近的征兆；商队内部分歧"),
    (13, "24仲夏日-深夜", 144.0, 0.97, 3, "第7-9次渡河；无月之夜抵达王都城门：全卷最大危机（盘查/追兵在黑暗中交错）"),
    (14, "25仲夏", 145.0, 1.0, 0, "初入拉托亚：河港都城的风物与瓦亚克祭司体系"),
    (15, "26-30仲夏", 148.0, 1.0, 0, "安身与寻访：学者/祭司线开启，转生之谜的第一块碎片"),
    (16, "1-6季夏", 153.0, 1.0, 0, "老王驾崩，国丧；两派（亲克里玛派/本土派）浮出；主角以信息差直觉立功或惹祸"),
    (17, "7-14季夏", 160.0, 1.0, 0, "卷入漩涡；伊莎的家书（在路上走了四天）；佩玛里什商人的另一种说法"),
    (18, "15-24季夏", 168.0, 1.0, 0, "加冕筹备：祭司宣布『双日交蚀』吉日（27季夏）；主角发现阴谋关窍"),
    (19, "27季夏", 176.7, 1.0, 0, "日食加冕日：白昼转暗、伴日如血的高潮对决"),
    (20, "28-30季夏", 179.0, 1.0, 0, "尘埃落定；转生线索指向克里玛王国；卷末钩子"),
]

STYLE = ("你是一位中文奇幻小说作者，擅长转生异世界题材。文风扎实，重风土的具体"
         "质感：气候的体感、天空的异象、市镇的声音与气味、距离与时间的真实重量。"
         "叙事第一人称（林晚舟/凯尔），时态沉稳，不滥用感叹号，不写设定说明书腔。")


# ------------------------------------------------------------- LLM call ----

def llm(messages, max_tokens=20000, retries=4, tag=""):
    payload = {"model": MODEL, "reasoning_effort": EFFORT,
               "max_completion_tokens": max_tokens, "messages": messages}
    for attempt in range(retries):
        try:
            t0 = time.time()
            r = requests.post(f"{API}/chat/completions",
                              headers={"Authorization": f"Bearer {KEY}",
                                       "Content-Type": "application/json"},
                              json=payload, timeout=2400)
            if r.status_code >= 500:  # transient server error -> retry
                print(f"[llm:{tag}] HTTP {r.status_code}: {r.text[:200]}",
                      flush=True)
                raise requests.ConnectionError(f"server {r.status_code}")
            if r.status_code != 200:  # 4xx: not retryable
                print(f"[llm:{tag}] HTTP {r.status_code}: {r.text[:300]}",
                      flush=True)
                r.raise_for_status()
            data = r.json()
            usage = data.get("usage", {})
            print(f"[llm:{tag}] {time.time()-t0:.0f}s, "
                  f"out={usage.get('completion_tokens')} "
                  f"(reasoning={usage.get('completion_tokens_details', {}).get('reasoning_tokens')})",
                  flush=True)
            return data["choices"][0]["message"]["content"]
        except (requests.ConnectionError, requests.Timeout) as exc:
            print(f"[llm:{tag}] attempt {attempt+1} failed: {exc}", flush=True)
            time.sleep(10 * (attempt + 1))
    raise RuntimeError(f"llm call failed after {retries} attempts: {tag}")


# ------------------------------------------------------------ the world ----

def load_world():
    from novel_demo import build_world, pick_town
    import astro
    import geoquery
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
    return s, geo, town, capital, rh, rw, obs


PHASE_CN = {"new": "朔（全暗）", "waxing crescent": "娥眉月（渐盈）",
            "first quarter": "上弦月", "waxing gibbous": "盈凸月",
            "full": "满月", "waning gibbous": "亏凸月",
            "last quarter": "下弦月", "waning crescent": "残月（渐亏）"}


def chapter_facts(s, geo, town, capital, obs, plan_row):
    idx, date_label, day, frac, fords, beat = plan_row
    cal = s.store.calendar
    lon = town["lon"] + (capital["lon"] - town["lon"]) * frac
    lat = town["lat"] + (capital["lat"] - town["lat"]) * frac
    d = geo.describe(lon, lat)
    moons = []
    illum_max = 0.0
    for m in obs.spec.moons:
        fr, name = obs.moon_phase(day + 0.75, m)
        moons.append(f"{m['name']}：{PHASE_CN.get(name, name)}，照明度{fr:.2f}")
        illum_max = max(illum_max, fr)
    night = ("亮夜（可夜行，地面有月影）" if illum_max > 0.7 else
             "半明之夜" if illum_max > 0.3 else
             "极暗，几乎无月光" if illum_max > 0.12 else
             "无月之夜（伸手不见五指，只有星光）")
    dist_left = 488.7 * max(0.0, 1.0 - frac)
    facts = {
        "章节日期": f"{date_label}（尘世历 {cal.format_day(day)}）",
        "所在位置": ("克莱亚镇" if frac < 0.02 else
                     "拉托亚王都" if frac >= 0.99 else
                     f"官道上（已走约{frac*100:.0f}%，剩余约{dist_left:.0f}公里）"),
        "当地环境": f"{d['biome']}，海拔{d['elevation_m']:.0f}米，"
                    f"年均温{d['temperature_c']:.1f}°C，"
                    f"年降水{d['precipitation_mm_yr']:.0f}毫米",
        "今夜月相": moons,
        "夜色": night,
        "本章渡河次数": fords,
        "白昼天象": "双日：主日明亮，伴日暗红（-13等，可直视的血色小日）",
    }
    if idx == 19:
        facts["特殊天象"] = ("日食发生在本章当日午后（27季夏，day 176.7）："
                             "主日被月轮遮蔽、白昼骤暗数刻，暗红伴日在昏暗的"
                             "天空中格外刺目。此前任何章节都不得发生日食。")
    return facts


# --------------------------------------------------------------- outline ---

BRIEF_PATH = REPO / "docs" / "samples" / "vol1_story_brief.md"


def gen_outline():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    out_path = OUT_DIR / "outline.json"
    if out_path.exists():
        print("[outline] exists, skip")
        return json.loads(out_path.read_text(encoding="utf-8"))
    brief = BRIEF_PATH.read_text(encoding="utf-8")
    plan_txt = "\n".join(
        f"第{p[0]}章｜{p[1]}｜行程位置{p[3]*100:.0f}%｜本章渡河{p[4]}次｜{p[5]}"
        for p in PLAN)
    prompt = f"""以下是一部二十章短卷的完整故事需求书，请你产出精修细纲。

{brief}

【时间线修订（以本节为准，覆盖需求书中不一致处）】
王都急报（老王病危，14仲夏发出）于18仲夏正午抵达克莱亚；商队为抢在国丧
封关与物价波动前入都，当天黄昏提前动身；主角以帮工身份当天随行。因此
18仲夏黄昏出发，骑行7天，24仲夏深夜（双月俱朔的无月之夜）抵达王都城门。

【逐章硬约束（日期与渡河数不可改动）】
{plan_txt}

请输出 JSON 数组，恰好 20 个元素，每个元素：
{{"idx": 章号, "title": "章名(4-8字)", "date": "日期标签",
  "summary": "细纲150字左右：本章事件链与情感落点",
  "scenes": ["场景1", "场景2", "场景3"],   // 3-5条
  "hook": "章末钩子一句话"}}
只输出 JSON，不要其它文字。细纲要让二十章连成因果链：伏笔（同行者暗线、
两派之争、转生之谜）要有铺设与回收的章号呼应。"""
    text = llm([{"role": "system", "content": STYLE},
                {"role": "user", "content": prompt}],
               max_tokens=30000, tag="outline")
    m = re.search(r"\[.*\]", text, re.S)
    outline = json.loads(m.group(0))
    assert len(outline) == 20, f"outline has {len(outline)} chapters"
    for k, ch in enumerate(outline):
        assert int(ch["idx"]) == k + 1
    out_path.write_text(json.dumps(outline, ensure_ascii=False, indent=1),
                        encoding="utf-8")
    print(f"[outline] written {out_path}")
    return outline


# -------------------------------------------------------------- chapters ---

MOON_BRIGHT_BAN = re.compile(r"满月|月光皎洁|月色明亮|月光如水|皎洁的月")
MOON_DARK_BAN = re.compile(r"无月|没有月亮|不见月|双月俱暗|漆黑得没有一点月")
ECLIPSE_PAT = re.compile(r"日食|交蚀|天狗|食甚|日冕")


def cjk_len(text: str) -> int:
    return len(re.findall(r"[一-鿿]", text))


def validate(idx, text, facts):
    problems = []
    n = cjk_len(text)
    if n < 2600:
        problems.append(f"正文过短（{n}字，需≥3200字左右）")
    illum = max(float(m.split("照明度")[1]) for m in facts["今夜月相"])
    if illum < 0.15 and MOON_BRIGHT_BAN.search(text):
        problems.append("今夜近朔无月，但正文出现了明亮月色的描写")
    if illum > 0.85 and MOON_DARK_BAN.search(text):
        problems.append("今夜有近满之月，但正文声称无月/漆黑")
    if idx <= 14 and ECLIPSE_PAT.search(text):
        problems.append("第15章之前不得出现日食相关描写（日食在第19章）")
    if idx == 19 and not re.search(r"食|蚀", text):
        problems.append("第19章必须写到日食")
    return problems


def chapter_prompt(outline, idx, facts, prev_tail, recent_summaries):
    ch = outline[idx - 1]
    toc = "\n".join(f"第{c['idx']}章 {c['title']}（{c['date']}）"
                    for c in outline)
    scenes = "\n".join(f"- {s}" for s in ch["scenes"])
    recent = "\n".join(recent_summaries) if recent_summaries else "（本章是开卷第一章）"
    tail = f"\n【上一章结尾原文（衔接语气用）】\n……{prev_tail}\n" if prev_tail else ""
    return f"""你在写二十章短卷《双日之下·王都之路》的第{idx}章。

【全卷目录】
{toc}

【前情提要（最近数章）】
{recent}
{tail}
【本章细纲】
标题：{ch['title']}（{ch['date']}）
{ch['summary']}
场景：
{scenes}
章末钩子：{ch['hook']}

【本章事实包（模拟数据，必须遵守）】
{json.dumps(facts, ensure_ascii=False, indent=1)}

【硬规则】
- 专名音译：Craiar克莱亚/Ratoia拉托亚/Dreul德留尔/Vaiaic瓦亚克/Pewick佩维克/
  Braiar布莱亚/Krimar克里玛/Pemarish佩玛里什/Fepuamar费普阿玛
- 天象只能按事实包写：月相、夜色、双日；日食仅存在于第19章当日
- 消息传播基准：王都↔克莱亚快马4天；不得出现更快的信息传递
- 王国只有四城（拉托亚/佩维克/布莱亚/克莱亚）与邻国克里玛，不得新增城市国家
- 夜空没有明亮的行星（本卷期间行星皆黯）
- 正文3200-4500字；以「## 第{idx}章 {ch['title']}」开头；只输出正文，
  不要任何解释或前后缀
"""


def gen_chapters(s, geo, town, capital, obs, outline):
    prev_tail = ""
    for row in PLAN:
        idx = row[0]
        path = OUT_DIR / f"ch{idx:02d}.md"
        if path.exists():
            prev_tail = path.read_text(encoding="utf-8")[-500:]
            print(f"[ch{idx:02d}] exists, skip")
            continue
        facts = chapter_facts(s, geo, town, capital, obs, row)
        recent = [f"第{c['idx']}章 {c['title']}：{c['summary']}"
                  for c in outline[max(0, idx - 4):idx - 1]]
        prompt = chapter_prompt(outline, idx, facts, prev_tail, recent)
        text = llm([{"role": "system", "content": STYLE},
                    {"role": "user", "content": prompt}],
                   tag=f"ch{idx:02d}")
        problems = validate(idx, text, facts)
        if problems:
            print(f"[ch{idx:02d}] violations: {problems}; rewriting", flush=True)
            fix = prompt + ("\n【上一稿违规，必须修正】\n"
                            + "\n".join(f"- {p}" for p in problems))
            text = llm([{"role": "system", "content": STYLE},
                        {"role": "user", "content": fix}],
                       tag=f"ch{idx:02d}-fix")
            problems = validate(idx, text, facts)
        if not text.strip().startswith("##"):
            text = f"## 第{idx}章 {outline[idx-1]['title']}\n\n" + text
        path.write_text(text.strip() + "\n", encoding="utf-8")
        (OUT_DIR / "validation.log").open("a", encoding="utf-8").write(
            f"ch{idx:02d}: {cjk_len(text)}字, "
            f"{'OK' if not problems else problems}\n")
        prev_tail = text[-500:]
        print(f"[ch{idx:02d}] done, {cjk_len(text)} chars", flush=True)


# --------------------------------------------------------------- assemble --

def assemble(s, geo, town, capital, rh, outline):
    import novelkit
    project = novelkit.Project("双日之下·卷一", s)
    hero = project.create_character("林晚舟/凯尔", town["lon"], town["lat"],
                                    day=T0)
    project.set_location(hero, T0 + rh["days"], capital["lon"], capital["lat"],
                         place=capital["name"])
    travel = project.check_travel(hero)
    verdict = "通过 ✓" if not travel else f"违规: {travel}"

    chapters = []
    total = 0
    for row in PLAN:
        text = (OUT_DIR / f"ch{row[0]:02d}.md").read_text(encoding="utf-8")
        chapters.append(text)
        total += cjk_len(text)
    val_log = (OUT_DIR / "validation.log").read_text(encoding="utf-8") \
        if (OUT_DIR / "validation.log").exists() else ""
    toc = "\n".join(f"- 第{c['idx']}章 {c['title']}（{c['date']}）"
                    for c in outline)
    body = "\n\n".join(chapters)
    out = REPO / "docs" / "samples" / "vol1_shuangri.md"
    out.write_text(f"""# 双日之下 · 卷一 王都之路

> 二十章短卷，正文约{total}字。世界由 WorldEngine 模拟生成（seed 909），
> 时间线/路程/月相/日食均为模拟事实；细纲与正文由 {MODEL}
> (reasoning {EFFORT}) 生成，每章注入当日事实包并经机器校验。

## 目录

{toc}

---

{body}

---

## 附录 · 生成与校验

- novelkit 行程终检（主角 18仲夏克莱亚 → 24仲夏拉托亚，489公里骑行）：{verdict}
- 各章校验日志：

```
{val_log}```
""", encoding="utf-8")
    print(f"[assemble] {out} ({total} chars)")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--phase", default="all",
                    choices=["outline", "chapters", "assemble", "all"])
    args = ap.parse_args()
    s, geo, town, capital, rh, rw, obs = load_world()
    outline = gen_outline()
    if args.phase in ("chapters", "all"):
        gen_chapters(s, geo, town, capital, obs, outline)
    if args.phase in ("assemble", "all"):
        assemble(s, geo, town, capital, rh, outline)


if __name__ == "__main__":
    main()
