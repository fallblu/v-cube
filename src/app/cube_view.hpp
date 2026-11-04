#pragma once

#include "app/cube_painter.hpp"

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

#include <deque>

namespace vcube {

// Shows a facelet cube in 3D (with animated turns), as a flat net, or not
// at all; clicking cycles through the three. The owner keeps the real cube
// state and feeds moves through animateMove(); the view's own copy catches
// up as the animations play.
class CubeView : public QWidget {
  Q_OBJECT
public:
  enum class Mode { ThreeD, Net, Hidden };

  explicit CubeView(QWidget *parent = nullptr);

  void setLook(const CubeLook &look);
  const CubeLook &look() const { return look_; }
  void setColourMap(const ColourMap &map);

  void setState(const FaceletCube &cube); // jumps, cancelling animations
  void animateMove(Move m);
  const FaceletCube &displayedState() const { return shown_; }

  // Duration of one quarter turn; 0 turns animation off.
  void setAnimationMs(int ms) { animationMs_ = ms; }

  void setMode(Mode m);
  Mode mode() const { return mode_; }
  void setClickCyclesMode(bool on) { clickCycles_ = on; }

  // Placeholder text drawn when the cube is hidden.
  void setHiddenText(const QString &text);

  QSize sizeHint() const override { return {360, 300}; }
  QSize minimumSizeHint() const override { return {120, 100}; }

signals:
  void modeChanged(vcube::CubeView::Mode mode);

protected:
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *e) override;

private:
  CubeLook look_;
  FaceletCube shown_;
  std::deque<Move> queue_;
  double progress_ = 0.0;
  int animationMs_ = 110;
  Mode mode_ = Mode::ThreeD;
  bool clickCycles_ = true;
  QString hiddenText_;
  QTimer frame_;
  QElapsedTimer clock_;
  qint64 lastFrame_ = 0;

  void advance();
};

} // namespace vcube
