"""RP kernel gates: seat isolation (inner never leaks), scene lifecycle,
deterministic movement physics, courier-delayed news distribution, thread
ledger, statecard deltas, auto-seat three-layer parsing, author edit
rights, transcript roundtrip, and staged-changeset integration."""

import json
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "build-linux"))
sys.path.insert(0, str(REPO / "tools" / "terrain_server"))

import weterrain  # noqa: E402
import civ  # noqa: E402
import novelkit  # noqa: E402
import rpkit  # noqa: E402
from llmpool import LLMPool  # noqa: E402
from worldstore import WorldStore  # noqa: E402

MOCK_CFG = {
    "providers": {"mock": {"protocol": "mock"}},
    "roles": {"arbiter": {"provider": "mock", "model": "m"},
              "kp": {"provider": "mock", "model": "m"},
              "actor_minor": {"provider": "mock", "model": "m"},
              "actor_major": {"provider": "mock", "model": "m"}},
    "seat_overrides": {},
}


class FakeSession:
    def __init__(self):
        self.params = {
            "world.seed": 909, "world.width": 256, "world.height": 128,
            "world.physics_grid_frequency": 88,
            "tectonics.grid_frequency": 48,
            "erosion.multigrid_levels": 4, "erosion.fixed_point_iterations": 4,
        }
        self.result = None
        self.features = []
        self.entities = []
        self.store = WorldStore()
        self.geo = None


@pytest.fixture(scope="module")
def session():
    s = FakeSession()
    p = weterrain.Params()
    for k, v in s.params.items():
        p.set(k, v)
    s.result = weterrain.generate(p)
    civ.generate_civilization(s)
    return s


@pytest.fixture(scope="module")
def towns(session):
    setts = session.store.find("settlement")
    by_pol = {}
    for t in setts:
        by_pol.setdefault(t.get("polity_id"), []).append(t)
    group = max(by_pol.values(), key=len)
    group.sort(key=lambda t: t.get("rank", 9))
    import geoquery
    geo = session.geo or geoquery.GeoIndex(session)
    session.geo = geo
    for a in group:
        for b in group:
            if a["id"] != b["id"]:
                r = geo.route((a["lon"], a["lat"]), (b["lon"], b["lat"]),
                              "horse")
                if r["reachable"] and r["days"] >= 2:
                    return a, b, r
    pytest.skip("no reachable settlement pair")


def make_rp(session, pool=None):
    return rpkit.Roleplay(session, pool=pool)


def stage_setup(rp, a, b):
    rp.add_seat("KP", kind="kp", automation="assist")
    rp.add_seat("凯尔", persona="转生者", goals="去王都",
                place=(a["lon"], a["lat"]))
    rp.add_seat("赛芮", persona="逃亡文书", goals="送出密卷",
                place=(a["lon"], a["lat"]))
    rp.add_seat("俄洛", persona="王都学者", goals="研究古碑",
                place=(b["lon"], b["lat"]))


def test_three_layer_isolation(session, towns):
    a, b, _ = towns
    rp = make_rp(session)
    stage_setup(rp, a, b)
    rp.open_scene((a["lon"], a["lat"]), ["凯尔", "赛芮"], "雨夜的货栈",
                  day=137.0)
    rp.say("凯尔", "action", "把湿透的外衣挂到火塘边")
    rp.say("凯尔", "dialogue", "「明天的渡口能过吗？」")
    rp.say("凯尔", "inner", "（其实我根本不敢想七天之后的事。）")
    sai_feed = "".join(e["text"] for e in rp.seats["赛芮"].feed)
    assert "火塘" in sai_feed and "渡口" in sai_feed
    assert "不敢想" not in sai_feed          # inner never leaks
    own_feed = "".join(e["text"] for e in rp.seats["凯尔"].feed)
    assert "不敢想" in own_feed
    assert "不敢想" not in json.dumps(
        [e for e in rp.seats["俄洛"].feed], ensure_ascii=False)


def test_move_physics(session, towns):
    a, b, route = towns
    rp = make_rp(session)
    stage_setup(rp, a, b)
    verdict = rp.move("凯尔", (b["lon"], b["lat"]),
                      claimed_days=route["days"] * 0.3)
    assert verdict["verdict"] == "reject" and verdict["reason"] == "too_fast"
    assert rp.seats["凯尔"].place == (a["lon"], a["lat"])  # did not move
    ok = rp.move("凯尔", (b["lon"], b["lat"]),
                 claimed_days=route["days"] * 1.2)
    assert ok["verdict"] == "ok"
    assert rp.seats["凯尔"].place == (b["lon"], b["lat"])


def test_news_arrives_with_courier_delay(session, towns):
    a, b, _ = towns
    rp = make_rp(session)
    stage_setup(rp, a, b)
    rp.open_scene((b["lon"], b["lat"]), ["赛芮"], "王都广场的告示", day=140.0)
    rp.say("赛芮", "action", "抄下告示全文")
    rp.close_scene(0.5, events=[{"desc": "新王病危的告示贴满王都",
                                 "visibility": "public"}])
    news = rp.seats["凯尔"].pending  # 凯尔 is off-stage at town a
    assert len(news) == 1
    arrives = news[0]["arrives_day"]
    assert arrives > 140.5 + 1.0  # a real courier delay, not instant
    rp.open_scene((a["lon"], a["lat"]), ["凯尔"], "镇上的清晨",
                  day=arrives + 0.5)
    feed_text = "".join(e["text"] for e in rp.seats["凯尔"].feed)
    assert "病危" in feed_text
    assert not rp.seats["凯尔"].pending


def test_threads_and_deltas(session, towns):
    a, b, _ = towns
    rp = make_rp(session)
    stage_setup(rp, a, b)
    th = rp.plant_thread("赛芮的密卷来历不明")
    rp.open_scene((a["lon"], a["lat"]), ["凯尔", "赛芮"], "夜谈", day=137.0)
    rp.close_scene(0.3, deltas={"凯尔": {"mood": "warier",
                                         "knowledge+": "赛芮在王印署当过差"}})
    assert rp.seats["凯尔"].card.mood == "warier"
    assert "王印署" in rp.seats["凯尔"].card.knowledge[-1]
    assert len(rp.open_threads()) == 1
    rp.pay_thread(th["id"])
    assert rp.open_threads() == []


def test_auto_seat_lands_three_layers(session, towns):
    a, b, _ = towns
    pool = LLMPool(MOCK_CFG)
    pool.mock.script["路人甲"] = [
        "行动：往火塘里添了根柴\n台词：「雨要下到明早。」\n内心：（这两个外乡人有古怪。）"]
    rp = make_rp(session, pool)
    stage_setup(rp, a, b)
    rp.add_seat("路人甲", automation="auto", place=(a["lon"], a["lat"]))
    rp.open_scene((a["lon"], a["lat"]), ["凯尔", "路人甲"], "货栈", day=137.0)
    draft = rp.suggest("路人甲")
    assert draft["landed"]
    scene = rp.scenes[-1]
    layers = [e["layer"] for e in scene["entries"] if e["seat"] == "路人甲"]
    assert set(layers) == {"action", "dialogue", "inner"}
    kaier_feed = "".join(e["text"] for e in rp.seats["凯尔"].feed)
    assert "雨要下到明早" in kaier_feed and "有古怪" not in kaier_feed


def test_assist_seat_only_drafts(session, towns):
    a, b, _ = towns
    pool = LLMPool(MOCK_CFG)
    rp = make_rp(session, pool)
    stage_setup(rp, a, b)
    rp.open_scene((a["lon"], a["lat"]), ["凯尔"], "清晨", day=137.0)
    before = len(rp.scenes[-1]["entries"])
    draft = rp.suggest("凯尔")
    assert not draft["landed"] and draft["text"]
    assert len(rp.scenes[-1]["entries"]) == before  # nothing landed


def test_author_edit_propagates(session, towns):
    a, b, _ = towns
    rp = make_rp(session)
    stage_setup(rp, a, b)
    rp.open_scene((a["lon"], a["lat"]), ["凯尔", "赛芮"], "货栈", day=137.0)
    entry = rp.say("凯尔", "dialogue", "「我叫凯尔。」")
    rp.edit_entry(entry["id"], "「我叫……凯尔。大概。」")
    assert entry["edited"]
    sai_feed = "".join(e["text"] for e in rp.seats["赛芮"].feed)
    assert "大概" in sai_feed and "「我叫凯尔。」" not in sai_feed


def test_transcript_roundtrip(session, towns, tmp_path):
    a, b, _ = towns
    rp = make_rp(session)
    stage_setup(rp, a, b)
    rp.plant_thread("线头一")
    rp.open_scene((a["lon"], a["lat"]), ["凯尔"], "开场", day=137.0)
    rp.say("凯尔", "inner", "（记录测试。）")
    rp.close_scene(1.0, events=[{"desc": "小事一桩", "visibility": "cast"}])
    for line in rp.transcript_jsonl().splitlines():
        json.loads(line)
    path = tmp_path / "rp.json"
    rp.save(path)
    rp2 = rpkit.Roleplay(session)
    rp2.load(path)
    assert rp2.clock == rp.clock
    assert rp2.seats["凯尔"].feed == rp.seats["凯尔"].feed
    assert rp2.open_threads()[0]["desc"] == "线头一"


def test_events_stage_changesets_author_merges(session, towns):
    a, b, _ = towns
    rp = make_rp(session)
    project = novelkit.Project("rp-canon", session)
    rp.project = project
    stage_setup(rp, a, b)
    rp.open_scene((a["lon"], a["lat"]), ["凯尔"], "货栈", day=137.0)
    settle = rp.close_scene(0.5, events=[{
        "desc": "凯尔在货栈登记为商队帮工", "visibility": "cast",
        "ops": []}])
    cs_id = settle["events"][0]["changeset"]
    cs = project._find(cs_id)
    assert cs["status"] == "staged"      # AI/world wrote staging only
    project.merge(cs_id)                 # the author decides
    assert project._find(cs_id)["status"] == "merged"
