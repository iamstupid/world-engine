"""RP-4: turn a roleplay transcript into prose (docs/RP_DESIGN.md §6).

The transcript is denser than a fact pack: it carries who did what, the
exact dialogue, and the POV seat's private inner layer. Novelization is
therefore a POV rewrite, not invention:

  chapterize(transcript, pov)  ->  per-scene material bundles containing
      ONLY what the POV could know: everyone's actions/dialogue/narration,
      the POV's own inner layer, news that reached the POV in time, the
      scene's sky/lighting/skyline reports.
  novelize(...)                ->  one chapter per bundle via the pool's
      "novelist" role, with hard rules (no invented events, no access to
      other seats' minds) and light validation.

POV discipline is enforced by construction: other seats' inner layers are
filtered out before the model ever sees the material.
"""

from __future__ import annotations

import json
import re
from pathlib import Path

STYLE = ("你是一位中文奇幻小说作者。文风扎实，重风土与心理的具体质感，"
         "第三人称贴身视角（跟随指定视点人物），不滥用感叹号，"
         "不写设定说明书腔。")


def load_transcript(path) -> list[dict]:
    """Accepts either a Roleplay.save() JSON object or transcript JSONL."""
    text = Path(path).read_text(encoding="utf-8")
    try:
        whole = json.loads(text)
        if isinstance(whole, dict) and "transcript" in whole:
            return whole["transcript"]
        if isinstance(whole, list):
            return whole
    except json.JSONDecodeError:
        pass
    return [json.loads(line) for line in text.splitlines() if line.strip()]


def chapterize(transcript: list[dict], pov: str) -> list[dict]:
    """POV-filtered material bundles, one per scene the POV attended."""
    news_for_pov = []
    for rec in transcript:
        if rec.get("type") == "settle":
            for d in rec.get("distributed", []):
                if d["to"] == pov:
                    news_for_pov.append(dict(d))
    bundles = []
    for rec in transcript:
        if rec.get("type") != "scene":
            continue
        if pov not in rec.get("cast", []):
            continue
        visible = []
        for say in transcript:
            if say.get("type") != "say" or say.get("scene") != rec["id"]:
                continue
            if say["layer"] == "inner" and say["seat"] != pov:
                continue  # other minds are closed books
            visible.append({"who": say["seat"], "layer": say["layer"],
                            "text": say["text"]})
        settle = next((x for x in transcript
                       if x.get("type") == "settle" and
                       x.get("scene") == rec["id"]), None)
        arrived = [n for n in news_for_pov
                   if n["arrives_day"] <= rec["day"]]
        for n in arrived:
            news_for_pov.remove(n)
        refpack = rec.get("refpack") or {}
        bundles.append({
            "scene_id": rec["id"], "day": rec["day"], "place": rec["place"],
            "cast": rec["cast"], "opening": rec["opening"],
            "lighting": (refpack.get("lighting") or {}).get("报告", ""),
            "skyline": refpack.get("skyline_report", []),
            "news_arrived_before": [n["desc"] for n in arrived],
            "visible": visible,
            "settle_summary": (settle or {}).get("summary", ""),
            "canon_events": [e["desc"] for e in
                             (settle or {}).get("events", [])],
        })
    return bundles


def _bundle_text(b: dict) -> str:
    lines = [f"【时间】第{b['day']:.1f}天　【在场】{'、'.join(b['cast'])}",
             f"【开场】{b['opening']}"]
    if b["lighting"]:
        lines.append(f"【光照天象（必须遵守）】{b['lighting']}")
    if b["skyline"]:
        lines.append(f"【远景地形（必须遵守）】{'；'.join(b['skyline'][:2])}")
    for n in b["news_arrived_before"]:
        lines.append(f"【视点人物此前得知的消息】{n}")
    lines.append("【实录（视点人物所能知道的一切；『内心』仅视点人物本人的）】")
    tag = {"action": "行动", "dialogue": "台词", "inner": "内心",
           "narration": "旁白"}
    for v in b["visible"]:
        lines.append(f"  {v['who']}·{tag[v['layer']]}：{v['text']}")
    if b["canon_events"]:
        lines.append("【本场正典事件】" + "；".join(b["canon_events"]))
    if b["settle_summary"]:
        lines.append(f"【场景小结】{b['settle_summary']}")
    return "\n".join(lines)


def chapter_prompt(bundle: dict, pov: str, prev_tail: str,
                   min_chars: int, max_chars: int) -> str:
    tail = (f"\n【上一章结尾（衔接语气）】\n……{prev_tail}\n" if prev_tail else "")
    return f"""你在把一段受控角色扮演的实录改写成小说章节。视点人物：{pov}。
{tail}
【本章素材（完整且排他——不得发明素材之外的事件、地名、天象）】
{_bundle_text(bundle)}

【硬规则】
- 贴身第三人称跟随{pov}；其他人物的心理只能通过言行推测，不得直写；
- {pov}的内心活动以素材中的『内心』条目为纲，可以扩写但不得改变其立场；
- 台词可润色但不得改变语义；事件顺序保持实录顺序；
- 天象、光照、地形描写必须与素材一致；
- 正文{min_chars}-{max_chars}字，只输出正文，以「## 」开头自拟章题。"""


BAN_OMNISCIENT = re.compile(r"(他|她)心里(想|暗想|明白|清楚)[:：]")


def validate_chapter(text: str, bundle: dict, pov: str,
                     min_chars: int) -> list[str]:
    problems = []
    n = len(re.findall(r"[一-鿿]", text))
    if n < min_chars * 0.7:
        problems.append(f"正文过短（{n}字）")
    for v in bundle["visible"]:
        if v["layer"] == "inner" and v["who"] != pov:
            problems.append("素材泄漏：包含了非视点人物的内心层")
    return problems


def novelize(transcript: list[dict], pov: str, pool, out_dir: Path,
             min_chars: int = 1800, max_chars: int = 3200) -> list[Path]:
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    bundles = chapterize(transcript, pov)
    paths = []
    prev_tail = ""
    for i, bundle in enumerate(bundles, 1):
        path = out_dir / f"novel_ch{i:02d}.md"
        if path.exists():
            prev_tail = path.read_text(encoding="utf-8")[-400:]
            paths.append(path)
            continue
        prompt = chapter_prompt(bundle, pov, prev_tail, min_chars, max_chars)
        text = pool.chat("novelist",
                         [{"role": "system", "content": STYLE},
                          {"role": "user", "content": prompt}],
                         max_tokens=12000)
        problems = validate_chapter(text, bundle, pov, min_chars)
        if problems:
            fix = prompt + "\n【上一稿问题，必须修正】\n" + "\n".join(problems)
            text = pool.chat("novelist",
                             [{"role": "system", "content": STYLE},
                              {"role": "user", "content": fix}],
                             max_tokens=12000)
        if not text.strip().startswith("##"):
            text = f"## 第{i}章\n\n" + text
        path.write_text(text.strip() + "\n", encoding="utf-8")
        prev_tail = text[-400:]
        paths.append(path)
    return paths


if __name__ == "__main__":
    import argparse
    import sys

    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from llmpool import LLMPool

    ap = argparse.ArgumentParser()
    ap.add_argument("transcript")
    ap.add_argument("--pov", required=True)
    ap.add_argument("--out", default="novelized")
    args = ap.parse_args()
    tr = load_transcript(args.transcript)
    files = novelize(tr, args.pov, LLMPool(), Path(args.out))
    print("\n".join(str(f) for f in files))
