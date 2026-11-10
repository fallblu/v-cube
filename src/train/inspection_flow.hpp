#pragma once

#include "cube/facelet.hpp"
#include "cube/hold.hpp"
#include "train/move_count.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace vcube {

// Inspection mode's attempts as a state machine: the keys and the clock
// come in, and what the page should do comes out (Effects), in the style of
// SolveTimer and TimedFlow. All times are milliseconds on a monotonic clock
// supplied by the caller.
//
// Plan: Space in Ready starts inspection (the cube shown, rotations only),
// Space again or the time limit hides it (Executing: any turn, unseen), and
// Space then asks the page to check and record the plan (a Space within
// kHideGraceMs of the limit hiding the cube was meant to hide it, and does
// nothing). Edges: Space shows the cube (Counting); the counts are typed
// for the axes asked, F/B first, each ended by Enter. Either then shows the
// result until Space gives the next scramble. Esc puts the scramble back
// during an attempt, or ends a retry.
class InspectionFlow {
public:
  enum class Phase { Ready, Inspecting, Executing, Counting, Result };

  struct Config {
    bool plan = true;
    // Plan: inspection ends after this many seconds (0: no limit), with the
    // WCA calls at 8 and 12 seconds when `wca`.
    int limitSeconds = 15;
    bool wca = true;
    // Edges: whether F/B ([0]) and R/L ([1]) are asked for.
    std::array<bool, 2> axes{true, true};
  };

  // A key as the page sees it. `turn`: it is a turning key too (a digit or
  // Enter may be one), used when nothing else takes it. `modifiers`: Ctrl,
  // Alt or Meta is held.
  enum class Key { Space, Escape, Tab, Digit, Backspace, Enter, Other };
  struct KeyInput {
    Key key = Key::Other;
    int digit = 0; // for Key::Digit
    bool turn = false;
    bool press = true;
    bool autoRepeat = false;
    bool modifiers = false;
  };

  // What a turning key does.
  enum class Turn {
    None,    // Ready: nothing
    Inspect, // the cube is shown: rotations only (the cube refuses others)
    Hidden,  // Executing: any turn, not shown
    Look,    // Result: rotations only, to look at the cube
  };

  // How the inspection countdown is shown.
  enum class Alert {
    None,
    Eight,  // WCA: "8 seconds!"
    Twelve, // WCA: "12 seconds!"
    Ending, // a limit without the calls, 3 seconds or less left
  };
  struct Countdown {
    std::int64_t shown = 0; // the seconds left (with a limit) or used
    Alert alert = Alert::None;
  };

  // What the page does after an event, in this order.
  struct Effects {
    bool used = false;            // the key was for the page
    bool waitScramble = false;    // say to wait for the scramble
    bool startInspection = false; // Plan: show the scramble, rotations only
    bool startCounting = false;   // Edges: show the scramble in its hold
    bool startExecution = false;  // Plan: hide the cube, the moves count
    bool finishPlan = false;      // Plan: check and record (then planDone())
    bool entryChanged = false;    // Edges: the count typed or its axis changed
    bool needEntry = false;       // Edges: say to type a count first
    bool finishEdges = false;     // Edges: record (then edgesDone())
    bool rings = false;           // Edges: the ringed axis changed
    bool cancel = false;          // back to Ready with the same scramble
    bool next = false;            // back to Ready with the next scramble
    Turn turn = Turn::None;
  };

  // A Space this soon after the time limit hid the cube was meant to hide
  // it.
  static constexpr std::int64_t kHideGraceMs = 600;

  // Taken by the next attempt (not during one).
  void setConfig(const Config &c) { config_ = c; }
  const Config &config() const { return config_; }

  Phase phase() const { return phase_; }
  // Inspecting, executing or counting.
  bool isBusy() const;
  // The attempt being planned again, if it is a retry.
  std::optional<std::size_t> retry() const { return retry_; }

  // A key down or up. `scrambleReady`: a new scramble is waiting (a retry
  // has its own).
  Effects key(const KeyInput &k, std::int64_t now, bool scrambleReady);
  // The clock: the time limit may hide the cube (startExecution).
  Effects tick(std::int64_t now);
  // Inspecting: the countdown at `now`.
  Countdown countdown(std::int64_t now) const;
  // Since inspection or counting began.
  std::int64_t elapsed(std::int64_t now) const { return now - started_; }
  // Plan: the inspection time used, once the cube is hidden.
  std::int64_t usedMs() const { return usedMs_; }

  // The page recorded the plan (finishPlan): on to the result. A plan not
  // recorded (nothing done) leaves the cube hidden.
  void planDone();
  // The page recorded the counts (finishEdges), the right ones being
  // `correct` (-1 for an axis not asked): on to the result, with the bad
  // edges of the first axis asked (or the first wrong one) ringed.
  void edgesDone(const std::array<int, 2> &correct);

  // Edges: the count being typed ("" for none yet), its axis (0 F/B, 1 R/L),
  // the counts entered (-1 for none) and the axis ringed in the result (-1
  // for none).
  const std::string &entry() const { return entry_; }
  int axis() const { return axis_; }
  const std::array<int, 2> &answers() const { return answers_; }
  int ringAxis() const { return ringAxis_; }

  // Plans attempt `index` again, not recorded: false (nothing changes)
  // during an attempt or when it isn't a Plan session.
  bool startRetry(std::size_t index);
  // Skip: the next scramble (`advanceFeed`: the one waiting is skipped
  // too); nothing during an attempt.
  struct Skip {
    bool done = false;
    bool advanceFeed = false;
  };
  Skip skip();
  // Back to Ready for the next scramble, ending a retry (Space in the
  // result, a new session).
  void next();
  // The page or window was left: an attempt is cancelled. True if one was
  // (the page puts the scramble back).
  bool leave();

private:
  Config config_;
  Phase phase_ = Phase::Ready;
  std::int64_t started_ = 0; // when inspection or counting began
  std::int64_t usedMs_ = 0;  // the inspection time used
  // When the time limit hid the cube; -1 when Space hid it.
  std::int64_t autoHiddenAt_ = -1;
  std::optional<std::size_t> retry_;
  std::string entry_;
  std::array<int, 2> answers_{-1, -1};
  int axis_ = 0;
  int ringAxis_ = 0;

  Effects space(std::int64_t now, bool scrambleReady);
  void hide(std::int64_t now);
  void typeDigit(int digit);
  Effects enterCount();
  void cancel();
};

// The axis whose bad edges are ringed after counting: the first axis asked,
// or the first one answered wrong (-1 when none was asked). `correct` is -1
// for an axis not asked.
int ringedAxis(const std::array<int, 2> &answers,
               const std::array<int, 2> &correct);

// The hold a step is done in, of those it is: the one the cube is held in
// now if it is one of them, else one with the same top colour, else the
// first; nullopt when the step isn't done.
std::optional<Hold> doneHold(const FaceletCube &cube, SolveStep step);

} // namespace vcube
