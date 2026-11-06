#pragma once

#include "app/app_settings.hpp"
#include "app/cube_view.hpp"
#include "app/mode_page.hpp"
#include "app/scramble_source.hpp"
#include "app/widgets.hpp"
#include "io/library.hpp"
#include "io/sessions.hpp"
#include "train/move_count.hpp"

#include <QTimer>

class QComboBox;
class QLabel;
class QMenu;
class QPushButton;
class QTableWidget;
class QToolButton;

namespace vcube {

// Timed mode, a cstimer-like timer: scrambles, a space-bar timer with
// optional inspection, sessions, statistics and a trend chart. Each
// session is for the virtual cube or a physical one. On a physical cube
// the scramble has a preview net and any key stops the timer. On the
// virtual cube the scrambled cube can only be turned as a whole until the
// timer starts; Space stops it, and a cube that isn't solved then is a
// DNF. Each virtual solve keeps its solution.
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
  void windowDeactivated() override;
  void setFocusMode(bool on) override;

  // The alg sets changed; an alg set scramble is made again only when its
  // own set (`changedSet`) changed.
  void refreshScrambleTypes(const QString &changedSet = {});

private:
  AppSettings &settings_;
  SessionStore &store_;
  AlgLibrary &library_;

  SolveTimer timer_;
  QTimer tick_;
  ScrambleFeed feed_;
  ScrambleAlgs *algs_;
  VirtualAttempt attempt_;

  QWidget *leftPanel_;
  QWidget *typeRow_;
  QToolButton *sessionButton_;
  QMenu *sessionMenu_;
  ScrambleTypeBox *typeBox_;
  QComboBox *cubeBox_;
  QWidget *viewPicker_;
  QLabel *scrambleLabel_;
  NetPreview *net_;
  CubeView *cubeView_;
  TimeDisplay *display_;
  QLabel *statusLabel_;
  QTableWidget *table_;
  QLabel *stats_;
  TrendChart *chart_;
  QPushButton *plus2Button_;
  QPushButton *dnfButton_;
  QPushButton *deleteButton_;
  QPushButton *nextButton_;
  bool controlsLocked_ = false;

  Session &session() { return store_.current(); }
  bool onVirtualCube() const;
  // Shows the cube, view and preview the session's cube needs.
  void showCubeKind();
  void showScramble();
  // Stops a virtual solve: the time if the cube is solved, else DNF.
  void stopVirtual(std::int64_t now);
  void onTick();
  // Locks the controls that would change the scramble or session while a
  // solve or inspection runs, and unlocks them after.
  void updateControls();
  void recordSolve(std::int64_t timeMs, Penalty penalty,
                   const std::string &solution = {});
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
