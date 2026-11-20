#pragma once

#include "app/app_settings.hpp"
#include "app/cube_view.hpp"
#include "app/mode_page.hpp"
#include "app/page_parts.hpp"
#include "app/practice_html.hpp"
#include "app/practice_keys.hpp"
#include "app/practice_list.hpp"
#include "app/timed_driver.hpp"
#include "app/widgets.hpp"
#include "io/practice_store.hpp"
#include "train/drill_flow.hpp"
#include "train/learn_session.hpp"
#include "train/practice.hpp"
#include "train/practice_flow.hpp"
#include "train/solve_review.hpp"

#include <QFutureWatcher>
#include <QIcon>

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class QComboBox;
class QKeyEvent;
class QLabel;
class QScrollArea;
class QStackedWidget;
class QTabBar;
class QToolButton;

namespace vcube {

// What EO Cross, F2L and TSLE mode share: a step of the solve practised in
// three tabs that work the same way in each mode.
//
//   Learn       (F2L, TSLE) untimed, on the virtual cube: one unit of the
//               step (an F2L pair, a TSLE trigger) at a time, as cards.
//               Cards come in a few a day, a new one is shown before it is
//               tried, and each try grades its card (Good when solved
//               unaided and well, Again after a hint, giving up or a poor
//               solution; 1-4 change the grade). A mode may have the
//               user choose a new card's solutions before it is shown.
//   Inspection  (EO Cross) the step planned in an inspection time and
//               done blind: the clock runs while the cube is looked at
//               (rotations only), the cube hides at the first face turn,
//               and Space shows how it went. The first state of a sitting
//               waits for Space, as nothing timed starts by itself.
//   Untimed     the whole step on the virtual cube, for efficiency: a line
//               on each unit as it is done, then the verdict.
//   Timed       the whole step against the clock, exactly as Drill times a
//               case: on the virtual cube a tap of Space arms the timer,
//               the first face turn starts it and it stops when the step
//               is done; on a physical cube a scramble is shown, Space
//               held and let go starts it and any key stops it. The result
//               is about the time; how efficient it was is behind Tab.
//
// States are what a solve gives: random ones, never picked by how the last
// tries went. After a solve there is one line (the verdict) and the best
// solution; Tab opens the detail. Enter plays the best solution, Space
// goes on, Esc gives the same state again, unrecorded.
//
// A mode says what its tabs, states, cases and solutions are (the
// virtuals below); the page does the rest.
class PracticePage : public ModePage, private TimedHost {
  Q_OBJECT
public:
  using Tab = PracticeTab;
  // The phases of the tabs that aren't timed (the Timed tab's are
  // DrillFlow's).
  using Phase = PracticePhase;
  // "learn", "inspection", "untimed", "timed".
  static QString tabId(Tab t);
  static QString tabName(Tab t);

  ~PracticePage() override;

  bool handleKey(QKeyEvent *e, bool press) override;
  SettingsParts settingsUsed() const override {
    return SettingsPart::Cube | SettingsPart::Keys | SettingsPart::Neutrality |
           SettingsPart::Timer | SettingsPart::Thumbnails | SettingsPart::Srs |
           SettingsPart::Drill;
  }
  void applySettings() override;
  bool isBusy() const override;
  void activated() override;
  void deactivated() override;
  void windowDeactivated() override;
  QWidget *sidePanel() const override { return list_; }

  // What the page shows, for tests and scripts.
  Tab tab() const { return tab_; }
  void selectTab(Tab t);
  Phase phase() const { return phase_; }
  DrillFlow::Phase timedPhase() const { return flow_.phase(); }
  // The mode's tabs, in order.
  virtual std::vector<Tab> tabs() const = 0;
  // Timed's cube.
  CubeKind cubeKind() const {
    return physical_ ? CubeKind::Physical : CubeKind::Virtual;
  }
  void setPhysical(bool on);
  const FaceletCube &cube() const { return cube_; }
  // The state the solve started from, as shown.
  const FaceletCube &startCube() const { return item_.start; }
  // The card being tried ("" when it isn't a card's try), and whether the
  // try grades it.
  const std::string &currentCard() const { return item_.card; }
  bool graded() const { return item_.graded; }
  std::optional<Grade> grade() const { return item_.grade; }
  // The best solution, from the state the solve started from.
  Alg bestSolution() const;
  // The new case's solution, in Demo.
  Alg shownDemo() const;
  QString promptHtml() const;
  QString verdictHtml() const;
  QString detailHtml() const;
  bool detailShown() const { return detailOpen_; }
  bool cubeHidden() const;
  // The lines said on the units done so far (Untimed).
  QStringList progressLines() const { return progress_; }
  // Inspection: the time looked before the first face turn (0 while the
  // clock runs or with no inspection).
  std::int64_t inspectedMs() const { return item_.inspectMs; }
  bool inspecting() const { return item_.inspecting; }

  // The scramble of a physical attempt ("" until it is worked out).
  QString scrambleText() const { return item_.scramble; }
  // The mode's solutions for the state are worked out.
  virtual bool analysisReady() const { return true; }
  const PracticeData &data() const { return store_.data; }
  // The Learn tab's cards (where Today sends).
  void showCards();
  // Sets a card's case up to practise, not graded.
  void practiseCard(const std::string &card);

protected:
  // `store` is the mode's file (AppData's). The subclass calls build() at
  // the end of its constructor.
  PracticePage(AppSettings &settings, PracticeStore &store, QWidget *parent);
  void build();

  // --- what a mode provides --------------------------------------------------

  // "eocross", "f2l", "tsle": names its saved choices and its cube view.
  virtual QString modeId() const = 0;
  // "F2L", as in "Whole F2L".
  virtual QString stepName() const = 0;
  // The mode's own options, added to the bar before the page's.
  virtual void addOptions(OptionBar *) {}
  // Buttons at the end of the bar.
  virtual void addButtons(OptionBar *) {}
  // Shows and hides them for the tab.
  virtual void showOptions() {}

  // The cards in the order they are introduced; empty for a mode without.
  virtual std::vector<std::string> cardPool() const { return {}; }

  // A state to solve.
  struct Setup {
    FaceletCube cube; // as it is shown, in `hold`'s colours
    // For a physical cube's scramble: turned to `hold`, the scrambled cube
    // shows `state` (as generateScramble() has it).
    CubieCube state;
    Hold hold = kWcaHold;
    std::string group; // what the try is kept under, when it isn't a card's
  };
  // A new state: the whole step, or with a `card` given one unit of it,
  // the card's case. nullopt when it can't be set up.
  virtual std::optional<Setup> setUp(const std::string &card) = 0;
  // The state is on the cube from the start (again, after Esc): whatever
  // the mode tracks of the solve starts again.
  virtual void restarted() {}
  // A turn was made in a solve: whether the item is now done.
  virtual bool turned() = 0;
  // The cube changed (a turn, a reset): e.g. to mark stickers.
  virtual void cubeChanged() {}

  using Outcome = PracticeOutcome;
  // Asked once the solve is over, and again when the mode says its
  // analysis arrived.
  virtual Outcome judge() = 0;
  // The full account, behind Tab.
  virtual QString detail() { return {}; }
  // The verdict of this item must wait for analysisArrived().
  virtual bool waitsForAnalysis() const { return false; }
  // A hint for the solve under way; empty when the mode has none to give.
  virtual QString hint() { return {}; }
  virtual bool hasHints() const { return false; }
  // A new case: its solution to show, and what is said about it.
  virtual Alg demoAlg() { return {}; }
  virtual QString demoText() { return {}; }
  // What to do, in a few words ("Solve the ringed pair"), and under it
  // ("last slot: FR").
  virtual QString task() const = 0;
  virtual QString taskNote() const { return {}; }
  // Hide the cube from the first face turn of a solve; Space shows it
  // again, and only then is the solve judged (Inspection).
  bool blind() const { return tab() == Tab::Inspection; }
  // Inspection's limit; 0 for none.
  virtual std::int64_t inspectionLimitMs() const { return 0; }

  // Choosing a card's solutions. The item's card needs them before it is
  // tried (asked when it comes up); the panel shown meanwhile in place of
  // the details; 1-9 pressed; Enter (false: not yet, say why yourself);
  // Esc while changing them after a try.
  virtual bool needsChoice() const { return false; }
  virtual QWidget *choicePanel() { return nullptr; }
  virtual void startChoosing() {}
  virtual void choiceKey(int) {}
  virtual bool confirmChoice() { return true; }
  virtual void cancelChoice() {}
  virtual QString choosingText() const { return {}; }
  // Keys of the mode's own once a try is done, and their hints.
  virtual bool doneKey(QKeyEvent *) { return false; }
  virtual QList<KeyHint> doneKeys() const { return {}; }

  // The parts of a solve, for the Timed tab's splits: each ends after a
  // move of the solve (an index into item().moves, exclusive).
  using Split = PracticeSplit;
  virtual std::vector<Split> splits() { return {}; }

  // The lists.
  using ListRow = PracticeList::Row;
  // The cards (keys are card ids).
  virtual std::vector<ListRow> cardRows() { return {}; }
  // The groups the Untimed (and Inspection) tab's solves are kept under:
  // by default the whole step, as kWholeGroup.
  static constexpr const char *kWholeGroup = "whole";
  virtual std::vector<ListRow> untimedGroups() {
    return {{kWholeGroup, tr("Whole %1").arg(stepName()), {}, {}}};
  }
  // The groups of timed attempts; by default those of Untimed.
  virtual std::vector<ListRow> timedGroups() { return untimedGroups(); }
  virtual QString listHelp() const { return {}; }
  // A group's row was double-clicked.
  virtual void groupActivated(const std::string &) {}
  virtual QString cardName(const std::string &card) const {
    return QString::fromStdString(card);
  }

  // --- for the modes ---------------------------------------------------------

  struct Item {
    int id = 0;
    std::string card;    // empty for a solve of the whole step
    bool isNew = false;  // a card's first sight
    bool graded = false; // the try grades the card
    std::string group;
    FaceletCube start;
    CubieCube scrambleState;
    Hold hold = kWcaHold; // drawn in its colours and scrambled for it
    QString scramble;
    Alg moves;                        // of the solve, rotations too
    std::vector<std::int64_t> moveMs; // since the solve began, per move
    std::int64_t began = 0;           // monotonicMs(); Inspection's start
    std::int64_t ms = 0;              // Timed: the time
    std::optional<double> meanBefore; // Timed: the group's mean before it
    std::int64_t inspectMs = 0;       // Inspection: the time looked
    bool inspecting = false;          // ...the clock runs
    bool done = false;                // the cube shows the item done
    bool hint = false;
    bool gaveUp = false;
    bool retry = false; // the same state again: not recorded
    bool recorded = false;
    bool hidden = false; // blind: the cube is out of sight
    std::optional<Outcome> outcome;
    std::optional<Grade> grade;
    bool gradedByHand = false; // with 1-4, not as judged
    SrsCard cardBefore;        // as it was before the grade
  };
  const Item &item() const { return item_; }
  AppSettings &settings() { return settings_; }
  const AppSettings &settings() const { return settings_; }
  Rng &rng() { return rng_; }
  PracticeData &editData() { return store_.data; }
  CubeView *cubeView() const { return cubeView_; }
  // The item is a solve whose outcome is up (either tab).
  bool finished() const;
  // An option changed: a new state, in the tab that is open.
  void optionChanged();
  // The mode's analysis of the item `id` arrived: a verdict waiting for it
  // is given, one that is up is given again.
  void analysisArrived(int id);
  // The lines on the units done so far changed (Untimed).
  void setProgress(const QStringList &lines);
  // Changes the solutions of the card just tried (from Done).
  void chooseAgain();
  void showChoosing();
  void save();
  void fillList();
  void refreshList();
  // The colour of text that matters less.
  QString mutedColour() const;
  // The link that plays `alg` from the start when clicked in the detail.
  static QString playLink(const Alg &alg, const QString &text);
  // The last slots a mode leaves (F2L's, TSLE's): kept as `name`, FR and FL
  // at first (and when none is kept).
  using Slots = std::array<bool, kSlots>;
  Slots loadSlots(const char *name) const;
  void saveSlots(const char *name, const Slots &picked) const;
  // The "Last slot:" picker of the slots `current`, made as `button`: a
  // slot ticked or unticked calls `chosen` with them as they would be, one
  // slot at least.
  QWidget *addSlotPicker(OptionBar *bar, QToolButton *&button,
                         const Slots &current,
                         const std::function<void(const Slots &)> &chosen);
  // Ticks the slots on the picker and names them on its button.
  static void showSlots(QToolButton *button, const Slots &picked);

private:
  AppSettings &settings_;
  PracticeStore &store_;
  Rng rng_;
  Tab tab_ = Tab::Learn;
  bool physical_ = false;
  bool readyPending_ = true; // Inspection: the next state waits for Space
  Phase afterChoice_ = Phase::Solving;
  QStringList progress_;
  Phase phase_ = Phase::Idle;
  TimedDriver driver_{*this, settings_.drill};
  DrillFlow &flow_ = driver_.flow();
  Item item_;
  int items_ = 0;
  bool detailOpen_ = false;
  bool waiting_ = false;   // Done, the verdict waiting for the analysis
  bool demoShown_ = false; // the picture is ahead of the cube
  FaceletCube cube_;
  std::string lastCard_;
  LearnUndoStack undo_;
  QString idleText_;
  QString note_; // said once under the verdict (a cancelled solve, ...)
  QFutureWatcher<GeneratedScramble> scrambles_;

  PracticeList *list_ = nullptr;
  QTabBar *tabs_ = nullptr;
  OptionBar *options_ = nullptr;
  QComboBox *cubeBox_ = nullptr;
  QWidget *cubeItem_ = nullptr;
  QWidget *viewItem_ = nullptr;
  QLabel *prompt_ = nullptr;
  QLabel *scrambleLabel_ = nullptr;
  QStackedWidget *cubeStack_ = nullptr;
  CubeView *cubeView_ = nullptr;
  QLabel *hiddenLabel_ = nullptr;
  TimeDisplay *display_ = nullptr;
  HintBar *hint_ = nullptr;
  QLabel *verdict_ = nullptr;
  QLabel *requestedHint_ = nullptr; // Tab's hint, under the cube
  QLabel *progressLabel_ = nullptr;
  QLabel *detail_ = nullptr;
  QScrollArea *detailScroll_ = nullptr;
  QScrollArea *choiceScroll_ = nullptr;
  int demoPlays_ = 0; // how many demos were played, to tell them apart

  bool hasCards() const { return !cardPool().empty(); }
  int tabIndex(Tab t) const;
  void buildSide();
  void showBar();
  void loadChoices();
  void saveChoices() const;

  // --- items
  void next(LearnAhead ahead = LearnAhead::WhenDue);
  void nextLearn(LearnAhead ahead);
  // Puts a card's case on the cube to try (a new one is shown first);
  // false if it can't be set up.
  bool startCard(const std::string &card, bool isNew, bool graded);
  // Ctrl+Z: takes back the last grade (or a new card shown), and the card
  // comes up again.
  void undoLast();
  void nextTimed();
  // Puts a set-up state on the cube as a new item.
  void begin(const Setup &setup);
  void sameAgain();
  // Nothing to solve: Idle, saying `text`, or Ready (the Inspection tab's
  // first state waits for Space).
  void showIdle(const QString &text, Phase phase = Phase::Idle);
  void showInspection();
  void resetCube();
  void showCubeKind();
  void applyTurn(Move m);
  void showHidden(bool hidden);
  void scrambleReady();

  // --- the tabs that aren't timed
  // Where the tab is, as its keys and their hints are read.
  PracticeFlow::State flowState();
  // Does what the flow asked after a key (`e`, for the mode's own keys).
  bool perform(const PracticeFlow::Step &step, QKeyEvent *e);
  void enterChoice(Phase after);
  // Leaves the choice of solutions, keeping what was ticked or not.
  enum class Choice { Confirmed, Cancelled };
  void leaveChoice(Choice choice);
  void learnTurn(Move m);
  // How a try on a tab that isn't timed ended.
  enum class Try { Finished, GaveUp };
  void finishLearn(Try how);
  void conclude();
  void introduced();
  void regrade(Grade g);
  void playDemo();
  void endDemo();

  // --- Timed
  bool handleTimedKey(QKeyEvent *e, bool press);
  // What the Timed tab's flow asks for (see TimedHost).
  void timedNext(bool) override { next(); }
  void timedRetry() override { sameAgain(); }
  void timedResetCube() override { resetCube(); }
  void timedRotate(Move m) override;
  void timedStarted(std::int64_t now) override { item_.began = now; }
  std::optional<DrillFlow::Turn> timedTurn(Move m, std::int64_t now) override;
  bool timedStepDone() override { return item_.done; }
  void timedFinish(std::int64_t ms, DrillFlow::Result result) override;
  // No practice here: Tab shows how efficient the solve was.
  void timedPractise(bool) override { toggleDetail(); }
  void timedToggleWrong() override;
  void timedDelete() override;
  void timedFinishAnimations() override { cubeView_->finishAnimations(); }
  void timedShowKeys() override { showKeys(); }
  void timedSay(const QString &text) override;
  void timedCancelled(const QString &why) override;
  void deleteLearnTry(); // Delete on a finished Untimed or Inspection try
  // The clock changes during a hold of Space or a solve on the Timed tab,
  // and while the inspection clock runs.
  bool clockNeeded() const override;
  void tick() override;
  void keyDone() override { updateTick(); }
  std::string timedGroupKey() const;

  // --- what is shown
  void playBest();
  void play(const Alg &alg);
  void toggleDetail();
  void showAll();
  void showPrompt();
  void showKeys();
  void showVerdict();
  void showSummary();
  QString gradeLine() const;
  QString timedLine() const;
  QString inspectionLine() const;
};

} // namespace vcube
