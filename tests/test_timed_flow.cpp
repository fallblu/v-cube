#include "train/session_edit.hpp"
#include "train/stats.hpp"
#include "train/timed_flow.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <vector>

using namespace vcube;

namespace {

Solve solve(std::int64_t ms, Penalty p = Penalty::None) {
  Solve s;
  s.timeMs = ms;
  s.penalty = p;
  return s;
}

TimedFlow flow(bool virt, bool inspection = false, bool hide = false) {
  TimedFlow f;
  f.setConfig({{300, inspection, 15000}, virt, hide});
  return f;
}

} // namespace

TEST_CASE("parseStatId reads single, moN and aoN", "[stats]") {
  CHECK(parseStatId("single")->kind == StatId::Kind::Single);
  auto mo3 = parseStatId("mo3");
  REQUIRE(mo3);
  CHECK(mo3->kind == StatId::Kind::Mean);
  CHECK(mo3->n == 3);
  auto ao100 = parseStatId("ao100");
  REQUIRE(ao100);
  CHECK(ao100->kind == StatId::Kind::Average);
  CHECK(ao100->n == 100);
  CHECK_FALSE(parseStatId("ao0"));
  CHECK_FALSE(parseStatId("ao"));
  CHECK_FALSE(parseStatId("aox"));
  CHECK_FALSE(parseStatId("best"));
}

TEST_CASE("statSpan finds the solves behind a statistic", "[stats]") {
  std::vector<Solve> s = {solve(5000), solve(3000), solve(4000),
                          solve(9000), solve(3500), solve(6000)};
  auto single = statSpan(s, *parseStatId("single"), false);
  REQUIRE(single);
  CHECK(single->from == 5);
  auto bestSingle = statSpan(s, *parseStatId("single"), true);
  REQUIRE(bestSingle);
  CHECK(bestSingle->from == 1);
  CHECK(bestSingle->count == 1);
  auto cur = statSpan(s, *parseStatId("ao5"), false);
  REQUIRE(cur);
  CHECK(cur->from == 1);
  CHECK(cur->count == 5);
  auto bestMo3 = statSpan(s, *parseStatId("mo3"), true);
  REQUIRE(bestMo3);
  CHECK(bestMo3->from == 0); // 5, 3, 4 is the best mean of 3
  CHECK_FALSE(statSpan(s, *parseStatId("ao12"), false));
  CHECK_FALSE(statSpan({}, *parseStatId("single"), false));
}

TEST_CASE("sessionStats gives each statistic's current and best", "[stats]") {
  std::vector<Solve> s = {solve(5000), solve(3000), solve(4000, Penalty::Dnf),
                          solve(6000), solve(2000)};
  static constexpr std::string_view ids[] = {"single", "mo3", "ao5", "ao12",
                                             "nonsense"};
  SessionStats st = sessionStats(s, ids);
  REQUIRE(st.lines.size() == 5);
  CHECK(st.lines[0].id == "single");
  CHECK(st.lines[0].current == 2000);
  CHECK(st.lines[0].best == 2000);
  CHECK(st.lines[1].current == currentMean(s, 3));
  CHECK(st.lines[1].best == bestMean(s, 3));
  CHECK(st.lines[2].current == currentAverage(s, 5));
  CHECK_FALSE(st.lines[3].current);
  CHECK_FALSE(st.lines[4].current);
  CHECK(st.summary.count == 5);
  CHECK(st.summary.dnfs == 1);
  // The last solve a DNF: the current single is DNF.
  s.push_back(solve(1000, Penalty::Dnf));
  CHECK(sessionStats(s, ids).lines[0].current == kDnfTime);
}

TEST_CASE("DeletedAttempt puts a deleted attempt back where it was",
          "[session]") {
  std::vector<int> list = {1, 2, 3, 4};
  DeletedAttempt<int> undo;
  CHECK_FALSE(undo.has());
  CHECK_FALSE(undo.restore(list));
  CHECK_FALSE(undo.erase(list, 9));
  REQUIRE(undo.erase(list, 1));
  CHECK(list == std::vector<int>{1, 3, 4});
  CHECK(undo.has());
  auto at = undo.restore(list);
  REQUIRE(at);
  CHECK(*at == 1);
  CHECK(list == std::vector<int>{1, 2, 3, 4});
  CHECK_FALSE(undo.has());
  // Put back at the end when the list is shorter by then.
  REQUIRE(undo.erase(list, 3));
  list.clear();
  CHECK(*undo.restore(list) == 0);
  CHECK(list == std::vector<int>{4});
  REQUIRE(undo.erase(list, 0));
  undo.forget();
  CHECK_FALSE(undo.restore(list));
}

TEST_CASE("TimedFlow times a physical solve with Space", "[flow]") {
  TimedFlow f = flow(false);
  auto e = f.space(true, 0, true);
  CHECK(e.used);
  CHECK(e.showTimer);
  CHECK(f.isHolding());
  f.tick(400, false);
  e = f.space(false, 400, true);
  CHECK(f.isRunning());
  CHECK(e.clearStatus);
  CHECK_FALSE(e.startAttempt); // no virtual cube
  e = f.stopKey(10400);
  REQUIRE(e.record);
  CHECK(e.record->timeMs == 10000);
  CHECK(f.isIdle());
}

TEST_CASE("TimedFlow waits for a scramble and lets a short hold go", "[flow]") {
  TimedFlow f = flow(false);
  auto e = f.space(true, 0, false);
  CHECK(e.used);
  CHECK_FALSE(e.showTimer);
  CHECK(f.isIdle());
  f.space(true, 0, true);
  f.space(false, 100, true); // released before the hold time
  CHECK_FALSE(f.isRunning());
  // A popup takes the release: the hold is let go of.
  f.space(true, 200, true);
  f.tick(300, true);
  CHECK_FALSE(f.isHolding());
  f.space(true, 400, true);
  CHECK(f.dropHold());
  CHECK_FALSE(f.dropHold());
}

TEST_CASE("TimedFlow on the virtual cube", "[flow]") {
  TimedFlow f = flow(true);
  CHECK(f.move(true) == TimedFlow::MoveAnswer::Turn); // rotations allowed
  f.space(true, 0, true);
  CHECK(f.move(false) == TimedFlow::MoveAnswer::Ignore); // waits for release
  f.tick(300, false);
  auto e = f.space(false, 300, true);
  CHECK(e.startAttempt);
  CHECK(f.isRunning());
  CHECK(f.move(false) == TimedFlow::MoveAnswer::Turn);
  e = f.space(true, 5300, true);
  CHECK(e.stopVirtual);
  auto r = f.stopVirtual(5300, false);
  REQUIRE(r);
  CHECK(r->penalty == Penalty::Dnf);
  CHECK(r->timeMs == 5000);
  f.keepDnf();
  CHECK(f.finished());
  // A DNF stays: only rotations, then Space gives the next scramble.
  CHECK(f.move(false) == TimedFlow::MoveAnswer::NextScramble);
  CHECK(f.move(true) == TimedFlow::MoveAnswer::Turn);
  e = f.space(true, 6000, true);
  CHECK(e.nextScramble);
  CHECK_FALSE(f.finished());
  // Enter only does anything after a DNF.
  CHECK_FALSE(f.enter().used);
  f.keepDnf();
  CHECK(f.enter().nextScramble);
}

TEST_CASE("TimedFlow Escape cancels or leaves the DNF", "[flow]") {
  TimedFlow f = flow(true);
  auto e = f.escape();
  CHECK(e.used);
  CHECK(e.restartCube);
  CHECK(e.clearStatus);
  CHECK_FALSE(e.showLastResult);
  f.space(true, 0, true);
  f.tick(300, false);
  f.space(false, 300, true);
  e = f.escape();
  CHECK(e.cancelled);
  CHECK(e.showLastResult);
  CHECK(e.restartCube);
  CHECK(f.isIdle());
  f.keepDnf();
  e = f.escape();
  CHECK(e.nextScramble);
  CHECK_FALSE(e.restartCube);
  CHECK_FALSE(f.finished());
  // Leaving the page cancels what is under way.
  CHECK_FALSE(f.leave());
  f.space(true, 0, true);
  CHECK(f.leave());
  CHECK(f.isIdle());
}

TEST_CASE("TimedFlow hides the virtual cube until inspection", "[flow]") {
  TimedFlow f = flow(true, true, true);
  CHECK(f.cubeHidden());
  CHECK(f.move(true) == TimedFlow::MoveAnswer::Hidden);
  f.space(true, 0, true);
  auto e = f.space(false, 50, true);
  CHECK(f.state() == SolveTimer::State::Inspecting);
  CHECK(e.clearStatus);
  CHECK_FALSE(f.cubeHidden());
  CHECK(flow(false, true, true).cubeHidden() == false);
  CHECK(flow(true, true, false).cubeHidden() == false);
}
