#pragma once

#include "cube/cubie.hpp"
#include "cube/facelet.hpp"
#include "train/alg_follow.hpp"
#include "train/alg_guide.hpp"
#include "train/learn_session.hpp"
#include "train/reduction.hpp"
#include "train/srs.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace vcube {

// Why a case of a Learn attempt was graded as it was, for the words the
// page puts to it: every reason, e.g. both a hint and leaving the algs.
struct GradeWhy {
  enum class Miss { None, NotCloser, StartedOver, LeftAlgs };
  // What went right, when nothing went wrong.
  enum class Kept { FromMemory, WalkedThrough, SetStep, AnotherStep };
  bool gaveUp = false;
  Miss miss = Miss::None;
  bool hint = false;     // the algs were shown
  bool nextHint = false; // only the next move was
  Kept kept = Kept::FromMemory;
};

// One case of a Learn attempt: the case that came up, then in a reduction
// set (see reduction.hpp) each case a step stopped at.
struct LearnLink {
  CaseView view;
  FaceletCube start;         // the cube when the case came up
  CubieCube state;           // the same, orientation removed
  std::vector<ViewAlg> algs; // its algs as done from there, main first
  // Once it is over:
  bool over = false;
  bool solved = false; // it finished the set's step
  std::string reached; // or the case it stopped at
  bool closer = true;  // fewer steps were left after it than before
  int stepsLeft = 0;   // (a reduction set) after it
  bool setStep = true; // the moves kept to one of the case's algs
  bool gaveUp = false;
  Grade grade = Grade::Good;
  GradeWhy why;
  // What became of a later case's card.
  enum class Note { None, AlreadyGraded, Practice, NotStarted, NotDue, Graded };
  Note note = Note::None;
  std::int64_t notedAt = 0; // when it was graded (Note::Graded)
  Alg moves;
};

// The set and the cards a Learn attempt is graded against.
struct LearnGrading {
  SetProgress *progress = nullptr;
  LearnUndoStack *undo = nullptr;
  const SrsConfig *srs = nullptr;
  std::mt19937_64 *rng = nullptr;
  std::string setId;
  bool chained = false; // a reduction set: a step at a time
  std::int64_t dayEnd = 0;
  // The case whose card a case's grade goes to (its family's when
  // learning by family).
  std::function<std::string(const std::string &)> cardId;
  // Steps left from a case by the set's algs (reduction sets).
  std::function<int(const std::string &)> stepsFrom;
};

// One Learn attempt at a case, from the case coming up to its grade: the
// cases it passes (links), the hints used, whether the moves kept to the
// algs, and the grades it gives. Grading a case changes its card in the
// set's progress and records an attempt, keeping what Ctrl+Z needs.
//
// The first case is graded by how it went (see learnGrade()): Good when
// solved from memory with one of its algs, Hard after only the next-move
// hint, Again after a hint, giving up or leaving the algs. In a reduction
// set any step that leaves fewer steps to go counts. A guided
// walkthrough of a case never seen isn't graded: the card is introduced
// to come back at its first learning step. A later case of the attempt is
// graded when it is due, or Again whenever its step is missed; one passed
// twice counts once.
class LearnAttempt {
public:
  // A new attempt at `caseId`; `practice`: a learned case that isn't due,
  // which isn't graded.
  void begin(const std::string &caseId, bool isNew, bool practice);
  // The case is shown to be recognised from `shownAtMs` (monotonic); with
  // `spoiled` (a walkthrough first) recognition isn't measured.
  void startTiming(std::int64_t shownAtMs, bool spoiled = false);
  void clear();

  const std::string &caseId() const { return caseId_; }
  bool isNew() const { return isNew_; }
  bool practice() const { return practice_; }

  // --- the cases solved
  // The cube shows a case: it is solved from here.
  void startLink(const CaseView &view, const FaceletCube &start,
                 const CubieCube &state);
  // The algs of the case being solved (again, e.g. after an edit).
  void setLinkAlgs(std::vector<ViewAlg> algs);
  const std::vector<LearnLink> &links() const { return links_; }
  bool hasLinks() const { return !links_.empty(); }
  // The case being solved: the one that came up, or the one the last step
  // stopped at.
  std::string linkCase() const;
  const LearnLink *lastLink() const {
    return links_.empty() ? nullptr : &links_.back();
  }

  // Whether the moves keep to the case's algs.
  const AlgFollower &follower() const { return follower_; }
  // A turn of the solve, at `nowMs` (monotonic): the first turn off U
  // and D of the first case ends recognition.
  void turned(Move m, std::int64_t nowMs);
  // The case is started again from where it came up.
  void restartCase();

  // --- hints
  bool hinted() const { return hinted_; }
  bool nextHinted() const { return nextHinted_; }
  // The algs were shown (Tab); before any turn that spoils recognition.
  void hint();
  // Only the next move was shown, in the case's learning steps.
  void nextMoveHint();
  void giveUp() { gaveUp_ = true; }
  bool gaveUp() const { return gaveUp_; }

  // The case being solved is over: the step was finished (`solved`),
  // stopped at `next`, or was given up. Grades its card. `guided`: the
  // guide showed every next move. `now` is Unix seconds, `nowMs` the
  // monotonic clock. True if a card changed.
  bool endLink(bool solved, const std::optional<std::string> &next, bool guided,
               std::int64_t now, std::int64_t nowMs, const LearnGrading &g);

  // --- the first case's grade
  Grade grade() const { return grade_; }
  Grade autoGrade() const { return autoGrade_; } // as the attempt earned it
  const GradeWhy &why() const { return why_; }
  bool byHand() const { return byHand_; }
  bool graded() const { return graded_; }               // its card changed
  bool walkedThrough() const { return walkedThrough_; } // introduced only
  bool leftAlgs() const { return leftAlgs_; }
  const std::string &gradedCard() const { return gradedCard_; }
  const SrsCard &cardBefore() const { return cardBefore_; }
  std::int64_t gradeTime() const { return gradeTime_; }
  // The case list changed the card since: the grade can't be changed.
  void replaceGrade() { gradeReplaced_ = true; }
  bool gradeReplaced() const { return gradeReplaced_; }
  // The first case's grade changed by hand after it was over; false if it
  // can't be (practice, never graded, or changed from the case list).
  bool regrade(Grade grade, std::int64_t now, const LearnGrading &g);

  // --- recognition reviews
  // Recalled (`knew`) or not, at `nowMs` (monotonic): Good or Again until
  // graded by hand.
  void recalled(bool knew, std::int64_t nowMs, std::int64_t now,
                const LearnGrading &g);
  std::int64_t recognitionMs() const { return recognitionMs_; }
  // Grades the review; the case's card changes and an attempt is recorded.
  void gradeRecognition(Grade grade, std::int64_t now, const LearnGrading &g);

private:
  std::string caseId_;
  bool isNew_ = false;
  bool practice_ = false;
  std::vector<LearnLink> links_;
  AlgFollower follower_;
  bool hinted_ = false;
  bool nextHinted_ = false;
  bool gaveUp_ = false;
  // The cards graded in this attempt: a case passed twice counts once.
  std::vector<std::string> gradedCards_;

  Grade grade_ = Grade::Good;
  Grade autoGrade_ = Grade::Good;
  GradeWhy why_;
  bool byHand_ = false;
  bool leftAlgs_ = false;
  SrsCard cardBefore_;           // to change the grade afterwards
  std::int64_t attemptTime_ = 0; // the attempt the grade was recorded with
  std::int64_t gradeTime_ = 0;   // when the first case was graded
  std::string gradedCard_;       // the card the first case's grade went to
  bool gradeReplaced_ = false;
  bool walkedThrough_ = false;
  bool graded_ = false;

  // Recognition timing (monotonic ms): when the case came up, and how long
  // until its first turn off U and D (0: not yet). Not measured once the
  // algs were shown first.
  std::int64_t shownAtMs_ = 0;
  std::int64_t recognitionMs_ = 0;
  bool recognitionSpoiled_ = false;
};

} // namespace vcube
