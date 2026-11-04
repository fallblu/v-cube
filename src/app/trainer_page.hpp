#pragma once

#include "app/app_settings.hpp"
#include "app/cube_view.hpp"
#include "app/widgets.hpp"
#include "io/library.hpp"
#include "train/case_logic.hpp"

#include <QFutureWatcher>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTreeWidget;
class QTreeWidgetItem;

namespace vcube {

// Drills the cases of an alg set: pick cases (randomly, weighted, recap,
// in order or by spaced repetition), solve them on a real cube with the
// space-bar timer or on the virtual cube, then see the algs and the grade.
class TrainerPage : public QWidget {
  Q_OBJECT
public:
  TrainerPage(AppSettings &settings, AlgLibrary &library, QWidget *parent = nullptr);

  bool handleKey(QKeyEvent *e, bool press);
  void applySettings();
  void reloadSets(); // the library changed
  std::string currentSetId() const { return setId_; }

signals:
  void editCaseRequested(const QString &setId, const QString &caseId);
  void message(const QString &text);

private:
  enum class Phase { NoCase, Waiting, Running, Revealed };

  struct Drill {
    std::string caseId;
    CubieCube state;
    ColourMap colours = kIdentityColourMap;
    QString scramble;
    bool isNew = false;
  };

  AppSettings &settings_;
  AlgLibrary &library_;
  std::string setId_;
  CasePicker picker_;
  std::mt19937_64 rng_;
  SolveTimer timer_;
  QTimer tick_;
  Phase phase_ = Phase::NoCase;
  std::optional<Drill> drill_;
  FaceletCube cube_;
  QFutureWatcher<GeneratedScramble> watcher_;
  int scrambleRequest_ = 0;

  // The attempt being graded, so a grade can be changed afterwards.
  bool hinted_ = false;
  bool failed_ = false;
  std::int64_t attemptMs_ = 0;
  SrsCard cardBefore_;
  Grade grade_ = Grade::Good;

  QComboBox *setBox_;
  QComboBox *modeBox_;
  QComboBox *focusBox_;
  QTreeWidget *tree_;
  QLabel *summary_;
  QSpinBox *learnCount_;
  QLabel *hintLabel_;
  QLabel *scrambleLabel_;
  TimeDisplay *display_;
  QLabel *statusLabel_;
  CubeView *cubeView_;
  QLabel *diagram_;
  QLabel *reveal_;
  QLabel *resultLabel_;
  QWidget *resultRow_;
  QPushButton *wrongButton_;
  std::array<QPushButton *, 4> gradeButtons_{};
  QCheckBox *virtualBox_;
  QCheckBox *showScrambleBox_;
  QComboBox *topBox_;
  QComboBox *frontBox_;
  QComboBox *neutralBox_;

  AlgSet *set();
  SetProgress &progress();
  QString setKey(const QString &name) const;
  bool isVirtual() const;

  void populateSets();
  void selectSet(const std::string &id);
  void buildTree();
  void updateCaseRow(QTreeWidgetItem *item);
  void updateAllRows();
  QTreeWidgetItem *itemFor(const std::string &caseId) const;
  std::vector<std::string> selectedCases() const;
  std::vector<std::string> pool() const;
  void saveSelection();
  void updateSummary();
  void showCaseMenu(const QPoint &pos);
  void learnNext();

  ColourMap baseColours() const;
  ColourMap drillColours();
  void loadOrientation();
  void saveOrientation();

  void nextCase();
  void retryCase();
  void startVirtualCase();
  void requestScramble();
  void scrambleReady();
  void onTick();
  void virtualMove(Move m);
  void finish(std::int64_t timeMs, bool failed);
  void regrade(std::optional<Grade> grade, std::optional<bool> failed);
  void showReveal();
  void showResult();
  void setPhase(Phase p);
  void updateStatus();
  void saveProgress();
};

} // namespace vcube
