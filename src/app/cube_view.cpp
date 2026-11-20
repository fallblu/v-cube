#include "app/cube_view.hpp"

#include "app/ui_state.hpp"
#include "train/enum_ids.hpp"

#include <QComboBox>
#include <QEasingCurve>
#include <QPainter>

#include <algorithm>

namespace vcube {

CubeView::CubeView(QWidget *parent) : QWidget(parent) {
  setFocusPolicy(Qt::NoFocus);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  // About a display frame: advance() moves by the time passed, so a
  // faster timer only copies more frames.
  frame_.setTimerType(Qt::PreciseTimer);
  frame_.setInterval(16);
  connect(&frame_, &QTimer::timeout, this, &CubeView::advance);
  clock_.start();
  setAccessibleName(tr("Cube"));
}

void CubeView::describe() {
  int solved = shown_.isSolved() ? 1 : 0;
  if (solved != describedSolved_) {
    describedSolved_ = solved;
    setAccessibleDescription(solved ? tr("Solved") : tr("Not solved"));
  }
}

constexpr std::array<std::string_view, 4> kModeNames = {"3d", "flat", "net",
                                                        "hidden"};

QString CubeView::modeName(Mode m) {
  return QString::fromLatin1(idOf(kModeNames, m, "3d"));
}

std::optional<CubeView::Mode> CubeView::modeFromName(const QString &name) {
  return fromId<Mode>(kModeNames, name.toStdString());
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

void CubeView::animateMove(Move m) { enqueue(m, Source::Typed); }

void CubeView::animateSequence(const Alg &moves) {
  for (Move m : moves) {
    enqueue(m, Source::Played);
  }
}

void CubeView::enqueue(Move m, Source source) {
  if (animationMs_ <= 0 || mode_ != Mode::ThreeD) {
    // Finish anything pending, then jump.
    finishAnimations();
    shown_.apply(m);
    update();
    return;
  }
  queue_.push_back({m, source == Source::Typed});
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
    double duration =
        animationMs_ * (queue_.front().move.amount == 2 ? 1.5 : 1.0);
    auto typed = std::ranges::count(queue_, true, &Queued::typed);
    duration /= static_cast<double>(std::clamp<std::ptrdiff_t>(typed, 1, 4));
    double remaining = (1.0 - progress_) * duration;
    if (dt >= remaining) {
      dt -= remaining;
      shown_.apply(queue_.front().move);
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
  for (const Queued &q : queue_) {
    shown_.apply(q.move);
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
}

void CubeView::setMarks(const StickerMarks &marks) {
  marks_ = marks;
  update();
}

void CubeView::clearMarks() { setMarks(StickerMarks{}); }

const StickerMarks *CubeView::shownMarks(StickerMarks &moved) const {
  if (marks_.empty()) {
    return nullptr;
  }
  if (queue_.empty()) {
    return &marks_;
  }
  // Number every sticker of the shown state and do the turns still to
  // animate: the sticker that ends up at facelet i is the one shown now at
  // where[i].
  FaceletCube::Stickers ids{};
  for (int i = 0; i < kFaceletCount; ++i) {
    ids[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
  }
  FaceletCube where(ids);
  for (const Queued &q : queue_) {
    where.apply(q.move);
  }
  moved.ringColour = marks_.ringColour;
  for (int f : marks_.rings) {
    moved.rings.push_back(where[f]);
  }
  for (int i = 0; i < kFaceletCount; ++i) {
    moved.labels[where[i]] = marks_.labels[static_cast<std::size_t>(i)];
  }
  return &moved;
}

void CubeView::setFillsArea(bool on) {
  fills_ = on;
  update();
}

void CubeView::paintEvent(QPaintEvent *) {
  describe();
  QPainter p(this);
  QRectF area = QRectF(rect()).adjusted(6, 6, -6, -6);
  StickerMarks moved;
  const StickerMarks *m = shownMarks(moved);
  // The 3D and flat views draw the cube at two thirds of the space it is
  // given, centred; the net keeps the full area so its stickers stay easy
  // to click.
  QRectF cubeArea = area;
  if (!fills_ && (mode_ == Mode::ThreeD || mode_ == Mode::Flat)) {
    QSizeF size = area.size() * (2.0 / 3.0);
    cubeArea = QRectF(QPointF(), size);
    cubeArea.moveCenter(area.center());
  }
  switch (mode_) {
  case Mode::ThreeD: {
    std::optional<TurnAnimation> turn;
    if (!queue_.empty()) {
      // Played sequences (demos, applied algs) ease out; typed moves turn
      // evenly, so that a backlog keeps up.
      double at = progress_;
      if (!queue_.front().typed) {
        at = QEasingCurve(QEasingCurve::OutCubic).valueForProgress(at);
      }
      turn = TurnAnimation{queue_.front().move, at};
    }
    cube_painter::paint3D(p, cubeArea, shown_, look_, turn, m);
    break;
  }
  case Mode::Flat:
    cube_painter::paintFlat(p, cubeArea, shown_, look_, m);
    break;
  case Mode::Net:
    cube_painter::paintNet(p, area, shown_, look_, m);
    break;
  case Mode::Hidden:
    p.setPen(palette().color(QPalette::PlaceholderText));
    p.drawText(area, Qt::AlignCenter, tr("Cube hidden"));
    break;
  }
}

QComboBox *createViewSelector(CubeView *view, const QString &mode,
                              QWidget *parent) {
  auto *box = new QComboBox(parent);
  box->setFocusPolicy(Qt::NoFocus);
  using Mode = CubeView::Mode;
  const std::pair<Mode, QString> items[] = {
      {Mode::Flat, QObject::tr("Flat")},
      {Mode::ThreeD, QObject::tr("3D")},
      {Mode::Net, QObject::tr("Net")},
      {Mode::Hidden, QObject::tr("Hidden")},
  };
  for (const auto &[m, label] : items) {
    box->addItem(label, CubeView::modeName(m));
  }
  Stored<QString> stored = saved::cubeViewMode(mode);
  Mode savedMode = CubeView::modeFromName(stored.get()).value_or(Mode::Flat);
  box->setCurrentIndex(box->findData(CubeView::modeName(savedMode)));
  view->setMode(savedMode);
  QObject::connect(box, &QComboBox::activated, view, [view, box, stored] {
    QString name = box->currentData().toString();
    view->setMode(CubeView::modeFromName(name).value_or(Mode::Flat));
    stored.set(name);
  });
  return box;
}

} // namespace vcube
