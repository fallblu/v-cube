#pragma once

#include "app/app_settings.hpp"
#include "app/mode_page.hpp"
#include "app/scramble_source.hpp"
#include "app/widgets.hpp"
#include "io/library.hpp"
#include "io/sessions.hpp"

#include <QTimer>

class QLabel;
class QMenu;
class QPushButton;
class QTableWidget;
class QToolButton;

namespace vcube {

// cstimer-like timer for a physical cube: scrambles with a preview net, a
// space-bar timer with optional inspection, sessions, statistics and a
// trend chart.
class TimerPage : public ModePage {
  Q_OBJECT
public:
  TimerPage(AppSettings &settings, SessionStore &store, AlgLibrary &library,
            QWidget *parent = nullptr);

  bool handleKey(QKeyEvent *e, bool press) override;
  void applySettings() override;
  bool isBusy() const override; // a solve or inspection is in progress
  void activated() override;
  void deactivated() override;
  void setFocusMode(bool on) override;

  void refreshScrambleTypes(); // alg sets changed

private:
  AppSettings &settings_;
  SessionStore &store_;
  AlgLibrary &library_;

  SolveTimer timer_;
  QTimer tick_;
  ScrambleFeed feed_;
  ScrambleAlgs *algs_;

  QWidget *leftPanel_;
  QWidget *typeRow_;
  QToolButton *sessionButton_;
  QMenu *sessionMenu_;
  ScrambleTypeBox *typeBox_;
  QLabel *scrambleLabel_;
  NetPreview *net_;
  TimeDisplay *display_;
  QLabel *statusLabel_;
  QTableWidget *table_;
  QLabel *stats_;
  TrendChart *chart_;
  QPushButton *plus2Button_;
  QPushButton *dnfButton_;
  QPushButton *deleteButton_;

  Session &session() { return store_.current(); }
  void showScramble();
  void onTick();
  void recordSolve(std::int64_t timeMs, Penalty penalty);
  void refreshSessionMenu();
  void selectSession(std::size_t index);
  void refreshSolves();
  void setLastPenalty(Penalty p);
  void deleteSolve(int index);
  void showSolveMenu(const QPoint &pos);
  void addSession();
  void renameSession();
  void deleteSession();
  void importCstimer();
  void exportCstimer();
  void save();
};

} // namespace vcube
