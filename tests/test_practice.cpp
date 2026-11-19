#include "io/practice_store.hpp"
#include "test_files.hpp"
#include "train/f2l.hpp"
#include "train/practice.hpp"
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
    d.addTimed(timedKey(false, "zz-1"),
               timedTry(static_cast<std::int64_t>(i), 1000));
  }
  const auto &kept = d.timed.at("virtual/zz-1");
  CHECK(kept.size() == PracticeData::kPerGroup);
  CHECK(kept.front().timestamp == 5);
  CHECK(timedKey(true, "eo") == "physical/eo");
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
  const std::vector<std::string> pairs = pairCardPool(true);
  CHECK(pairs.size() == 166);
  CHECK(pairCardPool(false).size() == 334);
  std::optional<PairCard> first = cardFromKey(pairs[0]);
  std::optional<PairCard> second = cardFromKey(pairs[1]);
  REQUIRE(first);
  REQUIRE(second);
  CHECK(first->side == PairSide::Front);
  CHECK(second->side == PairSide::Back);
  CHECK(first->pcase == pairCases(true).front());
  CHECK(second->pcase == first->pcase);
  CHECK(pairs[0].starts_with("front/"));
  CHECK_FALSE(cardFromKey(caseKey(first->pcase))); // as keyed before
}

TEST_CASE("a card's try is graded by how it went", "[practice]") {
  CHECK(practiceGrade(true, false, false) == Grade::Good);
  CHECK(practiceGrade(false, false, false) == Grade::Again);
  CHECK(practiceGrade(true, true, false) == Grade::Again);
  CHECK(practiceGrade(true, false, true) == Grade::Again);
}

TEST_CASE("practice data survives the file", "[practice][io]") {
  PracticeData d;
  SrsConfig config;
  startLearning(d.cards["UF-N-0120"].card, 1794000000, config);
  review(d.cards["UF-N-0120"].card, Grade::Good, 1794000100, config);
  d.cards["UF-N-0120"].record({1794000100, 0, false, AttemptMode::Learn});
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
  d.addTimed(timedKey(true, "zz-1"), timedTry(1794000400, 14200));
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
