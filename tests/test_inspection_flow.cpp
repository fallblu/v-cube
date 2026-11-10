#include "train/inspection_flow.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace vcube;

namespace {

using Phase = InspectionFlow::Phase;
using Key = InspectionFlow::Key;
using Turn = InspectionFlow::Turn;
using Alert = InspectionFlow::Alert;

InspectionFlow planFlow(int limit = 15, bool wca = true) {
  InspectionFlow f;
  f.setConfig({true, limit, wca, {true, true}});
  return f;
}

InspectionFlow edgesFlow(bool fb = true, bool rl = true) {
  InspectionFlow f;
  f.setConfig({false, 0, false, {fb, rl}});
  return f;
}

InspectionFlow::KeyInput down(Key k) {
  return {k, 0, false, true, false, false};
}
InspectionFlow::KeyInput up(Key k) {
  return {k, 0, false, false, false, false};
}
InspectionFlow::KeyInput digit(int d) {
  return {Key::Digit, d, false, true, false, false};
}
InspectionFlow::KeyInput turnKey() {
  return {Key::Other, 0, true, true, false, false};
}

InspectionFlow::Effects space(InspectionFlow &f, std::int64_t now,
                              bool ready = true) {
  return f.key(down(Key::Space), now, ready);
}

// Edges: shows the cube and types `text` (digits only).
void typeCount(InspectionFlow &f, const char *text) {
  for (const char *c = text; *c; ++c) {
    f.key(digit(*c - '0'), 0, true);
  }
}

} // namespace

TEST_CASE("Plan: Space inspects, hides and finishes", "[inspection]") {
  InspectionFlow f = planFlow();
  CHECK(f.phase() == Phase::Ready);
  CHECK_FALSE(f.isBusy());

  auto e = space(f, 1000);
  CHECK(e.used);
  CHECK(e.startInspection);
  CHECK(f.phase() == Phase::Inspecting);
  CHECK(f.isBusy());
  CHECK(f.elapsed(3500) == 2500);

  e = space(f, 5200);
  CHECK(e.startExecution);
  CHECK(f.phase() == Phase::Executing);
  CHECK(f.usedMs() == 4200);

  // Space hid it, so there is no grace: the next Space finishes.
  e = space(f, 5300);
  CHECK(e.finishPlan);
  CHECK(f.phase() == Phase::Executing); // until the page records it
  f.planDone();
  CHECK(f.phase() == Phase::Result);
  CHECK_FALSE(f.isBusy());

  e = space(f, 9000);
  CHECK(e.next);
  CHECK(f.phase() == Phase::Ready);
}

TEST_CASE("Plan: a plan not recorded keeps the cube hidden", "[inspection]") {
  InspectionFlow f = planFlow();
  space(f, 0);
  space(f, 100);
  auto e = space(f, 200);
  CHECK(e.finishPlan);
  // The page found nothing done and didn't call planDone().
  CHECK(f.phase() == Phase::Executing);
  CHECK(space(f, 300).finishPlan);
}

TEST_CASE("Plan: Space waits for a scramble", "[inspection]") {
  InspectionFlow f = planFlow();
  auto e = space(f, 0, false);
  CHECK(e.used);
  CHECK(e.waitScramble);
  CHECK_FALSE(e.startInspection);
  CHECK(f.phase() == Phase::Ready);
}

TEST_CASE("Plan: the time limit hides the cube, with a grace for Space",
          "[inspection]") {
  InspectionFlow f = planFlow(10, false);
  space(f, 1000);
  CHECK_FALSE(f.tick(10999).startExecution);
  CHECK(f.phase() == Phase::Inspecting);
  auto e = f.tick(11040);
  CHECK(e.startExecution);
  CHECK(f.phase() == Phase::Executing);
  CHECK(f.usedMs() == 10000); // capped at the limit

  // A Space just after was meant to hide the cube.
  e = space(f, 11040 + InspectionFlow::kHideGraceMs - 1);
  CHECK(e.used);
  CHECK_FALSE(e.finishPlan);
  CHECK(space(f, 11040 + InspectionFlow::kHideGraceMs).finishPlan);
}

TEST_CASE("Plan: no limit never hides the cube", "[inspection]") {
  InspectionFlow f = planFlow(0, false);
  space(f, 0);
  CHECK_FALSE(f.tick(600000).startExecution);
  CHECK(f.phase() == Phase::Inspecting);
  auto c = f.countdown(42500);
  CHECK(c.shown == 42); // the seconds used
  CHECK(c.alert == Alert::None);
  space(f, 50000);
  CHECK(f.usedMs() == 50000);
}

TEST_CASE("Plan: the countdown and its calls", "[inspection]") {
  InspectionFlow wca = planFlow(15, true);
  space(wca, 0);
  CHECK(wca.countdown(0).shown == 15);
  CHECK(wca.countdown(1).shown == 15);
  CHECK(wca.countdown(1000).shown == 14);
  CHECK(wca.countdown(7999).alert == Alert::None);
  CHECK(wca.countdown(8000).alert == Alert::Eight);
  CHECK(wca.countdown(11999).alert == Alert::Eight);
  CHECK(wca.countdown(12000).alert == Alert::Twelve);
  CHECK(wca.countdown(14500).shown == 1);

  InspectionFlow plain = planFlow(15, false);
  space(plain, 0);
  CHECK(plain.countdown(11999).alert == Alert::None);
  CHECK(plain.countdown(12000).alert == Alert::Ending);

  InspectionFlow tenWca = planFlow(10, true); // a file may say so
  space(tenWca, 0);
  CHECK(tenWca.countdown(9000).alert == Alert::Eight);
}

TEST_CASE("Turning keys by phase", "[inspection]") {
  InspectionFlow f = planFlow();
  auto e = f.key(turnKey(), 0, true);
  CHECK(e.used);
  CHECK(e.turn == Turn::None);
  space(f, 0);
  CHECK(f.key(turnKey(), 0, true).turn == Turn::Inspect);
  space(f, 100);
  CHECK(f.key(turnKey(), 0, true).turn == Turn::Hidden);
  space(f, 200);
  f.planDone();
  CHECK(f.key(turnKey(), 0, true).turn == Turn::Look);

  InspectionFlow g = edgesFlow();
  space(g, 0);
  CHECK(g.key(turnKey(), 0, true).turn == Turn::Inspect);
}

TEST_CASE("Keys not for the page, releases and repeats", "[inspection]") {
  InspectionFlow f = planFlow();
  CHECK_FALSE(f.key(down(Key::Other), 0, true).used);
  CHECK_FALSE(f.key(digit(3), 0, true).used);       // not counting, not a turn
  CHECK_FALSE(f.key(down(Key::Tab), 0, true).used); // Tab only in a result
  auto e = f.key(up(Key::Space), 0, true);
  CHECK(e.used);
  CHECK_FALSE(e.startInspection);
  InspectionFlow::KeyInput repeat = down(Key::Space);
  repeat.autoRepeat = true;
  e = f.key(repeat, 0, true);
  CHECK(e.used);
  CHECK_FALSE(e.startInspection);
  CHECK(f.phase() == Phase::Ready);

  // A turning key that is also Space is Space.
  InspectionFlow::KeyInput both = down(Key::Space);
  both.turn = true;
  e = f.key(both, 0, true);
  CHECK(e.startInspection);
  CHECK(e.turn == Turn::None);
}

TEST_CASE("Esc puts the scramble back or ends a retry", "[inspection]") {
  InspectionFlow f = planFlow();
  auto e = f.key(down(Key::Escape), 0, true);
  CHECK(e.used);
  CHECK_FALSE(e.cancel);
  CHECK_FALSE(e.next);

  space(f, 0);
  e = f.key(down(Key::Escape), 10, true);
  CHECK(e.cancel);
  CHECK(f.phase() == Phase::Ready);
  space(f, 20);
  space(f, 30);
  CHECK(f.key(down(Key::Escape), 40, true).cancel);
  CHECK(f.phase() == Phase::Ready);

  REQUIRE(f.startRetry(3));
  REQUIRE(f.retry());
  CHECK(*f.retry() == 3);
  e = f.key(down(Key::Escape), 50, true);
  CHECK(e.next);
  CHECK_FALSE(f.retry());
}

TEST_CASE("Retries", "[inspection]") {
  InspectionFlow f = planFlow();
  REQUIRE(f.startRetry(2));
  // A retry has its scramble: no need to wait for one.
  auto e = space(f, 0, false);
  CHECK(e.startInspection);
  CHECK_FALSE(f.startRetry(1)); // not during an attempt
  CHECK(*f.retry() == 2);
  // Esc during the retry puts its scramble back; the retry goes on.
  f.key(down(Key::Escape), 10, true);
  CHECK(f.retry());
  space(f, 20, false);
  space(f, 30);
  space(f, 40);
  f.planDone();
  CHECK(f.retry());
  space(f, 50); // the next scramble ends it
  CHECK_FALSE(f.retry());

  CHECK_FALSE(edgesFlow().startRetry(0)); // only Plan sessions retry
}

TEST_CASE("Skip and leaving the page", "[inspection]") {
  InspectionFlow f = planFlow();
  auto s = f.skip();
  CHECK(s.done);
  CHECK(s.advanceFeed);
  space(f, 0);
  CHECK_FALSE(f.skip().done); // not during an attempt
  CHECK(f.leave());
  CHECK(f.phase() == Phase::Ready);
  CHECK_FALSE(f.leave());

  f.startRetry(0);
  s = f.skip();
  CHECK(s.done);
  CHECK_FALSE(s.advanceFeed); // the retry's scramble wasn't the feed's
  CHECK_FALSE(f.retry());

  space(f, 0);
  space(f, 10);
  space(f, 20);
  f.planDone();
  s = f.skip();
  CHECK(s.done);
  CHECK_FALSE(s.advanceFeed); // the feed moved on when it was recorded
}

TEST_CASE("Edges: the counts are typed per axis", "[inspection]") {
  InspectionFlow f = edgesFlow();
  auto e = space(f, 1000);
  CHECK(e.startCounting);
  CHECK(f.phase() == Phase::Counting);
  CHECK(f.axis() == 0);
  CHECK(f.entry().empty());
  CHECK(space(f, 1100).used); // Space does nothing while counting
  CHECK(f.phase() == Phase::Counting);

  e = f.key(down(Key::Enter), 0, true);
  CHECK(e.needEntry);
  CHECK(f.axis() == 0);

  typeCount(f, "4");
  CHECK(f.entry() == "4");
  e = f.key(down(Key::Enter), 0, true);
  CHECK(e.entryChanged);
  CHECK_FALSE(e.finishEdges);
  CHECK(f.axis() == 1);
  CHECK(f.entry().empty());
  CHECK(f.answers()[0] == 4);

  typeCount(f, "6");
  e = f.key(down(Key::Enter), 0, true);
  CHECK(e.finishEdges);
  CHECK(f.answers()[1] == 6);
  CHECK(f.elapsed(3400) == 2400);
  f.edgesDone({4, 8});
  CHECK(f.phase() == Phase::Result);
  CHECK(f.ringAxis() == 1); // the wrong one
}

TEST_CASE("Edges: digits, Backspace and modifiers", "[inspection]") {
  InspectionFlow f = edgesFlow();
  space(f, 0);
  typeCount(f, "0");
  CHECK(f.entry() == "0");
  typeCount(f, "7"); // a leading 0 is replaced
  CHECK(f.entry() == "7");
  typeCount(f, "3"); // 73 is more than 12
  CHECK(f.entry() == "7");
  f.key(down(Key::Backspace), 0, true);
  CHECK(f.entry().empty());
  f.key(down(Key::Backspace), 0, true); // nothing to take back
  CHECK(f.entry().empty());
  typeCount(f, "12");
  CHECK(f.entry() == "12");
  typeCount(f, "0"); // three digits
  CHECK(f.entry() == "12");

  // Backspace repeats; other keys don't.
  InspectionFlow::KeyInput back = down(Key::Backspace);
  back.autoRepeat = true;
  CHECK(f.key(back, 0, true).entryChanged);
  CHECK(f.entry() == "1");
  InspectionFlow::KeyInput rep = digit(0);
  rep.autoRepeat = true;
  CHECK(f.key(rep, 0, true).used);
  CHECK(f.entry() == "1");

  // With Ctrl, Alt or Meta an entry key isn't the page's (Ctrl+7 switches
  // modes), even when it turns the cube.
  InspectionFlow::KeyInput ctrl = digit(0);
  ctrl.modifiers = true;
  ctrl.turn = true;
  CHECK_FALSE(f.key(ctrl, 0, true).used);
  CHECK(f.entry() == "1");

  // A digit that is a turning key types while counting.
  InspectionFlow::KeyInput both = digit(0);
  both.turn = true;
  auto e = f.key(both, 0, true);
  CHECK(e.turn == Turn::None);
  CHECK(f.entry() == "10");

  CHECK(f.key(down(Key::Escape), 0, true).cancel);
  CHECK(f.phase() == Phase::Ready);
  CHECK(f.entry().empty());
}

TEST_CASE("Edges: the axes asked", "[inspection]") {
  InspectionFlow rl = edgesFlow(false, true);
  space(rl, 0);
  CHECK(rl.axis() == 1);
  typeCount(rl, "5");
  CHECK(rl.key(down(Key::Enter), 0, true).finishEdges);
  CHECK(rl.answers()[0] == -1);
  CHECK(rl.answers()[1] == 5);

  InspectionFlow fb = edgesFlow(true, false);
  space(fb, 0);
  CHECK(fb.axis() == 0);
  typeCount(fb, "2");
  CHECK(fb.key(down(Key::Enter), 0, true).finishEdges);
  fb.edgesDone({2, -1});
  // Tab rings the other axis only when both are asked.
  auto e = fb.key(down(Key::Tab), 0, true);
  CHECK(e.used);
  CHECK_FALSE(e.rings);
  CHECK(fb.ringAxis() == 0);
}

TEST_CASE("Edges: Tab rings the other axis", "[inspection]") {
  InspectionFlow f = edgesFlow();
  space(f, 0);
  typeCount(f, "4");
  f.key(down(Key::Enter), 0, true);
  typeCount(f, "6");
  f.key(down(Key::Enter), 0, true);
  f.edgesDone({4, 6});
  CHECK(f.ringAxis() == 0);
  auto e = f.key(down(Key::Tab), 0, true);
  CHECK(e.rings);
  CHECK(f.ringAxis() == 1);
  f.key(down(Key::Tab), 0, true);
  CHECK(f.ringAxis() == 0);
  CHECK(space(f, 0).next);
  // A new count starts afresh.
  space(f, 0);
  CHECK(f.axis() == 0);
  CHECK(f.answers()[0] == -1);
}

TEST_CASE("ringedAxis rings the first asked or the first wrong",
          "[inspection]") {
  CHECK(ringedAxis({4, 6}, {4, 6}) == 0);
  CHECK(ringedAxis({4, 6}, {5, 6}) == 0);
  CHECK(ringedAxis({4, 6}, {4, 7}) == 1);
  CHECK(ringedAxis({3, 6}, {4, 7}) == 0);
  CHECK(ringedAxis({-1, 6}, {-1, 6}) == 1);
  CHECK(ringedAxis({4, -1}, {5, -1}) == 0);
  CHECK(ringedAxis({-1, -1}, {-1, -1}) == -1);
}

TEST_CASE("doneHold prefers the hold the cube is in", "[inspection]") {
  FaceletCube solved;
  CHECK(doneHold(solved, SolveStep::EOCross) == kWcaHold);
  FaceletCube turned;
  turned.apply(parseAlgOrEmpty("y"));
  CHECK(doneHold(turned, SolveStep::EOCross) == holdOf(turned));

  // R leaves the cross only on orange (L), done with red on top.
  FaceletCube r;
  r.apply(parseAlgOrEmpty("R"));
  std::optional<Hold> h = doneHold(r, SolveStep::EOCross);
  REQUIRE(h);
  CHECK(h->top == 1);

  FaceletCube scrambled;
  scrambled.apply(parseAlgOrEmpty("R U F' L2 D B R' F U2 L B' D2"));
  REQUIRE(stepReachedHolds(scrambled, SolveStep::EOCross).empty());
  CHECK_FALSE(doneHold(scrambled, SolveStep::EOCross));
}
