#pragma once

#include "app/case_page.hpp"
#include "app/cube_view.hpp"
#include "app/page_parts.hpp"
#include "app/widgets.hpp"
#include "train/case_picker.hpp"
#include "train/drill_flow.hpp"
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
class QToolButton;

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

  // Drills the set: its weakest cases (as the Weak column has them), or
  // the cases learned in Learn.
  void drillSet(const QString &setId, bool weakest);
  bool handleKey(QKeyEvent *e, bool press) override;
  SettingsParts settingsUsed() const override {
    return SettingsPart::Cube | SettingsPart::Keys | SettingsPart::Neutrality |
           SettingsPart::Thumbnails | SettingsPart::Timer | SettingsPart::Srs |
           SettingsPart::Drill;
  }
  void applySettings() override;
  bool isBusy() const override;
  void activated() override;
  void deactivated() override;
  void windowDeactivated() override;

protected:
  void focusModeChanged() override;
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
  void extendModeMenu(QMenu &menu) override;
  void ticksChanged() override;
  void neutralityChanged() override;

private:
  // Starts counting the session's attempts afresh.
  void resetTally();
  // A button under the status for an empty state that names another
  // mode (none hides it).
  void offer(const QString &text, std::function<void()> run);
  QPushButton *offerButton_ = nullptr;
  std::function<void()> offerRun_;
  using Phase = DrillFlow::Phase;
  enum class Pool { Learned, All, Learning, Queue, Weakest };
  // The scope drop-down's values: the ticked cases, those shown, a group.
  static inline const QString kScopeTicked = "ticked";
  static inline const QString kScopeShown = "shown";
  static inline const QString kScopeGroup = "group:";

  struct Current {
    std::string caseId;
    CubieCube state; // the case as the solver sees it, held as `hold`
    Hold hold = kWcaHold;
    QString scramble;    // physical: applied holding white top, green front
    Mirror world;        // the mirror image the case is set up in
    CubieCube caseState; // the case as the set had it when set up
  };

  CasePicker picker_;
  // The attempt's state: the timer, the phase and how the case went.
  DrillFlow flow_;
  QTimer tick_;
  std::optional<Current> current_;
  QFutureWatcher<GeneratedScramble> watcher_;
  int scrambleRequest_ = 0;
  VirtualAttempt attempt_;
  QString missText_; // why a virtual attempt was a DNF
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
  OptionBar *options_;
  QToolButton *casesButton_;
  QLabel *keysLabel_;
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
  DrillFlow::Config flowConfig() const;
  DrillFlow::KeyEvent flowKey(const QKeyEvent *e, bool press) const;
  // Does what the flow asked after a key.
  void perform(const DrillFlow::Step &step, std::int64_t now);
  QString hintText(DrillFlow::Hint hint) const;
  // The status line and, under it, the keys that do something now.
  void say(const QString &message, const QList<KeyHint> &keys = {});
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
  // Nothing is recorded; the same case is set up to try again.
  void cancelSolve(const QString &why);
  // Virtual, while the timer runs: turns the cube, and stops the timer if
  // that did the step and the timer stops by itself.
  void runningTurn(Move m, std::int64_t now);
  // The virtual cube as the solver sees it.
  std::optional<CubieCube> cubeState() const;
  // Whether the virtual cube shows the current case's step done.
  bool stepDone();
  void finish(std::int64_t timeMs, bool failed, bool gaveUp = false);
  // Gives the case up before or during the solve: a DNF.
  void giveUp(std::int64_t now);
  QString giveUpKeyText() const;
  // Sets the same case up again to solve with no timer and nothing
  // recorded, with the algs shown (aided) or hidden.
  void startPractice(bool aided);
  // A turn in practice; goes on by itself once the stage is solved.
  void practiceTurn(Move m);
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
  // The keys of the phase the flow is in, under the cube.
  void showStatus();
};

} // namespace vcube
