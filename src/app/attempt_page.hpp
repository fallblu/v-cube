#pragma once

#include "app/page_parts.hpp"
#include "app/session_solve_page.hpp"
#include "io/move_counts.hpp"
#include "train/attempt_flow.hpp"
#include "train/session_edit.hpp"

#include <QTimer>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class QLabel;
class QPushButton;

namespace vcube {

struct DetailRow;
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
class AttemptPage : public SessionSolvePage {
  Q_OBJECT
public:
  ~AttemptPage() override;

  bool handleKey(QKeyEvent *e, bool press) override;
  // A typed move count: Backspace and Esc go on as they are held.
  bool acceptsAutoRepeat(const QKeyEvent *e) const override;
  // Space held to start counts, as in Timed.
  bool isBusy() const override { return flow_.isBusy() || flow_.isHolding(); }
  void deactivated() override;
  void windowDeactivated() override;

  // How many of the worst solves "Retry the worst" queues.
  static constexpr std::size_t kWorst = 5;

protected:
  // The clock runs while Space is held, to let go of the hold when a
  // popup or dialog takes the release; holdTimer_ says when it is Ready.
  bool clockNeeded() const override { return flow_.isHolding(); }
  void tick() override;
  void keyDone() override { updateTick(); }
  void focusModeChanged() override;
  // `moveCount`: the flow is Move Count's (see AttemptFlow::Config);
  // `mode` names the page's settings ("untimed", "movecount"). The
  // subclass adds its own widgets, sets the scramble type box with
  // setTypeBox() and then calls start().
  AttemptPage(AppSettings &settings, MoveCountStore &store, AlgLibrary &library,
              const QString &mode, SolveLog log, QWidget *parent);

  // --- what the subclasses differ in ---
  // The widgets the session's cube needs (beyond the cube, view and net).
  virtual void showModeWidgets(bool virtualCube) = 0;
  // The cells after # of an attempt's row, with its rolling averages.
  virtual void addCells(std::vector<AttemptCell> &cells,
                        const MoveCountAttempt &a, const QString &ao5,
                        const QString &ao12) const = 0;
  // What the hint line says and the keys shown under it.
  struct Hint {
    QString message;
    QList<KeyHint> keys;
  };
  virtual Hint physicalHint() const = 0;
  virtual Hint solvingHint() const = 0;
  // The cube shows what the solve was for (solved, or the step done).
  virtual bool reached(const FaceletCube &cube) const = 0;
  virtual QString notReachedText() const = 0;
  // What a retry's result line adds about the attempt it repeats.
  virtual QString retryNote(const MoveCountAttempt &) const { return {}; }
  virtual bool reviewable(const MoveCountAttempt &a) const {
    return !a.solution.empty();
  }
  virtual void describeReview(ReviewedSolve &solve, int index) const = 0;
  virtual void addDetails(QList<DetailRow> &, const MoveCountAttempt &) const {}
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
  // The scramble gone back to, if any, and how it reads.
  virtual const GeneratedScramble *historyScramble() const { return nullptr; }
  virtual QString scrambleText(const GeneratedScramble &s) const {
    return s.text;
  }
  // Hooks around what the base does.
  virtual void sessionShown() {}
  virtual void phaseShown(bool) {}
  virtual void holdShown(const QColor &) {}
  virtual void controlsEnabled(bool) {}
  virtual void pageFocusChanged(bool) {}
  virtual void returnFocus() {}
  // An attempt was recorded from a solve on the virtual cube.
  virtual void solveRecorded(const MoveCountAttempt &) {}
  // Scrambles only: forward (keeping the scramble shown as done), back,
  // and on to the new scramble.
  virtual void forward() {}
  virtual void back() {}
  virtual void historyToEnd() {}
  virtual void historyCleared() {}

  // --- for the subclasses ---
  MoveCountSession &session() { return store_.current(); }
  const MoveCountSession &session() const { return store_.current(); }
  bool onVirtualCube() const override {
    return session().cube == CubeKind::Virtual;
  }
  // The scramble shown: a retry's, one gone back to, or the feed's.
  const GeneratedScramble *shownScramble() const;
  // Does what the flow said.
  void apply(const AttemptFlow::Effects &e);
  // Records the current scramble with `count` (nullopt: not counted), and
  // the solution done on the virtual cube.
  void record(std::optional<int> count, bool dnf,
              const std::string &solution = {});
  void showScramble() override;
  void showEntry();
  void updateControls();
  void refreshAttempts() override;
  void selectSession(std::size_t index) override;
  void save() override;

  MoveCountStore &store_;
  AttemptFlow flow_;
  QLabel *lastLabel_;

private:
  const bool moveCountFlow_; // the flow's configuration only
  // Fires when the hold of Space has lasted long enough.
  QTimer holdTimer_;
  // The scramble of the attempt being retried.
  GeneratedScramble retryScramble_;
  DeletedAttempt<MoveCountAttempt> deleted_;

  QPushButton *reviewButton_;

  std::size_t attemptCount() const override {
    return session().attempts.size();
  }
  void configureFlow() override;
  void showCubeKind() override;
  void cubeChosen(CubeKind kind) override;
  void typeChosen(const QString &type) override;
  void scrambleDropped() override { historyCleared(); }
  void showHint();
  // Shows the phase the flow is in: what the cube allows, and the hint.
  void showPhase();
  // Shows how far the hold is: red until it may be released, then green.
  void showHold();
  void turn(Move m);
  // Judges the cube at the end of a solve.
  void endSolve();
  void showRetryResult();
  void startRetry(std::size_t index);
  // Queues the worst solves of the session to retry one after another.
  void retryWorst();
  void skip() override;

  bool canReview(int index) const override {
    return reviewable(session().attempts[static_cast<std::size_t>(index)]);
  }
  void review(int index) override;
  void showDetails(int index) override;
  void commentAttempt(int index);
  void refreshSessionMenu() override;
  void showStat(const QString &link) override;
  void deleteAttempt(int index) override;
  void undoDelete() override;
  void showAttemptMenu(int index, const QPoint &globalPos) override;
  void addSession() override;
  void renameSession() override;
  void deleteSession() override;
  QString scrambleStatsHtml() const;
};

} // namespace vcube
