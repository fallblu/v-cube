#include "train/srs.hpp"
#include <map>

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

TEST_CASE("learn grades come from correctness, not time") {
  CHECK(learnGrade(false, false, false) == Grade::Good);
  CHECK(learnGrade(true, false, false) == Grade::Again);
  CHECK(learnGrade(false, true, false) == Grade::Again);
  CHECK(learnGrade(false, false, true) == Grade::Again);
  CHECK(learnGrade(true, true, true) == Grade::Again);
}

TEST_CASE("drill statistics ignore learn attempts") {
  CaseProgress p;
  p.record({10, 2000, false, AttemptMode::Drill});
  p.record({11, 0, true, AttemptMode::Learn});
  p.record({12, 0, false, AttemptMode::Learn});
  p.record({13, 4000, false, AttemptMode::Drill});
  p.record({14, 1000, true, AttemptMode::Drill});
  CHECK(p.drillCount() == 3);
  REQUIRE(p.recentMean());
  CHECK(*p.recentMean() == 3000.0);
  CHECK(p.best() == 2000);
  CHECK(p.failRate() == 1.0 / 3.0);
  // Only learn attempts: no drill statistics.
  CaseProgress learnOnly;
  learnOnly.record({1, 0, false, AttemptMode::Learn});
  CHECK_FALSE(learnOnly.recentMean());
  CHECK_FALSE(learnOnly.best());
  CHECK(learnOnly.failRate() == 0.0);
}

TEST_CASE("attempts are capped per mode") {
  CaseProgress p;
  p.record({0, 1234, false, AttemptMode::Drill});
  for (int i = 1; i <= 150; ++i) {
    p.record({i, 0, false, AttemptMode::Learn});
  }
  CHECK(p.attempts.size() == CaseProgress::kMaxAttempts + 1);
  CHECK(p.best() == 1234); // the one drill attempt survives
  CHECK(p.attempts.back().timestamp == 150);
}

TEST_CASE("learn picks learning cards, then due reviews, then the queue") {
  SrsConfig cfg;
  cfg.newPerDay = 1;
  cfg.learnAheadSeconds = 0;
  const std::int64_t now = 100000;
  SetProgress prog;
  std::vector<std::string> pool = {"queued1", "queued2", "learning", "review",
                                   "unstarted"};
  startLearning(prog["queued1"].card, 0, cfg);
  startLearning(prog["queued2"].card, 0, cfg);
  prog["learning"].card.state = CardState::Learning;
  prog["learning"].card.reps = 1;
  prog["learning"].card.due = now - 10;
  prog["review"].card.state = CardState::Review;
  prog["review"].card.reps = 3;
  prog["review"].card.due = now - 50; // due earlier, still after "learning"

  auto r = pickLearnCase(pool, prog, now, 0, cfg);
  REQUIRE(r.caseId);
  CHECK(*r.caseId == "learning");
  CHECK_FALSE(r.isNew);
  CHECK(r.queued == 2);

  prog["learning"].card.due = now + 600;
  r = pickLearnCase(pool, prog, now, 0, cfg);
  CHECK(*r.caseId == "review");

  prog["review"].card.due = now + 3 * kSecondsPerDay;
  r = pickLearnCase(pool, prog, now, 0, cfg);
  REQUIRE(r.caseId);
  CHECK(*r.caseId == "queued1"); // from the queue, in pool order
  CHECK(r.isNew);

  // Introduce it today: the daily limit of one is then reached.
  prog["queued1"].record({now, 0, false, AttemptMode::Learn});
  review(prog["queued1"].card, Grade::Good, now, cfg);
  r = pickLearnCase(pool, prog, now, now - 3600, cfg);
  CHECK_FALSE(r.caseId);
  CHECK(r.newLimitReached);
  CHECK(r.queued == 1);
  REQUIRE(r.nextDue);
  CHECK(*r.nextDue == now + 600); // "learning" comes back first

  // Unstarted cases never come up by themselves.
  SetProgress empty;
  r = pickLearnCase({"a", "b"}, empty, now, 0, cfg);
  CHECK_FALSE(r.caseId);
  CHECK_FALSE(r.nextDue);
  CHECK(r.queued == 0);
}

TEST_CASE("reviews are due all day on the day they fall due") {
  SrsConfig cfg;
  cfg.learnAheadSeconds = 0;
  const std::int64_t dayStart = 10 * kSecondsPerDay;
  const std::int64_t now = dayStart + 9 * 3600; // 09:00
  SrsCard card;
  card.state = CardState::Review;
  card.due = dayStart + 20 * 3600; // 20:00 today
  CHECK_FALSE(isDue(card, now, cfg));
  CHECK(isDue(card, now, cfg, dayStart + kSecondsPerDay));
  card.due = dayStart + kSecondsPerDay + 3600; // 01:00 tomorrow
  CHECK_FALSE(isDue(card, now, cfg, dayStart + kSecondsPerDay));

  SetProgress prog;
  prog["today"].card.state = CardState::Review;
  prog["today"].card.due = dayStart + 20 * 3600;
  prog["tomorrow"].card.state = CardState::Review;
  prog["tomorrow"].card.due = dayStart + kSecondsPerDay + 3600;
  auto r = pickLearnCase({"today", "tomorrow"}, prog, now, dayStart, cfg);
  REQUIRE(r.caseId);
  CHECK(*r.caseId == "today");
  prog["today"].card.due = dayStart + 2 * kSecondsPerDay;
  r = pickLearnCase({"today", "tomorrow"}, prog, now, dayStart, cfg);
  CHECK_FALSE(r.caseId);
  CHECK(r.nextDue == dayStart + kSecondsPerDay + 3600);
  // Learning steps keep to the clock.
  prog["step"].card.state = CardState::Learning;
  prog["step"].card.reps = 1;
  prog["step"].card.due = now + 600;
  r = pickLearnCase({"step"}, prog, now, dayStart, cfg);
  CHECK_FALSE(r.caseId);
}

TEST_CASE("learning cards come up early only when nothing else is left") {
  SrsConfig cfg; // 20 minutes of learn-ahead
  const std::int64_t now = 50000;
  SetProgress prog;
  prog["soon"].card.state = CardState::Learning;
  prog["soon"].card.reps = 1;
  prog["soon"].card.due = now + 300;
  startLearning(prog["queued"].card, 0, cfg);
  auto r = pickLearnCase({"soon", "queued"}, prog, now, 0, cfg);
  CHECK(*r.caseId == "queued");
  prog["queued"].record({now, 0, false, AttemptMode::Learn});
  review(prog["queued"].card, Grade::Good, now, cfg); // due in 10 min
  r = pickLearnCase({"soon", "queued"}, prog, now, 0, cfg, "queued");
  CHECK(*r.caseId == "soon"); // learn-ahead, avoiding the last case
  prog["soon"].card.due = now + 5000;
  prog["queued"].card.due = now + 4000;
  r = pickLearnCase({"soon", "queued"}, prog, now, 0, cfg);
  CHECK_FALSE(r.caseId);
  CHECK(r.nextDue == now + 4000);
}

TEST_CASE("learn avoids repeating the last case while another is due") {
  SrsConfig cfg;
  SetProgress prog;
  for (const char *id : {"a", "b"}) {
    prog[id].card.state = CardState::Learning;
    prog[id].card.reps = 1;
  }
  prog["a"].card.due = 10;
  prog["b"].card.due = 20;
  CHECK(*pickLearnCase({"a", "b"}, prog, 100, 0, cfg).caseId == "a");
  CHECK(*pickLearnCase({"a", "b"}, prog, 100, 0, cfg, "a").caseId == "b");
  CHECK(*pickLearnCase({"a"}, prog, 100, 0, cfg, "a").caseId == "a");
}

TEST_CASE("random drill order never repeats a case straight away") {
  CasePicker p;
  p.setMode(PickMode::Random);
  SetProgress prog;
  std::mt19937_64 rng(5);
  p.setPool({"a", "b"});
  std::string last = *p.next(prog, rng);
  for (int i = 0; i < 2000; ++i) {
    std::string pick = *p.next(prog, rng);
    REQUIRE(pick != last);
    last = pick;
  }
  // The others come up evenly.
  p.setPool({"a", "b", "c", "d"});
  std::map<std::string, int> seen;
  for (int i = 0; i < 6000; ++i) {
    std::string pick = *p.next(prog, rng);
    REQUIRE(pick != last);
    last = pick;
    ++seen[pick];
  }
  for (const auto &[id, n] : seen) {
    CHECK(n > 1300);
    CHECK(n < 1700);
  }
  // A single case still comes up.
  p.setPool({"a"});
  CHECK(*p.next(prog, rng) == "a");
  CHECK(*p.next(prog, rng) == "a");
}

TEST_CASE("drill picker orders and weights cases") {
  CasePicker p;
  p.setMode(PickMode::InOrder);
  p.setPool({"a", "b", "c"});
  SetProgress prog;
  std::mt19937_64 rng(3);
  CHECK(*p.next(prog, rng) == "a");
  CHECK(*p.next(prog, rng) == "b");
  CHECK(*p.next(prog, rng) == "c");
  CHECK(*p.next(prog, rng) == "a");

  // Weighted: a case well over the target time comes up far more often
  // than one under it.
  p.setMode(PickMode::Weighted);
  p.setTargetMs(2000);
  for (int i = 0; i < 5; ++i) {
    prog["a"].record({i, 1000, false, AttemptMode::Drill});
    prog["b"].record({i, 1000, false, AttemptMode::Drill});
    prog["c"].record({i, 6000, false, AttemptMode::Drill});
  }
  std::map<std::string, int> seen;
  for (int i = 0; i < 400; ++i) {
    ++seen[*p.next(prog, rng)];
  }
  CHECK(seen["c"] > seen["a"] + seen["b"]);
}
