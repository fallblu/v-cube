#pragma once

#include "app/case_page.hpp"
#include "app/cube_view.hpp"
#include "app/widgets.hpp"
#include "train/move_count.hpp"

#include <QFutureWatcher>
#include <QTimer>

class QPushButton;

namespace vcube {

// Timed practice of the ticked cases with the space-bar timer, on the
// virtual cube (the default) or a physical one, chosen per alg set. The
// two keep their drill statistics apart.
//
// On the virtual cube each case is set up on the cube with random AUFs
// and the step's free pieces scrambled. It can only be turned as a whole
// until the timer starts (hold Space and release); Space stops it, and the
// attempt is a DNF unless the cube then shows the step done (solved for
// PLL, the last layer oriented for OLL, and so on).
//
// On a physical cube a scramble sets the case up, and any key stops the
// timer; a wrong alg is marked by hand.
//
// After each case it shows the time and the algs with the AUFs the case
// needs. Drill attempts feed the case statistics and never change the
// spaced-repetition schedule.
class DrillPage : public CasePage {
  Q_OBJECT
public:
  DrillPage(AppSettings &settings, AlgLibrary &library,
            QWidget *parent = nullptr);

  bool handleKey(QKeyEvent *e, bool press) override;
  void applySettings() override;
  bool isBusy() const override;
  void activated() override;
  void deactivated() override;
  void windowDeactivated() override;
  void setFocusMode(bool on) override;

protected:
  QStringList columns() const override;
  std::vector<CaseStatusFilter> statusFilters() override;
  void fillRow(QTreeWidgetItem *item, const std::string &caseId) override;
  void updateSummary() override;
  void startSet() override;
  void setReloaded() override;
  void startCase(const std::string &caseId) override;
  QString startCaseLabel(const std::string &caseId) override;
  void ticksChanged() override;
  void neutralityChanged() override;

private:
  enum class Phase { NoCase, Waiting, Running, Revealed };
  enum class Pool { Learned, Ticked, Learning };

  struct Current {
    std::string caseId;
    CubieCube state; // the case as the solver sees it, held as `hold`
    Hold hold = kWcaHold;
    QString scramble; // physical: applied holding white top, green front
    Mirror world;     // the mirror image the case is set up in
  };

  CasePicker picker_;
  SolveTimer timer_;
  QTimer tick_;
  Phase phase_ = Phase::NoCase;
  std::optional<Current> current_;
  QFutureWatcher<GeneratedScramble> watcher_;
  int scrambleRequest_ = 0;
  VirtualAttempt attempt_;
  std::int64_t attemptMs_ = 0;
  bool failed_ = false;
  bool unfinished_ = false; // virtual: the step wasn't done at the stop

  QComboBox *cubeBox_;
  QComboBox *poolBox_;
  QComboBox *orderBox_;
  bool controlsLocked_ = false;
  QWidget *viewRow_;
  QLabel *scrambleLabel_;
  CubeView *cubeView_;
  TimeDisplay *display_;
  QLabel *statusLabel_;
  QWidget *resultRow_;
  QLabel *resultLabel_;
  QPushButton *wrongButton_;
  QWidget *diagramBalance_;
  QLabel *diagram_;
  QLabel *reveal_;

  bool onVirtualCube() const;
  // The attempt mode whose drill data is recorded and shown.
  AttemptMode attemptMode() const;
  // Shows the cube or the scramble, as the drill's cube needs.
  void showCubeKind();
  Pool poolChoice() const;
  std::vector<std::string> pool();
  void loadChoices();
  void nextCase();
  void retryCase();
  // Puts the case on the virtual cube as it was set up.
  void resetCube();
  void requestScramble();
  void scrambleReady();
  void onTick();
  // Locks the set, cube, case and order choices while a solve runs.
  void updateControls();
  // Virtual: stops the timer, and the attempt is a DNF unless the cube
  // shows the step done.
  void stopVirtual(std::int64_t now);
  // Whether the virtual cube shows the current case's step done.
  bool stepDone();
  void finish(std::int64_t timeMs, bool failed);
  void setWrong(bool wrong);
  void showResult();
  void showReveal();
  // Shows the case diagram right of the time, or hides it (null pixmap).
  void setDiagram(const QPixmap &pixmap);
  void setPhase(Phase p);
};

} // namespace vcube
