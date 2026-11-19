#pragma once

#include "app/cube_painter.hpp"

#include <QElapsedTimer>
#include <QRectF>
#include <QSize>
#include <QTimer>
#include <QWidget>

#include <deque>
#include <optional>

class QComboBox;

namespace vcube {

// Shows a facelet cube in 3D (with animated turns), in Alg-Trainer's flat
// view, as an unfolded net, or not at all. The owner keeps the real cube
// state and feeds moves through animateMove(); the view's own copy catches
// up as the animations play.
class CubeView : public QWidget {
  Q_OBJECT
public:
  enum class Mode { ThreeD, Flat, Net, Hidden };
  static QString modeName(Mode m); // "3d", "flat", "net", "hidden"
  static std::optional<Mode> modeFromName(const QString &name);

  explicit CubeView(QWidget *parent = nullptr);

  void setLook(const CubeLook &look);
  const CubeLook &look() const { return look_; }
  void setColourMap(const ColourMap &map);

  void setState(const FaceletCube &cube); // jumps, cancelling animations
  // A move as it is typed: a backlog of these plays faster, so that the
  // view keeps up with quick turning.
  void animateMove(Move m);
  // Moves played one after another at the normal speed, e.g. an alg typed
  // in and applied.
  void animateSequence(const Alg &moves);
  const FaceletCube &displayedState() const { return shown_; }

  // Duration of one quarter turn; 0 turns animation off.
  void setAnimationMs(int ms) { animationMs_ = ms; }

  void setMode(Mode m);
  Mode mode() const { return mode_; }
  // Finishes any animations at once.
  void finishAnimations();

  // Rings and labels drawn over stickers, by facelet of the state after the
  // moves fed so far: while turns are still animating, the marks move with
  // the stickers they will end up on.
  void setMarks(const StickerMarks &marks);
  void clearMarks();
  const StickerMarks &marks() const { return marks_; }

  // The 3D and top views are drawn at two thirds of the view, with room
  // around them; a view that fills its area draws them as large as it
  // allows, for a stage that sizes the view itself and puts what goes
  // with the cube right against it (see SolveStage).
  void setFillsArea(bool on);

  QSize sizeHint() const override { return {360, 300}; }
  QSize minimumSizeHint() const override { return {120, 100}; }

protected:
  void paintEvent(QPaintEvent *) override;

private:
  CubeLook look_;
  FaceletCube shown_;
  std::deque<Move> queue_;
  std::deque<bool> queuedTyped_; // per queued move: from animateMove()
  double progress_ = 0.0;
  int animationMs_ = 110;
  Mode mode_ = Mode::Flat;
  bool fills_ = false;
  StickerMarks marks_;
  QTimer frame_;
  QElapsedTimer clock_;
  qint64 lastFrame_ = 0;
  int describedSolved_ = -1; // the accessible description says so (0/1)

  void advance();
  // Says in the accessible description whether the cube shown is solved.
  void describe();
  void enqueue(Move m, bool typed);
  QRectF drawArea() const;
  // marks_ moved back through the turns still to animate, onto the stickers
  // of the state shown now.
  StickerMarks shownMarks() const;
};

// A drop-down (3D, Flat, Net, Hidden) that switches `view` and remembers
// the choice for `mode` under "ui/view/<mode>".
QComboBox *createViewSelector(CubeView *view, const QString &mode,
                              QWidget *parent);

} // namespace vcube
