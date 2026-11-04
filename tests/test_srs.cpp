#include <set>
#include "train/srs.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace vcube;

TEST_CASE("new cards go through learning steps then graduate") {
  SrsConfig cfg;
  SrsCard c;
  review(c, Grade::Good, 0, cfg);
  CHECK(c.state == CardState::Learning);
  CHECK(c.due == 600);
  review(c, Grade::Good, 600, cfg);
  CHECK(c.state == CardState::Review);
  CHECK(c.intervalDays == 1.0);
  CHECK(c.due == 600 + kSecondsPerDay);
  review(c, Grade::Good, c.due, cfg);
  CHECK(c.intervalDays == 2.5);
  review(c, Grade::Again, c.due, cfg);
  CHECK(c.state == CardState::Relearning);
  CHECK(c.lapses == 1);
  CHECK(c.ease < 2.5);
}

TEST_CASE("grades come from time against the target") {
  CHECK(gradeFromTime(1.0, 3.0, false) == Grade::Easy);
  CHECK(gradeFromTime(2.5, 3.0, false) == Grade::Good);
  CHECK(gradeFromTime(5.0, 3.0, false) == Grade::Hard);
  CHECK(gradeFromTime(1.0, 3.0, true) == Grade::Again);
}

TEST_CASE("SRS picker serves due cards, then new ones up to the limit") {
  SrsConfig cfg;
  cfg.newPerDay = 1;
  CasePicker p;
  p.setMode(PickMode::Srs);
  p.setPool({"a", "b", "c"});
  SetProgress prog;
  std::mt19937_64 rng(1);
  auto r = p.next(prog, 1000, 0, cfg, rng);
  REQUIRE(r.caseId);
  CHECK(*r.caseId == "a");
  CHECK(r.isNew);
  // Drill "a": record and review.
  prog["a"].record({1000, 2000, false});
  review(prog["a"].card, Grade::Good, 1000, cfg);
  // Nothing due (a is in 10 min, beyond learn-ahead? 600 < 1200 so due).
  r = p.next(prog, 1001, 0, cfg, rng);
  CHECK(*r.caseId == "a");
  cfg.learnAheadSeconds = 0;
  r = p.next(prog, 1001, 0, cfg, rng);
  CHECK_FALSE(r.caseId); // daily new limit reached
  CHECK(r.nextDue == 1600);
}

TEST_CASE("recap shows every case once per round") {
  CasePicker p;
  p.setMode(PickMode::Recap);
  p.setPool({"a", "b", "c", "d"});
  SetProgress prog;
  SrsConfig cfg;
  std::mt19937_64 rng(3);
  std::set<std::string> seen;
  for (int i = 0; i < 4; ++i) {
    seen.insert(*p.next(prog, 0, 0, cfg, rng).caseId);
  }
  CHECK(seen.size() == 4);
  CHECK(p.next(prog, 0, 0, cfg, rng).roundComplete);
}
