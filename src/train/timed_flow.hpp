#pragma once

#include "train/timer.hpp"

#include <cstdint>
#include <optional>

namespace vcube {

// Timed mode's solves as a state machine around SolveTimer: the keys come
// in, and what the page should do comes out (Effects), in the style of
// SolveTimer itself. All times are milliseconds on a monotonic clock
// supplied by the caller.
//
// On a physical cube Space holds, starts (and with inspection first
// inspects) and any key stops the timer. On the virtual cube the cube can
// only be turned as a whole until the timer starts, Space (or the cube
// being solved) stops it, and a solve stopped unsolved is a DNF that stays
// on screen, "finished", until Space or Enter asks for the next scramble.
// With `hideUntilInspection` (and inspection on) the virtual cube is
// hidden until inspection starts.
class TimedFlow {
public:
  struct Config {
    SolveTimer::Config timer;
    bool virtualCube = true;
    bool hideUntilInspection = false;
  };

  // What the page does after an event, in this order.
  struct Effects {
    bool used = false;           // the key was for the page
    bool showLastResult = false; // the display goes back to the last solve
    bool cancelled = false;      // say the solve was cancelled
    bool clearStatus = false;
    bool restartCube = false;  // the scramble as it was dealt, again
    bool nextScramble = false; // on to the next scramble
    bool startAttempt = false; // virtual: the solve's moves start counting
    bool stopVirtual = false;  // virtual: stop, and judge the cube (stopped())
    std::optional<SolveTimer::Result> record; // physical: the solve to keep
    bool showTimer = false; // the timer and the cube are shown afresh
  };

  // What a turning key does, before or after a solve. A Turn may still be
  // refused by the cube (only rotations before the start), which the page
  // tells as OnlyRotations.
  enum class MoveAnswer {
    Ignore,       // Space is held to start: the turn waits
    Turn,         // turn the cube (during the solve or a rotation before)
    Hidden,       // the cube is hidden: say Space starts inspection
    NextScramble, // a finished DNF: say Space gives the next scramble
  };

  void setConfig(const Config &c);
  const Config &config() const { return config_; }
  const SolveTimer &timer() const { return timer_; }
  SolveTimer::State state() const { return timer_.state(); }

  // A hold, inspection or solve is under way.
  bool isBusy() const;
  bool isRunning() const {
    return timer_.state() == SolveTimer::State::Running;
  }
  // Idle or stopped: the keys for the last solve may be used.
  bool isIdle() const;
  // Space is held to start (Holding or Ready).
  bool isHolding() const;
  // A virtual DNF is on screen until the next scramble.
  bool finished() const { return finished_; }
  // The virtual cube is hidden until inspection starts.
  bool cubeHidden() const;

  // Esc: cancels a hold, inspection or solve, or leaves a finished DNF.
  Effects escape();
  // Space down or up. `scrambleReady`: a scramble is shown to solve.
  Effects space(bool press, std::int64_t now, bool scrambleReady);
  // Enter down: the next scramble after a finished DNF (else unused).
  Effects enter();
  // Any key down while a physical solve runs: stops it.
  Effects stopKey(std::int64_t now);
  // A turning key down before or after the solve (during a virtual solve
  // every turn is the page's to do).
  MoveAnswer move(bool rotation) const;
  // Space or a solved cube ended a virtual solve (stopVirtual): the result
  // to keep, if the timer was running; unsolved, it is a DNF. Once it is
  // kept, keepDnf() leaves an unsolved cube on screen: finished().
  std::optional<SolveTimer::Result> stopVirtual(std::int64_t now, bool solved);
  void keepDnf() { finished_ = true; }
  // The next scramble is shown (Skip, a new scramble type or session).
  void nextScramble() { finished_ = false; }

  // Advances a hold. With `popupOpen` (a popup or dialog took the keys)
  // a hold is let go of, as its release won't come.
  void tick(std::int64_t now, bool popupOpen);
  // The page was left: anything under way is cancelled. True if a solve
  // or inspection was (the page says so and restarts the cube).
  bool leave();
  // The window lost the keyboard: a hold is let go of, its release won't
  // come. True if one was.
  bool dropHold();

private:
  Config config_;
  SolveTimer timer_;
  bool finished_ = false;
};

} // namespace vcube
