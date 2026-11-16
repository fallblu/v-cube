#pragma once

#include "app/attempt_page.hpp"
#include "app/eo_cross_check.hpp"
#include "train/scramble_history.hpp"

#include <optional>
#include <vector>

class QLabel;
class QLineEdit;

namespace vcube {

// Untimed mode: whole solves with no timer, from any scramble type, kept
// with their move counts. On the virtual cube the solve ends on Space (or
// once the cube is solved, with auto stop), and the cube then turns freely,
// with algs to apply to it, until Space or Enter asks for the next
// scramble; after each solve the optimal EO Cross of its scramble is
// shown. On a physical cube only the scrambles are kept: Space (or →) for
// the next, Backspace (or ←) back through the last ones done (a
// ScrambleHistory).
class UntimedPage : public AttemptPage {
  Q_OBJECT
public:
  UntimedPage(AppSettings &settings, MoveCountStore &store, AlgLibrary &library,
              QWidget *parent = nullptr);
  ~UntimedPage() override;

protected:
  bool eventFilter(QObject *obj, QEvent *event) override;

  void showModeWidgets(bool virtualCube) override;
  std::vector<AttemptColumn> columns() const override;
  void addCells(std::vector<AttemptCell> &cells, const MoveCountAttempt &a,
                const QString &ao5, const QString &ao12) const override;
  Hint physicalHint() const override;
  Hint solvingHint() const override;
  bool reached(const FaceletCube &cube) const override;
  QString notReachedText() const override;
  QString retryWorstTip() const override;
  void describeReview(ReviewedSolve &solve, int index) const override;
  QString suggestedName() const override;
  QString newSessionLabel() const override;
  void setUpSession(MoveCountSession &s, const QString &name,
                    const MoveCountSession &was) override;
  bool scramblesOnly() const override { return !onVirtualCube(); }
  const GeneratedScramble *historyScramble() const override;
  QString scrambleText(const GeneratedScramble &s) const override;
  void settingsApplied() override;
  void typesRefreshed() override;
  void scrambleTypeChanged(const QString &type, bool fresh) override;
  void scrambleShown(const GeneratedScramble *shown) override;
  void phaseShown(bool done) override;
  void pageFocusChanged(bool on) override;
  void returnFocus() override;
  bool toggleAlgs() override;
  void solveRecorded(const MoveCountAttempt &a) override;
  void forward() override;
  void back() override;
  void historyToEnd() override { history_.toEnd(); }
  void historyCleared() override { history_.clear(); }

private:
  ScrambleAlgs *algs_;
  // On a physical cube: the scrambles done, to go back to.
  ScrambleHistory<GeneratedScramble> history_;
  // The optimal EO Cross after a solve, and the solve's own.
  EoCrossCheck eoCheck_;

  QWidget *algPanel_;
  QLineEdit *algEdit_;
  QLabel *algError_;

  void applyAlg();
};

} // namespace vcube
