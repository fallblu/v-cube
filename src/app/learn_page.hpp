#pragma once

#include "app/case_page.hpp"
#include "app/cube_view.hpp"
#include "train/alg_follow.hpp"
#include "train/solve_review.hpp"

#include <array>

class QPushButton;
class QSpinBox;

namespace vcube {

// Untimed practice on the virtual cube with spaced repetition: each case
// is set up on the cube and solved from memory. The algs stay hidden until
// Tab; solving the case with one of its algs and without a hint grades it
// Good, while a hint, giving up or leaving the algs (see AlgFollower)
// grades it Again. Due reviews come first, then cards in their learning
// steps, then new cases from the learn queue.
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
  // Enter in the "Start learning" count starts learning and Esc leaves it,
  // so that the keys go back to the cube either way.
  bool eventFilter(QObject *watched, QEvent *event) override;
  QStringList columns() const override;
  std::vector<CaseStatusFilter> statusFilters() override;
  void fillRow(QTreeWidgetItem *item, const std::string &caseId) override;
  void updateSummary() override;
  void startSet() override;
  void setReloaded() override;
  void startCase(const std::string &caseId) override;
  QString startCaseLabel(const std::string &caseId) override;
  void extendCaseMenu(QMenu &menu, const std::string &caseId) override;
  void ticksChanged() override;
  void neutralityChanged() override;

private:
  // Demo: guided learning's walkthrough of a new case, stepped with the
  // arrow keys before the case is tried.
  // Context and ContextDone: in-context practice of a set of F2L pairs,
  // with several slots open at once (not graded).
  enum class Phase { NoCase, Demo, Solving, Done, Context, ContextDone };

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
  };

  Phase phase_ = Phase::NoCase;
  std::optional<Current> current_;
  FaceletCube cube_;
  std::string last_;
  bool hinted_ = false;
  bool nextHinted_ = false; // the next-move hint was used: graded Hard
  bool gaveUp_ = false;
  AlgFollower follower_; // whether the moves keep to the case's algs
  bool leftAlgs_ = false;
  SrsCard cardBefore_;           // to change the grade afterwards
  std::int64_t attemptTime_ = 0; // the attempt the grade was recorded with
  bool gradeReplaced_ = false;   // the case list changed the card since
  Grade grade_ = Grade::Good;
  Grade autoGrade_ = Grade::Good; // the grade the attempt earned

  QSpinBox *learnCount_;
  QWidget *optionsRow_;
  QComboBox *viewBox_;
  CubeView *cubeView_;
  QLabel *statusLabel_;
  QLabel *guideLabel_;
  QLabel *reveal_;
  QWidget *familyRow_; // after a case learned by family: all its members
  QWidget *gradeRow_;
  QLabel *gradeLabel_;
  std::array<QPushButton *, 4> gradeButtons_{};

  std::optional<Guide> guide_;
  std::optional<Context> context_;
  bool pairSet_ = false; // the set's cases are F2L pairs
  QSpinBox *slotsSpin_;
  QPushButton *contextButton_;

  // The cases to learn (one per family when learning by family).
  std::vector<std::string> pool() const;
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
  void nextCase();
  void begin(const std::string &caseId, bool isNew, bool practice = false);
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
  // The algs shown for a case, main alg first, as the follower checks them.
  std::vector<Alg> shownAlgs(const std::string &caseId);
  void learnNext();
  // Shows the cube the way it was last shown for this set; a set seen for
  // the first time keeps the view in use.
  void applySetView();
};

} // namespace vcube
