#include "train/drill_flow.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace vcube;

namespace {

using Key = DrillFlow::Key;
using Action = DrillFlow::Action;
using Phase = DrillFlow::Phase;
using Result = DrillFlow::Result;
using Turn = DrillFlow::Turn;

const Move kR{MoveType::R, 1};
const Move kY{MoveType::Y, 1};

DrillFlow::KeyEvent press(Key k, bool shift = false) {
  return {k, true, shift, {}};
}
DrillFlow::KeyEvent release(Key k) {
  return {.key = k, .press = false, .shift = false, .move = {}};
}
DrillFlow::KeyEvent turn(Move m) {
  return {.key = Key::Other, .press = true, .shift = false, .move = m};
}

DrillFlow flow(DrillFlow::Config c = {}) {
  DrillFlow f;
  f.setConfig(c);
  f.setUp(1000);
  return f;
}

DrillFlow::Config physical() { return {.virtualCube = false}; }

auto done(bool d) {
  return [d] { return d; };
}

} // namespace

TEST_CASE("drill: a tap of Space arms the virtual timer, a face turn starts") {
  DrillFlow f = flow();
  REQUIRE(f.phase() == Phase::Waiting);
  // Rotations look at the case; a face turn is refused before arming.
  CHECK(f.key(turn(kY), 1100).action == Action::Rotate);
  auto refused = f.key(turn(kR), 1100);
  CHECK(refused.action == Action::Hint);
  CHECK(refused.hint == DrillFlow::Hint::OnlyRotations);

  CHECK(f.key(press(Key::Space), 1200).action == Action::Armed);
  CHECK(f.armed());
  CHECK_FALSE(f.busy()); // armed is not under way
  CHECK(f.key(press(Key::Space), 1250).action == Action::Armed);
  CHECK_FALSE(f.armed()); // a second tap disarms
  f.key(press(Key::Space), 1300);

  auto start = f.key(turn(kR), 2000);
  CHECK(start.action == Action::Start);
  CHECK(start.move == kR);
  CHECK(f.phase() == Phase::Running);
  CHECK(f.busy());
  // The page turns the cube and reports it: not done yet, then done.
  CHECK_FALSE(f.turned(Turn::OffUdFace, 2000, done(false)));
  auto stopped = f.turned(Turn::UdFace, 3500, done(true));
  REQUIRE(stopped);
  CHECK(*stopped == 1500);

  f.finished(*stopped, Result::Solved, 2000, 3500);
  CHECK(f.phase() == Phase::Revealed);
  CHECK(f.recorded());
  CHECK(f.newBest());
  // Recognition from the set-up to the first turn off U-D, execution
  // after it.
  auto t = f.timing(1500, false);
  REQUIRE(t);
  CHECK(t->recognitionMs == 1000);
  CHECK(t->executionMs == 1500);
  CHECK_FALSE(f.timing(1500, true)); // a failed case wasn't recognised
}

TEST_CASE("drill: keys just after the stop are ignored") {
  DrillFlow f = flow();
  f.key(press(Key::Space), 1100);
  f.key(turn(kR), 1200);
  f.finished(800, Result::Solved, std::nullopt, 2000);
  auto early = f.key(press(Key::Space), 2000 + DrillFlow::kGuardMs - 1);
  CHECK(early.handled);
  CHECK(early.action == Action::None);
  CHECK(f.key(press(Key::Space), 2000 + DrillFlow::kGuardMs).action ==
        Action::NextCase);
}

TEST_CASE("drill: auto-stop off, Space stops; a DNF unless done") {
  const DrillFlow::Config c{.autoStop = false};
  DrillFlow f = flow(c);
  f.key(press(Key::Space), 1100);
  // Armed by Space, the first face turn starts the solve.
  REQUIRE(f.key(turn(kR), 1200).action == Action::Start);
  DrillFlow g = flow(c);
  g.key(press(Key::Space), 1100);
  g.key(press(Key::Space), 1150); // disarmed
  g.key(press(Key::Space), 1160);
  g.key(turn(kY), 1170); // a rotation doesn't start it
  CHECK(g.phase() == Phase::Waiting);

  CHECK_FALSE(f.turned(Turn::OffUdFace, 1200, done(true))); // no auto-stop
  auto stop = f.key(press(Key::Space), 1700);
  CHECK(stop.action == Action::Stopped);
  CHECK(stop.timeMs == 500);
  f.finished(stop.timeMs, Result::Failed, std::nullopt, 1700);
  CHECK(f.unfinished());
  CHECK_FALSE(f.newBest());
}

TEST_CASE("drill: Space before any face turn cancels the solve") {
  DrillFlow f = flow({.tapToArm = false});
  f.key(press(Key::Space), 1000);
  f.tick(1400);
  CHECK(f.holding());
  CHECK(f.busy());
  CHECK(f.key(release(Key::Space), 1400).action == Action::Start);
  CHECK(f.phase() == Phase::Running);
  CHECK(f.key(press(Key::Space), 1500).action == Action::CancelNoTurn);
  f.cancelSolve();
  CHECK(f.phase() == Phase::Waiting);
  CHECK_FALSE(f.busy());
  // A cancelled solve was seen: no recognition time any more.
  f.key(press(Key::Space), 2000);
  f.tick(2400);
  f.key(release(Key::Space), 2400);
  f.turned(Turn::OffUdFace, 2500, done(false));
  CHECK_FALSE(f.timing(900, false));
}

TEST_CASE("drill: holding Space, a face turn starts once ready") {
  DrillFlow f = flow({.tapToArm = false});
  f.key(press(Key::Space), 1000);
  auto early = f.key(turn(kR), 1100);
  CHECK(early.hint == DrillFlow::Hint::KeepHolding);
  f.tick(1300);
  CHECK(f.key(turn(kR), 1300).action == Action::Start);
  CHECK(f.phase() == Phase::Running);
  // Esc cancels the solve.
  CHECK(f.key(press(Key::Escape), 1500).action == Action::Cancel);
}

TEST_CASE("drill: a hold the window loses is called off") {
  DrillFlow f = flow({.tapToArm = false});
  f.key(press(Key::Space), 1000);
  CHECK(f.loseKeyboard());
  CHECK_FALSE(f.holding());
  CHECK_FALSE(f.loseKeyboard());
}

TEST_CASE("drill: physical cube: hold, let go, any key stops") {
  DrillFlow f = flow(physical());
  // No scramble yet: Space waits.
  CHECK(f.key(press(Key::Space), 1000, Scramble::Awaited).action ==
        Action::None);
  CHECK(f.key(press(Key::Space), 1000).action == Action::TimerShown);
  f.tick(1300);
  CHECK(f.key(release(Key::Space), 1300).action == Action::Start);
  // A key pressed in letting go of Space doesn't stop it.
  CHECK(f.key(press(Key::Other), 1300 + DrillFlow::kMinPhysicalMs - 1).action ==
        Action::None);
  // A modifier never stops it.
  auto mod = f.key(press(Key::Modifier), 2000);
  CHECK(mod.handled);
  CHECK(mod.action == Action::None);
  auto stop = f.key(press(Key::Other), 4300);
  CHECK(stop.action == Action::Stopped);
  CHECK(stop.timeMs == 3000);
  f.finished(stop.timeMs, Result::Solved, std::nullopt, 4300);
  CHECK_FALSE(f.timing(3000, false)); // only on the virtual cube
}

TEST_CASE("drill: giving up before the start is taken back by Delete") {
  DrillFlow f = flow();
  CHECK(f.key(press(Key::Space, true), 1500).action == Action::GiveUp);
  CHECK(f.giveUp(1500) == 0);
  CHECK(f.gaveUpBeforeStart());
  f.finished(0, Result::GaveUp, std::nullopt, 1500);
  CHECK(f.gaveUp());
  CHECK_FALSE(f.unfinished());
  // No time to go back to: Backspace doesn't toggle the DNF.
  CHECK(f.key(press(Key::Backspace), 2000).action == Action::None);
  CHECK_FALSE(f.setWrong(false));
  // Shift is still held from giving up: Shift+Space keeps the result up.
  CHECK(f.key(press(Key::Space, true), 2000).action == Action::None);
  CHECK(f.key(press(Key::Delete), 2000).action == Action::Delete);
  CHECK(f.deleted());
  CHECK(f.phase() == Phase::Waiting);
  CHECK_FALSE(f.recorded());
  CHECK_FALSE(f.gaveUp());
}

TEST_CASE("drill: giving up a running solve keeps it deleted") {
  DrillFlow f = flow();
  f.key(press(Key::Space), 1100);
  f.key(turn(kR), 1200);
  CHECK(f.key(press(Key::Space, true), 1700).action == Action::GiveUp);
  CHECK(f.giveUp(1700) == 500);
  CHECK_FALSE(f.gaveUpBeforeStart());
  f.finished(500, Result::GaveUp, std::nullopt, 1700);
  // Ctrl+Z doesn't delete: it puts a deleted attempt back.
  CHECK(f.key(press(Key::Undo), 2100).action == Action::Restore);
  CHECK_FALSE(f.restored()); // nothing was deleted
  CHECK(f.key(press(Key::Delete), 2100).action == Action::Delete);
  CHECK_FALSE(f.deleted()); // only the attempt goes
  CHECK(f.phase() == Phase::Revealed);
  CHECK_FALSE(f.recorded());
  CHECK(f.key(press(Key::Undo), 2200).action == Action::Restore);
  CHECK(f.restored());
  CHECK(f.recorded());
  CHECK(f.gaveUp());
  // In practice too.
  f.startPractice(true);
  CHECK(f.key(press(Key::Delete), 2300).action == Action::Delete);
  f.deleted();
  CHECK(f.key(press(Key::Undo), 2300).action == Action::Restore);
  CHECK(f.restored());
  // Not once the next case is up.
  f.key(press(Key::Delete), 2300);
  f.deleted();
  f.setUp(2400);
  CHECK_FALSE(f.restored());
}

TEST_CASE("drill: Backspace gives up when Settings say so") {
  DrillFlow f = flow({.shiftSpaceGivesUp = false});
  CHECK(f.key(press(Key::Backspace), 1100).action == Action::GiveUp);
  CHECK(f.key(press(Key::Space, true), 1100).action == Action::Armed);

  DrillFlow g = flow();
  auto hint = g.key(press(Key::Backspace), 1100);
  CHECK(hint.action == Action::Hint);
  CHECK(hint.hint == DrillFlow::Hint::GiveUpKey);
}

TEST_CASE("drill: the result can be marked a DNF and back") {
  DrillFlow f = flow();
  f.key(press(Key::Space), 1100);
  f.key(turn(kR), 1200);
  f.finished(900, Result::Solved, 1000, 2100);
  CHECK(f.newBest());
  CHECK(f.key(press(Key::Backspace), 2500).action == Action::ToggleWrong);
  CHECK(f.setWrong(true));
  CHECK(f.failed());
  CHECK_FALSE(f.newBest());
  CHECK(f.setWrong(false));
  CHECK(f.newBest());
  CHECK(f.key(press(Key::Escape), 2500).action == Action::Retry);
}

TEST_CASE("drill: practice with the algs, then without, then on") {
  DrillFlow f = flow();
  f.key(press(Key::Space), 1100);
  f.key(turn(kR), 1200);
  f.finished(900, Result::Failed, std::nullopt, 2100);
  auto tab = f.key(press(Key::Tab), 2500);
  REQUIRE(tab.action == Action::Practise);
  CHECK(tab.aided);
  f.startPractice(true);
  CHECK(f.phase() == Phase::Aided);
  CHECK(f.resultShown());
  int token = f.practiceToken();
  // Turns go to the cube; the stage is done once the step is.
  CHECK(f.key(turn(kR), 2600).action == Action::PracticeTurn);
  CHECK_FALSE(f.practiceTurned(false, done(true))); // a rotation
  CHECK(f.practiceTurned(true, done(true)));
  CHECK(f.practiceDone());
  CHECK_FALSE(f.practiceTurned(true, done(true))); // stays done
  CHECK(f.practiceToken() == token);
  // Esc resets the stage, Tab goes to the next one.
  auto esc = f.key(press(Key::Escape), 2700);
  CHECK(esc.action == Action::PracticeReset);
  CHECK(esc.aided);
  auto next = f.key(press(Key::Tab), 2700);
  CHECK(next.action == Action::Practise);
  CHECK_FALSE(next.aided);
  f.startPractice(false);
  CHECK(f.phase() == Phase::Unaided);
  CHECK_FALSE(f.practiceDone());
  CHECK(f.practiceToken() != token); // a pending advance is dropped
  // The DNF stays open to change; Space goes on.
  CHECK(f.key(press(Key::Backspace), 2800).action == Action::ToggleWrong);
  CHECK(f.setWrong(false));
  CHECK(f.key(press(Key::Space), 2800).action == Action::NextCase);
}

TEST_CASE("drill: Space practises after a DNF when Settings say so") {
  DrillFlow::Config c{.practiseAfterDnf = true};
  DrillFlow f = flow(c);
  f.key(press(Key::Space), 1100);
  f.key(turn(kR), 1200);
  f.finished(900, Result::Failed, std::nullopt, 2100);
  auto space = f.key(press(Key::Space), 2500);
  CHECK(space.action == Action::Practise);
  CHECK(space.aided);
  CHECK(f.key(press(Key::Enter), 2500).action == Action::NextCase);
  // With Backspace giving up, Shift+Space skips the practice.
  c.shiftSpaceGivesUp = false;
  f.setConfig(c);
  CHECK(f.key(press(Key::Space, true), 2500).action == Action::NextCase);
}

TEST_CASE("drill: physical practice and leaving the page") {
  DrillFlow f = flow(physical());
  f.startPractice(true);
  // No cube to reset or turn on a physical cube.
  CHECK(f.key(press(Key::Escape), 1000).action == Action::None);
  CHECK(f.key(turn(kR), 1000).action == Action::None);

  DrillFlow g = flow();
  g.key(press(Key::Space), 1100);
  CHECK_FALSE(g.leave()); // armed: disarmed
  CHECK_FALSE(g.armed());
  g.key(press(Key::Space), 1200);
  g.key(turn(kR), 1300);
  CHECK(g.leave()); // a running solve is cancelled
}

TEST_CASE("drill: with no case, Space looks again") {
  DrillFlow f;
  f.noCase();
  CHECK(f.phase() == Phase::NoCase);
  CHECK(f.key(press(Key::Space), 0).action == Action::NextFromIdle);
  CHECK(f.key(press(Key::Enter), 0).action == Action::NextFromIdle);
  CHECK_FALSE(f.key(press(Key::Tab), 0).handled);
  CHECK(f.key(turn(kR), 0).handled); // turns are swallowed
}

TEST_CASE("drill: time with the window in the back isn't recognition") {
  DrillFlow f = flow();
  f.windowLost();
  f.key(press(Key::Space), 1100);
  f.key(turn(kR), 1200);
  f.turned(Turn::OffUdFace, 1200, done(false));
  f.finished(800, Result::Solved, std::nullopt, 2000);
  CHECK_FALSE(f.timing(800, false));

  // After the first turn off U-D, recognition is over: it stays.
  DrillFlow g = flow();
  g.key(press(Key::Space), 1100);
  g.key(turn(kR), 1200);
  g.turned(Turn::OffUdFace, 1200, done(false));
  g.windowLost();
  g.finished(800, Result::Solved, std::nullopt, 2000);
  auto t = g.timing(800, false);
  REQUIRE(t);
  CHECK(t->recognitionMs == 200);
}
