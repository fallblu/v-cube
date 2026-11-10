#pragma once

#include "app/app_settings.hpp"
#include "app/cube_view.hpp"
#include "app/mode_page.hpp"
#include "app/scramble_source.hpp"
#include "app/widgets.hpp"
#include "io/library.hpp"
#include "io/move_counts.hpp"
#include "train/move_count.hpp"

#include <QFutureWatcher>
#include <QTimer>

#include <deque>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QTableWidget;
class QToolButton;

namespace vcube {

// Untimed and Move Count mode: solves with no timer, kept in sessions with
// their move counts. Each session is for the virtual cube or a physical
// one (and in Move Count for one step), fixed once it has solves.
//
// On the virtual cube the scrambled cube can only be turned as a whole
// until Space starts the solve, and Space ends it (Untimed can end it when
// the cube is solved). The solution and its move count (HTM) are kept, or
// DNF when the cube isn't where it should be: solved in Untimed, the
// session's step done (EO, EO Cross, ...) in Move Count. Move Count goes
// straight on to the next scramble; Untimed leaves the cube to turn freely
// (and apply algs to) until Space or Enter asks for the next one.
//
// On a physical cube Move Count has the count typed in for each scramble,
// and Untimed shows scrambles (Space for the next, Backspace to go back)
// and keeps each one that was done, with its date.
class AttemptPage : public ModePage {
  Q_OBJECT
public:
  AttemptPage(AppSettings &settings, MoveCountStore &store, AlgLibrary &library,
              QWidget *parent = nullptr);
  ~AttemptPage() override;

  bool handleKey(QKeyEvent *e, bool press) override;
  // A typed move count: Backspace and Esc go on as they are held.
  bool acceptsAutoRepeat(const QKeyEvent *e) const override;
  SettingsParts settingsUsed() const override {
    return SettingsPart::Cube | SettingsPart::Keys | SettingsPart::Neutrality |
           SettingsPart::Timer | SettingsPart::Counts;
  }
  void applySettings() override;
  bool isBusy() const override {
    return phase_ == Phase::Solving || hold_ != Hold::None;
  }
  void deactivated() override;
  void windowDeactivated() override { cancelHold(); }
  // The alg sets changed (Untimed offers alg set scrambles); an alg set
  // scramble is made again only when its own set (`changedSet`) changed.
  void refreshScrambleTypes(const QString &changedSet = {});

  static constexpr std::size_t kHistory = 100;
  // How many of the worst solves "Retry the worst" queues.
  static constexpr std::size_t kWorst = 5;

protected:
  void focusModeChanged() override;
  bool eventFilter(QObject *obj, QEvent *event) override;

private:
  // On the virtual cube: looking at the scramble (rotations only), solving,
  // and (Untimed) turning freely after the solve.
  enum class Phase { Inspect, Solving, Done };
  // Space held to start a solve on the virtual cube: as in Timed mode, it
  // must be held for the hold time (Ready) before releasing it starts.
  enum class Hold { None, Holding, Ready };
  // Optimal lengths by step, scramble and which holds (true: any hold).
  using OptimalKey = std::tuple<SolveStep, std::string, bool>;
  using OptimalResults = std::vector<std::pair<std::string, int>>;

  AppSettings &settings_;
  MoveCountStore &store_;
  AlgLibrary &library_;
  const bool moveCount_; // Move Count, not Untimed
  ScrambleFeed feed_;
  ScrambleAlgs *algs_ = nullptr; // Untimed only
  VirtualAttempt attempt_;
  Phase phase_ = Phase::Inspect;
  Hold hold_ = Hold::None;
  QTimer holdTimer_;
  // While Space is held: a popup or dialog taking the release lets go.
  QTimer holdWatch_;
  QString entry_; // Move Count, physical: the digits typed
  // Untimed, physical: the scrambles done, to go back to; history_[index_]
  // is shown while going back, the feed's scramble when index_ is at the
  // end.
  std::deque<GeneratedScramble> history_;
  std::size_t index_ = 0;
  // A recorded scramble solved again from its review: not recorded, and
  // compared with the attempt it repeats.
  struct Retry {
    std::size_t index = 0; // of the attempt
    MoveCountAttempt original;
    GeneratedScramble scramble;
    bool done = false;       // solved; Space goes back to the scrambles
    bool feedSolved = false; // the scramble shown before was solved
    MoveCountAttempt result; // the retry's own, once done
  };
  std::optional<Retry> retry_;
  // "Retry the worst": the attempts still to retry after this one.
  std::deque<std::size_t> retryQueue_;
  std::size_t retryCount_ = 0; // retries in the queue at the start
  // The last attempt deleted, and where it was, to undo.
  std::optional<std::pair<std::size_t, MoveCountAttempt>> deleted_;
  // Move Count: optimal lengths of attempts' scrambles for the session's
  // step, worked out off the GUI thread.
  std::map<OptimalKey, int> optimal_;
  QFutureWatcher<OptimalResults> optimalWatcher_;
  OptimalKey optimalFor_;   // what the watcher works on (scramble empty)
  int optimalRound_ = 0;    // bumped when the results so far are stale
  int optimalRoundRun_ = 0; // ...and the round the watcher works for
  // Untimed, virtual: the optimal EO Cross after a solve.
  QFutureWatcher<std::optional<int>> eoWatcher_;
  std::optional<int> eoYours_;

  QWidget *leftPanel_;
  QWidget *typeRow_;
  QToolButton *sessionButton_;
  QMenu *sessionMenu_;
  QComboBox *stepBox_ = nullptr; // Move Count only
  ScrambleTypeBox *typeBox_;
  QComboBox *cubeBox_;
  QWidget *viewPicker_;
  QPushButton *skipButton_;
  QLabel *scrambleLabel_;
  QLabel *holdLabel_;
  NetPreview *net_;
  CubeView *cubeView_;
  QLabel *stepLabel_;
  TimeDisplay *display_;
  QLabel *hintLabel_;
  QLabel *lastLabel_;
  QLabel *focusStats_;
  QTableWidget *table_;
  QLabel *stats_;
  TrendChart *chart_;
  QPushButton *recordButton_;
  QPushButton *dnfButton_;
  QPushButton *deleteButton_;
  QPushButton *undoButton_;
  QPushButton *reviewButton_;
  QWidget *algPanel_; // Untimed only
  QLineEdit *algEdit_;
  QLabel *algError_;

  MoveCountSession &session() { return store_.current(); }
  bool onVirtualCube() const;
  // Untimed on a physical cube: only scrambles are kept.
  bool scramblesOnly() const;
  // The scramble shown: the feed's, or one gone back to.
  const GeneratedScramble *shownScramble() const;

  // Shows the widgets the session's cube and mode need.
  void showCubeKind();
  void showScramble();
  // Shows the scramble, or the next one if the one shown was solved.
  void showFreshScramble();
  void showHint();
  void showEntry();
  void setEntry(const QString &digits);
  void setPhase(Phase p);
  // Locks what would change the scramble or session during a solve.
  void updateControls();
  void updateStepLabel();

  void turn(Move m);
  // Space pressed or released before the solve.
  void pressStart();
  void releaseStart();
  void cancelHold();
  // Shows how far the hold is: red until it may be released, then green.
  void showHold();
  void startSolve();
  void endSolve();
  // Records the current scramble with `count` (nullopt: not counted), and
  // the solution done on the virtual cube.
  void record(std::optional<int> count, bool dnf,
              const std::string &solution = {});
  void recordTyped(bool dnf); // Move Count, physical
  void next();                // on to the next scramble
  void previous();            // Untimed, physical: back one scramble
  void skip();
  void applyAlg();
  void returnFocus();
  // The keys for the last solve and the scramble.
  bool handleActionKey(QKeyEvent *e, bool press);

  // Opens the review of an attempt done on the virtual cube.
  void review(int index);
  bool reviewable(const MoveCountAttempt &a) const;
  void showDetails(int index);
  void commentAttempt(int index);
  void startRetry(std::size_t index);
  void endRetry();
  // Queues the worst solves of the session to retry one after another.
  void retryWorst();
  // The holds the Opt column is for, and whether that is every hold.
  std::vector<vcube::Hold> optimalHolds() const;
  // Works out the optimal lengths the attempt list is missing.
  void requestOptimal();
  // nullopt while it is being worked out, -1 if it can't be.
  std::optional<int> optimalOf(const MoveCountAttempt &a) const;
  // Works out the optimal EO Cross of the Untimed solve just done.
  void analyseEoCross(const MoveCountAttempt &a);

  void refreshSessionMenu();
  void selectSession(std::size_t index);
  void refreshAttempts();
  void showStat(const QString &link);
  void deleteAttempt(int index);
  void undoDelete();
  void showAttemptMenu(const QPoint &pos);
  void addSession();
  void renameSession();
  void deleteSession();
  void save();
};

} // namespace vcube
