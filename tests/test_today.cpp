#include "train/today.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace vcube;

TEST_CASE("activity is totalled per mode for today and the week") {
  constexpr std::int64_t day = kSecondsPerDay;
  std::int64_t today = 10 * day, week = 4 * day;
  std::vector<ActivityEvent> events = {
      {0, today + 5, 1000}, {0, today + 50, 2000}, // today
      {0, 6 * day, 4000},                          // this week
      {0, 2 * day, 8000},                          // too old
      {1, today + 1, 0},    {7, today, 5},         // out of range mode
      {-1, today, 5},
  };
  auto a = summarizeActivity(events, 2, today, week);
  REQUIRE(a.size() == 2);
  CHECK(a[0].today.count == 2);
  CHECK(a[0].today.ms == 3000);
  CHECK(a[0].week.count == 3);
  CHECK(a[0].week.ms == 7000);
  CHECK(a[1].today.count == 1);
  CHECK(a[1].week.ms == 0);
}

TEST_CASE("a streak counts the days in a row up to today") {
  auto dayOf = [](std::int64_t t) { return t / kSecondsPerDay; };
  constexpr std::int64_t d = kSecondsPerDay;
  SECTION("done today") {
    auto s = practiceStreak({9 * d + 5, 10 * d + 1, 8 * d, 8 * d + 7, 5 * d},
                            10, dayOf);
    CHECK(s.days == 3);
    CHECK(s.doneToday);
  }
  SECTION("not yet today: it runs to yesterday") {
    auto s = practiceStreak({9 * d, 8 * d}, 10, dayOf);
    CHECK(s.days == 2);
    CHECK_FALSE(s.doneToday);
  }
  SECTION("broken") {
    CHECK(practiceStreak({8 * d}, 10, dayOf).days == 0);
    CHECK(practiceStreak({}, 10, dayOf).days == 0);
  }
}

TEST_CASE("an alg set's due, queued and new cases") {
  SrsConfig cfg;
  cfg.newPerDay = 2;
  std::int64_t now = 100 * kSecondsPerDay;
  std::int64_t dayStart = now - 3600, dayEnd = dayStart + kSecondsPerDay;
  SetProgress p;
  // Due review, review due tomorrow, a queued case and one in its steps.
  p["a"].card.state = CardState::Review;
  p["a"].card.due = now - 10;
  p["b"].card.state = CardState::Review;
  p["b"].card.due = dayEnd + 10;
  startLearning(p["c"].card, now, cfg);
  startLearning(p["e"].card, now, cfg);
  p["d"].card.state = CardState::Learning;
  p["d"].card.reps = 1;
  p["d"].card.due = now - 30;
  p["d"].card.introduced = now - 60; // introduced today
  auto d =
      setWaiting({"a", "b", "c", "d", "e", "x"}, p, now, dayStart, dayEnd, cfg);
  CHECK(d.due == 2); // a, and d in its learning steps
  CHECK(d.learned == 2);
  CHECK(d.queued == 2);
  CHECK(d.newLeft == 1); // 2 a day, 1 introduced already
  CHECK(d.unstarted == 1);
}
