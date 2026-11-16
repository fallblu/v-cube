#pragma once

#include "app/app_settings.hpp"
#include "app/cube_view.hpp"
#include "app/mode_page.hpp"
#include "app/page_parts.hpp"
#include "app/widgets.hpp"
#include "io/alglab_store.hpp"
#include "io/f2l_store.hpp"
#include "io/library.hpp"
#include "train/f2l.hpp"

#include <QTimer>

#include <atomic>
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

// F2L mode: the F2L practised a part at a time on the virtual cube, for a
// method's F2L (ZZ F2L - 1 as ZZ-CT leaves it by default; the whole ZZ
// F2L; CFOP's, without EO).
//
//   Pair solutions  one pair to solve; afterwards your moves beside the
//                   best solutions found for that very state
//   Pair choice     several pairs open: solve the one you would do first;
//                   the pairs are then ranked by the cheapest way on
//   Full F2L        the whole F2L, then a pair-by-pair comparison
//   Case explorer   the pair cases with their ranked solutions to step
//                   through; nothing is recorded
//
// Solutions are searched for the state on the cube (see f2l.hpp) and
// ranked by the Alg Lab's score profile. How each pair case went is kept
// (f2l.json), and Pair solutions brings the weak cases up more often.
class F2LPage : public ModePage {
  Q_OBJECT
public:
  enum class Drill { Pair, Choice, Full, Explore };
  enum class Phase { Solving, Done, Explore };

  F2LPage(AppSettings &settings, AlgLibrary &library, AlgLabStore &lab,
          const QString &dataDir, QWidget *parent = nullptr);
  ~F2LPage() override;

  bool handleKey(QKeyEvent *e, bool press) override;
  SettingsParts settingsUsed() const override {
    return SettingsPart::Cube | SettingsPart::Keys | SettingsPart::Neutrality |
           SettingsPart::Timer | SettingsPart::Thumbnails;
  }
  void applySettings() override;
  void activated() override;
  void deactivated() override;
  QWidget *sidePanel() const override { return side_; }

  // The alg sets changed: the reference set is read again.
  void reloadSets();

  // What the page shows, for tests and scripts.
  Drill drill() const { return drill_; }
  Phase phase() const { return phase_; }
  void selectDrill(Drill d);
  const F2LConfig &config() const { return config_; }
  void setConfig(const F2LConfig &config);
  const CubieCube &startState() const { return round_.start; }
  std::optional<Slot> target() const;
  // The round's solutions and rankings have been worked out (Full F2L:
  // once the solve is done).
  bool analysisReady() const { return round_.analysed; }
  // The feedback shown under the cube, as rich text.
  QString resultHtml() const;
  // The solutions shown (best first); empty until worked out.
  std::vector<RankedSolution> shownSolutions() const;
  const F2LStats &stats() const { return store_.stats; }
  // Shows a case in the Case explorer.
  void exploreCase(PairCase c);

protected:
  void focusModeChanged() override;

private:
  // What a background job worked out, for the round that asked.
  struct Analysis {
    int round = 0;
    std::optional<PairAnalysis> pair;
    std::vector<PairRank> ranks;
    std::vector<std::vector<PairRank>> segments; // Full: per segment
  };
  struct Round {
    int id = 0;
    CubieCube start;
    std::optional<Slot> target; // Pair and Explore
    PairCase pcase;
    Hold hold = kWcaHold;
    Alg moves;
    std::vector<std::int64_t> moveMs; // since the round began, per move
    std::int64_t began = 0;           // monotonicMs()
    std::int64_t ms = 0;              // once done
    bool hint = false;
    int hintMoves = 0;
    bool gaveUp = false;
    bool retry = false; // the same state again: not recorded
    bool recorded = false;
    std::optional<Slot> picked;       // Choice
    std::vector<F2LSegment> segments; // Full
    Analysis analysis;
    bool analysed = false;
  };

  AppSettings &settings_;
  AlgLibrary &library_;
  AlgLabStore &lab_;
  F2LStore store_;
  Rng rng_;
  F2LConfig config_;
  Drill drill_ = Drill::Pair;
  Phase phase_ = Phase::Solving;
  Round round_;
  int rounds_ = 0;
  bool active_ = false;
  std::shared_ptr<std::atomic<bool>> stop_;
  std::shared_ptr<const AlgSet> reference_;
  FaceletCube cube_;
  // The solution being stepped through once a round is done.
  std::size_t shown_ = 0;   // index into shownSolutions()
  std::size_t stepped_ = 0; // its moves done on the cube
  bool stepping_ = false;   // the cube shows the start plus those moves
  PairCase explored_;

  QWidget *side_;
  QLabel *summary_;
  QTreeWidget *tree_;
  QPushButton *clearButton_;
  QTabBar *tabs_;
  OptionBar *options_;
  QComboBox *methodBox_;
  QToolButton *leaveButton_;
  QWidget *leaveItem_;
  QToolButton *movesButton_;
  QComboBox *stageBox_;
  QWidget *stageItem_;
  QComboBox *slotBox_;
  QWidget *slotItem_;
  QComboBox *contextBox_;
  QWidget *contextItem_;
  QLabel *prompt_;
  CubeView *cubeView_;
  QLabel *display_;
  HintBar *hint_;
  QLabel *result_;
  QTimer clock_;

  // --- building and options (f2l_page.cpp)
  void buildSide();
  void buildOptions();
  void loadChoices();
  void saveChoices() const;
  void showOptions();
  void configChanged();
  void fillStages();

  // --- rounds
  void newRound(bool sameState = false);
  void startRound(const CubieCube &state, std::optional<Slot> target);
  void showCube();
  void markPair();
  void turn(Move m);
  void checkDone();
  void finish(bool gaveUp);
  void resetCube();
  void showHintMove();
  void showPrompt();
  void showKeys();
  void showClock();
  QString clockText(std::int64_t ms) const;
  bool initiallySolvedKept(const CubieCube &c) const;

  // --- stepping through a solution
  void selectSolution(std::size_t index, bool play);
  void step(int by);
  void playRest();

  // --- analysis and results (f2l_page_results.cpp)
  AnalysisOptions analysisOptions() const;
  void requestAnalysis();
  void analysisArrived(const Analysis &a);
  void record();
  void showResult();
  QString pairResultHtml() const;
  QString choiceResultHtml() const;
  QString fullResultHtml() const;
  QString exploreHtml() const;
  QString solutionsHtml(const PairAnalysis &pair, std::size_t first) const;
  double costOf(const Alg &moves) const;
  void save();

  // --- the case list
  void fillTree();
  void refreshTree();
  void showSummary();
  void selectInTree(PairCase c);
  void treeSelected();
  void clearStats();
};

} // namespace vcube
