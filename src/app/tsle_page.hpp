#pragma once

#include "app/app_settings.hpp"
#include "app/cube_view.hpp"
#include "app/mode_page.hpp"
#include "app/page_parts.hpp"
#include "app/widgets.hpp"
#include "io/tsle_store.hpp"
#include "train/tsle.hpp"

#include <QTimer>

#include <array>
#include <memory>
#include <optional>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;
class QTabBar;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace vcube {

// TSLE mode: ZZ-CT's last-slot step practised a trigger at a time on the
// virtual cube (see tsle.hpp). From any state every good trigger is
// known, each with the U turn it needs first, so that the one that suits
// the AUF at hand can be chosen rather than one trigger learned per case.
//
//   Guided intro   the cases one after another, fewest steps first: each
//                  good trigger of a new case is shown and done, then the
//                  best one is picked from random AUFs
//   Next trigger   a state at a random AUF: do the next step; it is
//                  judged as good or not, and against the best one
//   Full TSLE      the whole step, compared trigger by trigger
//   Case explorer  the cases with their good triggers; the list follows
//                  the cube as triggers are done
//
// Next trigger and Full TSLE are timed from the moment a state is shown,
// so each opens on a ready page: nothing is shown until Space.
//
// How each case went is kept (tsle.json): weak cases come up more often,
// and cases with more steps left open as the shallower ones are solid.
class TslePage : public ModePage {
  Q_OBJECT
public:
  enum class Drill { Intro, Next, Full, Explore };
  // Ready: a timed drill waits for Space before its first state is shown.
  enum class Phase { Solving, Done, Explore, Idle, Ready };

  TslePage(AppSettings &settings, const QString &dataDir,
           QWidget *parent = nullptr);
  ~TslePage() override;

  bool handleKey(QKeyEvent *e, bool press) override;
  SettingsParts settingsUsed() const override {
    return SettingsPart::Cube | SettingsPart::Keys | SettingsPart::Neutrality |
           SettingsPart::Timer | SettingsPart::Thumbnails;
  }
  void applySettings() override;
  void activated() override;
  void deactivated() override;
  QWidget *sidePanel() const override { return side_; }

  // What the page shows, for tests and scripts.
  Drill drill() const { return drill_; }
  Phase phase() const { return phase_; }
  void selectDrill(Drill d);
  Slot slot() const { return round_.slot; }
  // The state the round began in and the one the cube is in now (both
  // seen for the front-right slot); the latter nullopt mid-trigger.
  TsleState startState() const { return round_.start; }
  std::optional<TsleState> currentState() const;
  const TsleSolver &solver(Slot slot) const;
  const TsleProgress &progress() const { return store_.progress; }
  std::array<bool, kSlots> lastSlots() const { return slots_; }
  void setLastSlots(const std::array<bool, kSlots> &chosen);
  bool allCases() const { return allCases_; }
  void setAllCases(bool on);
  void exploreCase(TsleState c);
  // Guided intro: the case being introduced, how many of its good
  // triggers there are and the stage it is at (the triggers in turn, then
  // kIntroPicks picks).
  std::optional<TsleState> introCase() const { return introCase_; }
  int introTriggers() const { return static_cast<int>(introGood_.size()); }
  int introStage() const { return introStage_; }
  static constexpr int kIntroPicks = 2;
  // The moves the prompt asks for in a show-and-do stage.
  Alg introAlg() const;
  QString resultHtml() const;
  QString promptHtml() const;

protected:
  void focusModeChanged() override;

private:
  // One step of an attempt, and how it compares.
  struct Step {
    std::size_t begin = 0;
    std::size_t end = 0;
    TsleState before;
    TsleState after;
    int moves = 0;     // HTM, as simplified
    bool good = false; // fewer steps left after it (or the OCLL solved)
    int extra = 0;     // moves over the best way, for a good one
    bool hint = false;
  };
  struct Round {
    int id = 0;
    Slot slot = Slot::FR;
    TsleState start;
    CubieCube startCube;
    Hold hold = kWcaHold;
    Alg moves;
    std::vector<std::int64_t> moveMs;
    std::int64_t began = 0;
    std::int64_t ms = 0;
    std::vector<Step> steps;
    std::size_t stepBegin = 0;
    TsleState stepState;
    bool stepHint = false;
    bool gaveUp = false;
    bool retry = false;
    bool recorded = false;
  };

  AppSettings &settings_;
  TsleStore store_;
  Rng rng_;
  std::array<std::unique_ptr<TsleSolver>, kSlots> solvers_;
  std::array<bool, kSlots> slots_{true, true, false, false};
  bool allCases_ = false;
  // Space was pressed since the page, the drill or an option was chosen:
  // states follow one another without the ready page.
  bool ready_ = false;
  Drill drill_ = Drill::Intro;
  Phase phase_ = Phase::Idle;
  Round round_;
  int rounds_ = 0;
  bool active_ = false;
  FaceletCube cube_;
  // Case explorer: the case chosen, the U turn it is shown at, and the
  // state the cube is in (the trigger table is for that).
  TsleState explored_;
  int exploreAuf_ = 0;
  TsleState anchor_;
  // Guided intro.
  std::optional<TsleState> introCase_;
  std::vector<TsleOption> introGood_; // of the case as it is named
  int introStage_ = 0;
  QString introNote_;

  QWidget *side_;
  QLabel *summary_;
  QTreeWidget *tree_;
  QPushButton *clearButton_;
  QTabBar *tabs_;
  OptionBar *options_;
  QToolButton *slotsButton_;
  QComboBox *casesBox_;
  QWidget *casesItem_;
  QComboBox *slotBox_;
  QWidget *slotItem_;
  QPushButton *ocllButton_;
  QLabel *prompt_;
  CubeView *cubeView_;
  QLabel *display_;
  HintBar *hint_;
  QLabel *result_;
  QTimer clock_;

  // --- building and options (tsle_page.cpp)
  void buildSide();
  void buildOptions();
  void loadChoices();
  void saveChoices() const;
  void showOptions();
  void rebuildSolvers();
  Slot randomSlot();
  // A case of the pool (the introduced ones, or all), weak ones more
  // often; `deep`: one with two steps or more to go, if there is one.
  TsleState pickCase(bool deep);

  // --- rounds
  void newRound(bool sameState = false);
  void showReady();
  bool timedDrill() const {
    return drill_ == Drill::Next || drill_ == Drill::Full;
  }
  // `cube` (a cube in the state) and `retry` for the same cube again.
  void startRound(TsleState state, Slot slot, const CubieCube *cube = nullptr,
                  bool retry = false);
  void turn(Move m);
  void stepDone(TsleState after);
  Step judged(TsleState before, TsleState after, const Alg &moves) const;
  void finish(bool gaveUp);
  void resetCube();
  void showHint();
  void showPrompt();
  void showKeys();
  void showClock();
  QString clockText(std::int64_t ms) const;
  void playOption(std::size_t index);
  // The triggers listed now, good first: of the state the round began in,
  // or in the explorer of the state the cube is in.
  std::vector<TsleOption> listedOptions() const;
  TsleState listedState() const;

  // --- Guided intro
  void introBegin();
  void introSetUp();
  void introStepDone(const Step &step);
  void introSkip();

  // --- results and the list (tsle_page_results.cpp)
  void record();
  void showResult();
  QString optionsHtml(TsleState state, Slot slot) const;
  QString pathHtml(TsleState state, Slot slot) const;
  QString nextResultHtml() const;
  QString fullResultHtml() const;
  QString exploreHtml() const;
  QString introHtml() const;
  QString verdictHtml(const Step &step) const;
  QString caseText(TsleState state) const;
  void save();
  void fillTree();
  void refreshTree();
  void showSummary();
  void selectInTree(TsleState c);
  void treeSelected();
  void clearProgress();
  void editOcllAlgs();
};

} // namespace vcube
