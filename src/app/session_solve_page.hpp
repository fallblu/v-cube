#pragma once

#include "app/app_settings.hpp"
#include "app/eo_cross_check.hpp"
#include "app/mode_page.hpp"
#include "app/scramble_source.hpp"
#include "app/session_panel.hpp"
#include "train/move_count.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;

namespace vcube {

class CubeView;
class SolveStage;

// What the modes that solve scrambles in sessions share (TimerPage, and
// AttemptPage for Untimed and Move Count): the session panel on the left,
// with the statistics, the attempts and the chart, and the SolveStage on
// the right, with the scramble type, the session's cube (virtual or
// physical, fixed once the session has attempts), the scramble and Skip.
// The subclass holds the sessions and does what the panel asks of them.
class SessionSolvePage : public ModePage {
  Q_OBJECT
public:
  SettingsParts settingsUsed() const override {
    return SettingsPart::Cube | SettingsPart::Keys | SettingsPart::Neutrality |
           SettingsPart::Timer | SettingsPart::Counts;
  }
  void applySettings() override;
  QWidget *sidePanel() const override { return panel_; }
  bool hasModeMenu() const override { return true; }
  void fillModeMenu(QMenu &menu) override { panel_->fillMenu(menu); }
  // The alg sets changed; an alg set scramble is made again only when its
  // own set (`changedSet`) changed.
  void refreshScrambleTypes(const QString &changedSet);

protected:
  // `mode` names the page's settings ("timed", "untimed", "movecount");
  // the panel starts `panelWidth` wide. The subclass adds its own widgets,
  // the chart to the stage (stage_->setChart(chart_)) and the type box
  // (setTypeBox()), and then calls start().
  SessionSolvePage(AppSettings &settings, AlgLibrary &library,
                   const QString &mode, int panelWidth, QWidget *parent);

  void focusModeChanged() override;

  // --- for the subclass to build the page ---
  // Adds "Scramble:", the type box (and `extra` after it, if any), the
  // cube box, the view picker and Skip to the option bar.
  void setTypeBox(ScrambleTypeBox *box, QWidget *extra = nullptr);
  // Shows session `index` and its scramble, and starts the feed.
  void start(std::size_t index);

  // --- for the subclass ---
  // Shows the cube, view and net the session's cube needs, its columns
  // and the chart.
  virtual void showCubeKind();
  // The chart, or the line of statistics in focus mode (neither when only
  // scrambles are kept).
  void showChart();
  // Locks the controls that would change the scramble or session while
  // the page is busy, and shows the undo button if there is a delete to
  // undo (`undo`).
  void enableControls(bool undo);
  void saveAndRefresh() {
    save();
    refreshAttempts();
  }
  // Shows the statistics of `solves` (`count` attempts) in the panel and
  // the focus-mode line: rounded to `unit`, each value as `format(id, v)`
  // (id "single", "ao5", ... or "mean") and "single" named `single`.
  void showStats(
      std::span<const Solve> solves, std::int64_t unit, const QString &single,
      const std::function<QString(const std::string &, StatValue)> &format,
      std::size_t count);

  // --- what the subclass has: the sessions and its own flow ---
  virtual bool onVirtualCube() const = 0;
  // The current session's.
  virtual std::size_t attemptCount() const = 0;
  virtual std::vector<AttemptColumn> columns() const = 0;
  // Only the scrambles are kept, without statistics.
  virtual bool scramblesOnly() const { return false; }
  // More statistics below the table of them.
  virtual void addStats(QString &) {}
  // The flow follows the settings and the session's cube.
  virtual void configureFlow() = 0;
  virtual void showScramble() = 0;
  virtual void skip() = 0;
  // The session, which has no attempts, was set to `kind` of cube.
  virtual void cubeChosen(CubeKind kind) = 0;
  // The type box chose `type`, which the feed is about to deal.
  virtual void typeChosen(const QString &type) = 0;
  // The feed is about to deal the scramble type again (its set changed).
  virtual void scrambleDropped() = 0;
  // The settings were applied; the attempts are shown again after.
  virtual void settingsApplied() {}
  virtual void refreshAttempts() = 0;
  virtual void save() = 0;
  // What the panel asks for.
  virtual void refreshSessionMenu() = 0;
  virtual void selectSession(std::size_t index) = 0;
  virtual void addSession() = 0;
  virtual void renameSession() = 0;
  virtual void deleteSession() = 0;
  virtual void deleteAttempt(int index) = 0;
  virtual void undoDelete() = 0;
  virtual void showAttemptMenu(int index, const QPoint &globalPos) = 0;
  // Attempt `index`, one of the session's, can be reviewed.
  virtual bool canReview(int index) const = 0;
  virtual void review(int index) = 0;
  virtual void showDetails(int index) = 0;
  // The solves behind a statistic, from a link in the statistics.
  virtual void showStat(const QString &link) = 0;

  AppSettings &settings_;
  AlgLibrary &library_;
  ScrambleFeed feed_;
  VirtualAttempt attempt_;
  // After a virtual solve: the optimal EO Cross of its scramble.
  EoCrossCheck eoCheck_;

  SessionPanel *panel_;
  SolveStage *stage_;
  ScrambleTypeBox *typeBox_ = nullptr;
  // The algs of an alg set scramble, if the page offers them.
  ScrambleAlgs *algs_ = nullptr;
  QComboBox *cubeBox_;
  QPushButton *skipButton_;
  QLabel *scrambleLabel_;
  NetPreview *net_;
  CubeView *cubeView_;
  TimeDisplay *display_;
  QLabel *hintLabel_;
  QLabel *focusStats_; // shown in focus mode
  TrendChart *chart_;

private:
  // applySettings() but for showing the attempts again.
  void useSettings();
};

} // namespace vcube
