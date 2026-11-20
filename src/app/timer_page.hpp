#pragma once

#include "app/session_solve_page.hpp"
#include "io/sessions.hpp"
#include "train/session_edit.hpp"
#include "train/solve_review.hpp"
#include "train/timed_flow.hpp"

#include <QFutureWatcher>

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace vcube {

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
class TimerPage : public SessionSolvePage {
  Q_OBJECT
public:
  TimerPage(AppSettings &settings, SessionStore &store, AlgLibrary &library,
            QWidget *parent = nullptr);
  ~TimerPage() override;

  bool handleKey(QKeyEvent *e, bool press) override;
  bool isBusy() const override; // a solve or inspection is in progress
  void activated() override;
  void deactivated() override;
  void windowDeactivated() override;
  void librarySetsChanged(const QString &setId) override {
    refreshScrambleTypes(setId);
  }

protected:
  void focusModeChanged() override;

private:
  using SplitResults = std::vector<std::pair<std::string, SolveSplits>>;

  SessionStore &store_;

  TimedFlow flow_;
  DeletedAttempt<Solve> deleted_;
  // The last solve was just recorded: no key since (Backspace marks it).
  bool justSolved_ = false;
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
  // The session's statistics, kept from one solve to the next, and in WCA
  // units for the chart when they are shown in others.
  RunningStats stats_;
  RunningStats chartStats_;
  // The solves the table has rows for.
  std::size_t rowsShown_ = 0;
  // The cross, F2L and LL times of each solve, as the chart shows them.
  std::vector<StatValue> crossSeries_;
  std::vector<StatValue> f2lSeries_;
  std::vector<StatValue> llSeries_;

  Session &session() { return store_.current(); }
  bool onVirtualCube() const override {
    return store_.current().cube == CubeKind::Virtual;
  }
  std::size_t attemptCount() const override {
    return store_.current().solves.size();
  }
  // The Moves, TPS, Cross (EO Cross in a ZZ solve), F2L and LL columns
  // come from a virtual solve's move times.
  std::vector<AttemptColumn> columns() const override;
  void configureFlow() override;
  void cubeChosen(CubeKind kind) override;
  void typeChosen(const QString &type) override;
  void scrambleDropped() override { flow_.nextScramble(); }
  void settingsApplied() override;
  // Does what the flow said a key or the timer called for.
  void apply(const TimedFlow::Effects &e, std::int64_t now);
  void turnDuringSolve(Move m, std::int64_t now);
  void showScramble() override;
  // The cube as it is, or blank while it is hidden.
  void showCube();
  // The last solve's result, or 0.00 with none.
  void showLastResult();
  // Stops a virtual solve: the time if the cube is solved, else DNF.
  void stopVirtual(std::int64_t now);
  void tick() override;
  // Locks the controls that would change the scramble or session while a
  // solve or inspection runs, and unlocks them after.
  void updateControls();
  void keyDone() override { updateControls(); }
  void recordSolve(std::int64_t timeMs, Penalty penalty,
                   const std::string &solution = {},
                   std::vector<std::int64_t> times = {});
  // The keys for the last solve and the scramble when no solve runs.
  bool handleActionKey(QKeyEvent *e);
  void skip() override;
  void refreshSessionMenu() override;
  void selectSession(std::size_t index) override;
  void refreshAttempts() override { showAttempts(Refresh::All); }
  // What changed since the attempts were last shown: anything, one solve
  // added at the end (with the statistics already brought up to date), or
  // only the splits worked out.
  enum class Refresh : std::uint8_t { All, Appended, Splits };
  void showAttempts(Refresh what);
  // The table's row for solve `i`, with its splits put in the series;
  // `missing` is set when they aren't known yet.
  AttemptRow solveRow(std::size_t i, bool &missing);
  // Brings the statistics up to date with the session: `appended` when
  // the only change since is a solve added at the end, which then gives
  // the new bests it set. Anything else works them out afresh.
  std::vector<NewBest> updateStats(bool appended);
  void setLastPenalty(Penalty p);
  void deleteAttempt(int index) override;
  void undoDelete() override;
  void commentSolve(int index);
  bool canReview(int index) const override {
    return !store_.current()
                .solves[static_cast<std::size_t>(index)]
                .moves.empty();
  }
  void review(int index) override;
  void showDetails(int index) override;
  void showAttemptMenu(int index, const QPoint &globalPos) override;
  void showStat(const QString &link) override;
  void addSession() override;
  void renameSession() override;
  void deleteSession() override;
  void importCstimer();
  void exportCstimer();
  void save() override;

  static std::string splitKey(const Solve &s);
  const SolveSplits *splitsOf(const Solve &s) const;
  void requestSplits();
};

} // namespace vcube
