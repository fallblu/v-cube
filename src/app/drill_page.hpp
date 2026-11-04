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

protected:
  QStringList columns() const override;
  void fillRow(QTreeWidgetItem *item, const std::string &caseId) override;
  void updateSummary() override;
  void startSet() override;
  void setReloaded() override;
  void startCase(const std::string &caseId) override;
  QString startCaseLabel() const override;
  void ticksChanged() override;
  void orientationChanged() override;

private:
  enum class Phase { NoCase, Waiting, Running, Revealed };
  enum class Pool { Learned, Ticked, Learning };

  struct Current {
    std::string caseId;
    CubieCube state; // the scrambled cube
    ColourMap colours = kIdentityColourMap;
    QString scramble;
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
  QLabel *hintLabel_;
  QLabel *scrambleLabel_;
  TimeDisplay *display_;
  QLabel *statusLabel_;
  QWidget *resultRow_;
  QLabel *resultLabel_;
  QPushButton *wrongButton_;
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
  void finish(std::int64_t timeMs);
  void setWrong(bool wrong);
  void showResult();
  void showReveal();
  void setPhase(Phase p);
};

} // namespace vcube
