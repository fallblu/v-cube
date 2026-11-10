#pragma once

#include "app/app_settings.hpp"
#include "app/cube_view.hpp"
#include "app/mode_page.hpp"
#include "app/scramble_source.hpp"
#include "app/session_panel.hpp"
#include "app/widgets.hpp"
#include "io/library.hpp"
#include "io/move_counts.hpp"
#include "train/attempt_flow.hpp"
#include "train/move_count.hpp"
#include "train/session_edit.hpp"

#include <QTimer>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class QComboBox;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace vcube {

class FlowLayout;
struct ReviewedSolve;

// What Untimed and Move Count mode share (UntimedPage, MoveCountPage):
// solves with no timer, kept in sessions with their move counts. Each
// session is for the virtual cube or a physical one (and in Move Count for
// one step), fixed once it has solves.
//
// On the virtual cube the scrambled cube can only be turned as a whole
// until Space starts the solve, and Space ends it (Untimed can end it when
// the cube is solved). The solution and its move count (HTM) are kept, or
// DNF when the cube isn't where it should be: solved in Untimed, the
// session's step done (EO, EO Cross, ...) in Move Count. Move Count goes
// straight on to the next scramble; Untimed leaves the cube to turn freely
// (and apply algs to) until Space or Enter asks for the next one. The
// states and what each key does are AttemptFlow's; the page does what it
// says.
//
// On a physical cube Move Count has the count typed in for each scramble,
// and Untimed shows scrambles (Space for the next, Backspace to go back)
// and keeps each one that was done, with its date.
class AttemptPage : public ModePage {
  Q_OBJECT
public:
  ~AttemptPage() override;

  bool handleKey(QKeyEvent *e, bool press) override;
  void applySettings() override;
  bool isBusy() const override { return flow_.isBusy(); }
  void deactivated() override;
  void windowDeactivated() override;
  void setFocusMode(bool on) override;
  // The alg sets changed (Untimed offers alg set scrambles); an alg set
  // scramble is made again only when its own set (`changedSet`) changed.
  void refreshScrambleTypes(const QString &changedSet = {});

  // How many of the worst solves "Retry the worst" queues.
  static constexpr std::size_t kWorst = 5;

protected:
  // `moveCount`: the flow is Move Count's (see AttemptFlow::Config);
  // `mode` names the page's settings ("untimed", "movecount"). The
  // subclass adds its own widgets, sets the scramble type box with
  // setTypeBox() and then calls start().
  AttemptPage(AppSettings &settings, MoveCountStore &store, AlgLibrary &library,
              const QString &mode, bool moveCount, QWidget *parent);

  // --- for the subclass to build the page ---
  // Adds "Scramble:", the type box (and `extra` after it, if any), the
  // cube box, the view picker and Skip to the type row.
  void setTypeBox(ScrambleTypeBox *box, QWidget *extra = nullptr);
  // Shows the session and scramble and starts the feed.
  void start();
  FlowLayout *typeLayout() const { return typeLayout_; }
  QWidget *typeRow() const { return typeRow_; }
  QVBoxLayout *centre() const { return centre_; }
  QHBoxLayout *buttonRow() const { return buttonRow_; }
  // Adds a widget below the cube, above the chart.
  void addBelowCentre(QWidget *w);

  // --- what the subclasses differ in ---
  // The widgets the session's cube needs (beyond the cube, view and net).
  virtual void showModeWidgets(bool virtualCube) = 0;
  virtual std::vector<AttemptColumn> columns() const = 0;
  // The cells after # of an attempt's row, with its rolling averages.
  virtual void addCells(std::vector<AttemptCell> &cells,
                        const MoveCountAttempt &a, const QString &ao5,
                        const QString &ao12) const = 0;
  // More statistics below the table of them.
  virtual void addStats(QString &html) { (void)html; }
  virtual QString physicalHint() const = 0;
  virtual QString solvingHint() const = 0;
  // The cube shows what the solve was for (solved, or the step done).
  virtual bool reached(const FaceletCube &cube) const = 0;
  virtual QString notReachedText() const = 0;
  // What a retry's result line adds about the attempt it repeats.
  virtual QString retryNote(const MoveCountAttempt &was) const {
    (void)was;
    return {};
  }
  virtual QString retryWorstTip() const = 0;
  virtual bool reviewable(const MoveCountAttempt &a) const {
    return !a.solution.empty();
  }
  virtual void describeReview(ReviewedSolve &solve, int index) const = 0;
  virtual void addDetails(QList<QPair<QString, QString>> &rows,
                          const MoveCountAttempt &a) const {
    (void)rows;
    (void)a;
  }
  // The optimal length of an attempt, for "Retry the worst": none by
  // default.
  virtual std::function<std::optional<int>(const MoveCountAttempt &)>
  optimalLookup() const {
    return {};
  }
  // A new session: the name suggested and the dialog's label, and the
  // session set up from the one that was current (`was`).
  virtual QString suggestedName() const = 0;
  virtual QString newSessionLabel() const = 0;
  virtual void setUpSession(MoveCountSession &s, const QString &name,
                            const MoveCountSession &was) = 0;
  // Untimed on a physical cube keeps only scrambles.
  virtual bool scramblesOnly() const { return false; }
  // The scramble gone back to, if any, and how it reads.
  virtual const GeneratedScramble *historyScramble() const { return nullptr; }
  virtual QString scrambleText(const GeneratedScramble &s) const {
    return s.text;
  }
  // Hooks around what the base does.
  virtual void sessionShown() {}
  virtual void settingsApplied() {}
  virtual void typesRefreshed() {}
  virtual void scrambleTypeChanged(const QString &type, bool fresh) {
    (void)type;
    (void)fresh;
  }
  virtual void scrambleShown(const GeneratedScramble *shown) { (void)shown; }
  virtual void phaseShown(bool done) { (void)done; }
  virtual void holdShown(const QColor &colour) { (void)colour; }
  virtual void entryShown() {}
  virtual void controlsEnabled(bool on) { (void)on; }
  virtual void focusModeChanged(bool on) { (void)on; }
  virtual void returnFocus() {}
  // Tab: shows or hides the algs; false when they aren't offered.
  virtual bool toggleAlgs() { return false; }
  // An attempt was recorded from a solve on the virtual cube.
  virtual void solveRecorded(const MoveCountAttempt &a) { (void)a; }
  // Scrambles only: forward (keeping the scramble shown as done), back,
  // and on to the new scramble.
  virtual void forward() {}
  virtual void back() {}
  virtual void historyToEnd() {}
  virtual void historyCleared() {}

  // --- for the subclasses ---
  MoveCountSession &session() { return store_.current(); }
  const MoveCountSession &session() const { return store_.current(); }
  bool onVirtualCube() const;
  bool focusMode() const { return focusMode_; }
  // The scramble shown: a retry's, one gone back to, or the feed's.
  const GeneratedScramble *shownScramble() const;
  // Does what the flow said.
  void apply(const AttemptFlow::Effects &e);
  // Records the current scramble with `count` (nullopt: not counted), and
  // the solution done on the virtual cube.
  void record(std::optional<int> count, bool dnf,
              const std::string &solution = {});
  void showScramble();
  void showEntry();
  void updateControls();
  void refreshAttempts();
  void selectSession(std::size_t index);
  void save();

  AppSettings &settings_;
  MoveCountStore &store_;
  AlgLibrary &library_;
  ScrambleFeed feed_;
  AttemptFlow flow_;
  VirtualAttempt attempt_;

  SessionPanel *panel_;
  ScrambleTypeBox *typeBox_ = nullptr;
  CubeView *cubeView_;
  TimeDisplay *display_;
  QLabel *lastLabel_;
  TrendChart *chart_;

private:
  const bool moveCountFlow_; // the flow's configuration only
  QTimer holdTimer_;
  // While Space is held: a popup or dialog taking the release lets go.
  QTimer holdWatch_;
  // The scramble of the attempt being retried.
  GeneratedScramble retryScramble_;
  DeletedAttempt<MoveCountAttempt> deleted_;
  bool focusMode_ = false;

  QWidget *typeRow_;
  FlowLayout *typeLayout_;
  QVBoxLayout *rightLayout_;
  QVBoxLayout *centre_;
  QHBoxLayout *buttonRow_;
  QComboBox *cubeBox_;
  QWidget *viewPicker_;
  QPushButton *skipButton_;
  QLabel *scrambleLabel_;
  QLabel *holdLabel_;
  NetPreview *net_;
  QLabel *hintLabel_;
  QLabel *focusStats_;
  QPushButton *reviewButton_;

  void configureFlow();
  // Shows the widgets the session's cube and mode need.
  void showCubeKind();
  void showHint();
  // Shows the phase the flow is in: what the cube allows, and the hint.
  void showPhase();
  // Shows how far the hold is: red until it may be released, then green.
  void showHold();
  void onHoldTimer();
  void turn(Move m);
  // Judges the cube at the end of a solve.
  void endSolve();
  void showRetryResult();
  void startRetry(std::size_t index);
  // Queues the worst solves of the session to retry one after another.
  void retryWorst();
  void skip();

  void review(int index);
  void showDetails(int index);
  void commentAttempt(int index);
  void refreshSessionMenu();
  void showStat(const QString &link);
  void deleteAttempt(int index);
  void undoDelete();
  void showAttemptMenu(int index, const QPoint &globalPos);
  void addSession();
  void renameSession();
  void deleteSession();
  QString scrambleStatsHtml() const;
};

} // namespace vcube
