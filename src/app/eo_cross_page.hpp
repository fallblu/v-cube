#pragma once

#include "app/app_settings.hpp"
#include "app/cube_view.hpp"
#include "app/mode_page.hpp"
#include "app/page_parts.hpp"
#include "app/widgets.hpp"
#include "io/eo_cross_store.hpp"
#include "train/eo_cross.hpp"

#include <QKeyEvent>
#include <QTimer>

#include <array>
#include <optional>
#include <string>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTabBar;
class QTreeWidget;
class QTreeWidgetItem;

namespace vcube {

// EO Cross mode: the start of a ZZ solve practised a part at a time on the
// virtual cube (see eo_cross.hpp).
//
//   Bad edges       a state is shown; type how many edges are bad for the
//                   F/B axis (and the R/L axis), then they are ringed
//   Axis choice     turn the cube to the hold you would do the step from
//                   and press Space; the holds are then ranked by their
//                   optimum
//   Plan blind      look at the cube (rotations only, within the
//                   inspection time), Space hides it, do your plan unseen,
//                   Space shows how it went
//   Step solutions  solve the step in view; afterwards your moves beside
//                   the optimal solutions for that very state
//   Full EO Cross   a whole EO Cross, then where its moves went
//
// Every drill is timed from the moment its state is shown, so each opens
// on a ready page: nothing is shown until Space.
//
// A step counts as done in any hold the colour neutrality allows, on
// either EO axis. The optimal solutions are searched for the state on the
// cube; how each kind of state went is kept (eocross.json), and the kinds
// that go badly come up more often.
class EoCrossPage : public ModePage {
  Q_OBJECT
public:
  using Drill = EoDrill;
  enum class Phase {
    Ready,      // waiting for Space
    Counting,   // Bad edges: typing the counts
    Choosing,   // Axis choice: turning the cube
    Inspecting, // Plan blind: looking
    Executing,  // Plan blind: the cube is hidden
    Solving,    // Step solutions and Full EO Cross
    Done,
  };
  // One of the solutions listed once a round is done.
  struct Solution {
    Hold hold;
    Alg alg;
  };

  // The statistics are kept in `dataDir`/eocross.json.
  EoCrossPage(AppSettings &settings, const QString &dataDir,
              QWidget *parent = nullptr);
  ~EoCrossPage() override;

  bool handleKey(QKeyEvent *e, bool press) override;
  // Backspace goes on deleting typed digits as it is held.
  bool acceptsAutoRepeat(const QKeyEvent *e) const override {
    return phase_ == Phase::Counting && e->key() == Qt::Key_Backspace;
  }
  SettingsParts settingsUsed() const override {
    return SettingsPart::Cube | SettingsPart::Keys | SettingsPart::Neutrality |
           SettingsPart::Timer;
  }
  void applySettings() override;
  void activated() override;
  void deactivated() override;
  QWidget *sidePanel() const override { return side_; }

  // What the page shows, for tests and scripts.
  Drill drill() const { return drill_; }
  Phase phase() const { return phase_; }
  void selectDrill(Drill d);
  SolveStep step() const { return step_; }
  void setStep(SolveStep step);
  // The bad edges every state has (-1: any, the weak kinds more often).
  int pinnedBad() const { return pinned_; }
  void setPinnedBad(int bad);
  // Bad edges asks for the R/L axis too.
  bool bothAxes() const { return bothAxes_; }
  void setBothAxes(bool on);
  // The inspection time of Plan blind and Axis choice (0: no limit).
  int limitSeconds() const { return limit_; }
  void setLimitSeconds(int seconds);
  // The round's state as scrambled (white top, green front), the hold it
  // was first shown in, and the cube as it is now.
  const CubieCube &scrambled() const { return round_.scrambled; }
  Hold shownHold() const { return round_.shown; }
  const FaceletCube &cube() const { return cube_; }
  int roundBadEdges() const { return round_.kind.bad; }
  bool cubeHidden() const;
  // The holds a step may be done in.
  std::vector<Hold> allowedHolds() const;
  // The round's optima have been worked out.
  bool analysisReady() const { return round_.analysed; }
  // The holds ranked by their optimum (empty until worked out).
  const std::vector<HeldOptimum> &ranked() const { return round_.ranked; }
  // The solutions listed once the round is done, best hold first.
  std::vector<Solution> shownSolutions() const;
  QString promptHtml() const;
  QString resultHtml() const;
  QString displayText() const;
  const EoCrossStats &stats() const { return store_.stats; }

protected:
  void focusModeChanged() override;

private:
  struct Analysis {
    int round = 0;
    std::vector<HeldOptimum> ranked;
  };
  struct Round {
    int id = 0;
    EoKind kind;
    CubieCube scrambled;
    Hold shown = kWcaHold;
    FaceletCube start;      // the scrambled cube turned to `shown`
    Alg moves;              // done since, rotations too
    std::int64_t began = 0; // monotonicMs()
    std::int64_t ms = 0;    // once done
    bool hint = false;
    bool gaveUp = false;
    bool retry = false; // the same state again: not recorded
    bool recorded = false;
    std::optional<Hold> solvedIn; // the hold the step was done in
    std::vector<HeldOptimum> ranked;
    bool analysed = false;
    // Bad edges: the axes asked (F/B, R/L), the counts typed and the right
    // ones (-1: not asked or not answered), the axis being typed, the one
    // ringed afterwards, and the rotations done to look at the cube.
    bool both = false;
    std::array<int, 2> answers{-1, -1};
    std::array<int, 2> correct{-1, -1};
    std::string entry;
    int axis = 0;
    int ringAxis = 0;
    Alg lookTurns;
    // Axis choice: the place of the hold picked in `ranked`.
    std::optional<std::size_t> picked;
    // Plan blind: the inspection time used, and when the limit hid the
    // cube (-1: Space did).
    std::int64_t usedMs = 0;
    std::int64_t hiddenAt = -1;
    int calls = 0; // the 8 s and 12 s calls made
    // Full EO Cross.
    std::optional<EoBreakdown> breakdown;
  };

  AppSettings &settings_;
  EoCrossStore store_;
  Rng rng_;
  Drill drill_ = Drill::Solve;
  SolveStep step_ = SolveStep::EOCross;
  int pinned_ = -1;
  bool bothAxes_ = false;
  int limit_ = 15;
  Phase phase_ = Phase::Ready;
  // Space was pressed since the page, the drill or an option was chosen:
  // states follow one another without the ready page.
  bool ready_ = false;
  Round round_;
  int rounds_ = 0;
  bool active_ = false;
  FaceletCube cube_;

  QWidget *side_;
  QLabel *summary_;
  QTreeWidget *tree_;
  QPushButton *clearButton_;
  QTabBar *tabs_;
  OptionBar *options_;
  QComboBox *stepBox_;
  QWidget *stepItem_;
  QComboBox *badBox_;
  QWidget *badItem_;
  QComboBox *axesBox_;
  QWidget *axesItem_;
  QComboBox *limitBox_;
  QWidget *limitItem_;
  QLabel *prompt_;
  QStackedWidget *cubeStack_;
  CubeView *cubeView_;
  QLabel *hiddenLabel_;
  QLabel *display_;
  HintBar *hint_;
  QLabel *result_;
  QTimer clock_;

  // --- building and options (eo_cross_page.cpp)
  void buildSide();
  void buildOptions();
  void loadChoices();
  void saveChoices() const;
  void showOptions();
  // An option was chosen: back to the ready page.
  void optionChanged();
  // The step the drill is about (EO Cross where it has no choice).
  SolveStep roundStep() const { return round_.kind.step; }
  QString stepText(SolveStep step) const;
  // Where a step may be done: "with a yellow or white cross, on either EO
  // axis".
  QString whereText(SolveStep step) const;
  QString drillName(Drill d) const;

  // --- rounds
  void newRound(bool sameState = false);
  void showReady();
  void startRound(EoKind kind, CubieCube scrambled, Hold shown, bool retry);
  void showCube(bool shown);
  void turn(Move m);
  void finish(bool gaveUp);
  void resetCube();
  void showHint();
  void showPrompt();
  void showKeys();
  void showClock();
  QString clockText(std::int64_t ms) const;
  // Bad edges.
  void typeDigit(int digit);
  void enterCount();
  void finishEdges();
  void showRings();
  // "F/B (green / blue)", the colours as rich text squares or plain.
  QString axisText(int axis, bool html) const;
  // Axis choice: takes the hold the cube is in (`timeUp`: whatever it is).
  void confirmHold(bool timeUp);
  // Plan blind.
  void hideCube(bool timeUp);
  void reveal();
  // The solutions listed.
  void playSolution(std::size_t index);

  // --- analysis and results (eo_cross_page_results.cpp)
  void requestAnalysis();
  void analysisArrived(const Analysis &a);
  // Works the round's optima out now if they haven't arrived.
  void ensureAnalysed();
  void record();
  void showResult();
  QString verdictHtml() const;
  QString edgesResultHtml() const;
  QString axisResultHtml() const;
  QString solveResultHtml() const;
  QString breakdownHtml() const;
  QString holdsHtml() const;
  void save();

  // --- the list of kinds
  void fillTree();
  void refreshTree();
  void showSummary();
  void treeActivated(QTreeWidgetItem *item);
  void clearStats();
};

} // namespace vcube
