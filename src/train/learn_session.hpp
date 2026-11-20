#pragma once

#include "cube/move.hpp"
#include "train/keys.hpp"
#include "train/srs.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace vcube {

// What Learn mode is doing, and what each key does then, as a pure state
// machine: the page passes keys in and does what key() returns, and tells
// the session when a phase begins.
//
//   NoCase      nothing due; Space or Enter looks again (and takes a
//               learning case early if nothing else is)
//   Demo        guided learning's walkthrough of a new case: → and ← step
//               along the main alg, Home or Esc back to the start, Space
//               tries it with each next move shown
//   Solving     the case is solved from memory: Tab shows a hint, Shift+
//               Space gives up, Esc resets the case; the turning keys turn
//   Done        the case is over: Space next, 1-4 change the grade, Esc
//               resets the cube to practise (free turning)
//   Recognise   a recognition review: Space once the alg is recalled
//               (Shift+Space if not), not in its first kGuardMs;
//               rotations and U and D turns look
//   Recalled    1-4 grade it, Space/Enter the grade shown
//   Quiz        "which case is this?": 1-n answer
//   QuizDone    Space/Enter another one
//   Context     in-context practice of F2L pairs (not graded): Tab the
//               set's cheapest order, Shift+Space gives up, Esc starts
//               again, Backspace back to learning
//   ContextDone the pairs are solved: Space another one
//
// Ctrl+Z takes back the last grades and F9 shows or hides the case list
// in every phase.
class LearnSession {
public:
  enum class Phase {
    NoCase,
    Demo,
    Solving,
    Done,
    Context,
    ContextDone,
    Recognise,
    Recalled,
    Quiz,
    QuizDone,
  };

  // Of the digits Learn reads 1-9.
  using Key = TrainKey;
  using KeyEvent = TrainKeyEvent;

  enum class Action {
    None,
    Undo,             // take back the last grades
    NextEarly,        // look again, taking a learning case early
    NextCase,         // on to the next case
    Turn,             // turn the cube by `move`
    Look,             // turn the cube as a whole, or U or D, to look at it
    ResetCube,        // the cube back as the case came up
    DemoStep,         // the walkthrough one move by `step` (+1 or -1)
    DemoRestart,      // the walkthrough back to its start
    StartFollowing,   // try the case, each next move shown
    Hint,             // Tab while solving: a hint
    GiveUp,           // Shift+Space while solving
    ResetCase,        // Esc while solving: the case from the start again
    Regrade,          // the case's grade changed to `grade`
    Recalled,         // recalled (`knew`) or not
    GradeRecognition, // the recognition review graded `grade`, or as shown
    Answer,           // the quiz answered with choice `choice`
    NextQuiz,         // another quiz case
    ContextHint,      // the set's cheapest order
    ContextGiveUp,
    ContextReset, // the same pairs from the start again
    ContextLeave, // back to learning
    ContextNext,  // other pairs
  };
  struct Step {
    bool handled = false;
    Action action = Action::None;
    std::optional<Move> move;
    int step = 0;
    std::optional<Grade> grade; // GradeRecognition: nullopt = as shown
    bool knew = false;
    std::size_t choice = 0;
  };

  // Space is ignored this long after a recognition review comes up, so
  // that the second press of a double tap meant for the case before
  // doesn't answer it (as Drill's DrillFlow::kGuardMs).
  static constexpr std::int64_t kGuardMs = 300;

  Phase phase() const { return phase_; }
  void setPhase(Phase p) { phase_ = p; }
  // A recognition review came up at `now` (monotonic ms): Recognise,
  // with Space held back for kGuardMs.
  void startRecognising(std::int64_t now) {
    phase_ = Phase::Recognise;
    guardUntil_ = now + kGuardMs;
  }
  // The quiz's choices, for its keys.
  void setQuizChoices(std::size_t n) { quizChoices_ = n; }

  // A key at `now` (monotonic ms).
  Step key(const KeyEvent &k, std::int64_t now = 0) const;

  // The session's "which case?" score.
  struct QuizScore {
    int right = 0;
    int asked = 0;
    std::int64_t totalMs = 0;
  };
  void answered(bool right, std::int64_t ms);
  const QuizScore &quizScore() const { return quiz_; }

private:
  Phase phase_ = Phase::NoCase;
  std::size_t quizChoices_ = 0;
  std::int64_t guardUntil_ = 0;
  QuizScore quiz_;
};

// What a graded attempt, or a change from the case list, changed, so that
// Ctrl+Z can take it back.
struct LearnUndo {
  std::string setId;
  std::string caseId; // the case that came up (none for the case list)
  bool isNew = false;
  bool recognition = false; // a recognition review
  // Each card as it was before, and the Learn attempt recorded for it
  // (its timestamp, or 0). A change from the case list keeps the card's
  // Learn attempts as they were instead, as resetting drops them.
  struct Card {
    std::string cardId;
    SrsCard before;
    std::int64_t attempt = 0;
    std::optional<std::vector<Attempt>> learnAttempts;
  };
  std::vector<Card> cards;
  bool fromList = false; // the case list's right-click menu changed them
};

// The last grades, latest last; the oldest are dropped past kDepth.
class LearnUndoStack {
public:
  static constexpr std::size_t kDepth = 10;

  // A new entry; `keepOpen`: the attempt's later grades are added to it.
  void push(LearnUndo entry, bool keepOpen = true);
  // The entry of the attempt being graded, if still open.
  LearnUndo *open();
  // Closes the open entry: a new attempt began.
  void close() { open_ = false; }
  std::optional<LearnUndo> pop();
  bool empty() const { return entries_.empty(); }
  std::size_t size() const { return entries_.size(); }

private:
  std::deque<LearnUndo> entries_;
  bool open_ = false;
};

// Puts the entry's cards back as they were and drops the Learn attempts it
// recorded, latest first (so that a card graded twice ends up as it was at
// the start); a card that kept its Learn attempts gets them back.
void takeBack(SetProgress &progress, const LearnUndo &undo);

// The case list is about to change `cardIds`: an entry that takes the
// change back (fromList), each card with its Learn attempts.
LearnUndo listChange(const SetProgress &progress, const std::string &setId,
                     const std::vector<std::string> &cardIds);
// Leaves out of the entry the cards the change left as they were; false
// if none is left.
bool keepChanged(LearnUndo &undo, const SetProgress &progress);

} // namespace vcube
