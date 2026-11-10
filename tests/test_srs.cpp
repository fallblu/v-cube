#include "train/drill_session.hpp"
#include "train/srs.hpp"
#include <algorithm>
#include <cmath>
#include <map>

#include <set>

#include <catch2/catch_approx.hpp>
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
  constexpr auto drill = AttemptMode::PhysicalDrill;
  CaseProgress p;
  p.record({10, 2000, false, drill});
  p.record({11, 0, true, AttemptMode::Learn});
  p.record({12, 0, false, AttemptMode::Learn});
  p.record({13, 4000, false, drill});
  p.record({14, 1000, true, drill});
  CHECK(p.drillCount(drill) == 3);
  REQUIRE(p.recentMean(drill));
  CHECK(*p.recentMean(drill) == 3000.0);
  CHECK(p.best(drill) == 2000);
  CHECK(p.failRate(drill) == 1.0 / 3.0);
  // Only learn attempts: no drill statistics.
  CaseProgress learnOnly;
  learnOnly.record({1, 0, false, AttemptMode::Learn});
  CHECK_FALSE(learnOnly.recentMean(drill));
  CHECK_FALSE(learnOnly.best(drill));
  CHECK(learnOnly.failRate(drill) == 0.0);
}

TEST_CASE("virtual and physical drill statistics are kept apart") {
  constexpr auto physical = AttemptMode::PhysicalDrill;
  constexpr auto virt = AttemptMode::VirtualDrill;
  CaseProgress p;
  p.record({1, 2000, false, physical});
  p.record({2, 5000, false, virt});
  p.record({3, 7000, true, virt});
  p.record({4, 1000, true, physical});
  CHECK(p.drillCount(physical) == 2);
  CHECK(p.drillCount(virt) == 2);
  CHECK(*p.recentMean(physical) == 2000.0);
  CHECK(*p.recentMean(virt) == 5000.0);
  CHECK(p.best(physical) == 2000);
  CHECK(p.best(virt) == 5000);
  CHECK(p.failRate(physical) == 0.5);
  CHECK(p.failRate(virt) == 0.5);
  // Each mode is capped on its own.
  for (int i = 0; i < 150; ++i) {
    p.record({10 + i, 9000, false, virt});
  }
  CHECK(p.drillCount(virt) == CaseProgress::kMaxAttempts);
  CHECK(p.drillCount(physical) == 2);
  CHECK(p.best(physical) == 2000);
}

TEST_CASE("attempts are capped per mode") {
  CaseProgress p;
  p.record({0, 1234, false, AttemptMode::PhysicalDrill});
  for (int i = 1; i <= 150; ++i) {
    p.record({i, 0, false, AttemptMode::Learn});
  }
  CHECK(p.attempts.size() == CaseProgress::kMaxAttempts + 1);
  // The one drill attempt survives.
  CHECK(p.best(AttemptMode::PhysicalDrill) == 1234);
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

TEST_CASE("a case marked learned counts as introduced but not as new") {
  SrsConfig cfg;
  cfg.newPerDay = 1;
  const std::int64_t dayStart = 10 * kSecondsPerDay;
  const std::int64_t now = dayStart + 3600;
  SetProgress prog;
  for (const char *id : {"m1", "m2"}) {
    markLearned(prog[id].card, now - 2 * kSecondsPerDay, cfg);
    CHECK(prog[id].card.introduced != 0);
    CHECK(prog[id].card.marked);
    CHECK(prog[id].card.reps == 0);
  }
  // Their first reviews today don't use up the day's new cases.
  for (const char *id : {"m1", "m2"}) {
    prog[id].record({now, 0, false, AttemptMode::Learn});
    review(prog[id].card, Grade::Good, now, cfg);
  }
  CHECK(newCasesIntroducedSince(prog, dayStart) == 0);
  startLearning(prog["queued"].card, now, cfg);
  CHECK(prog["queued"].card.introduced == 0);
  auto r = pickLearnCase({"m1", "m2", "queued"}, prog, now, dayStart, cfg);
  REQUIRE(r.caseId);
  CHECK(*r.caseId == "queued");
  CHECK(r.isNew);
  review(prog["queued"].card, Grade::Good, now, cfg);
  CHECK(prog["queued"].card.introduced == now);
  CHECK_FALSE(prog["queued"].card.marked);
  CHECK(newCasesIntroducedSince(prog, dayStart) == 1);
  // Marking a case learned that was learned in Learn keeps it as it was.
  markLearned(prog["queued"].card, now, cfg);
  CHECK_FALSE(prog["queued"].card.marked);
}

TEST_CASE("resetting a case's learning keeps its drill history") {
  SrsConfig cfg;
  CaseProgress p;
  p.record({100, 1500, false, AttemptMode::PhysicalDrill});
  p.record({200, 0, false, AttemptMode::Learn});
  p.record({300, 2500, false, AttemptMode::VirtualDrill});
  review(p.card, Grade::Good, 200, cfg);
  resetLearning(p);
  CHECK(p.card.state == CardState::New);
  CHECK(p.card.introduced == 0);
  REQUIRE(p.attempts.size() == 2);
  CHECK(p.drillCount(AttemptMode::PhysicalDrill) == 1);
  CHECK(p.drillCount(AttemptMode::VirtualDrill) == 1);
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
  SrsConfig cfg;
  cfg.learnAheadSeconds = 1200;
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
    prog["a"].record({i, 1000, false, AttemptMode::PhysicalDrill});
    prog["b"].record({i, 1000, false, AttemptMode::PhysicalDrill});
    prog["c"].record({i, 6000, false, AttemptMode::PhysicalDrill});
  }
  std::map<std::string, int> seen;
  for (int i = 0; i < 400; ++i) {
    ++seen[*p.next(prog, rng)];
  }
  // (It never comes twice in a row, so at most every other time: almost
  // always after each other case.)
  CHECK(seen["c"] > 180);
  CHECK(seen["c"] > seen["a"] + seen["b"] - 30);

  // Weighted goes by the statistics of its own drill mode: virtual drills
  // where "a" is the slow case.
  for (int i = 0; i < 5; ++i) {
    prog["a"].record({i, 6000, false, AttemptMode::VirtualDrill});
    prog["b"].record({i, 1000, false, AttemptMode::VirtualDrill});
    prog["c"].record({i, 1000, false, AttemptMode::VirtualDrill});
  }
  p.setAttemptMode(AttemptMode::VirtualDrill);
  seen.clear();
  for (int i = 0; i < 400; ++i) {
    ++seen[*p.next(prog, rng)];
  }
  CHECK(seen["a"] > 180);
  CHECK(seen["a"] > seen["b"] + seen["c"] - 30);
}

TEST_CASE("weighted order: no outlier takes over, no case twice in a row") {
  SetProgress prog;
  std::vector<std::string> pool;
  for (int c = 0; c < 20; ++c) {
    std::string id = "c" + std::to_string(c);
    pool.push_back(id);
    for (int i = 0; i < 5; ++i) {
      prog[id].record({i, 2000, false, AttemptMode::VirtualDrill});
    }
  }
  // One walked-away time: the median hardly moves.
  prog["c0"].record({9, 60000, false, AttemptMode::VirtualDrill});
  CHECK(*prog["c0"].recentMedian(AttemptMode::VirtualDrill) == 2000);
  double ref = drillReference(pool, prog, AttemptMode::VirtualDrill, 2000);
  CHECK(ref == 2000);
  CHECK(*drillWeakness(&prog["c0"], AttemptMode::VirtualDrill, ref) ==
        Catch::Approx(1.0));
  // Even every time slow, the ratio is held at 3.
  for (int i = 0; i < 5; ++i) {
    prog["c1"].record({i, 60000, false, AttemptMode::VirtualDrill});
  }
  CHECK(*drillWeakness(&prog["c1"], AttemptMode::VirtualDrill, ref) ==
        Catch::Approx(9.0));
  CHECK_FALSE(drillWeakness(nullptr, AttemptMode::VirtualDrill, ref));
  CHECK_FALSE(drillWeakness(&prog["c1"], AttemptMode::PhysicalDrill, ref));

  CasePicker p;
  p.setMode(PickMode::Weighted);
  p.setAttemptMode(AttemptMode::VirtualDrill);
  p.setTargetMs(2000);
  p.setPool(pool);
  std::mt19937_64 rng(11);
  std::map<std::string, int> seen;
  std::string last;
  for (int i = 0; i < 4000; ++i) {
    std::string pick = *p.next(prog, rng);
    REQUIRE(pick != last);
    last = pick;
    ++seen[pick];
  }
  // 9 against 19 x 1: under a third of the picks.
  CHECK(seen["c1"] < 4000 / 3);
  CHECK(seen["c0"] < 400);

  // Two cases alternate.
  p.setPool({"c0", "c1"});
  last = *p.next(prog, rng);
  for (int i = 0; i < 200; ++i) {
    std::string pick = *p.next(prog, rng);
    REQUIRE(pick != last);
    last = pick;
  }

  // The weakest cases, weakest first; never-drilled ones aren't among them.
  pool.push_back("new");
  auto weak = weakestCases(pool, prog, AttemptMode::VirtualDrill, 2000, 3);
  REQUIRE(weak.size() == 3);
  CHECK(weak[0] == "c1");
  CHECK(std::find(weak.begin(), weak.end(), "new") == weak.end());
}

TEST_CASE("the picker avoids a case started by hand and keeps its place") {
  SetProgress prog;
  std::mt19937_64 rng(2);
  CasePicker p;
  p.setPool({"a", "b"});
  for (int i = 0; i < 100; ++i) {
    p.setLast("a");
    CHECK(*p.next(prog, rng) == "b");
  }
  p.setMode(PickMode::InOrder);
  p.setPool({"a", "b", "c", "d"});
  CHECK(*p.next(prog, rng) == "a");
  CHECK(*p.next(prog, rng) == "b");
  // The pool changes (e.g. "a" untucked): on with "c", not from the top.
  p.setPool({"b", "c", "d"});
  CHECK(*p.next(prog, rng) == "c");
  // "d" started from the list: in order goes on after it.
  p.setLast("d");
  CHECK(*p.next(prog, rng) == "b");
}

TEST_CASE("a failed case comes back a few cases later") {
  SetProgress prog;
  std::mt19937_64 rng(4);
  CasePicker p;
  p.setMode(PickMode::InOrder);
  p.setPool({"a", "b", "c", "d", "e"});
  CHECK(*p.next(prog, rng) == "a");
  p.requeue("a", 2);
  CHECK(p.isRequeued("a"));
  CHECK(*p.next(prog, rng) == "b");
  CHECK(*p.next(prog, rng) == "c");
  CHECK(*p.next(prog, rng) == "a");
  CHECK_FALSE(p.isRequeued("a"));
  CHECK(*p.next(prog, rng) == "d");
  // Forgotten again, e.g. when the DNF is taken back.
  p.requeue("d", 1);
  p.requeue("d", 0);
  CHECK(*p.next(prog, rng) == "e");
  CHECK(*p.next(prog, rng) == "a");
}

TEST_CASE("requeued cases don't hold up the others") {
  SetProgress prog;
  std::mt19937_64 rng(4);
  CasePicker p;
  p.setMode(PickMode::InOrder);
  p.setPool({"a", "b", "c", "d", "e", "f"});
  // Every case fails and comes back one case later.
  std::vector<std::string> seen;
  for (int i = 0; i < 9; ++i) {
    std::string id = *p.next(prog, rng);
    seen.push_back(id);
    p.requeue(id, 1);
  }
  // New cases keep coming in between.
  CHECK(std::set<std::string>(seen.begin(), seen.end()).size() >= 5);
}

TEST_CASE("recap gives every case once, then stops") {
  SetProgress prog;
  std::mt19937_64 rng(8);
  CasePicker p;
  p.setMode(PickMode::Recap);
  p.setPool({"a", "b", "c", "d"});
  std::set<std::string> seen;
  for (int i = 0; i < 4; ++i) {
    auto id = p.next(prog, rng);
    REQUIRE(id);
    seen.insert(*id);
    if (i == 1) {
      p.requeue(*id, 5); // failed: it comes back before the end
    }
  }
  CHECK(seen.size() == 4);
  CHECK_FALSE(p.recapDone());
  CHECK(p.next(prog, rng)); // the requeued case
  CHECK(p.recapDone());
  CHECK_FALSE(p.next(prog, rng));
  p.restartRecap();
  CHECK(p.next(prog, rng));
  CHECK(p.recapLeft() == 3);
}

TEST_CASE("drill bests and daily roll-ups outlive the attempts kept") {
  CaseProgress cp;
  const std::int64_t day0 = 1'700'000'000;
  cp.record({day0, 900, false, AttemptMode::VirtualDrill});
  cp.record({day0 + 60, 5000, true, AttemptMode::VirtualDrill});
  for (std::size_t i = 0; i < CaseProgress::kMaxAttempts; ++i) {
    cp.record({day0 + 3 * kSecondsPerDay + static_cast<std::int64_t>(i), 2000,
               false, AttemptMode::VirtualDrill});
  }
  CHECK(cp.drillCount(AttemptMode::VirtualDrill) == CaseProgress::kMaxAttempts);
  // The 0.90 dropped out of the attempts but is still the best.
  CHECK(*cp.best(AttemptMode::VirtualDrill) == 900);
  CHECK_FALSE(cp.best(AttemptMode::PhysicalDrill));
  auto days = cp.days(AttemptMode::VirtualDrill);
  REQUIRE(days.size() == 2);
  CHECK(days[0].day == localDayNumber(day0));
  CHECK(days[0].count == 2);
  CHECK(days[0].fails == 1);
  CHECK(*days[0].meanMs() == 900);
  CHECK(days[1].count == 100);
  CHECK(days[1].bestMs == 2000);
  // Deleting and clearing.
  CHECK(cp.removeLast(AttemptMode::VirtualDrill));
  CHECK(cp.drillCount(AttemptMode::VirtualDrill) == 99);
  CHECK_FALSE(cp.removeLast(AttemptMode::PhysicalDrill));
  cp.record({day0, 1500, false, AttemptMode::PhysicalDrill});
  cp.clearDrill(AttemptMode::VirtualDrill);
  CHECK(cp.drillCount(AttemptMode::VirtualDrill) == 0);
  CHECK_FALSE(cp.best(AttemptMode::VirtualDrill));
  CHECK(cp.days(AttemptMode::VirtualDrill).empty());
  CHECK(*cp.best(AttemptMode::PhysicalDrill) == 1500);
  // Learn attempts aren't archived.
  for (std::size_t i = 0; i <= CaseProgress::kMaxAttempts; ++i) {
    cp.record({day0, 0, false, AttemptMode::Learn});
  }
  CHECK_FALSE(cp.archive.contains(AttemptMode::Learn));
}

TEST_CASE("local days follow the calendar") {
  const std::int64_t t = 1'700'000'000;
  CHECK(localDayNumber(t + kSecondsPerDay) == localDayNumber(t) + 1);
  CHECK(localDayNumber(t) > 19000);
}

TEST_CASE("a drill session counts attempts, DNFs, new bests and the slowest") {
  DrillSession s;
  s.add({"a", 1000, false, true});
  s.add({"b", 3000, false, false});
  std::size_t i = s.add({"a", 2500, false, false});
  s.add({"c", 9000, true, true}); // a DNF is no new best
  s.add({"d", 1500, false, false});
  DrillSummary sum = s.summary(0, 2);
  CHECK(sum.count == 5);
  CHECK(sum.dnfs == 1);
  CHECK(sum.newBests == 1);
  CHECK(*sum.meanMs == Catch::Approx(2000));
  REQUIRE(sum.slowest.size() == 2);
  CHECK(sum.slowest[0].caseId == "b");
  CHECK(sum.slowest[1].caseId == "a");
  CHECK(sum.slowest[1].timeMs == 2500);
  s.setFailed(i, true);
  CHECK(s.summary().dnfs == 2);
  s.removeLast();
  CHECK(s.summary().count == 4);
  // From an index on, e.g. since a recap started.
  CHECK(s.summary(3).count == 1);
  s.clear();
  CHECK_FALSE(s.summary().meanMs);
}

TEST_CASE("new cases can be introduced in several orders") {
  std::vector<NewCase> cases = {
      {"a1", "A", 9, 3, 50}, {"a2", "A", 7, 1, 10}, {"a3", "A", 11, 2, 40},
      {"b1", "B", 5, 2, 30}, {"c1", "C", 8, 1, 20},
  };
  using V = std::vector<std::string>;
  CHECK(orderNewCases(cases, NewOrder::ListOrder, 0) ==
        V{"a1", "a2", "a3", "b1", "c1"});
  CHECK(orderNewCases(cases, NewOrder::Queued, 0) ==
        V{"a2", "c1", "b1", "a3", "a1"});
  CHECK(orderNewCases(cases, NewOrder::RoundRobin, 0) ==
        V{"a1", "b1", "c1", "a2", "a3"});
  CHECK(orderNewCases(cases, NewOrder::ShortestAlg, 0) ==
        V{"b1", "a2", "c1", "a1", "a3"});
  // Ties keep list order.
  CHECK(orderNewCases(cases, NewOrder::FewestSteps, 0) ==
        V{"a2", "c1", "a3", "b1", "a1"});
  // Shuffled: the same order for the same seed, all cases kept.
  V s1 = orderNewCases(cases, NewOrder::Shuffled, 7);
  CHECK(s1 == orderNewCases(cases, NewOrder::Shuffled, 7));
  V sorted = s1;
  std::sort(sorted.begin(), sorted.end());
  CHECK(sorted == V{"a1", "a2", "a3", "b1", "c1"});
  bool differs = false;
  for (std::uint64_t seed = 1; seed < 20 && !differs; ++seed) {
    differs = orderNewCases(cases, NewOrder::Shuffled, seed) != s1;
  }
  CHECK(differs);
}

TEST_CASE("the learn queue goes in pool order and waits for free places") {
  SrsConfig cfg;
  cfg.newPerDay = 10;
  cfg.maxLearning = 2;
  const std::int64_t now = 100000;
  SetProgress prog;
  // The pool's order is the introduction order: case 40 before case 3.
  std::vector<std::string> pool = {"c40", "c3", "c7"};
  for (const auto &id : pool) {
    startLearning(prog[id].card, now - 100, cfg);
  }
  auto r = pickLearnCase(pool, prog, now, 0, cfg);
  CHECK(*r.caseId == "c40");
  introduce(prog["c40"].card, now, cfg);
  CHECK_FALSE(isQueued(prog["c40"].card));
  CHECK(prog["c40"].card.reps == 0);
  CHECK(prog["c40"].card.due == now + 60);
  r = pickLearnCase(pool, prog, now, 0, cfg);
  CHECK(*r.caseId == "c3");
  review(prog["c3"].card, Grade::Good, now, cfg);
  // Two cards in their learning steps: the queue waits, also with nothing
  // else to do, instead of bringing another walkthrough straight away.
  r = pickLearnCase(pool, prog, now, 0, cfg);
  CHECK_FALSE(r.caseId);
  CHECK(r.learningLimitReached);
  CHECK_FALSE(r.newLimitReached);
  CHECK(r.nextDue == now + 60);
  // Space with nothing due takes the next learning card early.
  LearnPickOptions early;
  early.early = true;
  r = pickLearnCase(pool, prog, now, 0, cfg, early);
  CHECK(*r.caseId == "c40");
  // Once c40 graduates there is room again.
  review(prog["c40"].card, Grade::Easy, now + 60, cfg);
  r = pickLearnCase(pool, prog, now + 60, 0, cfg);
  CHECK(*r.caseId == "c7");
  CHECK(r.isNew);
  CHECK(learningCount(pool, prog) == 1);
}

TEST_CASE("suspended and buried cards stay out of Learn") {
  SrsConfig cfg;
  const std::int64_t now = 100000;
  SetProgress prog;
  for (const char *id : {"a", "b"}) {
    prog[id].card.state = CardState::Review;
    prog[id].card.reps = 2;
    prog[id].card.introduced = 1;
    prog[id].card.due = now - 10;
  }
  prog["a"].card.suspended = true;
  bury(prog["b"].card, now + 500);
  CHECK(isHeld(prog["a"].card, now));
  CHECK(isBuried(prog["b"].card, now));
  CHECK_FALSE(pickLearnCase({"a", "b"}, prog, now, 0, cfg).caseId);
  CHECK(*pickLearnCase({"a", "b"}, prog, now + 600, 0, cfg).caseId == "b");
  LearnCounts n = learnCounts({"a", "b"}, prog, now, 0, cfg);
  CHECK(n.held == 2);
  CHECK(n.due == 0);
  CHECK(n.learned == 2);
}

TEST_CASE("reviews due today come up in a random order") {
  SrsConfig cfg;
  const std::int64_t now = 100000;
  SetProgress prog;
  std::vector<std::string> pool;
  for (int i = 0; i < 6; ++i) {
    std::string id = "r" + std::to_string(i);
    pool.push_back(id);
    prog[id].card.state = CardState::Review;
    prog[id].card.reps = 2;
    prog[id].card.introduced = 1;
    prog[id].card.due = now - 1000 + i;
  }
  std::mt19937_64 rng(11);
  LearnPickOptions opts;
  opts.rng = &rng;
  std::map<std::string, int> first;
  for (int i = 0; i < 600; ++i) {
    ++first[*pickLearnCase(pool, prog, now, 0, cfg, opts).caseId];
  }
  CHECK(first.size() == 6);
  // Without a generator, earliest first as before.
  CHECK(*pickLearnCase(pool, prog, now, 0, cfg).caseId == "r0");
  // The last case is still avoided.
  opts.last = "r3";
  for (int i = 0; i < 100; ++i) {
    CHECK(*pickLearnCase(pool, prog, now, 0, cfg, opts).caseId != "r3");
  }
}

TEST_CASE("long intervals are spread out a little") {
  SrsConfig cfg;
  std::mt19937_64 rng(3);
  std::map<std::int64_t, int> dues;
  for (int i = 0; i < 50; ++i) {
    SrsCard c;
    c.state = CardState::Review;
    c.intervalDays = 10;
    c.ease = 2.5;
    c.due = 0;
    review(c, Grade::Good, 0, cfg, &rng);
    CHECK(c.intervalDays >= 23.75);
    CHECK(c.intervalDays <= 26.25);
    ++dues[c.due];
  }
  CHECK(dues.size() > 10);
  // Short intervals aren't.
  SrsCard c;
  review(c, Grade::Good, 0, cfg, &rng);
  review(c, Grade::Good, 600, cfg, &rng);
  CHECK(c.intervalDays == cfg.graduatingDays);
}

TEST_CASE("scheduler: ease recovers, lapses keep less, late reviews count") {
  SrsConfig cfg;
  SrsCard c;
  c.state = CardState::Review;
  c.intervalDays = 10;
  c.ease = 2.0;
  c.due = 0;
  review(c, Grade::Good, 0, cfg);
  CHECK(std::abs(c.ease - 2.05) < 1e-9);
  CHECK(c.intervalDays == 20.0);
  c.ease = 2.48;
  review(c, Grade::Good, c.due, cfg);
  CHECK(c.ease == cfg.startingEase); // never above the starting ease
  // A lapse keeps a fifth of the interval.
  c.intervalDays = 50;
  review(c, Grade::Again, c.due, cfg);
  CHECK(std::abs(c.intervalDays - 10.0) < 1e-9);
  CHECK(c.state == CardState::Relearning);
  // The old behaviour is a setting away.
  SrsConfig old = cfg;
  old.lapseFactor = 0.5;
  old.easeRecovery = 0;
  old.creditLateReviews = false;
  SrsCard d;
  d.state = CardState::Review;
  d.intervalDays = 50;
  d.ease = 2.0;
  review(d, Grade::Again, 0, old);
  CHECK(d.intervalDays == 25.0);

  // Ten days late: Good credits half of that.
  SrsCard late;
  late.state = CardState::Review;
  late.intervalDays = 10;
  late.ease = 2.5;
  late.due = 0;
  SrsCard onTime = late;
  review(late, Grade::Good, 10 * kSecondsPerDay, cfg);
  CHECK(late.intervalDays == 37.5);
  review(onTime, Grade::Good, 10 * kSecondsPerDay, old);
  CHECK(onTime.intervalDays == 25.0);
}

TEST_CASE("hard moves a learning card on but never graduates it") {
  SrsConfig cfg; // 1m 10m
  SrsCard c;
  review(c, Grade::Hard, 0, cfg);
  CHECK(c.state == CardState::Learning);
  CHECK(c.step == 1);
  CHECK(c.due == 600);
  review(c, Grade::Hard, 600, cfg);
  CHECK(c.state == CardState::Learning);
  CHECK(c.step == 1);
  CHECK(c.due == 600 + 900);
  SrsConfig old;
  old.hardAdvancesSteps = false;
  SrsCard d;
  review(d, Grade::Hard, 0, old);
  CHECK(d.step == 0);
  CHECK(d.due == 90);
  // What each grade would give, for the buttons.
  SrsCard fresh;
  CHECK(nextInterval(fresh, Grade::Again, 0, cfg) == 60);
  CHECK(nextInterval(fresh, Grade::Good, 0, cfg) == 600);
  CHECK(nextInterval(fresh, Grade::Easy, 0, cfg) == 4 * kSecondsPerDay);
  CHECK(fresh.reps == 0);
}

TEST_CASE("leeches, recognition means and counts") {
  SrsConfig cfg;
  SrsCard c;
  c.lapses = cfg.leechLapses - 1;
  CHECK_FALSE(isLeech(c, cfg));
  ++c.lapses;
  CHECK(isLeech(c, cfg));
  cfg.leechLapses = 0;
  CHECK_FALSE(isLeech(c, cfg));

  CaseProgress p;
  CHECK_FALSE(p.recognitionMean());
  Attempt a{10, 0, false, AttemptMode::Learn};
  a.recognitionMs = 1000;
  p.record(a);
  p.record({11, 0, false, AttemptMode::Learn}); // not measured
  Attempt d{12, 3000, false, AttemptMode::VirtualDrill};
  d.recognitionMs = 2000;
  p.record(d);
  REQUIRE(p.recognitionMean());
  CHECK(*p.recognitionMean() == 1500.0);
  CHECK(*p.recognitionMean(1) == 2000.0);

  SrsConfig c2;
  const std::int64_t dayStart = 10 * kSecondsPerDay;
  const std::int64_t now = dayStart + 3600;
  SetProgress prog;
  startLearning(prog["q"].card, now, c2);
  prog["r"].card.state = CardState::Review;
  prog["r"].card.introduced = 1;
  prog["r"].card.due = dayStart + kSecondsPerDay + 100; // tomorrow
  prog["r"].record({now - 10, 0, true, AttemptMode::Learn});
  prog["r"].record({now - 5, 0, false, AttemptMode::Learn});
  prog["r"].record({dayStart - 5, 0, false, AttemptMode::Learn}); // yesterday
  prog["l"].card.state = CardState::Learning;
  prog["l"].card.reps = 1;
  prog["l"].card.introduced = 1;
  prog["l"].card.due = now;
  LearnCounts n = learnCounts({"q", "r", "l", "x"}, prog, now, dayStart, c2);
  CHECK(n.queued == 1);
  CHECK(n.learning == 1);
  CHECK(n.learned == 1);
  CHECK(n.due == 1);
  CHECK(n.doneToday == 2);
  CHECK(n.againToday == 1);
  CHECK(n.dueTomorrow == 1);
}

TEST_CASE("a family card's learn attempts count for each of its cases") {
  CaseProgress own;
  Attempt drill{10, 3000, false, AttemptMode::VirtualDrill};
  drill.recognitionMs = 1000;
  own.record(drill);
  own.record({30, 0, false, AttemptMode::Learn}); // before families
  CaseProgress family;
  Attempt learn{20, 0, false, AttemptMode::Learn};
  learn.recognitionMs = 3000;
  family.record(learn);
  family.record({25, 2000, false, AttemptMode::PhysicalDrill}); // not its
  std::vector<Attempt> all = caseAttempts(&own, &family);
  REQUIRE(all.size() == 3);
  CHECK(all[0].timestamp == 10);
  CHECK(all[1].timestamp == 20);
  CHECK(all[2].timestamp == 30);
  CHECK(*recognitionMean(all) == 2000.0);
  CHECK(caseAttempts(&own, nullptr).size() == 2);
  CHECK(caseAttempts(nullptr, &family).size() == 1);
  CHECK(caseAttempts(nullptr, nullptr).empty());
}

TEST_CASE("reviews across sets are interleaved by what is due") {
  std::mt19937_64 rng(1);
  CHECK_FALSE(pickSetToReview({}, rng));
  CHECK_FALSE(pickSetToReview({{"a", 0}, {"b", 0}}, rng));
  CHECK(*pickSetToReview({{"a", 0}, {"b", 3}}, rng) == 1);
  std::map<std::size_t, int> seen;
  for (int i = 0; i < 400; ++i) {
    ++seen[*pickSetToReview({{"a", 1}, {"b", 3}}, rng)];
  }
  CHECK(seen[0] > 50);
  CHECK(seen[1] > seen[0]);
}
