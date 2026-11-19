#pragma once

#include "cube/move.hpp"
#include "train/keys.hpp"
#include "train/practice.hpp"
#include "train/srs.hpp"

#include <optional>

namespace vcube {

// What the keys of a practice mode's tabs that aren't timed do (EO Cross,
// F2L and TSLE; the Timed tab's are DrillFlow's), as a pure function of
// where the tab is: the page passes a key and the state in and does what
// key() returns. The page's key hints are worked out from the same state
// (untimedKeys()), so that the keys and their hints can't drift apart.
//
//   Idle     Space or Enter looks again (taking a card early)
//   Ready    Inspection: Space shows a state and starts the clock
//   Choose   1-9 tick or untick, Enter keeps them, Esc (changing them after
//            a try) leaves them as they were; a turning key says to choose
//            first
//   Demo     Enter shows the case's solution, Space skips it, Esc starts
//            again; a turn that does the case ends it
//   Solving  Tab a hint, the give-up key (Shift+Space, or Backspace in
//            Settings) gives up, Esc starts again; on a hidden cube Space
//            shows it and the try is judged
//   Done     Space next, Enter plays the best, Tab the details, 1-4 change
//            a card's grade, 0 changes its solutions, Delete drops an
//            Untimed or Inspection try just kept, Esc the same state again;
//            the mode's own keys, then the turning keys turn freely
//
// On the Learn tab, Ctrl+Z takes back the last grade in every phase.
class PracticeFlow {
public:
  using Phase = PracticePhase;
  using Tab = PracticeTab;

  // Where the tab is.
  struct State {
    Tab tab = Tab::Learn;
    Phase phase = Phase::Idle;
    bool choosingAgain = false; // Choose: of a card already tried
    bool hidden = false;        // the cube is out of sight
    bool done = false;          // the cube shows the item done
    bool inspecting = false;    // the Inspection clock runs
    bool hints = false;         // Tab gives a hint while solving
    bool waiting = false;       // Done, the verdict still being worked out
    bool best = false;          // there is a best solution to play
    bool detailOpen = false;
    bool graded = false;   // the try gave its card a grade (1-4 change it)
    bool chooser = false;  // the card's solutions can be changed (0)
    bool recorded = false; // Done: an Untimed or Inspection try was kept
    bool shiftSpaceGivesUp = true; // else Backspace does
  };

  enum class Action {
    None,
    Undo,          // take back the last grade (Learn)
    NextEarly,     // Idle: look again, taking a card early
    Start,         // Ready: the first state of the sitting
    ConfirmChoice, // keep the solutions ticked
    CancelChoice,  // leave them as they were
    ChoiceKey,     // tick or untick solution `digit`
    ChooseFirst,   // a turning key while choosing: say to choose first
    Next,          // on to the next state
    StillWorking,  // Space while the verdict is worked out: say so
    Introduced,    // a new case skipped or done: shown
    Reveal,        // the hidden cube shown again: judge the try (`gaveUp`)
    GiveUp,        // give the try up (the cube shown again)
    GiveUpKeyHint, // Backspace while Shift+Space gives up: say so
    Hint,          // a hint for the solve under way
    ToggleDetail,  // the details on or off
    DeleteTry,     // drop the Untimed or Inspection try just kept
    Retry,         // the same state again, not recorded
    ResetCube,     // the cube back to the start of the state
    PlayDemo,      // show the new case's solution
    PlayBest,      // play the best solution from the start
    Regrade,       // the card's grade changed to `grade`
    ChooseAgain,   // change the card's solutions
    ModeKey,       // the mode's own key, if it is one; else turn by `move`
    Turn,          // turn the cube by `move`
  };
  struct Step {
    bool handled = false;
    Action action = Action::None;
    std::optional<Move> move;
    int digit = 0;
    Grade grade = Grade::Again;
    bool gaveUp = false; // Reveal: the cube didn't show the item done
  };

  // A key pressed (releases do nothing on these tabs). `k.move` is the
  // turn the key makes, which the page fills in.
  static Step key(const TrainKeyEvent &k, const State &s);
};

} // namespace vcube
