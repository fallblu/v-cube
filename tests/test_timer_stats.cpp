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
  CHECK(averageOf(twelve) == 65); // drops 10 and 1000
  CHECK(bestAverage(solves({5000, 1000, 1000, 1000, 1000, 1000, 9000}), 5) ==
        1000);
}

TEST_CASE("times format like cstimer") {
  CHECK(formatTime(12345) == "12.34");
  CHECK(formatTime(62345) == "1:02.34");
  CHECK(formatTime(3723450) == "1:02:03.45");
  CHECK(formatTime(kDnfTime) == "DNF");
  CHECK(formatTime(std::nullopt) == "-");
  CHECK(formatTime(999, 3) == "0.999");
  Solve plus{10000, Penalty::Plus2, {}, {}, 0, {}};
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
