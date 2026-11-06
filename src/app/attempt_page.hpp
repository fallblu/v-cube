#pragma once

#include "app/app_settings.hpp"
#include "app/cube_view.hpp"
#include "app/mode_page.hpp"
#include "app/scramble_source.hpp"
#include "app/widgets.hpp"
#include "io/library.hpp"
#include "io/move_counts.hpp"
#include "train/move_count.hpp"

#include <QTimer>

#include <deque>

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
// one.
//
// On the virtual cube the scrambled cube can only be turned as a whole
// until Space starts the solve, and Space ends it. The solution and its
// move count (HTM) are kept, or DNF when the cube isn't where it should be:
// solved in Untimed, the session's step done (EO, EO Cross, ...) in Move
// Count. Move Count goes straight on to the next scramble; Untimed leaves
// the cube to turn freely (and apply algs to) until Space asks for the
// next one.
//
// On a physical cube Move Count has the count typed in for each scramble,
// and Untimed shows scrambles (Space for the next, Backspace to go back)
// and keeps each one that was done.
class AttemptPage : public ModePage {
  Q_OBJECT
public:
  AttemptPage(AppSettings &settings, MoveCountStore &store, AlgLibrary &library,
              QWidget *parent = nullptr);

  bool handleKey(QKeyEvent *e, bool press) override;
  void applySettings() override;
  bool isBusy() const override { return phase_ == Phase::Solving; }
  void deactivated() override;
  void windowDeactivated() override { cancelHold(); }
  void setFocusMode(bool on) override;
  // The alg sets changed (Untimed offers alg set scrambles); an alg set
  // scramble is made again only when its own set (`changedSet`) changed.
  void refreshScrambleTypes(const QString &changedSet = {});

  static constexpr std::size_t kHistory = 100;

protected:
  bool eventFilter(QObject *obj, QEvent *event) override;

private:
  // On the virtual cube: looking at the scramble (rotations only), solving,
  // and (Untimed) turning freely after the solve.
  enum class Phase { Inspect, Solving, Done };
  // Space held to start a solve on the virtual cube: as in Timed mode, it
  // must be held for the hold time (Ready) before releasing it starts.
  enum class Hold { None, Holding, Ready };

  AppSettings &settings_;
  MoveCountStore &store_;
  const bool moveCount_; // Move Count, not Untimed
  ScrambleFeed feed_;
  ScrambleAlgs *algs_ = nullptr; // Untimed only
  VirtualAttempt attempt_;
  Phase phase_ = Phase::Inspect;
  Hold hold_ = Hold::None;
  QTimer holdTimer_;
  QString entry_; // Move Count, physical: the digits typed
  // Untimed, physical: the scrambles done, to go back to; history_[index_]
  // is shown while going back, the feed's scramble when index_ is at the
  // end.
  std::deque<GeneratedScramble> history_;
  std::size_t index_ = 0;

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
  NetPreview *net_;
  CubeView *cubeView_;
  QLabel *stepLabel_;
  TimeDisplay *display_;
  QLabel *hintLabel_;
  QLabel *lastLabel_;
  QTableWidget *table_;
  QLabel *stats_;
  TrendChart *chart_;
  QPushButton *recordButton_;
  QPushButton *dnfButton_;
  QPushButton *deleteButton_;
  QWidget *algPanel_; // Untimed only
  QLineEdit *algEdit_;
  QLabel *algError_;

  MoveCountSession &session() { return store_.current(); }
  bool onVirtualCube() const;
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

  void refreshSessionMenu();
  void selectSession(std::size_t index);
  void refreshAttempts();
  void deleteAttempt(int index);
  void showAttemptMenu(const QPoint &pos);
  void addSession();
  void renameSession();
  void deleteSession();
  void save();
};

} // namespace vcube
