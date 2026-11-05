#pragma once

#include "app/case_page.hpp"
#include "app/cube_view.hpp"

#include <array>

class QPushButton;
class QSpinBox;

namespace vcube {

// Untimed practice on the virtual cube with spaced repetition: each case
// is set up on the cube and solved from memory. The algs stay hidden until
// Tab; solving without a hint grades the case Good, a hint or giving up
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
  QStringList columns() const override;
  void fillRow(QTreeWidgetItem *item, const std::string &caseId) override;
  void updateSummary() override;
  void startSet() override;
  void setReloaded() override;
  void startCase(const std::string &caseId) override;
  QString startCaseLabel() const override;
  void extendCaseMenu(QMenu &menu, const std::string &caseId) override;
  void ticksChanged() override;
  void orientationChanged() override;

private:
  enum class Phase { NoCase, Solving, Done };

  struct Current {
    std::string caseId;
    CubieCube start;
    ColourMap colours = kIdentityColourMap;
    bool isNew = false;
  };

  Phase phase_ = Phase::NoCase;
  std::optional<Current> current_;
  FaceletCube cube_;
  std::string last_;
  bool hinted_ = false;
  bool gaveUp_ = false;
  SrsCard cardBefore_; // to change the grade afterwards
  Grade grade_ = Grade::Good;

  QSpinBox *learnCount_;
  QWidget *optionsRow_;
  QLabel *hintLabel_;
  CubeView *cubeView_;
  QLabel *statusLabel_;
  QLabel *reveal_;
  QWidget *gradeRow_;
  QLabel *gradeLabel_;
  std::array<QPushButton *, 4> gradeButtons_{};

  std::vector<std::string> pool() const { return tickedEligible(); }
  void nextCase();
  void begin(const std::string &caseId, bool isNew);
  void resetCube();
  void turn(Move m);
  void finish();
  void regrade(Grade grade);
  void showReveal();
  void showGrade();
  void setPhase(Phase p);
  void learnNext();
};

} // namespace vcube
