#pragma once

#include "app/cube_painter.hpp"

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

#include <deque>

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
  void animateMove(Move m);
  const FaceletCube &displayedState() const { return shown_; }

  // Duration of one quarter turn; 0 turns animation off.
  void setAnimationMs(int ms) { animationMs_ = ms; }

  void setMode(Mode m);
  Mode mode() const { return mode_; }
  // Finishes any animations at once.
  void finishAnimations();

  // Placeholder text drawn when the cube is hidden.
  void setHiddenText(const QString &text);

  QSize sizeHint() const override { return {360, 300}; }
  QSize minimumSizeHint() const override { return {120, 100}; }

signals:
  void modeChanged(vcube::CubeView::Mode mode);

protected:
  void paintEvent(QPaintEvent *) override;

private:
  CubeLook look_;
  FaceletCube shown_;
  std::deque<Move> queue_;
  double progress_ = 0.0;
  int animationMs_ = 110;
  Mode mode_ = Mode::ThreeD;
  QString hiddenText_;
  QTimer frame_;
  QElapsedTimer clock_;
  qint64 lastFrame_ = 0;

  void advance();
};

// A drop-down (3D, Flat, Net, Hidden) that switches `view` and remembers
// the choice for `mode` under "ui/view/<mode>".
QComboBox *createViewSelector(CubeView *view, const QString &mode,
                              QWidget *parent);

} // namespace vcube
