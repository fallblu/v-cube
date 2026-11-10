#include "app/widgets.hpp"

#include "app/colours.hpp"
#include "app/ui_state.hpp"

#include "cube/solver.hpp"
#include "train/move_count.hpp"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QElapsedTimer>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>

namespace vcube {

std::int64_t monotonicMs() {
  static QElapsedTimer clock = [] {
    QElapsedTimer t;
    t.start();
    return t;
  }();
  return clock.elapsed();
}

std::int64_t unixNow() { return QDateTime::currentSecsSinceEpoch(); }

std::int64_t localDayStart() {
  QDateTime now = QDateTime::currentDateTime();
  return QDateTime(now.date(), QTime(0, 0)).toSecsSinceEpoch();
}

std::int64_t localDayEnd() {
  QDateTime now = QDateTime::currentDateTime();
  return QDateTime(now.date().addDays(1), QTime(0, 0)).toSecsSinceEpoch();
}

namespace {

int gScrambleFontPt = 17;
int gTimerFontPt = 54;
std::function<void(int)> gInspectionCall;
const char *const kScrambleLabel = "vcubeScrambleLabel";

void setPointSize(QWidget *w, int pt) {
  QFont f = w->font();
  f.setPointSize(pt);
  w->setFont(f);
}

} // namespace

void markScrambleLabel(QLabel *label) {
  label->setProperty(kScrambleLabel, true);
  setPointSize(label, gScrambleFontPt);
}

void setDisplayFontSizes(int scramblePt, int timerPt) {
  gScrambleFontPt = scramblePt;
  gTimerFontPt = timerPt;
  for (QWidget *w : QApplication::allWidgets()) {
    if (w->property(kScrambleLabel).toBool()) {
      setPointSize(w, scramblePt);
    } else if (auto *display = qobject_cast<TimeDisplay *>(w)) {
      setPointSize(display, timerPt);
      display->setMinimumHeight(QFontMetrics(display->font()).height());
    }
  }
}

void setInspectionCallHandler(std::function<void(int seconds)> call) {
  gInspectionCall = std::move(call);
}

TimeDisplay::TimeDisplay(QWidget *parent) : QLabel(parent) {
  setAlignment(Qt::AlignCenter);
  QFont f = font();
  f.setPointSize(gTimerFontPt);
  f.setBold(true);
  // The first of these that is installed: Linux, Windows, then macOS.
  f.setFamilies({"DejaVu Sans Mono", "Noto Sans Mono", "Liberation Mono",
                 "Cascadia Mono", "Consolas", "Menlo", "Courier New"});
  f.setStyleHint(QFont::Monospace);
  setFont(f);
  setMinimumHeight(QFontMetrics(f).height());
  showTime(0);
}

void TimeDisplay::setColour(const QColor &c) {
  colour_ = c;
  // Only a colour of its own is set, so that the text otherwise follows
  // the theme, also when it changes.
  QPalette p;
  if (c.isValid()) {
    p.setColor(QPalette::WindowText, c);
  }
  setPalette(p);
}

void TimeDisplay::showText(const QString &text, const QColor &colour) {
  beforeHold_.reset();
  show(text, colour);
}

void TimeDisplay::show(const QString &text, const QColor &colour) {
  setColour(colour);
  setText(text);
}

void TimeDisplay::showTime(std::int64_t ms, Penalty penalty) {
  Solve s;
  s.timeMs = ms;
  s.penalty = penalty;
  showText(QString::fromStdString(formatSolve(s, decimals_)));
}

void TimeDisplay::showTimer(const SolveTimer &timer, std::int64_t now) {
  using S = SolveTimer::State;
  auto inspection = timer.inspectionElapsed(now);
  QString inspectText;
  if (inspection) {
    std::int64_t left = timer.config().inspectionMs - *inspection;
    if (left > 0) {
      inspectText = QString::number((left + 999) / 1000);
    } else if (left > -2000) {
      inspectText = "+2";
    } else {
      inspectText = "DNF";
    }
  }
  if (inspection) {
    for (int call : {8, 12}) {
      std::int64_t at = call * 1000;
      if (*inspection >= at && lastInspection_ < at && gInspectionCall) {
        gInspectionCall(call);
      }
    }
    lastInspection_ = *inspection;
  } else {
    lastInspection_ = -1;
  }
  bool holding = timer.state() == S::Holding || timer.state() == S::Ready;
  if (holding && !beforeHold_) {
    beforeHold_ = Shown{text(), colour_};
  }
  switch (timer.state()) {
  case S::Idle:
    // Leave the last result up, or bring it back after a hold that was let
    // go of (or cancelled) before the timer started.
    if (beforeHold_) {
      Shown was = *beforeHold_;
      showText(was.text, was.colour);
    }
    break;
  case S::Inspecting: {
    std::int64_t t = *inspection;
    QColor c = t >= 12000  ? statusColour(Status::Bad)
               : t >= 8000 ? statusColour(Status::Warning)
                           : QColor();
    showText(inspectText, c);
    break;
  }
  case S::Holding:
    show(inspection ? inspectText
                    : QString::fromStdString(formatTime(0, decimals_)),
         statusColour(Status::Bad));
    break;
  case S::Ready:
    show(inspection ? inspectText
                    : QString::fromStdString(formatTime(0, decimals_)),
         statusColour(Status::Good));
    break;
  case S::Running: {
    // One decimal while running keeps the display calm, like cstimer.
    showText(QString::fromStdString(formatTime(timer.elapsed(now), 1)));
    break;
  }
  case S::Stopped:
    break;
  }
}

QFuture<GeneratedScramble>
generateScramble(std::function<ScrambleSetup(Rng &)> makeState,
                 ColourNeutrality neutrality) {
  return QtConcurrent::run([makeState = std::move(makeState),
                            neutrality = std::move(neutrality)] {
    thread_local Rng rng = makeRng();
    ScrambleSetup setup = makeState(rng);
    Hold hold = neutrality.pick(rng);
    Alg alg = scrambleToState(heldState(setup.state, hold));
    return GeneratedScramble{setup.state, QString::fromStdString(toString(alg)),
                             std::move(setup.setId), std::move(setup.caseId),
                             hold};
  });
}

TrendChart::TrendChart(QWidget *parent)
    : QWidget(parent), emptyText_(tr("Times will be charted here")) {
  // The chart gives way first when the window is small: it only gets the
  // room the cube, scramble and timer leave over.
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
  setMouseTracking(true);
}

void TrendChart::setEmptyText(const QString &text) {
  emptyText_ = text;
  update();
}

void TrendChart::setMoveCounts(bool on) {
  moves_ = on;
  update();
}

void TrendChart::setSettingsKey(const QString &key) {
  settingsKey_ = key;
  last_ = std::max(0, saved::chartLast(key).get());
  showSeries_ = saved::chartSeries(key).get();
  update();
}

void TrendChart::setSolves(const std::vector<Solve> &solves) {
  solves_ = solves;
  hover_ = -1;
  update();
}

void TrendChart::setSeries(std::vector<Series> series) {
  series_ = std::move(series);
  update();
}

std::size_t TrendChart::first() const {
  std::size_t n = solves_.size();
  auto last = static_cast<std::size_t>(last_);
  return last > 0 && n > last ? n - last : 0;
}

QRectF TrendChart::area() const {
  return QRectF(rect()).adjusted(44, 10, -10, -20);
}

QString TrendChart::valueText(StatValue v) const {
  return QString::fromStdString(
      moves_ ? formatMoves(v, v && *v % 1000 == 0 ? 0 : 2) : formatTime(v));
}

double niceTickStep(double lo, double hi, int ticks, double minStep) {
  double range = std::max(hi - lo, 1e-9);
  double raw = range / std::max(ticks, 1);
  double power = std::pow(10.0, std::floor(std::log10(raw)));
  double step = power;
  for (double m : {1.0, 2.0, 5.0, 10.0}) {
    step = m * power;
    if (step >= raw) {
      break;
    }
  }
  return std::max(step, minStep);
}

void TrendChart::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  QRectF area = this->area();
  QColor axis = palette().color(QPalette::PlaceholderText);
  p.setPen(axis);
  if (height() < 60) {
    return; // too squeezed to read
  }
  std::size_t from = first();
  std::size_t n = solves_.size() - from;
  if (solves_.size() < 2 || n < 2) {
    p.drawText(rect(), Qt::AlignCenter, emptyText_);
    return;
  }
  bool splits = showSeries_ && !series_.empty();
  std::vector<double> times;
  double lo = 1e18, hi = 0;
  auto see = [&](double v) {
    if (!std::isnan(v)) {
      lo = std::min(lo, v);
      hi = std::max(hi, v);
    }
  };
  for (std::size_t i = from; i < solves_.size(); ++i) {
    std::int64_t t = effectiveTime(solves_[i]);
    double v = t == kDnfTime ? std::nan("") : static_cast<double>(t) / 1000.0;
    times.push_back(v);
    see(v);
  }
  if (splits) {
    for (const Series &s : series_) {
      for (std::size_t i = from; i < s.values.size(); ++i) {
        if (s.values[i] && *s.values[i] != kDnfTime) {
          see(static_cast<double>(*s.values[i]) / 1000.0);
        }
      }
    }
  }
  if (hi <= 0 && lo > hi) {
    p.drawText(rect(), Qt::AlignCenter,
               moves_ ? tr("Every attempt here is a DNF")
                      : tr("Every solve here is a DNF"));
    return;
  }
  // Clip extreme outliers so the interesting range stays readable.
  std::vector<double> sorted;
  for (double v : times) {
    if (!std::isnan(v)) {
      sorted.push_back(v);
    }
  }
  if (!sorted.empty()) {
    std::sort(sorted.begin(), sorted.end());
    hi = std::min(hi, sorted[sorted.size() * 95 / 100] * 1.15);
  }
  double minStep = moves_ ? 1.0 : 0.0;
  double step = niceTickStep(lo, hi, 4, minStep);
  lo = std::max(0.0, std::floor(lo / step) * step);
  hi = std::ceil(hi / step) * step;
  if (hi <= lo) {
    hi = lo + step;
  }
  auto px = [&](std::size_t i) {
    return area.left() +
           area.width() * static_cast<double>(i) / static_cast<double>(n - 1);
  };
  auto py = [&](double v) {
    return area.bottom() - area.height() * (std::min(v, hi) - lo) / (hi - lo);
  };
  // Grid with labels, on round values.
  int decimals = step >= 1 ? 0 : step >= 0.1 ? 1 : 2;
  p.setPen(QPen(axis, 0.5, Qt::DotLine));
  for (double v = lo; v <= hi + step / 2; v += step) {
    double y = py(v);
    p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    p.drawText(QRectF(0, y - 8, 40, 16), Qt::AlignRight | Qt::AlignVCenter,
               QString::number(v, 'f', decimals));
  }
  auto drawSeries = [&](const std::vector<StatValue> &vals, const QColor &c,
                        double width) {
    QPainterPath path;
    bool started = false;
    for (std::size_t i = 0; i < n; ++i) {
      std::size_t at = from + i;
      if (at >= vals.size() || !vals[at] || *vals[at] == kDnfTime) {
        started = false;
        continue;
      }
      QPointF pt(px(i), py(static_cast<double>(*vals[at]) / 1000.0));
      if (started) {
        path.lineTo(pt);
      } else {
        path.moveTo(pt);
        started = true;
      }
    }
    p.setPen(QPen(c, width));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
  };
  QColor single = palette().color(QPalette::WindowText);
  single.setAlpha(splits ? 60 : 110);
  p.setPen(Qt::NoPen);
  p.setBrush(single);
  for (std::size_t i = 0; i < n; ++i) {
    if (!std::isnan(times[i])) {
      p.drawEllipse(QPointF(px(i), py(times[i])), 2.2, 2.2);
    }
  }
  std::vector<std::pair<QString, QColor>> legend;
  if (splits) {
    for (const Series &s : series_) {
      drawSeries(s.values, s.colour, 1.6);
      legend.emplace_back(s.name, s.colour);
    }
  } else {
    drawSeries(rollingAverages(solves_, 5), QColor(0x3d, 0x8e, 0xf0), 1.8);
    drawSeries(rollingAverages(solves_, 12), QColor(0xff, 0x8a, 0x3d), 1.8);
    legend = {{"ao5", QColor(0x3d, 0x8e, 0xf0)},
              {"ao12", QColor(0xff, 0x8a, 0x3d)}};
  }
  legend.emplace_back(tr("single"), single);
  double x = area.left() + 4;
  for (const auto &[name, colour] : legend) {
    p.setPen(colour);
    p.drawText(QPointF(x, height() - 5), name);
    x += p.fontMetrics().horizontalAdvance(name) + 14;
  }
  if (from > 0 || last_ > 0) {
    p.setPen(axis);
    p.drawText(QRectF(area.left(), height() - 18, area.width(), 16),
               Qt::AlignRight | Qt::AlignVCenter,
               tr("last %1 of %2").arg(n).arg(solves_.size()));
  }
  // The solve under the mouse.
  if (hover_ >= 0 && static_cast<std::size_t>(hover_) >= from &&
      static_cast<std::size_t>(hover_) < solves_.size()) {
    double hx = px(static_cast<std::size_t>(hover_) - from);
    p.setPen(QPen(axis, 0.8));
    p.drawLine(QPointF(hx, area.top()), QPointF(hx, area.bottom()));
  }
}

void TrendChart::mouseMoveEvent(QMouseEvent *e) {
  std::size_t from = first();
  std::size_t n = solves_.size() - from;
  QRectF a = area();
  if (n < 2 || a.width() <= 0) {
    return;
  }
  double t = (e->position().x() - a.left()) / a.width();
  auto i = static_cast<std::size_t>(
      std::clamp(std::lround(t * static_cast<double>(n - 1)), 0L,
                 static_cast<long>(n - 1)));
  std::size_t at = from + i;
  if (static_cast<int>(at) != hover_) {
    hover_ = static_cast<int>(at);
    update();
  }
  const Solve &s = solves_[at];
  QStringList lines;
  lines << tr("#%1: %2").arg(at + 1).arg(
      moves_ ? valueText(effectiveTime(s))
             : QString::fromStdString(formatSolve(s)));
  if (showSeries_ && !series_.empty()) {
    for (const Series &sr : series_) {
      if (at < sr.values.size() && sr.values[at]) {
        lines << QString("%1: %2").arg(sr.name, valueText(sr.values[at]));
      }
    }
  } else {
    auto ao5 = rollingAverages(std::span(solves_).first(at + 1), 5);
    auto ao12 = rollingAverages(std::span(solves_).first(at + 1), 12);
    if (ao5.back()) {
      lines << QString("ao5: %1").arg(valueText(ao5.back()));
    }
    if (ao12.back()) {
      lines << QString("ao12: %1").arg(valueText(ao12.back()));
    }
  }
  QToolTip::showText(e->globalPosition().toPoint(), lines.join("\n"), this);
}

void TrendChart::leaveEvent(QEvent *e) {
  hover_ = -1;
  update();
  QWidget::leaveEvent(e);
}

void TrendChart::contextMenuEvent(QContextMenuEvent *e) {
  QMenu menu(this);
  for (int n : {0, 12, 50, 100, 500}) {
    QAction *a = menu.addAction(n == 0 ? tr("Chart every solve")
                                       : tr("Chart the last %1").arg(n));
    a->setCheckable(true);
    a->setChecked(last_ == n);
    connect(a, &QAction::triggered, this, [this, n] {
      last_ = n;
      if (!settingsKey_.isEmpty()) {
        saved::chartLast(settingsKey_).set(n);
      }
      update();
    });
  }
  if (!series_.empty()) {
    menu.addSeparator();
    QStringList names;
    for (const Series &s : series_) {
      names << s.name;
    }
    QAction *a = menu.addAction(tr("Show %1").arg(names.join(" / ")));
    a->setCheckable(true);
    a->setChecked(showSeries_);
    connect(a, &QAction::toggled, this, [this](bool on) {
      showSeries_ = on;
      if (!settingsKey_.isEmpty()) {
        saved::chartSeries(settingsKey_).set(on);
      }
      update();
    });
  }
  menu.exec(e->globalPos());
}

} // namespace vcube
