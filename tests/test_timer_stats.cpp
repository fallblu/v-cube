#include "train/stats.hpp"
#include "train/timer.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace vcube;

namespace {
std::vector<Solve> solves(std::initializer_list<std::int64_t> ms) {
  std::vector<Solve> out;
  for (auto t : ms) {
    Solve s;
    if (t < 0) {
      s.penalty = Penalty::Dnf;
      s.timeMs = -t;
    } else {
      s.timeMs = t;
    }
    out.push_back(s);
  }
  return out;
}
} // namespace

TEST_CASE("trimmed averages follow WCA rules") {
  auto a = solves({10000, 12000, 11000, 9000, 15000});
  CHECK(averageOf(a) == 11000);
  auto withDnf = solves({10000, 12000, 11000, 9000, -15000});
  CHECK(averageOf(withDnf) == 11000);
  auto twoDnf = solves({10000, -12000, 11000, 9000, -15000});
  CHECK(averageOf(twoDnf) == kDnfTime);
  CHECK(meanOf(solves({1000, 2000, 3000})) == 2000);
  CHECK(meanOf(solves({1000, -2000, 3000})) == kDnfTime);
  CHECK_FALSE(currentAverage(a, 12));
  auto twelve = solves({10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 1000});
  CHECK(averageOf(twelve) == 70); // drops 10 and 1000: 0.065 rounds up
  CHECK(averageOf(twelve, 1) == 65);
  CHECK(bestAverage(solves({5000, 1000, 1000, 1000, 1000, 1000, 9000}), 5) ==
        1000);
}

TEST_CASE("averages are rounded to hundredths as the WCA rounds them") {
  // mo3 of 1.00, 1.00, 1.02 is 1.0067: 1.01, not the truncated 1.00.
  auto three = solves({1000, 1000, 1020});
  CHECK(meanOf(three) == 1010);
  CHECK(formatTime(meanOf(three)) == "1.01");
  // Singles are truncated first: 1.009 counts as 1.00.
  CHECK(meanOf(solves({1009, 1009, 1009})) == 1000);
  CHECK(meanOf(solves({1009, 1009, 1009}), 1) == 1009);
  // ao5 of 10.00, 10.01, 10.00, 9.00, 15.00: (10.00 + 10.01 + 10.00) / 3.
  CHECK(averageOf(solves({10000, 10010, 10000, 9000, 15000})) == 10000);
  CHECK(averageOf(solves({10000, 10010, 10010, 9000, 15000})) == 10010);
  CHECK(statUnit(1) == 100);
  CHECK(statUnit(2) == 10);
  CHECK(statUnit(3) == 1);
  CHECK(meanOf(solves({1000, 1000, 1050}), statUnit(1)) == 1000);
  CHECK(summarize(three).mean == 1010);
}

TEST_CASE("the solves an average drops and the best average's start") {
  auto a = solves({10000, 12000, 11000, 9000, 15000});
  CHECK(trimmedIndices(a) == std::vector<std::size_t>{3, 4});
  CHECK(trimmedIndices(solves({1, 2, 3})).empty());
  auto seven = solves({5000, 1000, 1000, 1000, 1000, 1000, 9000});
  CHECK(bestAverageStart(seven, 5) == std::optional<std::size_t>(0));
  CHECK(bestAverageStart(solves({9000, 5000, 1000, 1000, 1000, 1000}), 4) ==
        std::optional<std::size_t>(1));
  CHECK_FALSE(bestAverageStart(seven, 12));
}

TEST_CASE("a solve that beats every earlier value is a new best") {
  auto a = solves({10000, 11000, 12000, 13000, 14000});
  CHECK(newBests(a).empty());
  a.push_back(solves({5000})[0]);
  std::vector<NewBest> bests = newBests(a);
  REQUIRE(bests.size() == 2);
  CHECK(bests[0].name == "single");
  CHECK(bests[0].value == 5000);
  CHECK(bests[1].name == "mo3");
  CHECK(bests[1].value == 10670); // 13, 14, 5; the ao5 is 12 again
  a.push_back(solves({4000})[0]);
  bests = newBests(a);
  REQUIRE(bests.size() == 3);
  CHECK(bests[2].name == "ao5");
  CHECK(bests[2].value == 10000); // 12, 13 and 5; 4 and 14 dropped
  a.push_back(solves({-5000})[0]);
  CHECK(newBests(a).empty());
  CHECK(newBests(solves({1000})).empty());
}

TEST_CASE("times format like cstimer") {
  CHECK(formatTime(12345) == "12.34");
  CHECK(formatTime(62345) == "1:02.34");
  CHECK(formatTime(3723450) == "1:02:03.45");
  CHECK(formatTime(kDnfTime) == "DNF");
  CHECK(formatTime(std::nullopt) == "-");
  CHECK(formatTime(999, 3) == "0.999");
  Solve plus{10000, Penalty::Plus2, {}, {}, 0, {}, {}};
  CHECK(formatSolve(plus) == "12.00+");
}

TEST_CASE("space-bar timer needs a hold before it starts") {
  SolveTimer t;
  t.setConfig({300, false, 15000});
  t.pressStart(0);
  CHECK(t.state() == SolveTimer::State::Holding);
  t.releaseStart(100); // released too early
  CHECK(t.state() == SolveTimer::State::Idle);
  t.pressStart(1000);
  t.tick(1350);
  CHECK(t.state() == SolveTimer::State::Ready);
  t.releaseStart(1400);
  CHECK(t.state() == SolveTimer::State::Running);
  CHECK(t.elapsed(2400) == 1000);
  t.pressStop(11400);
  CHECK(t.state() == SolveTimer::State::Stopped);
  auto r = t.takeResult();
  REQUIRE(r);
  CHECK(r->timeMs == 10000);
  CHECK(r->penalty == Penalty::None);
  CHECK_FALSE(t.takeResult());
}

TEST_CASE("a release after the hold time starts the timer without a tick") {
  SolveTimer t;
  t.setConfig({300, false, 15000});
  t.pressStart(0);
  t.releaseStart(400); // no tick since the press
  CHECK(t.state() == SolveTimer::State::Running);
}

TEST_CASE("inspection gives +2 and DNF") {
  SolveTimer t;
  t.setConfig({0, true, 15000});
  t.pressStart(0);
  t.releaseStart(50); // inspection starts
  CHECK(t.state() == SolveTimer::State::Inspecting);
  t.pressStart(15500);
  CHECK(t.state() == SolveTimer::State::Ready);
  t.releaseStart(16000);
  t.pressStop(26000);
  CHECK(t.takeResult()->penalty == Penalty::Plus2);

  t.arm(0); // virtual cube flow
  CHECK(t.state() == SolveTimer::State::Inspecting);
  t.startNow(18000);
  t.stopNow(20000);
  auto r = t.takeResult();
  CHECK(r->penalty == Penalty::Dnf);
  CHECK(r->timeMs == 2000);
}

TEST_CASE("cancelling the timer leaves no result and starts afresh") {
  SolveTimer t;
  t.setConfig({300, false, 15000});
  // While holding: back to idle, and the release does nothing.
  t.pressStart(0);
  t.cancel();
  CHECK(t.state() == SolveTimer::State::Idle);
  t.releaseStart(500);
  CHECK(t.state() == SolveTimer::State::Idle);
  // Once ready.
  t.pressStart(1000);
  t.tick(1400);
  REQUIRE(t.state() == SolveTimer::State::Ready);
  t.cancel();
  t.releaseStart(1500);
  CHECK(t.state() == SolveTimer::State::Idle);
  CHECK_FALSE(t.takeResult());
  // While running: no result, no time, and the stop key does nothing.
  t.pressStart(2000);
  t.releaseStart(2400);
  REQUIRE(t.state() == SolveTimer::State::Running);
  t.cancel();
  CHECK(t.state() == SolveTimer::State::Idle);
  CHECK(t.elapsed(5000) == 0);
  t.pressStop(5000);
  t.stopNow(5000);
  CHECK(t.state() == SolveTimer::State::Idle);
  CHECK_FALSE(t.takeResult());
  // The next solve is timed as usual.
  t.pressStart(6000);
  t.releaseStart(6400);
  t.pressStop(8400);
  auto r = t.takeResult();
  REQUIRE(r);
  CHECK(r->timeMs == 2000);
  CHECK(r->penalty == Penalty::None);
  // From stopped, a hold starts the next one.
  t.pressStart(9000);
  CHECK(t.state() == SolveTimer::State::Holding);
}

TEST_CASE("cancelled inspection gives no penalty to the next solve") {
  SolveTimer t;
  t.setConfig({0, true, 15000});
  // Pressed but not yet released: cancelling forgets the inspection.
  t.pressStart(0);
  t.cancel();
  t.releaseStart(10);
  CHECK(t.state() == SolveTimer::State::Idle);
  CHECK_FALSE(t.isInspecting());
  // Inspecting, then cancelled long after the DNF limit.
  t.pressStart(100);
  t.releaseStart(150);
  REQUIRE(t.state() == SolveTimer::State::Inspecting);
  CHECK(t.inspectionPenalty(20000) == Penalty::Dnf);
  t.cancel();
  CHECK_FALSE(t.isInspecting());
  CHECK_FALSE(t.inspectionElapsed(20000));
  CHECK(t.inspectionPenalty(20000) == Penalty::None);
  // A new inspection starts from its own release.
  t.pressStart(30000);
  t.releaseStart(30050);
  REQUIRE(t.state() == SolveTimer::State::Inspecting);
  CHECK(*t.inspectionElapsed(31050) == 1000);
  // Cancelled while holding during inspection: idle, not inspecting.
  t.pressStart(32000);
  CHECK(t.state() == SolveTimer::State::Ready); // no hold time
  t.cancel();
  CHECK(t.state() == SolveTimer::State::Idle);
  CHECK_FALSE(t.isInspecting());

  // The virtual cube: armed, cancelled, then started by a turn.
  t.arm(40000);
  REQUIRE(t.isInspecting());
  t.cancel();
  t.startNow(60000);
  t.stopNow(61000);
  auto r = t.takeResult();
  REQUIRE(r);
  CHECK(r->penalty == Penalty::None);
  CHECK(r->timeMs == 1000);
  // The limits: 15 s exactly is fine, just over is +2, 17 s is still +2,
  // just over that is DNF.
  t.arm(0);
  CHECK(t.inspectionPenalty(15000) == Penalty::None);
  CHECK(t.inspectionPenalty(15001) == Penalty::Plus2);
  CHECK(t.inspectionPenalty(17000) == Penalty::Plus2);
  CHECK(t.inspectionPenalty(17001) == Penalty::Dnf);
  // Arming again drops a result not yet taken.
  t.startNow(1000);
  t.stopNow(2000);
  t.arm(3000);
  CHECK_FALSE(t.takeResult());
}

TEST_CASE("ao100 drops five solves each way and rounds like the others") {
  // 1.00 to 100.00 in whole seconds: the middle 90 average 50.50.
  std::vector<Solve> hundred;
  for (int i = 1; i <= 100; ++i) {
    hundred.push_back(solves({i * 1000})[0]);
  }
  CHECK(trimmedIndices(hundred).size() == 10);
  CHECK(averageOf(hundred) == 50500);
  CHECK(currentAverage(hundred, 100) == 50500);
  CHECK_FALSE(currentAverage(std::span(hundred).first(99), 100));
  // Five DNFs are trimmed; a sixth makes it DNF.
  std::vector<Solve> dnfs = hundred;
  for (std::size_t i = 0; i < 5; ++i) {
    dnfs[i].penalty = Penalty::Dnf;
  }
  // 1..5 are now the worst five, and 6..10 the best: 11..100 count.
  CHECK(averageOf(dnfs) == 55500);
  dnfs[5].penalty = Penalty::Dnf;
  CHECK(averageOf(dnfs) == kDnfTime);
  // A +2 counts two seconds more: 50.5222 rounds down to 50.52.
  std::vector<Solve> plus = hundred;
  plus[50].penalty = Penalty::Plus2;
  CHECK(averageOf(plus) == 50520);
  // A mean of exactly half a hundredth rounds up.
  std::vector<Solve> half = hundred;
  half[50].timeMs += 450; // 51.45: the mean is 50.505
  CHECK(averageOf(half) == 50510);
  half[50].timeMs -= 10; // 51.44: 50.5049
  CHECK(averageOf(half) == 50500);
  // Thousandths are truncated before averaging.
  half[50].timeMs += 9; // 51.449 counts as 51.44
  CHECK(averageOf(half) == 50500);

  // The best rolling ao100, and a new best when it improves: 100.00 down
  // to 1.00, then another 1.00.
  std::vector<Solve> down(hundred.rbegin(), hundred.rend());
  down.push_back(solves({1000})[0]);
  CHECK(averageOf(std::span(down).last(100)) == 49500);
  CHECK(bestAverage(down, 100) == 49500);
  CHECK(bestAverageStart(down, 100) == std::optional<std::size_t>(1));
  std::vector<NewBest> bests = newBests(down);
  REQUIRE_FALSE(bests.empty());
  CHECK(bests.back().name == "ao100");
  CHECK(bests.back().value == 49500);
}

TEST_CASE("rolling averages are those of each window") {
  // Times that repeat, with a +2 and a DNF now and then, and two DNFs
  // close enough to share a window of 12.
  std::vector<Solve> all;
  std::uint32_t seed = 12345;
  for (int i = 0; i < 260; ++i) {
    seed = seed * 1664525u + 1013904223u;
    Solve s;
    s.timeMs = 8000 + static_cast<std::int64_t>((seed >> 8) % 4000);
    if (i % 17 == 3) {
      s.penalty = Penalty::Plus2;
    }
    if (i % 23 == 5 || i % 23 == 9 || (i > 200 && i < 215)) {
      s.penalty = Penalty::Dnf;
    }
    all.push_back(s);
  }
  for (std::int64_t unit : {1, 10, 1000}) {
    for (std::size_t n : {0u, 2u, 3u, 5u, 12u, 100u, 260u, 300u}) {
      CAPTURE(unit, n);
      std::vector<StatValue> rolling = rollingAverages(all, n, unit);
      REQUIRE(rolling.size() == all.size());
      StatValue best;
      std::optional<std::size_t> bestAt;
      for (std::size_t end = 1; end <= all.size(); ++end) {
        StatValue expected;
        if (n > 0 && end >= n) {
          expected = averageOf(std::span(all).subspan(end - n, n), unit);
          if (expected && (!best || *expected < *best)) {
            best = expected;
            bestAt = end - n;
          }
        }
        CHECK(rolling[end - 1] == expected);
      }
      CHECK(bestAverageStart(all, n, false, unit) == bestAt);
      CHECK(bestAverage(all, n, unit) == best);
    }
  }
}

TEST_CASE("means round half up at every unit") {
  CHECK(meanOf(solves({1000, 1010})) == 1010);              // 1.005
  CHECK(meanOf(solves({1000, 1000, 1010})) == 1000);        // 1.0033
  CHECK(meanOf(solves({1000, 1010, 1010})) == 1010);        // 1.0067
  CHECK(meanOf(solves({1000, 1001}), 1) == 1001);           // 1.0005
  CHECK(meanOf(solves({1000, 1100}), statUnit(1)) == 1100); // 1.05
  CHECK(meanOf(solves({1000, 2000}), statUnit(0)) == 2000); // 1.5
  CHECK(meanOf(solves({1000, 1999}), statUnit(0)) == 1000); // 1 and 1
  CHECK(formatTime(1005, 3) == "1.005");
  CHECK(formatTime(1009) == "1.00"); // shown truncated
  CHECK(formatTime(59999) == "59.99");
  CHECK(formatTime(60000) == "1:00.00");
  CHECK(formatTime(1999, 0) == "1");
  CHECK(formatTime(1590, 1) == "1.5");
}
