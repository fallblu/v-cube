#pragma once

#include "app/app_settings.hpp"
#include "app/cube_view.hpp"
#include "app/mode_page.hpp"
#include "app/scramble_source.hpp"
#include "app/widgets.hpp"
#include "io/inspection_store.hpp"
#include "io/library.hpp"
#include "train/move_count.hpp"
#include "train/step_solver.hpp"

#include <QFutureWatcher>
#include <QTimer>

#include <optional>
#include <vector>

class QComboBox;
class QLabel;
class QMenu;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QToolButton;

namespace vcube {

// Inspection mode: practice for the inspection of a ZZ solve on the
// virtual cube, kept in sessions (inspection.json in the data folder).
//
// Plan sessions are for one step (EO, EO+1, EO+2 or EO Cross). Space shows
// the scrambled cube, which only turns as a whole while you plan (within
// the session's time limit, WCA style with calls at 8 and 12 seconds, or
// none); Space again (or the limit) hides it, and you do your plan with
// the turning keys without seeing it. Space reveals the cube: the step is
// checked, the moves counted and compared with the optimal solutions.
//
// Edges sessions show the scrambled cube in a hold from the colour
// neutrality; type how many edges are bad for the F/B axis (and the R/L
// axis), then the answer is checked and the bad edges ringed.
class InspectionPage : public ModePage {
  Q_OBJECT
public:
  // Sessions are kept in `dataDir`/inspection.json.
  InspectionPage(AppSettings &settings, const QString &dataDir,
                 QWidget *parent = nullptr);

  bool handleKey(QKeyEvent *e, bool press) override;
  void applySettings() override;
  bool isBusy() const override;
  void deactivated() override;
  void windowDeactivated() override;
  void setFocusMode(bool on) override;

signals:
  // "Review…" on a Plan attempt: its scramble and solution, the session's
  // step (a SolveStep) and the attempt's index in the session.
  void reviewRequested(const QString &scramble, const QString &solution,
                       int step, int index);

private:
  // Ready: the cube hidden until Space. Plan: Inspecting (the cube shown,
  // rotations only) then Executing (the cube hidden, any turn). Edges:
  // Counting (the cube shown, rotations only, the counts typed). Result:
  // the outcome, until Space gives the next scramble.
  enum class Phase { Ready, Inspecting, Executing, Counting, Result };

  // The optimal solutions for a Plan attempt, worked out off the GUI
  // thread, for the attempt `attempt` of session `session`.
  struct Analysis {
    std::size_t session = 0;
    std::size_t attempt = 0;
    std::string scramble;
    SolveStep step = SolveStep::EOCross;
    std::vector<HeldOptimum> neutral; // the neutrality's holds, both axes
    std::optional<int> overall;       // over all 24 holds
  };
  // The optimum of an attempt recorded without one.
  struct Filled {
    std::size_t attempt = 0;
    std::string scramble;
    int optimal = 0;
  };

  AppSettings &settings_;
  InspectionStore store_;
  AlgLibrary noSets_; // the feed only gives WCA scrambles
  ScrambleFeed feed_;
  VirtualAttempt attempt_;
  Phase phase_ = Phase::Ready;
  std::int64_t started_ = 0; // monotonicMs() when inspection or counting began
  std::int64_t usedMs_ = 0;  // the inspection time used
  QTimer tick_;
  // Edges: the counts typed so far ([0] F/B, [1] R/L), the axis being
  // typed, and the axis whose bad edges are ringed in the result.
  QString entry_;
  std::array<int, 2> answers_{-1, -1};
  int axis_ = 0;
  int ringAxis_ = 0;
  Hold hold_ = kWcaHold; // Edges: the hold the cube is shown in
  std::optional<GeneratedScramble> scramble_; // the one being practised
  QFutureWatcher<Analysis> analysis_;
  std::optional<Analysis> lastAnalysis_;
  QString resultHead_; // the result panel without the optimal solutions
  // Plan attempts recorded before their optimum was known, filled in later.
  QFutureWatcher<std::vector<Filled>> backfill_;
  std::size_t backfillSession_ = 0;

  QWidget *leftPanel_;
  QWidget *optionsRow_;
  QToolButton *sessionButton_;
  QMenu *sessionMenu_;
  QComboBox *drillBox_;
  QWidget *stepPicker_;
  QComboBox *stepBox_;
  QWidget *axesPicker_;
  QComboBox *axesBox_;
  QWidget *limitPicker_;
  QComboBox *limitBox_;
  QWidget *viewPicker_;
  QPushButton *skipButton_;
  QLabel *stepLabel_;
  QLabel *scrambleLabel_;
  QStackedWidget *cubeStack_;
  CubeView *cubeView_;
  QLabel *hiddenLabel_;
  TimeDisplay *display_;
  QLabel *hintLabel_;
  QLabel *resultLabel_;
  QTableWidget *table_;
  QLabel *stats_;
  TrendChart *chart_;

  InspectionSession &session() { return store_.current(); }
  const InspectionSession &session() const {
    return store_.sessions()[store_.currentIndex()];
  }
  bool planning() const { return session().drill == InspectionDrill::Plan; }

  void setPhase(Phase p);
  void showCube(bool shown);
  void showHint();
  // Locks what would change the scramble or session during an attempt.
  void updateControls();
  // Shows the session's drill options.
  void showDrill();
  void showScramble();
  void tick();

  // Plan.
  void startInspection();
  void startExecution();
  void finishPlan();
  void analyse(const std::string &scramble, SolveStep step);
  void showAnalysis();
  // Works out the optimum of Plan attempts recorded without one.
  void backfillOptima();
  // Edges.
  void startCounting();
  void typeDigit(int digit);
  void enterCount();
  void finishEdges();
  void showRings();

  void turn(Move m);
  // Back to Ready with the same scramble, recording nothing.
  void cancel();
  void next();
  void skip();

  void refreshSessionMenu();
  void selectSession(std::size_t index);
  void refreshAttempts();
  void showPlanStats();
  void showEdgesStats();
  void showAttemptMenu(const QPoint &pos);
  void requestReview(int index);
  void deleteAttempt(int index);
  void addSession();
  void renameSession();
  void deleteSession();
  void chooseLimit(int index);
  void save();
};

} // namespace vcube
