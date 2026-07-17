"""RP-4 gates: chapterize enforces POV discipline by construction (other
seats' inner layers never reach the material; courier-delayed news enters
the right chapter), and novelize writes validated chapters through the
pool's novelist role (mocked here)."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]
                       / "tools" / "terrain_server"))

import novelize  # noqa: E402
from llmpool import LLMPool  # noqa: E402

TRANSCRIPT = [
    {"type": "scene", "id": 1, "day": 137.0, "place": [10, 20],
     "cast": ["凯尔", "赛芮"], "opening": "货栈之夜",
     "refpack": {"lighting": {"报告": "夜晚；缇月盈凸"},
                 "skyline_report": ["东面有山"]},
     "status": "closed", "entries": []},
    {"type": "say", "id": 1, "scene": 1, "seat": "凯尔", "layer": "dialogue",
     "text": "「明早出发。」"},
    {"type": "say", "id": 2, "scene": 1, "seat": "凯尔", "layer": "inner",
     "text": "（我在害怕。）"},
    {"type": "say", "id": 3, "scene": 1, "seat": "赛芮", "layer": "inner",
     "text": "（他不能知道底簿的事。）"},
    {"type": "say", "id": 4, "scene": 1, "seat": "赛芮", "layer": "action",
     "text": "抱紧了行李。"},
    {"type": "settle", "scene": 1, "elapsed_days": 0.5, "day_after": 137.5,
     "events": [{"desc": "急报贴出", "visibility": "public"}],
     "distributed": [{"to": "凯尔", "arrives_day": 138.2, "desc": "远方的战报"}],
     "summary": "出发前夜"},
    {"type": "scene", "id": 2, "day": 139.0, "place": [10, 21],
     "cast": ["凯尔"], "opening": "官道清晨", "refpack": {},
     "status": "closed", "entries": []},
    {"type": "say", "id": 5, "scene": 2, "seat": "凯尔", "layer": "action",
     "text": "翻身上马。"},
    {"type": "settle", "scene": 2, "elapsed_days": 1.0, "day_after": 140.0,
     "events": [], "distributed": [], "summary": ""},
    {"type": "scene", "id": 3, "day": 140.0, "place": [11, 21],
     "cast": ["赛芮"], "opening": "凯尔不在场的场景", "refpack": {},
     "status": "closed", "entries": []},
]


def test_chapterize_pov_discipline():
    bundles = novelize.chapterize(TRANSCRIPT, "凯尔")
    assert len(bundles) == 2  # scene 3 (not attended) is excluded
    b1 = bundles[0]
    texts = [v["text"] for v in b1["visible"]]
    assert "（我在害怕。）" in texts            # own inner kept
    assert all("底簿" not in t for t in texts)  # other's inner filtered
    assert "抱紧了行李。" in texts              # other's action visible
    assert b1["lighting"] == "夜晚；缇月盈凸"
    assert b1["canon_events"] == ["急报贴出"]
    # the courier news (arrives 138.2) belongs to the day-139 chapter
    assert b1["news_arrived_before"] == []
    assert bundles[1]["news_arrived_before"] == ["远方的战报"]


def test_chapterize_other_pov():
    bundles = novelize.chapterize(TRANSCRIPT, "赛芮")
    assert [b["scene_id"] for b in bundles] == [1, 3]
    texts = [v["text"] for v in bundles[0]["visible"]]
    assert "（他不能知道底簿的事。）" in texts
    assert all("我在害怕" not in t for t in texts)


def test_novelize_writes_validated_chapters(tmp_path):
    cfg = {"providers": {"mock": {"protocol": "mock"}},
           "roles": {"novelist": {"provider": "mock", "model": "m"}},
           "seat_overrides": {}}
    pool = LLMPool(cfg)
    pool.mock.handler = lambda key, messages: (
        "## 试写章\n\n" + "夜色沉沉。" * 40)
    paths = novelize.novelize(TRANSCRIPT, "凯尔", pool, tmp_path,
                              min_chars=100)
    assert len(paths) == 2
    text = paths[0].read_text(encoding="utf-8")
    assert text.startswith("## ")
    # material fed to the model never contained the other seat's inner
    sent = "".join(json_dump(m) for m in pool.mock.calls[0]["messages"])
    assert "底簿" not in sent


def json_dump(m):
    return m["content"]
