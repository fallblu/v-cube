#pragma once

#include "app/app_settings.hpp"
#include "app/cube_view.hpp"
#include "app/mode_page.hpp"
#include "app/scramble_source.hpp"
#include "app/widgets.hpp"
#include "io/inspection_store.hpp"
#include "io/library.hpp"
#include "train/inspection_flow.hpp"
#include "train/move_count.hpp"
#include "train/step_solver.hpp"

#include <QFutureWatcher>
#include <QTimer>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;
class QStackedWidget;

namespace vcube {

class SessionPanel;

// Inspection mode: practice for the inspection of a ZZ solve on the
// virtual cube, kept in sessions (inspection.json in the data folder).
//
// Plan sessions are for one step (EO, EOLine, EO+1, EO+2 or EO Cross, the
// last optionally followed by a named F2L pair). Space shows the scrambled
// cube, which only turns as a whole while you plan (within the session's
// time limit, counted down WCA style with calls at 8 and 12 seconds, or
// none); Space again (or the limit) hides it, and you do your plan with
// the turning keys without seeing it. Space reveals the cube: the step is
// checked, the moves counted and compared with the optimal solutions, and
// the cube can be turned as a whole to look at it. An attempt can be
// planned again (Retry) without being recorded.
//
// Edges sessions show the scrambled cube in a hold from the colour
// neutrality; type how many edges are bad for the F/B axis (and the R/L
// axis), then the answer is checked and the bad edges ringed.
//
// The phases and keys are InspectionFlow's; the page shows them.
class InspectionPage : public ModePage {
  Q_OBJECT
public:
  // Sessions are kept in `dataDir`/inspection.json.
  InspectionPage(AppSettings &settings, const QString &dataDir,
                 QWidget *parent = nullptr);

  bool handleKey(QKeyEvent *e, bool press) override;
  // Backspace goes on deleting typed digits as it is held.
  bool acceptsAutoRepeat(const QKeyEvent *e) const override {
    return e->key() == Qt::Key_Backspace;
  }
  SettingsParts settingsUsed() const override {
    return SettingsPart::Cube | SettingsPart::Keys | SettingsPart::Neutrality;
  }
  void applySettings() override;
  QWidget *sidePanel() const override;
  bool isBusy() const override;
  void deactivated() override;
  void windowDeactivated() override;

  // Plans Plan attempt `index` of the session again, not recorded.
  void startRetry(int index);

signals:
  // "Review…" on a Plan attempt: its scramble and solution, the session's
  // step (a SolveStep) and the attempt's index in the session.
  void reviewRequested(const QString &scramble, const QString &solution,
                       int step, int index);

protected:
  void focusModeChanged() override;

private:
  using Phase = InspectionFlow::Phase;

  // The optimal solutions for a Plan attempt, worked out off the GUI
  // thread, for the attempt `attempt` of session `session`.
  // (A retry's analysis is for the attempt it repeats.)
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
  // The phase, the time limit, the counts typed and the retry.
  InspectionFlow flow_;
  // During a retry (flow_.retry()): how the attempt it repeats went then.
  InspectionAttempt retryOf_;
  // Result: the moves of the attempt shown (unless DNF), to compare with
  // the optimum, and the rotations done since to look at the cube.
  std::optional<int> shownMoves_;
  Alg lookTurns_;
  QTimer tick_;
  Hold hold_ = kWcaHold; // Edges: the hold the cube is shown in
  std::optional<GeneratedScramble> scramble_; // the one being practised
  QFutureWatcher<Analysis> analysis_;
  std::optional<Analysis> lastAnalysis_;
  QString resultHead_; // the result panel without the optimal solutions
  // Plan attempts recorded before their optimum was known, filled in later.
  QFutureWatcher<std::vector<Filled>> backfill_;
  std::size_t backfillSession_ = 0;

  SessionPanel *panel_;
  QWidget *optionsRow_;
  QComboBox *drillBox_;
  QWidget *stepPicker_;
  QComboBox *stepBox_;
  QWidget *axesPicker_;
  QComboBox *axesBox_;
  QWidget *limitPicker_;
  QComboBox *limitBox_;
  QWidget *pairPicker_;
  QComboBox *pairBox_;
  QComboBox *chartBox_;
  QWidget *viewPicker_;
  QPushButton *skipButton_;
  QPushButton *newSessionButton_;
  std::optional<std::pair<int, InspectionAttempt>> deleted_;
  QLabel *stepLabel_;
  QLabel *scrambleLabel_;
  QStackedWidget *cubeStack_;
  CubeView *cubeView_;
  QLabel *hiddenLabel_;
  TimeDisplay *display_;
  QLabel *hintLabel_;
  QLabel *keysLabel_;
  QLabel *resultLabel_;
  QPushButton *reviewButton_;
  QPushButton *retryButton_;
  QPushButton *deleteButton_;
  TrendChart *chart_; // the panel's, laid out beside chartBox_

  InspectionSession &session() { return store_.current(); }
  const InspectionSession &session() const {
    return store_.sessions()[store_.currentIndex()];
  }
  bool planning() const { return session().drill == InspectionDrill::Plan; }
  // The pair tracked after EO Cross, if the session has one.
  std::optional<Slot> trackedPair() const;

  // Shows the flow's phase: the cube or not, the clock, the controls.
  void showPhase();
  void showCube(bool shown);
  void showHint();
  // Locks what would change the scramble or session during an attempt.
  void updateControls();
  // Shows the session's drill options (and gives them to the flow).
  void showDrill();
  void showScramble();
  // The clock while inspecting or counting: the limit may hide the cube.
  void tick();
  // The display (and while counting the hint) as of now.
  void showDisplay();
  // Does what the flow said to after an event; `move` is the key's turn.
  void apply(const InspectionFlow::Effects &e,
             std::optional<Move> move = std::nullopt);

  // Plan.
  void startInspection();
  void startExecution();
  void finishPlan();
  void analyse(const std::string &scramble, SolveStep step,
               std::size_t attempt);
  void showAnalysis();
  // Works out the optimum of Plan attempts recorded without one.
  void backfillOptima();
  // Edges.
  void startCounting();
  void finishEdges();
  void showRings();

  void turn(InspectionFlow::Turn how, Move m);
  // Result: turns the cube as a whole to look at it, marks and all.
  void look(Move m);
  // Back to Ready with the same scramble, recording nothing (the flow is
  // there already).
  void showCancelled();
  // Back to Ready for the next scramble (the flow is there already).
  void showNext();
  void skip();

  void refreshSessionMenu();
  void selectSession(std::size_t index);
  void refreshAttempts();
  void showPlanStats();
  void showEdgesStats();
  void showChart();
  void showAttemptMenu(int index, const QPoint &globalPos);
  void requestReview(int index);
  void deleteAttempt(int index);
  // Puts the attempt deleted last back where it was.
  void undoDelete();
  // What the stage says while the cube waits to be shown.
  QString readyText() const;
  void addSession();
  void renameSession();
  void deleteSession();
  void chooseLimit(int index);
  void save();
};

} // namespace vcube
