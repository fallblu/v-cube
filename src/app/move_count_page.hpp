#pragma once

#include "app/attempt_page.hpp"
#include "train/optimal_cache.hpp"

#include <QFutureWatcher>

#include <optional>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;

namespace vcube {

// Move Count mode: how many moves a step (EO, EO Cross, ZZ F2L, ...) takes
// from each scramble, in sessions each for one step. On the virtual cube
// the solve counts if the step is done when Space ends it, and the next
// scramble follows; the list shows each scramble's optimal length for the
// step, worked out off the GUI thread. On a physical cube the count is
// typed in (TypedCount): digits, Backspace, Esc, then Enter, or Alt+3 for
// a DNF.
class MoveCountPage : public AttemptPage {
  Q_OBJECT
public:
  MoveCountPage(AppSettings &settings, MoveCountStore &store,
                AlgLibrary &library, QWidget *parent = nullptr);
  ~MoveCountPage() override;

protected:
  void showModeWidgets(bool virtualCube) override;
  std::vector<AttemptColumn> columns() const override;
  void addCells(std::vector<AttemptCell> &cells, const MoveCountAttempt &a,
                const QString &ao5, const QString &ao12) const override;
  void addStats(QString &html) override;
  Hint physicalHint() const override;
  Hint solvingHint() const override;
  bool reached(const FaceletCube &cube) const override;
  QString notReachedText() const override;
  QString retryNote(const MoveCountAttempt &was) const override;
  bool reviewable(const MoveCountAttempt &a) const override;
  void describeReview(ReviewedSolve &solve, int index) const override;
  void addDetails(QList<QPair<QString, QString>> &rows,
                  const MoveCountAttempt &a) const override;
  std::function<std::optional<int>(const MoveCountAttempt &)>
  optimalLookup() const override;
  QString suggestedName() const override;
  QString newSessionLabel() const override;
  void setUpSession(MoveCountSession &s, const QString &name,
                    const MoveCountSession &was) override;
  void sessionShown() override;
  void settingsApplied() override;
  void holdShown(const QColor &colour) override;
  void controlsEnabled(bool on) override;

private:
  // The optimal lengths of the attempts' scrambles, and the job working
  // out the missing ones.
  OptimalCache optimal_;
  QFutureWatcher<OptimalCache::Results> optimalWatcher_;
  OptimalCache::Job optimalJob_;

  QComboBox *stepBox_;
  QLabel *stepLabel_;

  bool hasOptimal() const;
  // The holds the Opt column is for.
  // nullopt while it is being worked out, -1 if it can't be.
  std::optional<int> optimalOf(const MoveCountAttempt &a) const;
  // Works out the optimal lengths the attempt list is missing.
  void requestOptimal();
  void recordTyped(bool dnf);
};

} // namespace vcube
