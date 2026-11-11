#pragma once

#include "app/alglab_common.hpp"
#include "app/mode_page.hpp"
#include "train/alg_search.hpp"

#include <QFuture>
#include <QTimer>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QTreeWidget;

namespace vcube {

class LabTestPanel;

// Alg Lab: finds, compares and times algs for a case.
//
// The case comes from an alg set or from Alg Lab's own lab cases (a setup
// alg and what its algs must solve). The Search tab lists the case's
// candidate algs (the set's own, then those generated, pasted or typed)
// with their move counts in HTM, STM, QTM and ETM, an ergonomic score from
// the chosen profile and each alg's timed statistics, filtered by move set,
// rotations and length. The generator finds every alg of a move set from
// the shortest up to a limit, in the background, for the case or every
// case of its group or set. A candidate can be added to the case's set or
// made its main alg, and added to a test session, which the Test tab times
// (see LabTestPanel).
class AlgLabPage : public ModePage {
  Q_OBJECT
public:
  AlgLabPage(AppSettings &settings, AlgLibrary &library, AlgLabStore &store,
             QWidget *parent = nullptr);
  ~AlgLabPage() override;

  bool handleKey(QKeyEvent *e, bool press) override;
  SettingsParts settingsUsed() const override {
    return SettingsPart::Cube | SettingsPart::Keys | SettingsPart::Timer |
           SettingsPart::Theme;
  }
  void applySettings() override;
  bool isBusy() const override;
  void activated() override;
  void deactivated() override;
  void windowDeactivated() override;
  // The library's sets changed (in Library, say).
  void reloadSets();
  // Shows a set's case.
  void selectCase(const QString &setId, const QString &caseId);
  // Changes a set case's algs (as Library's undoable edit, when the window
  // sets it up); false if it couldn't be saved.
  using AlgEditor =
      std::function<bool(const QString &setId, const QString &caseId,
                         std::vector<std::string> algs, const QString &text)>;
  void setAlgEditor(AlgEditor editor) { algEditor_ = std::move(editor); }

signals:
  // An alg was added to a set or made its main alg.
  void setsChanged(const QString &setId);

protected:
  void focusModeChanged() override;

private:
  AppSettings &settings_;
  AlgLibrary &library_;
  AlgLabStore &store_;
  AlgEditor algEditor_;
  std::optional<ResolvedCase> case_;
  std::vector<CandidateRow> rows_;
  bool loading_ = false;

  // The generator, running in the background.
  struct Job;
  std::shared_ptr<Job> job_;
  QFuture<void> jobFuture_;
  QTimer refreshTimer_;
  MoveSet genSet_ = MoveSet::allFaces();
  MoveSet filterSet_;
  bool filterOn_ = false;

  QWidget *left_;
  QComboBox *sourceBox_;
  QLineEdit *caseFilter_;
  QTreeWidget *caseTree_;
  QWidget *labButtons_;
  QTabWidget *tabs_;
  QLabel *diagram_;
  QLabel *caseTitle_;
  QComboBox *statsCube_;
  QComboBox *statsWindow_;
  QComboBox *profileBox_;
  QComboBox *filterBox_;
  QCheckBox *noRotations_;
  QSpinBox *maxHtm_;
  QTableWidget *table_;
  QLineEdit *addEdit_;
  QComboBox *genBox_;
  QComboBox *lengthMode_;
  QSpinBox *extra_;
  QSpinBox *maxLength_;
  QSpinBox *maxResults_;
  QPushButton *runButton_;
  QPushButton *batchButton_;
  QPushButton *stopButton_;
  QLabel *genStatus_;
  LabTestPanel *test_;

  void saveStore();
  void fillSources();
  void fillCases();
  void caseChosen();
  void showCase(const std::optional<LabCaseKey> &key);
  void fillProfiles();
  void fillMoveSetBoxes();
  void filterChosen();
  void genChosen();
  void fillTable();
  std::vector<std::string> selectedAlgs() const;
  const ErgoProfile &profile() const;

  void newLabCase();
  void editLabCase();
  void deleteLabCase();
  void editProfile(bool copy);
  void addTyped();
  void pasteCandidates();
  void addToSet(bool makeMain);
  void addToSession();
  void removeCandidates();
  void clearTimes();
  void tableMenu(const QPoint &at);

  SearchOptions searchOptions() const;
  void runSearch(std::vector<ResolvedCase> cases);
  void runBatch(bool wholeSet);
  void stopSearch();
  void searchFinished();
  void updateGenControls();
};

} // namespace vcube
