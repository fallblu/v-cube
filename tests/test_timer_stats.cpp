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
