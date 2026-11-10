#pragma once

#include "app/case_page.hpp"
#include "app/cube_view.hpp"
#include "train/alg_follow.hpp"
#include "train/solve_review.hpp"

#include <QTimer>

#include <array>
#include <deque>

class QPushButton;
class QSpinBox;
class QSplitter;

namespace vcube {

// Untimed practice on the virtual cube with spaced repetition: each case
// is set up on the cube and solved from memory. The algs stay hidden until
// Tab; solving the case with one of its algs and without a hint grades it
// Good, while a hint, giving up or leaving the algs (see AlgFollower)
// grades it Again. Cards in their learning steps come first, then reviews
// due today (in a random order), then new cases from the learn queue, in
// the order chosen for the set, while few enough cards are in their
// learning steps. A guided set's walkthrough of a new case isn't graded:
// the case comes back at its first learning step to be done from memory.
//
// Each attempt records the recognition time (from the case being set up
// to the first turn of a layer off the U-D axis) and the execution time
// after it. "Recognise" reviews only show the case: Space once its alg is
// recalled, then the algs are shown and the review is graded by hand.
// "Which case?" is an ungraded quiz: name the case among others of its
// group. Reviews can also be done across every set at once ("All sets" in
// the set drop-down), interleaved. Ctrl+Z takes back the last grades.
//
// In a reduction set (see reduction.hpp) a case's card asks for its next
// step only: any step that leaves fewer steps to go is Good. The cube then
// shows the case the step stopped at, which is carried on from, and so on
// to the end of the set's step; each case passed on the way is checked
// against its own card. A set with mirrors has each case set up in a
// random one of its worlds.
class LearnPage : public CasePage {
  Q_OBJECT
public:
  LearnPage(AppSettings &settings, AlgLibrary &library,
            QWidget *parent = nullptr);

  bool handleKey(QKeyEvent *e, bool press) override;
  void applySettings() override;
  void activated() override;
  void deactivated() override;
  void setFocusMode(bool on) override;

protected:
  // Enter in the "Start learning" count starts learning when a number was
  // typed, and any other key leaves the field, so that the keys go back to
  // the cube either way.
  bool eventFilter(QObject *watched, QEvent *event) override;
  QStringList columns() const override;
  QStringList defaultHiddenColumns() const override {
    return {tr("Lapses"), tr("Ease")};
  }
  std::vector<CaseStatusFilter> statusFilters() override;
  void fillRow(QTreeWidgetItem *item, const std::string &caseId) override;
  void updateSummary() override;
  void startSet() override;
  void setReloaded() override;
  void startCase(const std::string &caseId) override;
  QString startCaseLabel(const std::string &caseId) override;
  void extendCaseMenu(QMenu &menu,
                      const std::vector<std::string> &caseIds) override;
  void ticksChanged() override;
  void neutralityChanged() override;
  bool offersAcrossSets() const override { return true; }
  QString setItemText(const AlgSet &set) override;
  QString acrossItemText() override;

private:
  // Demo: guided learning's walkthrough of a new case, stepped with the
  // arrow keys before the case is tried.
  // Context and ContextDone: in-context practice of a set of F2L pairs,
  // with several slots open at once (not graded).
  // Recognise and Recalled: a recognition review, before and after Space.
  // Quiz and QuizDone: "which case is this?".
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
  // How reviews are done (the drop-down above the cube).
  enum class Practice { Solve, Recognise, Quiz };

  struct Context {
    CubieCube start; // in the solver's frame
    Hold hold = kWcaHold;
    Alg moves; // done since the start
    bool gaveUp = false;
  };

  // Guided learning: the main alg as done from the case as set up (its U
  // turns written in), and how far the cube has got along it.
  struct Guide {
    Alg moves;
    QStringList tokens;              // each move as text
    std::vector<FaceletCube> states; // after each prefix, orientation removed
    std::vector<FaceletCube> held;   // the same as the cube is held
    std::size_t at = 0;  // Demo: moves shown; else: the prefix reached last
    bool matched = true; // the cube is at states[at]
    bool follow = false; // every next move is shown (a new case's first try)
    bool next = false;   // the next move is shown (a learning-step hint)
  };

  struct Current {
    std::string caseId;
    CubieCube start;
    Hold hold = kWcaHold; // the cube is drawn in this hold's colours
    bool isNew = false;
    bool practice = false; // a learned case that isn't due: not graded
    Mirror world;          // the mirror image the case is set up in
  };

  // One case of the attempt: the case that came up, then in a reduction
  // set each case a step stopped at.
  struct Link {
    CaseView view;
    FaceletCube start;         // the cube when the case came up
    CubieCube state;           // the same, orientation removed
    std::vector<ViewAlg> algs; // its algs as done from there, main first
    // Once it is over:
    bool over = false;
    bool solved = false; // it finished the set's step
    std::string reached; // or the case it stopped at
    bool closer = true;  // fewer steps were left after it than before
    bool setStep = true; // the moves kept to one of the case's algs
    bool gaveUp = false;
    Grade grade = Grade::Good;
    QString note; // what became of its card
    Alg moves;
  };

  // What a graded attempt changed, so that Ctrl+Z can take it back.
  struct Undo {
    std::string setId;
    std::string caseId; // the case that came up
    bool isNew = false;
    bool recognition = false; // a recognition review
    // Each card as it was before, and the Learn attempt recorded for it
    // (its timestamp, or 0).
    struct Card {
      std::string cardId;
      SrsCard before;
      std::int64_t attempt = 0;
    };
    std::vector<Card> cards;
  };
  static constexpr std::size_t kUndoDepth = 10;

  Phase phase_ = Phase::NoCase;
  std::optional<Current> current_;
  std::vector<Link> links_; // the last one is being solved
  QString linkNote_;        // what the last step came to
  bool viewMoved_ = false;  // the cube is seen from another corner since
  QString why_;             // why the first case was graded as it was
  // The cards graded in this attempt: a case passed twice counts once.
  std::vector<std::string> gradedCards_;
  FaceletCube cube_;
  std::string last_;
  bool hinted_ = false;
  bool nextHinted_ = false; // the next-move hint was used: graded Hard
  bool gaveUp_ = false;
  AlgFollower follower_; // whether the moves keep to the case's algs
  bool leftAlgs_ = false;
  SrsCard cardBefore_;           // to change the grade afterwards
  std::int64_t attemptTime_ = 0; // the attempt the grade was recorded with
  std::int64_t gradeTime_ = 0;   // when the first case was graded
  std::string gradedCard_;       // the card the first case's grade went to
  bool gradeReplaced_ = false;   // the case list changed the card since
  bool walkedThrough_ = false;   // the first case was a walkthrough
  bool graded_ = false;          // the first case's card was changed
  Grade grade_ = Grade::Good;
  Grade autoGrade_ = Grade::Good; // the grade the attempt earned
  std::deque<Undo> undo_;
  bool undoOpen_ = false; // undo_.back() is this attempt's

  // Recognition timing (monotonic ms): when the case came up to be solved
  // or recognised, and how long until its first turn off U and D (0: not
  // yet). Not measured once the algs were shown first.
  std::int64_t shownAtMs_ = 0;
  std::int64_t recognitionMs_ = 0;
  bool recognitionSpoiled_ = false;

  // "Which case?": the choices, the right one, and the session's score.
  std::vector<std::string> quizChoices_;
  std::size_t quizAnswer_ = 0;
  int quizRight_ = 0, quizAsked_ = 0;
  std::int64_t quizTotalMs_ = 0;

  QSpinBox *learnCount_;
  bool countTyped_ = false; // a number was typed into the count
  QPushButton *learnButton_;
  QComboBox *orderBox_;
  QLabel *queueLabel_;
  QPushButton *learnNextButton_;
  QPushButton *unqueueButton_;
  QTimer queueMessageTimer_;
  QWidget *optionsRow_;
  QComboBox *viewBox_;
  QComboBox *practiceBox_;
  QPushButton *listButton_;
  QLabel *headerLabel_;
  CubeView *cubeView_;
  QLabel *statusLabel_;
  QLabel *guideLabel_;
  QLabel *reveal_;
  QWidget *familyRow_; // after a case learned by family: all its members
  QWidget *quizRow_;   // the quiz's answers
  QWidget *gradeRow_;
  QLabel *gradeBadge_; // the outcome, coloured
  QLabel *gradeLabel_;
  std::array<QPushButton *, 4> gradeButtons_{};
  QSplitter *split_;
  bool listHidden_ = false;
  bool focusMode_ = false;
  QTimer idleTimer_; // with nothing due: looks again when a card falls due

  std::optional<Guide> guide_;
  std::optional<Context> context_;
  bool pairSet_ = false; // the set's cases are F2L pairs
  QSpinBox *slotsSpin_;
  QPushButton *contextButton_;

  // The cases to learn (one per family when learning by family), in the
  // set's order for new cases.
  std::vector<std::string> pool() const;
  NewOrder newOrder() const;
  // Turns of the main alg, for "shortest alg first".
  int algMoves(const std::string &caseId) const;
  // A random ticked case of a family to set up for its card.
  std::string pickMember(const std::string &card);
  // Updates the rows of every case sharing the case's card.
  void updateCardRows(const std::string &caseId);
  // The set walks through new cases and gives next-move hints.
  bool guided();
  // The case was never graded: it is seen for the first time.
  bool firstSight(const std::string &caseId);
  // The case's card is in its learning or relearning steps.
  bool inLearningSteps(const std::string &caseId);
  void buildGuide();
  // Moves the walkthrough `delta` moves forward or back.
  void stepDemo(int delta);
  // From the walkthrough to trying the case with each next move shown.
  void startFollowing();
  // Where the cube is along the guide after a turn.
  void trackGuide();
  void showGuide();
  // In-context practice: a new state with the chosen number of open slots,
  // set up from the cases learned so far where it can be.
  void startContext();
  void resetContext();
  void finishContext(bool gaveUp);
  void showContextStatus();
  void showContextResult();
  void leaveContext();
  // The set is solved a step at a time.
  bool chained();
  // The case being solved: the one that came up, or the one the last step
  // stopped at.
  std::string linkCase() const;
  // The pieces the step leaves free in the world the case is set up in.
  PieceMask linkFree();
  // The cube shows a case: it is solved from here.
  void startLink(const CaseView &view);
  // The case being solved is over: the step was finished, stopped at
  // `next`, or was given up. Grades its card.
  void endLink(bool solved, const std::optional<CaseView> &next);
  // The link's algs again, e.g. after they were edited in the Library.
  void refreshLink();
  // Each case of a reduction set's attempt, with its step and how it went.
  QString chainHtml();
  // The next case: `early` takes a learning card before it is due when
  // nothing else is.
  void nextCase(bool early = false);
  // Across sets: the next due case of any set, switching the list to its
  // set; false when nothing is due anywhere.
  bool nextAcrossSets(bool early);
  // Nothing to do: says why, and when to look again.
  void showIdle(const LearnPick &pick);
  void begin(const std::string &caseId, bool isNew, bool practice = false);
  // Sets the case up on the cube in a hold, world and AUF picked for it.
  CubieCube setUp(const std::string &caseId, Mirror world, bool scramble);
  // A learned case that isn't due yet, which "Learn this case now" only
  // practises.
  bool notDue(const std::string &caseId);
  void resetCube();
  void turn(Move m);
  void finish();
  // Turns the cube back to the way the case was held, e.g. after an alg
  // that tilts it and solves the case before its last rotation.
  void turnUpright();
  void regrade(Grade grade);
  void showReveal();
  // After a case learned by family: every case of the family, with its
  // main alg.
  void showFamily();
  void showGrade();
  void setPhase(Phase p);
  void showSolvingStatus();
  void learnNext();
  // Shows a message under the learn queue for a few seconds.
  void showQueueMessage(const QString &text);
  void updateQueue();
  // Introduces the next queued case now, whatever the limits.
  void learnQueueHead();
  void unqueueHead();
  // The queued cases in the order they will be introduced.
  std::vector<std::string> queueOrder();
  // Shows the cube the way it was last shown for this set; a set seen for
  // the first time keeps the view in use.
  void applySetView();
  // What starting a set or showing it across sets needs.
  void adoptSet();
  Practice practice() const;
  // A recognition review of a learned case.
  void beginRecognition(const std::string &caseId);
  void recalled(bool knew);
  void gradeRecognition(Grade grade);
  // "Which case?".
  void nextQuiz();
  void answerQuiz(std::size_t choice);
  // Records the first case's grade, keeping what Ctrl+Z needs.
  void pushUndo(Undo entry);
  void undoLast();
  // The case list on or off (F9), apart from focus mode.
  void toggleList();
  void updateHeader();
  // The interval a grade gives, for the buttons: "1 min", "4 d".
  static QString intervalText(std::int64_t seconds);
  // "tomorrow", "Wed 12 Nov" or "in 4 min".
  QString whenText(std::int64_t due);
};

} // namespace vcube
