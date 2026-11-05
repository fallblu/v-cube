#pragma once

#include "app/case_page.hpp"
#include "app/cube_view.hpp"
#include "train/alg_follow.hpp"

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
  enum class Phase { NoCase, Solving, Done };

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
  CubeView *cubeView_;
  QLabel *statusLabel_;
  QLabel *reveal_;
  QWidget *gradeRow_;
  QLabel *gradeLabel_;
  std::array<QPushButton *, 4> gradeButtons_{};

  std::vector<std::string> pool() const { return tickedEligible(); }
  void nextCase();
  void begin(const std::string &caseId, bool isNew, bool practice = false);
  // A learned case that isn't due yet, which "Learn this case now" only
  // practises.
  bool notDue(const std::string &caseId);
  void resetCube();
  void turn(Move m);
  void finish();
  void regrade(Grade grade);
  void showReveal();
  void showGrade();
  void setPhase(Phase p);
  void showSolvingStatus();
  // The algs shown for a case, main alg first, as the follower checks them.
  std::vector<Alg> shownAlgs(const std::string &caseId);
  void learnNext();
};

} // namespace vcube
