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
//
// Backspace gives a case up before or during the solve, which is a DNF. A
// DNF is followed by practice of the same case as it was set up, untimed
// and unrecorded: once with the algs shown, then without them, and the
// next case comes once it is solved without help.
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
  enum class Phase {
    NoCase,
    Waiting,
    Running,
    Revealed,
    Aided,   // after a DNF: the same case again with the algs shown
    Unaided, // and then without them
  };
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
  bool unfinished_ = false;   // virtual: the step wasn't done at the stop
  bool gaveUp_ = false;       // the case was given up, so it stays a DNF
  bool practiceDone_ = false; // virtual: the practice solve reached the end

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
  // In a mirrored reduction set: looks at the cube from where the case it
  // now shows is done from.
  void followView();
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
  void finish(std::int64_t timeMs, bool failed, bool gaveUp = false);
  // Gives the case up before or during the solve: a DNF.
  void giveUp(std::int64_t now);
  // After a DNF: sets the same case up again to solve with no timer and
  // nothing recorded, with the algs shown (aided) or hidden.
  void startPractice(bool aided);
  bool handlePracticeKey(QKeyEvent *e, bool press, std::optional<Move> move);
  bool inPractice() const {
    return phase_ == Phase::Aided || phase_ == Phase::Unaided;
  }
  void setWrong(bool wrong);
  void showResult();
  void showReveal();
  // Shows the case diagram right of the time, or hides it (null pixmap).
  void setDiagram(const QPixmap &pixmap);
  void setPhase(Phase p);
};

} // namespace vcube
