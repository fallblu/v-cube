#pragma once

#include "app/app_settings.hpp"
#include "app/cube_view.hpp"
#include "app/eo_cross_check.hpp"
#include "app/mode_page.hpp"
#include "app/scramble_source.hpp"
#include "app/session_panel.hpp"
#include "app/widgets.hpp"
#include "io/library.hpp"
#include "io/sessions.hpp"
#include "train/move_count.hpp"
#include "train/session_edit.hpp"
#include "train/solve_review.hpp"
#include "train/timed_flow.hpp"

#include <QFutureWatcher>
#include <QTimer>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;

namespace vcube {

class SolveStage;

// Timed mode, a cstimer-like timer: scrambles, a space-bar timer with
// optional inspection, sessions, statistics and a trend chart. Each
// session is for the virtual cube or a physical one, fixed once it has
// solves. On a physical cube the scramble has a preview net and any key
// stops the timer. On the virtual cube the scrambled cube can only be
// turned as a whole until the timer starts (with inspection on, it can be
// hidden until inspection starts); the timer stops when the cube is solved
// (or on Space, as a DNF when it isn't). A DNF leaves the cube as it was
// until Space. Each virtual solve keeps its solution and when each move
// was turned, which give its step splits (cross, pairs, last layer) and a
// review.
class TimerPage : public ModePage {
  Q_OBJECT
public:
  TimerPage(AppSettings &settings, SessionStore &store, AlgLibrary &library,
            QWidget *parent = nullptr);
  ~TimerPage() override;

  bool handleKey(QKeyEvent *e, bool press) override;
  SettingsParts settingsUsed() const override {
    return SettingsPart::Cube | SettingsPart::Keys | SettingsPart::Neutrality |
           SettingsPart::Timer | SettingsPart::Counts;
  }
  void applySettings() override;
  QWidget *sidePanel() const override;
  bool isBusy() const override; // a solve or inspection is in progress
  bool hasModeMenu() const override { return true; }
  void fillModeMenu(QMenu &menu) override;
  void activated() override;
  void deactivated() override;
  void windowDeactivated() override;

  // The alg sets changed; an alg set scramble is made again only when its
  // own set (`changedSet`) changed.
  void refreshScrambleTypes(const QString &changedSet = {});

protected:
  void focusModeChanged() override;

private:
  using SplitResults = std::vector<std::pair<std::string, SolveSplits>>;

  AppSettings &settings_;
  SessionStore &store_;
  AlgLibrary &library_;

  TimedFlow flow_;
  QTimer tick_;
  ScrambleFeed feed_;
  ScrambleAlgs *algs_;
  VirtualAttempt attempt_;
  DeletedAttempt<Solve> deleted_;
  // Step splits of virtual solves, by splitKey(), worked out off the GUI
  // thread.
  std::map<std::string, SolveSplits> splits_;
  QFutureWatcher<SplitResults> splitsWatcher_;
  // What the table shows of a virtual solve that takes work to find: its
  // move count and its splits (null until they are worked out), kept by
  // the solve's place in the session so that a refresh after each solve
  // doesn't parse every solution and look every solve up again. A row is
  // for the solve it was made from: one that differs in what the two
  // depend on (edited, deleted before, of another session) is made anew.
  struct SolveRow {
    std::string scramble;
    std::string moves;
    std::int64_t timeMs = 0;
    std::size_t moveTimes = 0;
    int htm = 0;
    const SolveSplits *splits = nullptr;

    bool isOf(const Solve &s) const;
  };
  std::vector<SolveRow> solveRows_;
  // After a virtual solve: the optimal EO Cross of its scramble.
  EoCrossCheck eoCheck_;

  SessionPanel *panel_;
  SolveStage *stage_;
  ScrambleTypeBox *typeBox_;
  QComboBox *cubeBox_;
  QLabel *scrambleLabel_;
  NetPreview *net_;
  CubeView *cubeView_;
  TimeDisplay *display_;
  QLabel *statusLabel_;
  QLabel *focusStats_;
  TrendChart *chart_;
  QPushButton *nextButton_;

  Session &session() { return store_.current(); }
  bool onVirtualCube() const;
  // Does what the flow said a key or the timer called for.
  void apply(const TimedFlow::Effects &e, std::int64_t now);
  void turnDuringSolve(Move m, std::int64_t now);
  // Shows the cube, view and preview the session's cube needs.
  void showCubeKind();
  void showScramble();
  // The cube as it is, or blank while it is hidden.
  void showCube();
  // The last solve's result, or 0.00 with none.
  void showLastResult();
  // Stops a virtual solve: the time if the cube is solved, else DNF.
  void stopVirtual(std::int64_t now);
  void onTick();
  // Locks the controls that would change the scramble or session while a
  // solve or inspection runs, and unlocks them after.
  void updateControls();
  void recordSolve(std::int64_t timeMs, Penalty penalty,
                   const std::string &solution = {},
                   std::vector<std::int64_t> times = {});
  // The keys for the last solve and the scramble when no solve runs.
  bool handleActionKey(QKeyEvent *e);
  void nextScramble();
  void refreshSessionMenu();
  // The flow follows the settings and the session's cube.
  void configureFlow();
  void selectSession(std::size_t index);
  void refreshSolves();
  void setLastPenalty(Penalty p);
  void deleteSolve(int index);
  void undoDelete();
  void commentSolve(int index);
  void review(int index);
  void showDetails(int index);
  void showSolveMenu(int index, const QPoint &globalPos);
  void showColumns();
  // The solves behind a statistic, from a link in the statistics.
  void showStat(const QString &link);
  void addSession();
  void renameSession();
  void deleteSession();
  void importCstimer();
  void exportCstimer();
  void save();

  static std::string splitKey(const Solve &s);
  const SolveSplits *splitsOf(const Solve &s) const;
  void requestSplits();
  // Works out the optimal EO Cross of the solve just done.
};

} // namespace vcube
