#pragma once

#include "app/case_page.hpp"
#include "app/cube_view.hpp"
#include "app/widgets.hpp"
#include "train/drill_session.hpp"
#include "train/move_count.hpp"

#include <QFutureWatcher>
#include <QStringList>
#include <QTimer>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class QPushButton;

namespace vcube {

// Timed practice of a set's cases with the space-bar timer, on the virtual
// cube (the default) or a physical one, chosen per alg set. The two keep
// their drill statistics apart.
//
// Which cases come up is chosen apart from the ticks Learn shares: the
// ticked cases, those the list shows or one group, narrowed to the learned
// ones, those being learned, those in Learn's queue or the weakest few.
// The order is random, weighted to slow and failed cases, in list order
// (the list can be sorted by its columns) or a recap of every case once,
// with a summary at the end. A failed case comes back a few cases later.
//
// On the virtual cube each case is set up on the cube with random AUFs
// and the step's free pieces scrambled. It can only be turned as a whole
// until the timer starts: by default a tap of Space arms it and the first
// face turn starts it (or Space is held and let go, or a turn made while
// it is held starts it). The timer stops as soon as the cube shows the
// step done (solved for PLL, the last layer oriented for OLL, and so on),
// or on Space, when the attempt is a DNF unless the step is done; a DNF
// says why and shows the moves made. Stopped before any face turn, the
// attempt is cancelled.
//
// On a physical cube a scramble sets the case up, Space held and let go
// starts the timer and any key but a modifier stops it; a wrong alg is
// marked by hand.
//
// After each case it shows the time and the algs with the AUFs the case
// needs, and the case is selected in the list (not before, so that the list
// doesn't name it). Keys are ignored for a moment after the stop. On the
// virtual cube each attempt also records the recognition time (from the
// case being set up to the first turn of a layer off the U-D axis) and the
// execution time after it. Drill attempts feed the case statistics and
// never change the spaced-repetition schedule.
//
// Shift+Space (or Backspace, in Settings) gives a case up, a DNF; given up
// before the timer started, Delete takes it back. Space goes on to the next
// case and Tab practises the case as it was set up, untimed and
// unrecorded: once with the algs shown, then without them (going on by
// itself as each is solved). Backspace toggles the DNF, and Delete deletes
// the attempt, until the next case.
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
  void fillGroupRow(QTreeWidgetItem *item,
                    const std::vector<std::string> &caseIds) override;
  bool sortableColumns() const override { return true; }
  void updateSummary() override;
  void startSet() override;
  void setReloaded() override;
  void startCase(const std::string &caseId) override;
  QString startCaseLabel(const std::string &caseId) override;
  void extendCaseMenu(QMenu &menu,
                      const std::vector<std::string> &caseIds) override;
  void ticksChanged() override;
  void neutralityChanged() override;

private:
  enum class Phase {
    NoCase,
    Waiting,
    Running,
    Revealed,
    Aided,   // practice: the same case again with the algs shown
    Unaided, // and then without them
  };
  enum class Pool { Learned, All, Learning, Queue, Weakest };

  struct Current {
    std::string caseId;
    CubieCube state; // the case as the solver sees it, held as `hold`
    Hold hold = kWcaHold;
    QString scramble;    // physical: applied holding white top, green front
    Mirror world;        // the mirror image the case is set up in
    CubieCube caseState; // the case as the set had it when set up
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
  bool gaveUp_ = false;     // the case was given up, so it stays a DNF
  bool gaveUpBeforeStart_ = false;
  bool recorded_ = false; // the attempt is recorded (not deleted)
  bool newBest_ = false;
  std::optional<std::int64_t> bestBefore_; // the case's best before it
  bool practiceDone_ = false;   // virtual: the practice solve reached the end
  int practiceToken_ = 0;       // drops a pending automatic advance
  bool armed_ = false;          // virtual: Space tapped, waiting for a turn
  int faceTurns_ = 0;           // face turns since the timer started
  std::int64_t guardUntil_ = 0; // keys are ignored until then (monotonic)
  QString missText_;            // why a virtual attempt was a DNF
  // Recognition timing on the virtual cube (monotonic ms): when the case
  // was set up, when the timer started and when a layer off the U-D axis
  // was first turned (0: not yet). Not valid once a solve was cancelled.
  std::int64_t shownAtMs_ = 0;
  std::int64_t startMs_ = 0;
  std::int64_t firstTurnMs_ = 0;
  bool timingValid_ = false;
  DrillSession session_;
  std::optional<std::size_t> sessionIndex_; // the current attempt's
  std::size_t recapFrom_ = 0; // the session's first attempt of the recap
  int retryDepth_ = 0;

  QComboBox *cubeBox_;
  QComboBox *scopeBox_;
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
  QPushButton *deleteButton_;
  QLabel *diagram_;
  QLabel *reveal_;
  QLabel *sessionLabel_;

  bool onVirtualCube() const;
  // The attempt mode whose drill data is recorded and shown.
  AttemptMode attemptMode() const;
  // Shows the cube or the scramble, as the drill's cube needs.
  void showCubeKind();
  Pool poolChoice() const;
  // The cases the scope chooses, and of those the pool's, which can be
  // drilled.
  std::vector<std::string> scopeCases();
  std::vector<std::string> pool();
  // The case is in the set, with a passing alg.
  bool drillable(const std::string &caseId);
  void fillScopes();
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
  // Cancels the timer, a hold of Space or the armed timer.
  void cancelTimer();
  // Nothing is recorded; the same case is set up to try again.
  void cancelSolve(const QString &why);
  // Virtual: starts the timer (on the first face turn, or Space let go).
  void startVirtual(std::int64_t now);
  // Virtual, while the timer runs: turns the cube, and stops the timer if
  // that did the step and the timer stops by itself.
  void runningTurn(Move m, std::int64_t now);
  // Virtual: stops the timer, and the attempt is a DNF unless the cube
  // shows the step done.
  void stopVirtual(std::int64_t now);
  // The virtual cube as the solver sees it.
  std::optional<CubieCube> cubeState() const;
  // Whether the virtual cube shows the current case's step done.
  bool stepDone();
  void finish(std::int64_t timeMs, bool failed, bool gaveUp = false);
  // Gives the case up before or during the solve: a DNF.
  void giveUp(std::int64_t now);
  bool isGiveUpKey(const QKeyEvent *e) const;
  QString giveUpKeyText() const;
  bool handleWaitingKey(QKeyEvent *e, bool press, std::optional<Move> move,
                        std::int64_t now);
  bool handleRevealedKey(QKeyEvent *e, bool press, std::optional<Move> move);
  // Sets the same case up again to solve with no timer and nothing
  // recorded, with the algs shown (aided) or hidden.
  void startPractice(bool aided);
  bool handlePracticeKey(QKeyEvent *e, bool press, std::optional<Move> move);
  bool inPractice() const {
    return phase_ == Phase::Aided || phase_ == Phase::Unaided;
  }
  void setWrong(bool wrong);
  // Deletes the attempt just recorded; one given up before the timer
  // started is taken back, and the case set up as before.
  void deleteAttempt();
  // Clears the drill times of the cases in this cube's drill mode.
  void clearTimes(const std::vector<std::string> &caseIds);
  QString caseName(const std::string &caseId);
  void showResult();
  void showReveal();
  void refreshSession();
  // The summary of the session's attempts from `from` on.
  QString sessionText(std::size_t from);
  // Shows the case diagram beside the algs, or hides it (null pixmap).
  void setDiagram(const QPixmap &pixmap);
  void setPhase(Phase p);
};

} // namespace vcube
