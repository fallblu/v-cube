#include "io/practice_store.hpp"
#include "test_files.hpp"
#include "train/f2l.hpp"
#include "train/practice.hpp"
#include "train/practice_flow.hpp"
#include "train/tsle.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QJsonArray>
#include <QTemporaryDir>

using namespace vcube;
using namespace vcube::testing;

namespace {

PracticeTry timedTry(std::int64_t at, std::int64_t ms, bool dnf = false) {
  PracticeTry t;
  t.timestamp = at;
  t.ms = ms;
  t.dnf = dnf;
  return t;
}

} // namespace

TEST_CASE("a group's tries are summed up", "[practice]") {
  std::vector<PracticeTry> tries;
  CHECK(summarize(tries).tries == 0);
  CHECK_FALSE(summarize(tries).meanMs);
  tries.push_back(timedTry(100, 9000));
  tries.push_back(timedTry(200, 5000, true)); // a DNF: counted, not timed
  tries.push_back(timedTry(300, 7000));
  PracticeSummary s = summarize(tries);
  CHECK(s.tries == 3);
  CHECK(s.solved == 2);
  CHECK(*s.meanMs == 8000.0);
  CHECK(*s.bestMs == 7000);
  CHECK_FALSE(s.meanMoves);
  // Only what was done since a time.
  PracticeSummary late = summarize(tries, 250);
  CHECK(late.tries == 1);
  CHECK(*late.meanMs == 7000.0);

  // The means are of the last few; the best is of them all.
  tries.clear();
  tries.push_back(timedTry(1, 1000));
  for (std::size_t i = 0; i < kRecentTries; ++i) {
    tries.push_back(timedTry(2, 4000));
  }
  CHECK(*summarize(tries).meanMs == 4000.0);
  CHECK(*summarize(tries).bestMs == 1000);
}

TEST_CASE("inspection and turning speed are summed up", "[practice]") {
  std::vector<PracticeTry> tries;
  PracticeTry a;
  a.inspectMs = 9000;
  a.moves = 7;
  tries.push_back(a);
  PracticeTry b;
  b.inspectMs = 17000;
  b.over = true;
  b.dnf = true; // not done: its inspection counts all the same
  tries.push_back(b);
  PracticeSummary s = summarize(tries);
  CHECK(*s.meanInspectMs == 13000.0);
  CHECK(s.overTime == 1);
  CHECK(s.solved == 1);
  CHECK_FALSE(s.meanTps); // untimed

  std::vector<PracticeTry> timed{timedTry(1, 10000), timedTry(2, 5000)};
  timed[0].moves = 30;
  timed[1].moves = 20;
  CHECK(*summarize(timed).meanTps == 3.5);
}

TEST_CASE("untimed tries are summed up by moves", "[practice]") {
  std::vector<PracticeTry> tries;
  PracticeTry a;
  a.moves = 9;
  a.best = 7;
  a.extra = 2;
  PracticeTry b;
  b.moves = 7;
  b.best = 7;
  b.extra = 0;
  PracticeTry unknown; // the best not worked out
  unknown.moves = 8;
  PracticeTry gaveUp;
  gaveUp.dnf = true;
  tries = {a, b, unknown, gaveUp};
  PracticeSummary s = summarize(tries);
  CHECK(s.tries == 4);
  CHECK(s.solved == 3);
  CHECK(*s.meanMoves == 8.0);
  CHECK(*s.meanExtra == 1.0);
  CHECK_FALSE(s.meanMs);
  CHECK_FALSE(s.bestMs);
}

TEST_CASE("a group keeps its newest tries", "[practice]") {
  PracticeData d;
  for (std::size_t i = 0; i < PracticeData::kPerGroup + 5; ++i) {
    d.addTimed(timedKey(CubeKind::Virtual, "zz-1"),
               timedTry(static_cast<std::int64_t>(i), 1000));
  }
  const auto &kept = d.timed.at("virtual/zz-1");
  CHECK(kept.size() == PracticeData::kPerGroup);
  CHECK(kept.front().timestamp == 5);
  CHECK(timedKey(CubeKind::Physical, "eo") == "physical/eo");
}

TEST_CASE("the newest try of a group is changed or taken back", "[practice]") {
  PracticeData d;
  const std::string key = timedKey(CubeKind::Virtual, "zz-1");
  CHECK(newestTry(d.timed, key) == nullptr);
  dropNewestTry(d.timed, key); // nothing to take back
  CHECK(d.timed.empty());
  d.addTimed(key, timedTry(1, 1000));
  d.addTimed(key, timedTry(2, 2000));
  d.addTimed(timedKey(CubeKind::Virtual, "zz-2"), timedTry(3, 3000));
  PracticeTry *newest = newestTry(d.timed, key);
  REQUIRE(newest);
  CHECK(newest->ms == 2000);
  newest->dnf = true;
  CHECK(d.timed.at(key).back().dnf);
  CHECK_FALSE(d.timed.at(key).front().dnf);
  dropNewestTry(d.timed, key);
  REQUIRE(d.timed.at(key).size() == 1);
  CHECK(d.timed.at(key).back().ms == 1000);
  // A group left without tries is gone, the others as they were.
  dropNewestTry(d.timed, key);
  CHECK_FALSE(d.timed.contains(key));
  CHECK(newestTry(d.timed, key) == nullptr);
  CHECK(d.timed.at("virtual/zz-2").size() == 1);
}

TEST_CASE("timed attempts are cleared a cube at a time", "[practice]") {
  PracticeData d;
  d.addTimed(timedKey(CubeKind::Virtual, "zz-1"), timedTry(1, 1000));
  d.addTimed(timedKey(CubeKind::Virtual, "eo"), timedTry(2, 2000));
  d.addTimed(timedKey(CubeKind::Physical, "zz-1"), timedTry(3, 3000));
  clearTimed(d.timed, CubeKind::Virtual);
  REQUIRE(d.timed.size() == 1);
  CHECK(d.timed.contains("physical/zz-1"));
  clearTimed(d.timed, CubeKind::Physical);
  CHECK(d.timed.empty());
}

TEST_CASE("cards come in by themselves, a few a day", "[practice]") {
  SrsConfig config;
  config.newPerDay = 2;
  config.maxLearning = 0;
  const std::vector<std::string> pool = tsleCardPool();
  REQUIRE(pool.size() == 97);
  const std::int64_t now = 1'800'000'000, dayStart = now - 100,
                     dayEnd = dayStart + kSecondsPerDay;
  SetProgress cards;
  PracticeWaiting w =
      practiceWaiting(pool, cards, now, dayStart, dayEnd, config);
  CHECK(w.total == 97);
  CHECK(w.started == 0);
  CHECK(w.due == 0);
  CHECK(w.newLeft == 2);

  CHECK(queueUnstarted(pool, cards, now, config));
  CHECK_FALSE(queueUnstarted(pool, cards, now, config));
  // The first case of the pool is the first to come in.
  LearnPick pick = pickLearnCase(pool, cards, now, dayStart, config);
  REQUIRE(pick.caseId);
  CHECK(*pick.caseId == pool.front());
  CHECK(pick.isNew);
  introduce(cards[pool.front()].card, now, config);
  pick = pickLearnCase(pool, cards, now, dayStart, config);
  REQUIRE(pick.caseId);
  CHECK(*pick.caseId == pool[1]);
  introduce(cards[pool[1]].card, now, config);
  // The day's two are in: the first one's learning step comes next.
  w = practiceWaiting(pool, cards, now, dayStart, dayEnd, config);
  CHECK(w.started == 2);
  CHECK(w.newLeft == 0);
  pick = pickLearnCase(pool, cards, now + 3600, dayStart, config);
  REQUIRE(pick.caseId);
  CHECK_FALSE(pick.isNew);
  CHECK(
      practiceWaiting(pool, cards, now + 3600, dayStart, dayEnd, config).due ==
      2);
}

TEST_CASE("the card pools are the cases in learning order", "[practice]") {
  // TSLE: fewest steps first, no OCLLs.
  int steps = 1;
  for (const std::string &key : tsleCardPool()) {
    std::optional<TsleState> c = tsleFromKey(key);
    REQUIRE(c);
    CHECK_FALSE(c->ocll());
    CHECK(tsleStepsLeft(*c) >= steps);
    steps = tsleStepsLeft(*c);
  }
  // F2L: each case for the front slots, then for the back ones.
  const std::vector<std::string> pairs = pairCardPool(Edges::Oriented);
  CHECK(pairs.size() == 166);
  CHECK(pairCardPool(Edges::Any).size() == 334);
  std::optional<PairCard> first = cardFromKey(pairs[0]);
  std::optional<PairCard> second = cardFromKey(pairs[1]);
  REQUIRE(first);
  REQUIRE(second);
  CHECK(first->side == PairSide::Front);
  CHECK(second->side == PairSide::Back);
  CHECK(first->pcase == pairCases(Edges::Oriented).front());
  CHECK(second->pcase == first->pcase);
  CHECK(pairs[0].starts_with("front/"));
  CHECK_FALSE(cardFromKey(caseKey(first->pcase))); // as keyed before
}

TEST_CASE("a card's try is graded by how it went", "[practice]") {
  CHECK(practiceGrade({.solvedWell = true}) == Grade::Good);
  CHECK(practiceGrade({}) == Grade::Again);
  CHECK(practiceGrade({.solvedWell = true, .hinted = true}) == Grade::Again);
  CHECK(practiceGrade({.solvedWell = true, .gaveUp = true}) == Grade::Again);
}

TEST_CASE("practice data survives the file", "[practice][io]") {
  PracticeData d;
  SrsConfig config;
  startLearning(d.cards["UF-N-0120"].card, 1794000000, config);
  review(d.cards["UF-N-0120"].card, Grade::Good, 1794000100, config);
  d.cards["UF-N-0120"].record({1794000100, 0, false, AttemptMode::Learn});
  Attempt timed{1794000150, 2100, false, AttemptMode::VirtualDrill};
  timed.recognitionMs = 600;
  timed.executionMs = 1500;
  d.cards["UF-N-0120"].record(timed);
  Attempt recognised{1794000160, 700, false, AttemptMode::Learn};
  recognised.recognitionMs = 700;
  recognised.recognitionOnly = true;
  d.cards["UF-N-0120"].record(recognised);
  d.cards["UF-N-0120"].archive[AttemptMode::VirtualDrill] = {
      1900, {{20764, 3, 1, 4100, 1900}}};
  PracticeTry learn;
  learn.timestamp = 1794000200;
  learn.moves = 31;
  learn.best = 24;
  learn.extra = 4.5;
  learn.hint = true;
  d.addLearn("whole", learn);
  PracticeTry dnf;
  dnf.timestamp = 1794000300;
  dnf.dnf = true;
  d.addLearn("whole", dnf);
  d.addTimed(timedKey(CubeKind::Physical, "zz-1"), timedTry(1794000400, 14200));
  PracticeTry planned;
  planned.timestamp = 1794000500;
  planned.inspectMs = 16200;
  planned.over = true;
  planned.moves = 8;
  planned.best = 7;
  planned.extra = 1.0;
  d.addInspection("eocross", planned);
  d.choices["front/URF1-UB0"] = {"U R U' R'", "R' F R F'"};
  d.notes["ocll/antisune"] = "L' U' L U' L' U2 L";
  CHECK(practiceFromJson(practiceToJson(d)) == d);
  CHECK(practiceToJson(d).value("inspection").toObject().contains("eocross"));
  CHECK_FALSE(practiceToJson(PracticeData{}).contains("choices"));

  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  QString path = QDir(dir.path()).filePath("tsle.json");
  PracticeStore store(path);
  CHECK(store.load());
  CHECK(store.data == PracticeData{});
  store.data = d;
  REQUIRE(store.save());
  PracticeStore again(path);
  REQUIRE(again.load());
  CHECK(again.data == d);
  // Nothing of the new format is taken for an old file.
  CHECK(removeOldPracticeFile(path).isEmpty());
  CHECK(QFile::exists(path));
}

TEST_CASE("a practice file is read with care", "[practice][io]") {
  QJsonObject o = practiceToJson(PracticeData{});
  QJsonArray many;
  for (int i = 0; i < 230; ++i) {
    many.append(QJsonObject{{"timestamp", i}, {"ms", -5}, {"moves", -1}});
  }
  o["timed"] = QJsonObject{{"virtual/eo", many}};
  PracticeData d = practiceFromJson(o);
  REQUIRE(d.timed.at("virtual/eo").size() == PracticeData::kPerGroup);
  CHECK(d.timed.at("virtual/eo").back().timestamp == 229);
  CHECK(d.timed.at("virtual/eo").back().ms == 0);
  CHECK(d.timed.at("virtual/eo").back().moves == 0);
  CHECK(d.timed.at("virtual/eo").back().extra < 0);
}

TEST_CASE("the files the modes kept before are dropped", "[practice][io]") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  QString path = QDir(dir.path()).filePath("tsle.json");
  writeText(path, R"({"format": "v-cube.tsle", "version": 1,
    "cases": {"UF-N-0120": [{"timestamp": 5}]},
    "ocll": {"antisune": "L' U' L U' L' U2 L", "sune": ""}})");
  QJsonObject old = removeOldPracticeFile(path);
  CHECK_FALSE(QFile::exists(path));
  // It is kept in a folder of its own, not removed.
  CHECK(QDir(QDir(dir.path()).filePath("deleted"))
            .entryList({"tsle.*.json"}, QDir::Files)
            .size() == 1);
  // The OCLL algs are carried over.
  auto notes = oldOcllNotes(old);
  REQUIRE(notes.size() == 1);
  CHECK(notes.at("ocll/antisune") == "L' U' L U' L' U2 L");

  for (const char *format : {"v-cube.f2l", "v-cube.eocross"}) {
    writeText(path, QByteArray(R"({"format": ")") + format + R"("})");
    CHECK(oldOcllNotes(removeOldPracticeFile(path)).empty());
    CHECK_FALSE(QFile::exists(path));
  }
  // Any other file is left alone.
  writeText(path, R"({"format": "v-cube.sessions"})");
  CHECK(removeOldPracticeFile(path).isEmpty());
  CHECK(QFile::exists(path));
  CHECK(
      removeOldPracticeFile(QDir(dir.path()).filePath("none.json")).isEmpty());
}

namespace {

TrainKeyEvent press(TrainKey key, bool shift = false) {
  TrainKeyEvent k;
  k.key = key;
  k.shift = shift;
  return k;
}

TrainKeyEvent digit(int d) {
  TrainKeyEvent k = press(TrainKey::Digit);
  k.digit = d;
  return k;
}

TrainKeyEvent turn(Move m) {
  TrainKeyEvent k = press(TrainKey::Other);
  k.move = m;
  return k;
}

PracticeFlow::State in(PracticePhase phase) {
  PracticeFlow::State s;
  s.phase = phase;
  return s;
}

} // namespace

TEST_CASE("the practice tabs' keys follow the phase", "[practice]") {
  using A = PracticeFlow::Action;
  using K = TrainKey;
  using P = PracticePhase;
  auto act = [](const TrainKeyEvent &k, const PracticeFlow::State &s) {
    return PracticeFlow::key(k, s).action;
  };
  const Move r{MoveType::R, 1};

  SECTION("releases do nothing") {
    TrainKeyEvent k = press(K::Space);
    k.press = false;
    CHECK_FALSE(PracticeFlow::key(k, in(P::Done)).handled);
  }
  SECTION("idle and ready") {
    CHECK(act(press(K::Space), in(P::Idle)) == A::NextEarly);
    CHECK(act(press(K::Enter), in(P::Idle)) == A::NextEarly);
    CHECK_FALSE(PracticeFlow::key(press(K::Escape), in(P::Idle)).handled);
    CHECK_FALSE(PracticeFlow::key(turn(r), in(P::Idle)).handled);
    CHECK(act(press(K::Space), in(P::Ready)) == A::Start);
    CHECK_FALSE(PracticeFlow::key(press(K::Enter), in(P::Ready)).handled);
  }
  SECTION("choosing") {
    PracticeFlow::State s = in(P::Choose);
    CHECK(act(press(K::Enter), s) == A::ConfirmChoice);
    const PracticeFlow::Step five = PracticeFlow::key(digit(5), s);
    CHECK(five.action == A::ChoiceKey);
    CHECK(five.digit == 5);
    CHECK_FALSE(PracticeFlow::key(digit(0), s).handled);
    CHECK(act(turn(r), s) == A::ChooseFirst);
    // A new case needs its solutions: Esc leaves only a change.
    const PracticeFlow::Step esc = PracticeFlow::key(press(K::Escape), s);
    CHECK((esc.handled && esc.action == A::None));
    s.choosingAgain = true;
    CHECK(act(press(K::Escape), s) == A::CancelChoice);
    for (K k : {K::Space, K::Tab, K::Backspace}) {
      const PracticeFlow::Step st = PracticeFlow::key(press(k), s);
      CHECK((st.handled && st.action == A::None));
    }
  }
  SECTION("a new case") {
    const PracticeFlow::State s = in(P::Demo);
    CHECK(act(press(K::Enter), s) == A::PlayDemo);
    CHECK(act(press(K::Space), s) == A::Introduced);
    CHECK(act(press(K::Escape), s) == A::ResetCube);
    CHECK(act(turn(r), s) == A::Turn);
  }
  SECTION("solving") {
    PracticeFlow::State s = in(P::Solving);
    // Space alone doesn't give up: a double tap mustn't.
    const PracticeFlow::Step space = PracticeFlow::key(press(K::Space), s);
    CHECK((space.handled && space.action == A::None));
    CHECK(act(press(K::Space, true), s) == A::GiveUp);
    CHECK(act(press(K::Backspace), s) == A::GiveUpKeyHint);
    s.shiftSpaceGivesUp = false;
    CHECK(act(press(K::Backspace), s) == A::GiveUp);
    CHECK(act(press(K::Space, true), s) == A::None);
    CHECK(act(press(K::Tab), s) == A::None);
    s.hints = true;
    CHECK(act(press(K::Tab), s) == A::Hint);
    CHECK(act(press(K::Escape), s) == A::ResetCube);
    s.tab = PracticeTab::Inspection;
    CHECK(act(press(K::Escape), s) == A::Retry);
    CHECK(act(press(K::Delete), s) == A::None);
    const PracticeFlow::Step t = PracticeFlow::key(turn(r), s);
    CHECK(t.action == A::Turn);
    CHECK(t.move == r);
  }
  SECTION("blind") {
    PracticeFlow::State s = in(P::Solving);
    s.hidden = true;
    s.hints = true;
    CHECK(act(press(K::Tab), s) == A::None);
    PracticeFlow::Step shown = PracticeFlow::key(press(K::Space), s);
    CHECK(shown.action == A::Reveal);
    CHECK(shown.gaveUp);
    s.done = true;
    shown = PracticeFlow::key(press(K::Space, true), s);
    CHECK(shown.action == A::Reveal);
    CHECK_FALSE(shown.gaveUp);
  }
  SECTION("done") {
    PracticeFlow::State s = in(P::Done);
    CHECK(act(press(K::Space), s) == A::Next);
    CHECK(act(press(K::Enter), s) == A::PlayBest);
    CHECK(act(press(K::Tab), s) == A::ToggleDetail);
    CHECK(act(press(K::Delete), s) == A::DeleteTry);
    CHECK(act(press(K::Escape), s) == A::Retry);
    CHECK(act(press(K::Backspace), s) == A::None);
    // 1-4 only for a graded card, 0 only with solutions to change.
    CHECK(act(digit(3), s) == A::ModeKey);
    CHECK_FALSE(PracticeFlow::key(digit(3), s).handled);
    s.graded = true;
    const PracticeFlow::Step g = PracticeFlow::key(digit(3), s);
    CHECK(g.action == A::Regrade);
    CHECK(g.grade == Grade::Good);
    CHECK(act(digit(5), s) == A::ModeKey);
    CHECK(act(digit(0), s) == A::ModeKey);
    s.chooser = true;
    CHECK(act(digit(0), s) == A::ChooseAgain);
    // The mode's own keys, then the turning keys.
    const PracticeFlow::Step m = PracticeFlow::key(turn(r), s);
    CHECK(m.action == A::ModeKey);
    CHECK(m.move == r);
  }
  SECTION("waiting for the verdict") {
    PracticeFlow::State s = in(P::Done);
    s.waiting = true;
    s.graded = true;
    CHECK(act(press(K::Space), s) == A::StillWorking);
    CHECK(act(press(K::Escape), s) == A::None);
    CHECK(act(press(K::Delete), s) == A::None);
    CHECK(act(digit(2), s) == A::None);
    CHECK(act(turn(r), s) == A::Turn);
  }
  SECTION("undo takes a grade back on the Learn tab only") {
    for (P phase : {P::Idle, P::Choose, P::Solving, P::Done}) {
      CHECK(act(press(K::Undo), in(phase)) == A::Undo);
    }
    PracticeFlow::State s = in(P::Done);
    s.tab = PracticeTab::Untimed;
    CHECK_FALSE(PracticeFlow::key(press(K::Undo), s).handled);
  }
}
