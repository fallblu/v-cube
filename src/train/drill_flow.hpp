#pragma once

#include "cube/move.hpp"
#include "train/keys.hpp"
#include "train/timer.hpp"

#include <cstdint>
#include <functional>
#include <optional>

namespace vcube {

// One Drill case from its set-up to the next case, as a pure state machine
// in the style of SolveTimer: the page passes in keys, turns and the time
// (monotonic milliseconds) and does what key() asks (turn the cube, record
// the attempt, set the next case up, ...), telling the flow what came of
// it. The page keeps the case, the cube and what is recorded.
//
//   NoCase   nothing to drill; Space or Enter looks again
//   Waiting  the case is set up. Virtual cube: only rotations until the
//            timer starts, armed by a tap of Space (the first face turn
//            starts it) or by holding Space (let go, or a face turn once
//            it is ready). Physical: hold Space and let go.
//   Running  the timer runs. Virtual: it stops by itself once the step is
//            done (autoStop) or on Space (a DNF unless done); stopped
//            before any face turn, the solve is cancelled. Physical: any
//            key stops it, but not one pressed just after the start.
//   Revealed the result and the algs; keys are ignored for a moment after
//            the stop. Space goes on (or practises after a DNF), Tab
//            practises, Backspace toggles the DNF, Delete deletes the
//            attempt (one given up before the start is taken back) and
//            Ctrl+Z puts it back, Esc tries the case again.
//   Aided    practice, untimed and unrecorded: the case with the algs,
//   Unaided  then without them.
//
// The give-up key is Shift+Space (or Backspace, in Settings): a DNF, in
// Waiting or Running.
class DrillFlow {
public:
  enum class Phase { NoCase, Waiting, Running, Revealed, Aided, Unaided };

  struct Config {
    bool virtualCube = true;
    bool tapToArm = true;
    bool autoStop = true;
    bool shiftSpaceGivesUp = true;
    bool practiseAfterDnf = false;
    std::int64_t holdMs = 300;
  };

  using Key = TrainKey;
  using KeyEvent = TrainKeyEvent;

  // What the page is to do after a key.
  enum class Action {
    None,
    NextCase,      // set the next case up
    NextFromIdle,  // the same from NoCase (a recap that is done restarts)
    Retry,         // set the same case up again
    ResetWaiting,  // the timer is off again: the case back as it was set up
    Rotate,        // turn the cube as a whole by `move`
    Start,         // the timer started (and, with `move`, that turn)
    Turn,          // a turn of the solve: turn and then call turned()
    Armed,         // a tap of Space armed or disarmed the timer: show it
    TimerShown,    // Space is held or let go: show the timer
    Stopped,       // the timer stopped at `timeMs`: finish the attempt
    CancelNoTurn,  // virtual: stopped before any face turn
    Cancel,        // Esc during the solve
    GiveUp,        // call giveUp(), then finish the attempt as a DNF
    Practise,      // start practice, `aided` or not
    PracticeReset, // virtual: the practice stage from the start again
    PracticeTurn,  // turn and then call practiceTurned()
    ToggleWrong,   // mark the attempt failed or not
    Delete,        // delete the attempt, then call deleted()
    Restore,       // put the attempt deleted back, then call restored()
    Hint,          // say `hint`
  };
  enum class Hint {
    None,
    GiveUpKey,     // Backspace while waiting: the give-up key is Shift+Space
    KeepHolding,   // a face turn while Space is held but not ready yet
    OnlyRotations, // a face turn before the timer can start
  };
  struct Step {
    bool handled = false; // the key was used (or swallowed)
    Action action = Action::None;
    std::optional<Move> move{};
    bool aided = false;
    std::int64_t timeMs = 0;
    Hint hint = Hint::None;
  };

  // A key stopping the timer this soon after a physical start is taken for
  // one pressed in letting go of Space and ignored.
  static constexpr std::int64_t kMinPhysicalMs = 150;
  // Keys are ignored this long after the stop, so that a second key meant
  // for it doesn't skip the result.
  static constexpr std::int64_t kGuardMs = 300;

  void setConfig(const Config &c);
  const Config &config() const { return config_; }

  // --- events
  // A key; the scramble is one for a physical cube, which may be awaited.
  Step key(const KeyEvent &k, std::int64_t now,
           Scramble scramble = Scramble::Shown);
  // A case is set up to solve (again): Waiting, with nothing recorded.
  void setUp(std::int64_t now);
  // No case to drill.
  void noCase();
  // On to another case: whatever was under way stops.
  void leaveCase();
  // A turn of the solve: a rotation, a face turn on the U-D axis, or a
  // face turn off it, which ends recognition.
  enum class Turn : std::uint8_t { Rotation, UdFace, OffUdFace };
  // Virtual: a turn of the solve was made. Returns the time if that
  // stopped the timer, i.e. it did the step (`stepDone`, asked only when
  // the timer stops by itself) after a face turn.
  std::optional<std::int64_t> turned(Turn turn, std::int64_t now,
                                     const std::function<bool()> &stepDone);
  // Nothing is recorded; the case is set up again to try.
  void cancelSolve();
  // Gives the case up: the time so far, or 0 before the timer started.
  std::int64_t giveUp(std::int64_t now);
  // How an attempt ended: the case solved, the timer stopped with it
  // unsolved, or given up.
  enum class Result : std::uint8_t { Solved, Failed, GaveUp };
  // The attempt was recorded: `bestBefore` is the case's best before it.
  void finished(std::int64_t timeMs, Result result,
                std::optional<std::int64_t> bestBefore, std::int64_t now);
  // Marks the attempt failed or not; false if it can't be (nothing
  // recorded, given up, or no result shown).
  bool setWrong(bool wrong);
  // The attempt was deleted. True if it was a case given up before the
  // timer started, which is taken back: Waiting again.
  bool deleted();
  // The attempt deleted is back (Ctrl+Z, while the result is up); false
  // if it can't be (it is still recorded, or no result is shown).
  bool restored();
  // The attempt's record is gone (e.g. its times were cleared).
  void forgotten() { recorded_ = false; }
  // Practice of the case, with the algs or without.
  void startPractice(bool aided);
  // A face turn (or not) in practice; true once the cube first shows the
  // step done, after which it stays done.
  bool practiceTurned(bool face, const std::function<bool()> &stepDone);
  // The page was left: true if a running solve must be cancelled
  // (cancelSolve()); anything else under way is called off.
  bool leave();
  // The keyboard went away: a held Space won't be let go. True if a hold
  // was called off.
  bool loseKeyboard();
  // The window went to the back. Recognition not yet ended would count
  // the time away, so the attempt has no recognition time.
  void windowLost();
  void tick(std::int64_t now) { timer_.tick(now); }

  // --- state
  Phase phase() const { return phase_; }
  bool inPractice() const {
    return phase_ == Phase::Aided || phase_ == Phase::Unaided;
  }
  // The result is up (after the case, or in practice).
  bool resultShown() const { return phase_ == Phase::Revealed || inPractice(); }
  const SolveTimer &timer() const { return timer_; }
  // Virtual: Space tapped, waiting for the first face turn.
  bool armed() const { return armed_; }
  // Space is held for the timer.
  bool holding() const;
  // Space held or the solve running (armed is not busy).
  bool busy() const;
  // Face turns since the case was shown; read by the tests only (a seam,
  // kept on purpose).

  // The attempt: its time and how it went.
  std::int64_t timeMs() const { return timeMs_; }
  bool failed() const { return failed_; }
  bool unfinished() const { return unfinished_; } // virtual: step not done
  bool gaveUp() const { return gaveUp_; }
  bool gaveUpBeforeStart() const { return gaveUpBeforeStart_; }
  bool recorded() const { return recorded_; }
  bool newBest() const { return newBest_; }
  bool practiceDone() const { return practiceDone_; }
  // Changes whenever a pending automatic step of practice must be dropped.
  int practiceToken() const { return practiceToken_; }

  // Virtual cube: recognition (from the set-up to the first turn off the
  // U-D axis) and execution (from there to the stop) of a solve of
  // `timeMs` that wasn't failed, when they were measured.
  struct Timing {
    std::int64_t recognitionMs = 0;
    std::int64_t executionMs = 0;
  };
  std::optional<Timing> timing(std::int64_t timeMs, bool failed) const;

private:
  Config config_;
  SolveTimer timer_;
  Phase phase_ = Phase::NoCase;
  bool armed_ = false;
  int faceTurns_ = 0;
  std::int64_t guardUntil_ = 0;
  std::int64_t startMs_ = 0;
  std::int64_t shownAtMs_ = 0;
  std::int64_t firstTurnMs_ = 0;
  bool timingValid_ = false;
  std::int64_t timeMs_ = 0;
  bool failed_ = false;
  bool unfinished_ = false;
  bool gaveUp_ = false;
  bool gaveUpBeforeStart_ = false;
  bool recorded_ = false;
  bool newBest_ = false;
  std::optional<std::int64_t> bestBefore_;
  bool practiceDone_ = false;
  int practiceToken_ = 0;

  bool isGiveUp(const KeyEvent &k) const;
  void cancelTimer();
  void start(std::int64_t now);
  Step waitingKey(const KeyEvent &k, std::int64_t now, Scramble scramble);
  Step runningKey(const KeyEvent &k, std::int64_t now);
  Step revealedKey(const KeyEvent &k);
  Step practiceKey(const KeyEvent &k);
};

} // namespace vcube
