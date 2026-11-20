#pragma once

#include "app/case_page.hpp"
#include "app/case_stage.hpp"
#include "app/context_practice.hpp"
#include "app/cube_view.hpp"
#include "app/page_parts.hpp"
#include "app/quiz_practice.hpp"
#include "train/alg_follow.hpp"
#include "train/alg_guide.hpp"
#include "train/learn_attempt.hpp"
#include "train/learn_session.hpp"
#include "train/solve_review.hpp"

#include <QStringList>
#include <QTimer>

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

class QAction;
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

  SettingsParts settingsUsed() const override {
    return SettingsPart::Cube | SettingsPart::Keys | SettingsPart::Neutrality |
           SettingsPart::Thumbnails | SettingsPart::Srs;
  }
  void applySettings() override;
  void activated() override;
  void deactivated() override;
  void windowDeactivated() override;
  void aboutToClose() override;

signals:
  // Done for now: drill the set (its weakest cases, when it was drilled).
  void drillSetRequested(const QString &setId, bool weakest);

protected:
  bool handleKey(QKeyEvent *e, bool press) override;
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
  QString startCaseLabel(const std::string &caseId) const override;
  void extendCaseMenu(QMenu &menu,
                      const std::vector<std::string> &caseIds) override;
  void ticksChanged() override;
  void neutralityChanged() override;
  bool offersAcrossSets() const override { return true; }
  QString setItemText(const AlgSet &set) const override;
  QString acrossItemText() const override;

private:
  // What Learn is doing (see LearnSession).
  using Phase = LearnSession::Phase;
  // How reviews are done (the drop-down above the cube).
  enum class Practice { Solve, Recognise, Quiz };
  // "solve", "recognise" or "quiz", as settings keep it.
  static QString practiceId(Practice practice);
  // nullopt for any other text.
  static std::optional<Practice> practiceFromId(const QString &id);

  struct Current {
    std::string caseId;
    CubieCube start;
    Hold hold = kWcaHold; // the cube is drawn in this hold's colours
    bool isNew = false;
    bool practice = false; // a learned case that isn't due: not graded
    Mirror world;          // the mirror image the case is set up in
    LearnAttempt::Kind kind() const {
      return isNew      ? LearnAttempt::Kind::New
             : practice ? LearnAttempt::Kind::Practice
                        : LearnAttempt::Kind::Known;
    }
  };

  LearnSession session_;
  // The case being learned, and the cases its steps pass.
  LearnAttempt attempt_;
  LearnUndoStack undo_;
  std::optional<Current> current_;
  QString linkNote_;       // what the last step came to
  bool viewMoved_ = false; // the cube is seen from another corner since
  FaceletCube cube_;
  std::string last_;

  // "Which case?": the question that is up.
  std::optional<LearnQuiz> quiz_;

  QSpinBox *learnCount_;
  bool countTyped_ = false; // a number was typed into the count
  QPushButton *learnButton_;
  QComboBox *orderBox_;
  QLabel *queueLabel_;
  QPushButton *learnNextButton_;
  QAction *unqueueAction_;
  QTimer queueMessageTimer_;
  // The cube and what is said about it; the page's own widgets on it.
  CaseStage *stage_;
  CubeView *cubeView_;
  QLabel *statusLabel_;
  QLabel *reveal_;
  QWidget *gradeRow_;
  QComboBox *viewBox_;
  SegmentedPicker *practiceBox_;
  QPushButton *listButton_;
  QSplitter *split_;
  QTimer idleTimer_; // with nothing due: looks again when a card falls due
  // The splitters moved since their sizes were last saved, and the pause
  // after which they are.
  QTimer layoutTimer_;
  bool splitMoved_ = false;
  bool rightSplitMoved_ = false;
  void saveSplitters();

  std::optional<AlgGuide> guide_;
  std::optional<ContextPractice> context_;
  bool pairSet_ = false; // the set's cases are F2L pairs
  QSpinBox *slotsSpin_;
  QPushButton *contextButton_;

  // The cases to learn (one per family when learning by family), in the
  // set's order for new cases.
  std::vector<std::string> pool() const;
  NewOrder newOrder() const;
  // The case's attempts: its own, and its family's Learn attempts when the
  // family shares a card.
  std::vector<Attempt> attemptsOf(const std::string &caseId) const;
  // Turns of the main alg, for "shortest alg first".
  int algMoves(const std::string &caseId) const;
  // A random ticked case of a family to set up for its card.
  std::string pickMember(const std::string &card);
  // Updates the rows of every case sharing the case's card.
  void updateCardRows(const std::string &caseId);
  // The set walks through new cases and gives next-move hints.
  bool guided() const;
  // The case was never graded: it is seen for the first time.
  bool firstSight(const std::string &caseId) const;
  // The case's card is in its learning or relearning steps.
  bool inLearningSteps(const std::string &caseId) const;
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
  void finishContext(ContextPractice::End end);
  void showContextStatus();
  void showContextResult();
  void leaveContext();
  Phase phase() const { return session_.phase(); }
  // What grading a case needs to know about the set and its cards.
  LearnGrading grading();
  LearnSession::KeyEvent sessionKey(const QKeyEvent *e, bool press) const;
  // Does what the session asked after a key.
  void perform(const LearnSession::Step &step);
  // Tab while solving: the next move in the learning steps, else the algs.
  void solvingHint();
  // Why the first case was graded as it was, in words.
  QString whyText() const;
  // The set is solved a step at a time.
  bool chained() const;
  // The case being solved: the one that came up, or the one the last step
  // stopped at.
  std::string linkCase() const;
  // The pieces the step leaves free in the world the case is set up in.
  PieceMask linkFree() const;
  // The cube shows a case: it is solved from here.
  void startLink(const CaseView &view);
  // Turns the 3D view to where `view`'s case is looked at from (see
  // startLink()); false if it was there already.
  bool lookAt(const CaseView &view);
  // The case being solved is over: the step was finished, stopped at
  // `next`, or was given up. Grades its card.
  void endLink(LearnAttempt::LinkEnd end, const std::optional<CaseView> &next);
  // The link's algs again, e.g. after they were edited in the Library.
  void refreshLink();
  // Each case of a reduction set's attempt, with its step and how it went.
  QString chainHtml() const;
  // The next case: `early` takes a learning card before it is due when
  // nothing else is.
  void nextCase(LearnAhead ahead = LearnAhead::WhenDue);
  // Across sets: the next due case of any set, switching the list to its
  // set; false when nothing is due anywhere.
  bool nextAcrossSets();
  // Begins the case picked, a member of its family when learning by family.
  void beginPick(const LearnPick &pick);
  // Nothing to do: says why, and when to look again.
  void showIdle(const LearnPick &pick);
  // A case is timed from the moment it is shown, so the first one waits
  // for Space: on opening the page or coming back to it, after a set or a
  // way of practising is picked with the mouse, and when a card falls due
  // while nothing was up. Until then `pending_` is what Space starts, and
  // nothing of the case is shown. Once started (`ready_`), the cases
  // follow one another as they are graded.
  bool ready_ = false;
  std::function<void()> pending_;
  void holdBack(std::function<void()> start);
  // Whether a case is waiting for Space (for tests).
public:
  bool waitingForSpace() const { return static_cast<bool>(pending_); }

private:
  void begin(const std::string &caseId, LearnAttempt::Kind kind);
  // Sets the case up on the cube in a hold, world and AUF picked for it.
  CubieCube setUp(const std::string &caseId, Mirror world, FreePieces pieces);
  // A learned case that isn't due yet, which "Learn this case now" only
  // practises.
  bool notDue(const std::string &caseId) const;
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
  // The reviews due in a set now or later today.
  int dueIn(const AlgSet &s) const;
  // The status line and, under it, the keys that do something now.
  void say(const QString &message, const QList<KeyHint> &keys = {},
           bool warn = false);
  void showSolvingStatus();
  void learnNext();
  // Shows a message under the learn queue for a few seconds.
  void showQueueMessage(const QString &text);
  void updateQueue();
  // Introduces the next queued case now, whatever the limits.
  void learnQueueHead();
  void unqueueHead();
  // The queued cases in the order they will be introduced.
  std::vector<std::string> queueOrder() const;
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
  void undoLast();
  // The case list on or off (F9), apart from focus mode.
  void toggleList();
  void sideHiddenChanged() override;
  void updateHeader();
};

} // namespace vcube
