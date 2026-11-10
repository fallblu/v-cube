#include "app/cube_view.hpp"

#include <QComboBox>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>

#include <algorithm>

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
  queuedTyped_.clear();
  progress_ = 0.0;
  frame_.stop();
  shown_ = cube;
  update();
}

void CubeView::animateMove(Move m) { enqueue(m, true); }

void CubeView::animateSequence(const Alg &moves) {
  for (Move m : moves) {
    enqueue(m, false);
  }
}

void CubeView::enqueue(Move m, bool typed) {
  if (animationMs_ <= 0 || mode_ != Mode::ThreeD) {
    // Finish anything pending, then jump.
    for (Move q : queue_) {
      shown_.apply(q);
    }
    queue_.clear();
    queuedTyped_.clear();
    progress_ = 0.0;
    shown_.apply(m);
    update();
    return;
  }
  queue_.push_back(m);
  queuedTyped_.push_back(typed);
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
    // Half turns take a bit longer; a backlog of typed moves speeds
    // everything up so the view never lags far behind fast typing, while
    // a sequence plays at the normal speed.
    double duration = animationMs_ * (queue_.front().amount == 2 ? 1.5 : 1.0);
    auto typed = std::count(queuedTyped_.begin(), queuedTyped_.end(), true);
    duration /= static_cast<double>(std::clamp<std::ptrdiff_t>(typed, 1, 4));
    double remaining = (1.0 - progress_) * duration;
    if (dt >= remaining) {
      dt -= remaining;
      shown_.apply(queue_.front());
      queue_.pop_front();
      queuedTyped_.pop_front();
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
  queuedTyped_.clear();
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

void CubeView::setMarks(const StickerMarks &marks) {
  marks_ = marks;
  update();
}

void CubeView::clearMarks() { setMarks(StickerMarks{}); }

StickerMarks CubeView::shownMarks() const {
  if (queue_.empty() || marks_.empty()) {
    return marks_;
  }
  // Number every sticker of the shown state and do the turns still to
  // animate: the sticker that ends up at facelet i is the one shown now at
  // where[i].
  FaceletCube::Stickers ids{};
  for (int i = 0; i < kFaceletCount; ++i) {
    ids[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
  }
  FaceletCube where(ids);
  for (Move m : queue_) {
    where.apply(m);
  }
  StickerMarks out;
  out.ringColour = marks_.ringColour;
  for (int f : marks_.rings) {
    out.rings.push_back(where[f]);
  }
  for (int i = 0; i < kFaceletCount; ++i) {
    out.labels[where[i]] = marks_.labels[static_cast<std::size_t>(i)];
  }
  return out;
}

StickerMarks movedMarks(const StickerMarks &marks, Move m) {
  // where[i]: the facelet the sticker now at i was at before the move.
  FaceletCube::Stickers ids{};
  for (int i = 0; i < kFaceletCount; ++i) {
    ids[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
  }
  FaceletCube where(ids);
  where.apply(m);
  StickerMarks out;
  out.ringColour = marks.ringColour;
  for (int i = 0; i < kFaceletCount; ++i) {
    int from = where[i];
    if (std::find(marks.rings.begin(), marks.rings.end(), from) !=
        marks.rings.end()) {
      out.rings.push_back(i);
    }
    out.labels[static_cast<std::size_t>(i)] =
        marks.labels[static_cast<std::size_t>(from)];
  }
  return out;
}

QRectF CubeView::drawArea() const {
  return QRectF(rect()).adjusted(6, 6, -6, -6);
}

void CubeView::mousePressEvent(QMouseEvent *e) {
  if (mode_ == Mode::Net && e->button() == Qt::LeftButton) {
    if (auto f = cube_painter::netFaceletAt(drawArea(), e->position())) {
      emit stickerClicked(*f);
      return;
    }
  }
  QWidget::mousePressEvent(e);
}

void CubeView::paintEvent(QPaintEvent *) {
  QPainter p(this);
  QRectF area = drawArea();
  StickerMarks marks = shownMarks();
  const StickerMarks *m = marks.empty() ? nullptr : &marks;
  switch (mode_) {
  case Mode::ThreeD: {
    std::optional<TurnAnimation> turn;
    if (!queue_.empty()) {
      turn = TurnAnimation{queue_.front(), progress_};
    }
    cube_painter::paint3D(p, area, shown_, look_, turn, m);
    break;
  }
  case Mode::Flat:
    cube_painter::paintFlat(p, area, shown_, look_, m);
    break;
  case Mode::Net:
    cube_painter::paintNet(p, area, shown_, look_, m);
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
