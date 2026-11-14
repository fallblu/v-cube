#pragma once

#include <cstdint>

namespace vcube {

// What each key does while Alg Lab times a test session's algs, apart
// from the timer itself: the panel (LabTestPanel) tells the flow its
// phase and does what the flow answers.
//
// Idle: nothing set up yet. Ready: an alg is set up; on the virtual cube
// the first layer turn starts the timer, on a physical cube Space held and
// let go does. Running: the timer runs. Done: an attempt was recorded.
// Finished: every alg has its reps.
class LabFlow {
public:
  enum class Phase { Idle, Ready, Running, Done, Finished };
  enum class Key { Space, Escape, Backspace, Delete, Move, Modifier, Other };

  struct Input {
    Key key = Key::Other;
    bool press = true;
    // No Ctrl, Alt or Meta is held.
    bool plain = true;
    std::int64_t now = 0; // ms, on the clock stopped() was given
  };

  enum class Action {
    None,
    Next,         // set up the next alg
    ToggleDnf,    // on the attempt just recorded
    DeleteLast,   // delete it and set its alg up again
    SameAgain,    // set the same alg up again
    ResetReady,   // put the case back as it was set up; the hold is dropped
    Turn,         // turn the virtual cube (it may start or end the solve)
    HoldPress,    // Space went down before a physical solve
    HoldRelease,  // and came up: the timer starts if it was held long enough
    StopVirtual,  // Space ends a virtual solve: a DNF unless it is solved
    StopPhysical, // any key ends a physical solve
    Cancel,       // nothing is recorded
  };

  struct Step {
    Action action = Action::None;
    // The key was the flow's: it goes no further.
    bool used = false;
  };

  // Keys are ignored for this long after the timer stops, so that a second
  // press doesn't skip the result.
  static constexpr std::int64_t kStopGraceMs = 300;

  void setVirtualCube(bool on) { virtual_ = on; }
  Phase phase() const { return phase_; }
  void setPhase(Phase p) { phase_ = p; }
  // The timer stopped at `now` and the attempt was recorded.
  void stopped(std::int64_t now) {
    phase_ = Phase::Done;
    stoppedAt_ = now;
  }

  Step key(const Input &in) const;

private:
  Phase phase_ = Phase::Idle;
  bool virtual_ = false;
  std::int64_t stoppedAt_ = 0;
};

} // namespace vcube
