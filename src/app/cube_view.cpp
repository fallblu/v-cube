#include "app/cube_view.hpp"

#include <QComboBox>
#include <QPainter>
#include <QSettings>

namespace vcube {

CubeView::CubeView(QWidget *parent) : QWidget(parent) {
  setFocusPolicy(Qt::NoFocus);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  frame_.setInterval(8);
  connect(&frame_, &QTimer::timeout, this, &CubeView::advance);
  clock_.start();
}

QString CubeView::modeName(Mode m) {
  switch (m) {
  case Mode::ThreeD:
    return "3d";
  case Mode::Flat:
    return "flat";
  case Mode::Net:
    return "net";
  case Mode::Hidden:
    return "hidden";
  }
  return "3d";
}

std::optional<CubeView::Mode> CubeView::modeFromName(const QString &name) {
  for (Mode m : {Mode::ThreeD, Mode::Flat, Mode::Net, Mode::Hidden}) {
    if (modeName(m) == name) {
      return m;
    }
  }
  return std::nullopt;
}

void CubeView::setLook(const CubeLook &look) {
  look_ = look;
  update();
}

void CubeView::setColourMap(const ColourMap &map) {
  look_.map = map;
  update();
}

void CubeView::setState(const FaceletCube &cube) {
  queue_.clear();
  progress_ = 0.0;
  frame_.stop();
  shown_ = cube;
  update();
}

void CubeView::animateMove(Move m) {
  if (animationMs_ <= 0 || mode_ != Mode::ThreeD) {
    // Finish anything pending, then jump.
    for (Move q : queue_) {
      shown_.apply(q);
    }
    queue_.clear();
    progress_ = 0.0;
    shown_.apply(m);
    update();
    return;
  }
  queue_.push_back(m);
  if (!frame_.isActive()) {
    lastFrame_ = clock_.elapsed();
    frame_.start();
  }
}

void CubeView::advance() {
  qint64 now = clock_.elapsed();
  double dt = static_cast<double>(now - lastFrame_);
  lastFrame_ = now;
  while (!queue_.empty() && dt > 0) {
    // Half turns take a bit longer; a backlog speeds everything up so the
    // view never lags far behind fast typing.
    double duration = animationMs_ * (queue_.front().amount == 2 ? 1.5 : 1.0);
    duration /= static_cast<double>(std::min<std::size_t>(queue_.size(), 4));
    double remaining = (1.0 - progress_) * duration;
    if (dt >= remaining) {
      dt -= remaining;
      shown_.apply(queue_.front());
      queue_.pop_front();
      progress_ = 0.0;
    } else {
      progress_ += dt / duration;
      dt = 0;
    }
  }
  if (queue_.empty()) {
    frame_.stop();
  }
  update();
}

void CubeView::finishAnimations() {
  for (Move q : queue_) {
    shown_.apply(q);
  }
  queue_.clear();
  progress_ = 0.0;
  frame_.stop();
  update();
}

void CubeView::setMode(Mode m) {
  if (m == mode_) {
    return;
  }
  if (m != Mode::ThreeD) {
    finishAnimations();
  }
  mode_ = m;
  update();
  emit modeChanged(m);
}

void CubeView::setHiddenText(const QString &text) {
  hiddenText_ = text;
  update();
}

void CubeView::paintEvent(QPaintEvent *) {
  QPainter p(this);
  QRectF area = QRectF(rect()).adjusted(6, 6, -6, -6);
  switch (mode_) {
  case Mode::ThreeD: {
    std::optional<TurnAnimation> turn;
    if (!queue_.empty()) {
      turn = TurnAnimation{queue_.front(), progress_};
    }
    cube_painter::paint3D(p, area, shown_, look_, turn);
    break;
  }
  case Mode::Flat:
    cube_painter::paintFlat(p, area, shown_, look_);
    break;
  case Mode::Net:
    cube_painter::paintNet(p, area, shown_, look_);
    break;
  case Mode::Hidden:
    p.setPen(palette().color(QPalette::PlaceholderText));
    p.drawText(area, Qt::AlignCenter,
               hiddenText_.isEmpty() ? tr("Cube hidden") : hiddenText_);
    break;
  }
}

QComboBox *createViewSelector(CubeView *view, const QString &mode,
                              QWidget *parent) {
  auto *box = new QComboBox(parent);
  box->setFocusPolicy(Qt::NoFocus);
  box->setToolTip(QObject::tr("How the virtual cube is shown"));
  using Mode = CubeView::Mode;
  const std::pair<Mode, QString> items[] = {
      {Mode::ThreeD, QObject::tr("3D")},
      {Mode::Flat, QObject::tr("Flat")},
      {Mode::Net, QObject::tr("Net")},
      {Mode::Hidden, QObject::tr("Hidden")},
  };
  for (const auto &[m, label] : items) {
    box->addItem(label, CubeView::modeName(m));
  }
  QString key = "ui/view/" + mode;
  Mode saved = CubeView::modeFromName(QSettings().value(key).toString())
                   .value_or(Mode::ThreeD);
  box->setCurrentIndex(box->findData(CubeView::modeName(saved)));
  view->setMode(saved);
  QObject::connect(box, &QComboBox::activated, view, [view, box, key] {
    QString name = box->currentData().toString();
    view->setMode(CubeView::modeFromName(name).value_or(Mode::ThreeD));
    QSettings().setValue(key, name);
  });
  return box;
}

} // namespace vcube
