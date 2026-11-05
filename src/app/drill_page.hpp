#pragma once

#include "app/case_page.hpp"
#include "app/widgets.hpp"

#include <QFutureWatcher>
#include <QTimer>

class QPushButton;

namespace vcube {

// Timed practice on a physical cube with the space-bar timer. Scrambles
// set up a case with random AUFs and the step's free pieces scrambled.
// After each case it shows the time and the algs with the AUFs the
// scramble needs. Drill attempts feed the case statistics and never change
// the spaced-repetition schedule.
class DrillPage : public CasePage {
  Q_OBJECT
public:
  DrillPage(AppSettings &settings, AlgLibrary &library,
            QWidget *parent = nullptr);

  bool handleKey(QKeyEvent *e, bool press) override;
  void applySettings() override;
  bool isBusy() const override;
  void activated() override;
  void deactivated() override;
  void windowDeactivated() override;

protected:
  QStringList columns() const override;
  std::vector<CaseStatusFilter> statusFilters() override;
  void fillRow(QTreeWidgetItem *item, const std::string &caseId) override;
  void updateSummary() override;
  void startSet() override;
  void setReloaded() override;
  void startCase(const std::string &caseId) override;
  QString startCaseLabel(const std::string &caseId) override;
  void ticksChanged() override;
  void neutralityChanged() override;

private:
  enum class Phase { NoCase, Waiting, Running, Revealed };
  enum class Pool { Learned, Ticked, Learning };

  struct Current {
    std::string caseId;
    CubieCube state; // the case as the solver sees it, held as `hold`
    Hold hold = kWcaHold;
    QString scramble; // applied holding white top, green front
  };

  CasePicker picker_;
  SolveTimer timer_;
  QTimer tick_;
  Phase phase_ = Phase::NoCase;
  std::optional<Current> current_;
  QFutureWatcher<GeneratedScramble> watcher_;
  int scrambleRequest_ = 0;
  std::int64_t attemptMs_ = 0;
  bool failed_ = false;

  QComboBox *poolBox_;
  QComboBox *orderBox_;
  bool controlsLocked_ = false;
  QLabel *scrambleLabel_;
  TimeDisplay *display_;
  QLabel *statusLabel_;
  QWidget *resultRow_;
  QLabel *resultLabel_;
  QPushButton *wrongButton_;
  QWidget *diagramBalance_;
  QLabel *diagram_;
  QLabel *reveal_;

  Pool poolChoice() const;
  std::vector<std::string> pool();
  void loadChoices();
  void nextCase();
  void retryCase();
  void requestScramble();
  void scrambleReady();
  void onTick();
  // Locks the set, case and order choices while a solve runs.
  void updateControls();
  void finish(std::int64_t timeMs);
  void setWrong(bool wrong);
  void showResult();
  void showReveal();
  // Shows the case diagram right of the time, or hides it (null pixmap).
  void setDiagram(const QPixmap &pixmap);
  void setPhase(Phase p);
};

} // namespace vcube
