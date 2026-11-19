#pragma once

#include "app/app_settings.hpp"
#include "app/cube_view.hpp"
#include "app/mode_page.hpp"
#include "app/page_parts.hpp"
#include "app/widgets.hpp"
#include "io/practice_store.hpp"
#include "train/drill_flow.hpp"
#include "train/practice.hpp"

#include <QFutureWatcher>
#include <QIcon>
#include <QTimer>

#include <optional>
#include <string>
#include <vector>

class QComboBox;
class QKeyEvent;
class QLabel;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTabBar;
class QTreeWidget;
class QTreeWidgetItem;

namespace vcube {

// What EO Cross, F2L and TSLE mode share: a step of the solve practised in
// two tabs that work the same way in each mode.
//
//   Learn  untimed, on the virtual cube. Scope picks what comes up: one
//          unit of the step (an F2L pair, a TSLE trigger) or the whole
//          step. A unit is a card where the mode has cases: cards come in
//          a few a day, a new one is shown before it is tried, and each
//          try grades its card (Good when solved unaided and well, Again
//          after a hint, giving up or a poor solution; 1-4 change the
//          grade). Solves of the whole step are practice: kept, not graded.
//   Timed  the whole step against the clock, exactly as Drill times a
//          case: on the virtual cube a tap of Space arms the timer, the
//          first face turn starts it and it stops when the step is done;
//          on a physical cube a scramble is shown, Space held and let go
//          starts it and any key stops it.
//
// States are what a solve gives: random ones, never picked by how the last
// tries went. After a solve there is one line (the verdict) and the best
// solution; Tab opens the detail. Enter plays the best solution, Space
// goes on, Esc gives the same state again, unrecorded.
//
// A mode says what its states, cases and solutions are (the virtuals
// below); the page does the rest.
class PracticePage : public ModePage {
  Q_OBJECT
public:
  enum class Tab { Learn, Timed };
  // Learn's phases (the Timed tab's are DrillFlow's).
  //   Idle     no card is due; Space looks again
  //   Demo     a new case: Enter shows its solution, then it is done
  //   Solving  Tab a hint, Backspace gives up, Esc starts again
  //   Done     the verdict is up
  enum class Phase { Idle, Demo, Solving, Done };

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
  QWidget *sidePanel() const override { return side_; }

  // What the page shows, for tests and scripts.
  Tab tab() const { return tab_; }
  void selectTab(Tab t);
  Phase phase() const { return phase_; }
  DrillFlow::Phase timedPhase() const { return flow_.phase(); }
  // Learn's scope: 0 one unit, 1 the whole step.
  int scope() const { return scope_; }
  void setScope(int scope);
  // Timed's cube.
  bool physical() const { return physical_; }
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
  // The scramble of a physical attempt ("" until it is worked out).
  QString scrambleText() const { return item_.scramble; }
  // The mode's solutions for the state are worked out.
  virtual bool analysisReady() const { return true; }
  const PracticeData &data() const { return store_.data; }
  // Learn's cards, one unit at a time (where Today sends).
  void showCards();
  // Sets a card's case up to practise, not graded.
  void practiseCard(const std::string &card);

protected:
  // `file` is the mode's file in `dataDir` ("f2l.json"). The subclass
  // calls build() at the end of its constructor.
  PracticePage(AppSettings &settings, const QString &dataDir,
               const QString &file, QWidget *parent);
  void build();
  void focusModeChanged() override;

  // --- what a mode provides --------------------------------------------------

  // "eocross", "f2l", "tsle": names its saved choices and its cube view.
  virtual QString modeId() const = 0;
  // "F2L", as in "Whole F2L".
  virtual QString stepName() const = 0;
  // What one unit of the step is called in Learn's scope picker ("One
  // pair"); empty when the step has no units (then there is no picker and
  // every solve is of the whole step).
  virtual QString unitName() const { return {}; }
  // The mode's own options, added to the bar before the page's.
  virtual void addOptions(OptionBar *bar) { (void)bar; }
  // Buttons at the end of the bar.
  virtual void addButtons(OptionBar *bar) { (void)bar; }
  // Shows and hides them for the tab and scope.
  virtual void showOptions() {}

  // The cards in the order they are introduced; empty for a mode without.
  virtual std::vector<std::string> cardPool() const { return {}; }

  // A state to solve.
  struct Setup {
    FaceletCube cube;        // as it is shown
    Hold colours = kWcaHold; // the hold whose colours it is drawn in
    // For a physical cube's scramble: turned to `hold`, the scrambled cube
    // shows `state` (as generateScramble() has it).
    CubieCube state;
    Hold hold = kWcaHold;
    std::string group; // what the try is kept under, when it isn't a card's
  };
  // A new state: the whole step, or one unit, which is `card`'s case when
  // one is given. nullopt when it can't be set up.
  virtual std::optional<Setup> setUp(bool whole, const std::string &card) = 0;
  // The state is on the cube from the start (again, after Esc): whatever
  // the mode tracks of the solve starts again.
  virtual void restarted() {}
  // A turn was made in a solve: whether the item is now done.
  virtual bool turned() = 0;
  // The cube changed (a turn, a reset): e.g. to mark stickers.
  virtual void cubeChanged() {}

  // How a solve went.
  struct Outcome {
    QString verdict;  // one line of rich text
    Alg best;         // the best solution from the start; empty if none
    QString bestText; // how it is written (default: the alg)
    int moves = 0;
    int bestMoves = 0;
    double extra = -1.0;
    bool good = false; // a card's try: solved well
  };
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
  // Hide the cube from the first face turn of a Learn solve; Space shows
  // it again, and only then is the solve judged.
  virtual bool blind() const { return false; }

  // The lists.
  struct ListRow {
    std::string key;
    QString name;
    QString section; // rows are grouped under these, in order
    QIcon icon;
  };
  // The cards (keys are card ids).
  virtual std::vector<ListRow> cardRows() { return {}; }
  // The groups Learn's other solves are kept under.
  virtual std::vector<ListRow> learnGroups() = 0;
  // The groups of timed attempts.
  virtual std::vector<ListRow> timedGroups() = 0;
  virtual QString listHelp() const { return {}; }
  // A group's row was double-clicked.
  virtual void groupActivated(const std::string &key) { (void)key; }
  virtual QString cardName(const std::string &card) const {
    return QString::fromStdString(card);
  }

  // --- for the modes ---------------------------------------------------------

  struct Item {
    int id = 0;
    bool whole = true;
    std::string card;
    bool isNew = false;  // a card's first sight
    bool graded = false; // the try grades the card
    std::string group;
    FaceletCube start;
    Hold colours = kWcaHold;
    CubieCube scrambleState;
    Hold scrambleHold = kWcaHold;
    QString scramble;
    Alg moves;                        // of the solve, rotations too
    std::vector<std::int64_t> moveMs; // since the solve began, per move
    std::int64_t began = 0;           // monotonicMs()
    std::int64_t ms = 0;              // Timed: the time
    bool done = false;                // the cube shows the item done
    bool hint = false;
    bool gaveUp = false;
    bool retry = false; // the same state again: not recorded
    bool recorded = false;
    bool hidden = false; // blind: the cube is out of sight
    std::optional<Outcome> outcome;
    std::optional<Grade> grade;
    SrsCard cardBefore; // as it was before the grade
  };
  const Item &item() const { return item_; }
  AppSettings &settings() { return settings_; }
  const AppSettings &settings() const { return settings_; }
  Rng &rng() { return rng_; }
  PracticeData &editData() { return store_.data; }
  CubeView *cubeView() const { return cubeView_; }
  bool active() const { return active_; }
  // The item is a solve whose outcome is up (either tab).
  bool finished() const;
  // An option changed: a new state, in the tab that is open.
  void optionChanged();
  // The mode's analysis of the item `id` arrived: a verdict waiting for it
  // is given, one that is up is given again.
  void analysisArrived(int id);
  void save();
  void fillList();
  void refreshList();
  // "12.34".
  QString clockText(std::int64_t ms) const;
  // The colour of text that matters less.
  QString mutedColour() const;
  // The link that plays `alg` from the start when clicked in the detail.
  static QString playLink(const Alg &alg, const QString &text);

private:
  AppSettings &settings_;
  PracticeStore store_;
  Rng rng_;
  Tab tab_ = Tab::Learn;
  int scope_ = 0;
  bool physical_ = false;
  Phase phase_ = Phase::Idle;
  DrillFlow flow_;
  Item item_;
  int items_ = 0;
  bool active_ = false;
  bool built_ = false;
  bool detailOpen_ = false;
  bool waiting_ = false;   // Done, the verdict waiting for the analysis
  bool demoShown_ = false; // the picture is ahead of the cube
  FaceletCube cube_;
  std::string lastCard_;
  QString idleText_;
  QString note_; // said once under the verdict (a cancelled solve, ...)
  std::int64_t sittingStart_ = 0;
  QFutureWatcher<GeneratedScramble> scrambles_;
  int scrambleRequest_ = 0;

  QWidget *side_ = nullptr;
  QLabel *summary_ = nullptr;
  QTreeWidget *tree_ = nullptr;
  QLabel *listHelp_ = nullptr;
  QPushButton *clearButton_ = nullptr;
  QTabBar *tabs_ = nullptr;
  OptionBar *options_ = nullptr;
  QComboBox *scopeBox_ = nullptr;
  QWidget *scopeItem_ = nullptr;
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
  QLabel *detail_ = nullptr;
  QScrollArea *detailScroll_ = nullptr;
  QTimer tick_;

  bool hasCards() const { return !cardPool().empty(); }
  // Learn's solves are of the whole step.
  bool learnWhole() const;
  void buildSide();
  void showBar();
  void loadChoices();
  void saveChoices() const;
  DrillFlow::Config flowConfig() const;

  // --- items
  void next(bool early = false);
  void nextLearn(bool early);
  void nextTimed();
  // Puts a set-up state on the cube as a new item.
  void begin(const Setup &setup, bool whole);
  void sameAgain();
  void showIdle(const QString &text);
  void resetCube();
  void showCubeKind();
  void applyTurn(Move m);
  void showHidden(bool hidden);
  void requestScramble();
  void scrambleReady();

  // --- Learn
  bool handleLearnKey(QKeyEvent *e);
  void learnTurn(Move m);
  void finishLearn(bool gaveUp);
  void conclude();
  void introduced();
  void regrade(Grade g);
  void playDemo();
  void endDemo();

  // --- Timed
  bool handleTimedKey(QKeyEvent *e, bool press);
  DrillFlow::KeyEvent flowKey(const QKeyEvent *e, bool press) const;
  void perform(const DrillFlow::Step &step, std::int64_t now);
  void runningTurn(Move m, std::int64_t now);
  void finishTimed(std::int64_t ms, bool failed, bool gaveUp = false);
  void cancelTimed(const QString &why);
  void toggleDnf();
  void deleteTry();
  void onTick();
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
  void rowActivated(QTreeWidgetItem *row);
  void clearProgress();
};

// "in 10 min", "in 4 d", "tomorrow".
QString dueText(std::int64_t due, std::int64_t now, std::int64_t dayEnd);

} // namespace vcube
