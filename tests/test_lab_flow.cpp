#include "train/lab_flow.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace vcube;

namespace {

using Key = LabFlow::Key;
using Action = LabFlow::Action;
using Phase = LabFlow::Phase;

const std::int64_t kNow = 10'000;

LabFlow::KeyEvent press(Key k) {
  LabFlow::KeyEvent e;
  e.key = k;
  return e;
}
LabFlow::KeyEvent release(Key k) {
  LabFlow::KeyEvent e = press(k);
  e.press = false;
  return e;
}
// A key that turns the virtual cube.
LabFlow::KeyEvent turnKey() {
  LabFlow::KeyEvent e;
  e.move = Move{MoveType::R, 1};
  return e;
}

LabFlow at(Phase p, bool virt) {
  LabFlow f;
  f.setVirtualCube(virt);
  f.setPhase(p);
  return f;
}

} // namespace

TEST_CASE("lab flow: Space starts and goes on", "[lab_flow]") {
  CHECK(at(Phase::Idle, true).key(press(Key::Space), kNow).action ==
        Action::Next);
  CHECK(at(Phase::Finished, false).key(press(Key::Space), kNow).action ==
        Action::Next);
  LabFlow::KeyEvent held = press(Key::Space);
  held.modified = true; // Ctrl+Space is not the flow's
  CHECK_FALSE(at(Phase::Idle, true).key(held, kNow).used);
}

TEST_CASE("lab flow: a virtual solve starts and ends with turns",
          "[lab_flow]") {
  LabFlow f = at(Phase::Ready, true);
  CHECK(f.key(turnKey(), kNow).action == Action::Turn);
  CHECK_FALSE(
      f.key(press(Key::Space), kNow).used); // no hold on the virtual cube
  CHECK(f.key(press(Key::Escape), kNow).action == Action::ResetReady);
  f.setPhase(Phase::Running);
  CHECK(f.key(turnKey(), kNow).action == Action::Turn);
  CHECK(f.key(press(Key::Space), kNow).action == Action::StopVirtual);
  CHECK(f.key(press(Key::Escape), kNow).action == Action::Cancel);
  CHECK(f.key(release(Key::Space), kNow).used);
}

TEST_CASE("lab flow: a physical solve is held to start, any key stops it",
          "[lab_flow]") {
  LabFlow f = at(Phase::Ready, false);
  CHECK(f.key(press(Key::Space), kNow).action == Action::HoldPress);
  CHECK(f.key(release(Key::Space), kNow).action == Action::HoldRelease);
  CHECK_FALSE(f.key(turnKey(), kNow).used);
  f.setPhase(Phase::Running);
  CHECK(f.key(press(Key::Other), kNow).action == Action::StopPhysical);
  CHECK(f.key(turnKey(), kNow).action == Action::StopPhysical);
  // Shift, Ctrl or a lock key never stops the timer.
  CHECK_FALSE(f.key(press(Key::Modifier), kNow).used);
  CHECK(f.key(press(Key::Escape), kNow).action == Action::Cancel);
}

TEST_CASE("lab flow: after an attempt", "[lab_flow]") {
  LabFlow f = at(Phase::Ready, false);
  f.stopped(10'000);
  CHECK(f.phase() == Phase::Done);
  // A second press just after the stop is swallowed.
  LabFlow::Step early = f.key(press(Key::Space), 10'100);
  CHECK(early.used);
  CHECK(early.action == Action::None);
  // But a modifier isn't held back.
  CHECK_FALSE(f.key(press(Key::Modifier), 10'100).used);
  std::int64_t later = 10'000 + LabFlow::kStopGraceMs;
  CHECK(f.key(press(Key::Space), later).action == Action::Next);
  CHECK(f.key(press(Key::Backspace), later).action == Action::ToggleDnf);
  CHECK(f.key(press(Key::Delete), later).action == Action::DeleteLast);
  CHECK(f.key(press(Key::Escape), later).action == Action::SameAgain);
  CHECK_FALSE(f.key(press(Key::Other), later).used);
}
