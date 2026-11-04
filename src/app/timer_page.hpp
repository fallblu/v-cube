#pragma once

#include "app/app_settings.hpp"
#include "app/cube_view.hpp"
#include "app/widgets.hpp"
#include "io/library.hpp"
#include "io/sessions.hpp"

#include <QFutureWatcher>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace vcube {

// cstimer-like timer: scrambles, a space-bar or virtual-cube timer,
// sessions, statistics and a trend chart.
class TimerPage : public QWidget {
  Q_OBJECT
public:
  TimerPage(AppSettings &settings, SessionStore &store, AlgLibrary &library,
            QWidget *parent = nullptr);

  // Key events forwarded by the main window; true if used.
  bool handleKey(QKeyEvent *e, bool press);

  void applySettings();      // after the settings dialog
  void reloadSessions();     // after an import
  void refreshScrambleTypes(); // alg sets changed
  bool isBusy() const;       // a solve or inspection is in progress

signals:
  void message(const QString &text);

private:
  AppSettings &settings_;
  SessionStore &store_;
  AlgLibrary &library_;

  SolveTimer timer_;
  QTimer tick_;
  QFutureWatcher<GeneratedScramble> watcher_;
  std::optional<GeneratedScramble> current_;
  std::optional<GeneratedScramble> prefetched_;
  QString prefetchedType_;
  bool wantNext_ = false;

  // Virtual cube
  FaceletCube cube_;
  QStringList solveMoves_;
  bool virtualScrambled_ = false;

  QComboBox *sessionBox_;
  QComboBox *typeBox_;
  QCheckBox *virtualBox_;
  QLabel *scrambleLabel_;
  TimeDisplay *display_;
  QLabel *statusLabel_;
  CubeView *cubeView_;
  QTableWidget *table_;
  QLabel *stats_;
  TrendChart *chart_;
  QPushButton *plus2Button_;
  QPushButton *dnfButton_;
  QPushButton *deleteButton_;

  Session &session() { return store_.current(); }
  QString currentType() const;
  std::function<CubieCube(Rng &)> stateMaker(const QString &type) const;
  void requestScramble();
  void scrambleReady();
  void showScramble();
  void onTick();
  void recordSolve(std::int64_t timeMs, Penalty penalty);
  void refreshSessionBox();
  void refreshSolves();
  void setLastPenalty(Penalty p);
  void deleteSolve(int index);
  void showSolveMenu(const QPoint &pos);
  void addSession();
  void renameSession();
  void deleteSession();
  void setVirtual(bool on);
  void virtualMove(Move m);
  void save();
};

} // namespace vcube
