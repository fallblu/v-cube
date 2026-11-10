#pragma once

#include "app/blind_drill.hpp"
#include "app/scramble_source.hpp"

#include <QTimer>

class QLineEdit;

namespace vcube {

// Memo tracing: the memo of a scrambled cube is typed and checked by
// doing it, so any valid cycle break counts. The trace's clock stops while
// the page or the window is away.
class TracingDrill : public BlindDrill {
  Q_OBJECT
public:
  TracingDrill(BlindContext &context, QWidget *parent);

  QWidget *leftPanel() const override;
  QWidget *inputRow() const override;
  View view() const override;

  void enter(bool changed) override;
  void leave() override;
  bool handleKey(QKeyEvent *e, bool press) override;
  void deactivated() override;
  void pause() override;
  void resume() override;
  void refreshLeft() override;
  void showHint() override;
  void scrambleChanged() override;

protected:
  bool eventFilter(QObject *obj, QEvent *event) override;

private:
  TraceFlow flow_;
  QTimer tick_;
  BlindStatsPanel *left_;
  QWidget *row_;
  QLineEdit *edgesEdit_;
  QLineEdit *cornersEdit_;
  // The scramble traced: the feed moves on as soon as one is shown, so
  // that no scramble is seen twice.
  std::optional<GeneratedScramble> used_;
  FaceletCube heldStart_; // the scramble as held for blind (solver frame)
  CubieCube heldCubie_;
  FaceletCube traceCube_; // turned as a whole while tracing

  void apply(const TraceFlow::Effects &e, std::optional<Move> move);
  void startTracing();
  void giveUp();
  void focusTrace();
  void checkTrace();
  void updateTick();
  void onTick();
  void showTraceMenu(int index, const QPoint &globalPos);
};

} // namespace vcube
