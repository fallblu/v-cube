#pragma once

#include "app/alglab_common.hpp"
#include "app/cube_view.hpp"
#include "app/widgets.hpp"
#include "train/alg_follow.hpp"
#include "train/move_count.hpp"
#include "train/timer.hpp"

#include "train/lab_flow.hpp"

#include <QFutureWatcher>
#include <QTimer>
#include <QWidget>

#include <optional>

class QCheckBox;
class QComboBox;
class QKeyEvent;
class QLabel;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTabWidget;

namespace vcube {

class HintBar;
class LabChart;

// Alg Lab's Test tab: test sessions that time algs one at a time, each alg
// keeping its own times.
//
// A session lists (case, alg) entries from any cases, and has a cube kind,
// an order (interleaved rounds or blocked), reps per alg, warm-up reps and
// an outlier trim for the statistics, and whether a random U turn comes
// before each case. Each attempt names the alg to do and sets its case up
// from the side the alg starts from: on a physical cube with a
// random-state scramble (hold Space and let go to start, any key stops),
// on the virtual cube on the cube (the first turn starts the timer, which
// stops once the step is done; moves that leave the alg mark the time,
// which is then left out of the statistics). After an attempt Space goes
// on, Backspace toggles DNF, Delete deletes the attempt and Esc tries the
// same alg again.
//
// Below, the session's algs are compared (statistics and a chart, for this
// session or all time), and its times listed.
class LabTestPanel : public QWidget {
  Q_OBJECT
public:
  LabTestPanel(AppSettings &settings, AlgLibrary &library, AlgLabStore &store,
               QWidget *parent = nullptr);

  bool handleKey(QKeyEvent *e, bool press);
  bool isBusy() const;
  void applySettings();
  // The store changed elsewhere (candidates, cases, sets): shows it again.
  void refresh();
  // Adds entries to the current session (those it has already are left
  // out); returns how many were added.
  int addEntries(const std::vector<LabEntry> &entries);
  // Stops an attempt under way (the page is being left).
  void cancelAttempt();
  QString sessionName() const;

signals:
  void attemptsChanged();
  void message(const QString &text);

private:
  using Phase = LabFlow::Phase;

  struct Current {
    std::size_t entry = 0;
    ResolvedCase c;
    std::string alg;  // the entry's alg
    QString shownAlg; // as it is done from the setup
    CubieCube setup;
    QString scramble; // physical
  };

  AppSettings &settings_;
  AlgLibrary &library_;
  AlgLabStore &store_;
  Rng rng_;
  std::optional<Current> current_;
  std::optional<std::size_t> lastEntry_;
  // Points current_ at its entry again, or moves on if that is gone.
  void rebaseCurrent();
  std::optional<std::size_t> lastAttempt_; // index into the store
  SolveTimer timer_;
  QTimer tick_;
  LabFlow flow_;
  VirtualAttempt attempt_;
  AlgFollower follower_;
  QFutureWatcher<GeneratedScramble> watcher_;
  bool loading_ = false;

  QComboBox *sessionBox_;
  QComboBox *cubeBox_;
  QComboBox *orderBox_;
  QSpinBox *repsBox_;
  QSpinBox *warmupBox_;
  QSpinBox *trimBox_;
  QCheckBox *randomAufBox_;
  QTableWidget *entries_;
  QLabel *caseLabel_;
  QLabel *algLabel_;
  QLabel *diagram_;
  QLabel *scrambleLabel_;
  CubeView *cubeView_;
  QWidget *viewRow_;
  TimeDisplay *display_;
  HintBar *status_;
  QComboBox *windowBox_;
  QComboBox *chartBox_;
  QTableWidget *compare_;
  LabChart *chart_;
  QTableWidget *times_;
  QTabWidget *bottom_;

  LabSession &session();
  const LabSession &session() const;
  bool onVirtual() const;
  void saveStore();
  void loadSessions();
  void loadSessionOptions();
  void optionsChanged();
  void fillEntries();
  void fillCompare();
  void fillTimes();
  LabWindow window() const;
  WarmupOf warmup() const;

  void newSession();
  void renameSession();
  void deleteSession();
  void removeEntries();
  void moveEntry(int direction);

  // Picks the next entry and sets its case up; Finished when every entry
  // has its reps.
  void next();
  void setUp(std::size_t entry);
  void showCurrent();
  void requestScramble();
  void scrambleReady();
  void onTick();
  void showStatus();
  bool stepDone() const;
  void virtualTurn(Move m, std::int64_t now);
  void finish(std::int64_t ms, bool dnf);
  void resetCube();
  void toggleDnf();
  void deleteLast();
  void timesMenu(const QPoint &at);
};

} // namespace vcube
