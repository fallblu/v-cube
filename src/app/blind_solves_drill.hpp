#pragma once

#include "app/blind_drill.hpp"
#include "app/scramble_source.hpp"
#include "train/move_count.hpp"

#include <QTimer>

namespace vcube {

class TrendChart;

// Blind solves, in sessions: memo with the cube shown, then execution with
// it hidden (virtual cube), or a timer with a memo split (physical cube).
// A running solve keeps going while the window is away, and is cancelled
// when the page is left.
class SolvesDrill : public BlindDrill {
  Q_OBJECT
public:
  SolvesDrill(BlindContext &context, QWidget *parent);

  // The session's trend chart, for the page to lay out.
  TrendChart *chart() const;

  QWidget *leftPanel() const override;
  std::vector<QWidget *> options() const override;
  Layout layout() const override;
  View view() const override;

  void enter(bool changed) override;
  void leave() override;
  bool handleKey(QKeyEvent *e, bool press) override;
  bool isBusy() const override { return flow_.isBusy(); }
  void deactivated() override;
  void refreshLeft() override;
  void showHint() override;
  void scrambleChanged() override;

private:
  BlindSolveFlow flow_;
  QTimer tick_;
  SessionPanel *panel_;
  QComboBox *cubeBox_;
  // The scramble solved: the feed moves on as soon as one is shown, so
  // that no scramble is seen twice.
  std::optional<GeneratedScramble> used_;
  FaceletCube heldStart_; // the scramble as held for blind (solver frame)
  CubieCube heldCubie_;
  VirtualAttempt attempt_;

  BlindSession &session() { return ctx_.store.current(); }
  bool onVirtualCube() const;
  void apply(const BlindSolveFlow::Effects &e, std::optional<Move> move);
  // The session button, the cube box and the clock follow isBusy().
  void updateControls();
  void showScramble();
  void startMemo();
  void startExecution();
  void stopSolve(const BlindSolveFlow::Stop &stop);
  void cancelSolve();
  void recordSolve(std::int64_t timeMs, std::int64_t memoMs, bool dnf,
                   const std::string &solution);
  void next();
  void onTick();
  void refreshSessionMenu();
  void selectSession(std::size_t index);
  void showAttemptMenu(int index, const QPoint &globalPos);
  void addSession();
  void renameSession();
  void deleteSession();
};

} // namespace vcube
