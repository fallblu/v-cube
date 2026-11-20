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

#include <map>
#include <optional>

class QCheckBox;
class QComboBox;
class QKeyEvent;
class QLabel;
class QSpinBox;
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
  // The time lastAttempt_ is of, as it was made: times can be erased
  // elsewhere (Clear times, a lab case deleted), which moves the others.
  LabAttempt lastMade_;
  // Points lastAttempt_ at its time again, or at none if that is gone.
  void rebaseLastAttempt();
  SolveTimer timer_;
  QTimer tick_;
  LabFlow flow_;
  VirtualAttempt attempt_;
  AlgFollower follower_;
  QFutureWatcher<GeneratedScramble> watcher_;
  bool loading_ = false;

  QComboBox *sessionBox_ = nullptr;
  QComboBox *cubeBox_ = nullptr;
  QComboBox *orderBox_ = nullptr;
  QSpinBox *repsBox_ = nullptr;
  QSpinBox *warmupBox_ = nullptr;
  QSpinBox *trimBox_ = nullptr;
  QCheckBox *randomAufBox_ = nullptr;
  QTableWidget *entries_ = nullptr;
  QLabel *caseLabel_ = nullptr;
  QLabel *algLabel_ = nullptr;
  QLabel *diagram_ = nullptr;
  QLabel *scrambleLabel_ = nullptr;
  CubeView *cubeView_ = nullptr;
  QWidget *viewRow_ = nullptr;
  TimeDisplay *display_ = nullptr;
  HintBar *status_ = nullptr;
  QComboBox *windowBox_ = nullptr;
  QComboBox *chartBox_ = nullptr;
  QTableWidget *compare_ = nullptr;
  LabChart *chart_ = nullptr;
  QTableWidget *times_ = nullptr;
  QTabWidget *bottom_ = nullptr;

  LabSession &session();
  const LabSession &session() const;
  bool onVirtual() const;
  void saveStore();
  void loadSessions();
  void loadSessionOptions();
  void optionsChanged();
  // The names of the cases a fill has looked up (none for a case that is
  // gone), so that each is looked up once however many rows show it. Made
  // anew for every refresh: cases and sets change elsewhere.
  using CaseNames = std::map<LabCaseKey, std::optional<QString>>;
  const std::optional<QString> &caseName(CaseNames &names,
                                         const LabCaseKey &key) const;
  void fillEntries(CaseNames &names);
  void fillCompare(CaseNames &names);
  void fillTimes(CaseNames &names);

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
  // How a test solve ended: the step done, or (virtual cube) not: a DNF.
  enum class Result { Solved, Dnf };
  void finish(std::int64_t ms, Result result);
  void resetCube();
  void toggleDnf();
  void deleteLast();
  void timesMenu(const QPoint &at);
};

} // namespace vcube
