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
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <optional>

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
  setAccessibleName(tr("Timer"));
  showTime(0);
}

void TimeDisplay::setCue(HoldCue cue) {
  if (cue == cue_) {
    return;
  }
  cue_ = cue;
  setAccessibleDescription(
      cue == HoldCue::Holding ? tr("Holding: keep holding until ready")
      : cue == HoldCue::Ready ? tr("Ready: let go to start")
                              : QString());
  update();
}

void TimeDisplay::paintEvent(QPaintEvent *e) {
  QLabel::paintEvent(e);
  if (cue_ == HoldCue::None) {
    return;
  }
  // A ring (hold on) or a filled dot (let go) on each side of the time.
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  QFontMetricsF fm(font());
  double d = std::max(8.0, fm.height() * 0.22);
  double half = fm.horizontalAdvance(text()) / 2.0;
  QPointF centre(width() / 2.0, height() / 2.0);
  QColor c = palette().color(QPalette::WindowText);
  p.setPen(QPen(c, std::max(2.0, d / 5.0)));
  p.setBrush(cue_ == HoldCue::Ready ? QBrush(c) : QBrush(Qt::NoBrush));
  for (double side : {-1.0, 1.0}) {
    QPointF at = centre + QPointF(side * (half + d * 1.2), 0);
    p.drawEllipse(at, d / 2.0, d / 2.0);
  }
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
  setCue(HoldCue::None);
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
    setCue(HoldCue::Holding);
    break;
  case S::Ready:
    show(inspection ? inspectText
                    : QString::fromStdString(formatTime(0, decimals_)),
         statusColour(Status::Good));
    setCue(HoldCue::Ready);
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
    : QWidget(parent), pointName_(tr("single")),
      emptyText_(tr("Times will be charted here")) {
  // The chart gives way first when the window is small: it only gets the
  // room the cube, scramble and timer leave over.
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
  setMouseTracking(true);
  setAccessibleName(tr("Trend chart"));
}

namespace {

Solve asSolve(std::int64_t timeMs, Penalty penalty) {
  Solve s;
  s.timeMs = timeMs;
  s.penalty = penalty;
  return s;
}

// The points of a line. Where there are far more points
// than pixel columns, only the lowest and highest point of each column are
// kept, in their order, which draws the same picture from at most about
// two points a column.
std::vector<QPointF> decimated(std::vector<QPointF> pts, double width) {
  if (static_cast<double>(pts.size()) <= 2 * width + 2) {
    return pts;
  }
  std::vector<QPointF> out;
  out.reserve(static_cast<std::size_t>(2 * width) + 4);
  std::size_t i = 0;
  while (i < pts.size()) {
    double column = std::floor(pts[i].x());
    std::size_t lo = i, hi = i;
    std::size_t j = i + 1;
    for (; j < pts.size() && std::floor(pts[j].x()) == column; ++j) {
      // Screen y grows downwards: the lowest value has the largest y.
      if (pts[j].y() > pts[lo].y()) {
        lo = j;
      }
      if (pts[j].y() < pts[hi].y()) {
        hi = j;
      }
    }
    out.push_back(pts[std::min(lo, hi)]);
    if (lo != hi) {
      out.push_back(pts[std::max(lo, hi)]);
    }
    i = j;
  }
  return out;
}

} // namespace

void TrendChart::invalidate() {
  plot_ = QPixmap();
  update();
}

void TrendChart::setEmptyText(const QString &text) {
  emptyText_ = text;
  invalidate();
}

void TrendChart::setMoveCounts(bool on) {
  moves_ = on;
  invalidate();
}

void TrendChart::setSettingsKey(const QString &key) {
  settingsKey_ = key;
  last_ = std::max(0, saved::chartLast(key).get());
  showSeries_ = saved::chartSeries(key).get();
  invalidate();
}

void TrendChart::setSolves(std::span<const Solve> solves) {
  solves_.clear();
  solves_.reserve(solves.size());
  for (const Solve &s : solves) {
    solves_.push_back({s.timeMs, s.penalty});
  }
  ao5_ = rollingAverages(solves, 5);
  ao12_ = rollingAverages(solves, 12);
  hover_ = -1;
  // The last solve and its averages in words, for assistive technology.
  setAccessibleDescription(
      solves_.empty() ? emptyText_
                      : tr("%1 solves. Last: %2")
                            .arg(solves_.size())
                            .arg(hoverText(solves_.size() - 1)
                                     .replace('\n', QStringLiteral(", "))));
  invalidate();
}

void TrendChart::setSeries(std::vector<Series> series) {
  series_ = std::move(series);
  invalidate();
}

void TrendChart::setPointName(const QString &name) {
  pointName_ = name;
  invalidate();
}

void TrendChart::setAverages(bool on) {
  averages_ = on;
  invalidate();
}

void TrendChart::setPointLabels(std::vector<QString> labels) {
  pointLabels_ = std::move(labels);
  invalidate();
}

bool TrendChart::showsSeries() const {
  return !series_.empty() && (showSeries_ || !averages_);
}

QStringList TrendChart::lineNames() const {
  QStringList names;
  if (showsSeries()) {
    for (const Series &s : series_) {
      names << s.name;
    }
  } else if (averages_) {
    names << QStringLiteral("ao5") << QStringLiteral("ao12");
  }
  return names;
}

std::size_t TrendChart::first() const {
  std::size_t n = solves_.size();
  auto last = static_cast<std::size_t>(last_);
  return last > 0 && n > last ? n - last : 0;
}

QRectF TrendChart::area() const {
  // On the left, the axis labels; under the plot, a line for the read-out
  // of the solve under the mouse, then the legend; on the right, room for
  // the name at the end of each line. All in lines of the font, so that
  // larger text isn't cut.
  const QFontMetrics fm = fontMetrics();
  const int lineH = fm.height();
  int right = fm.horizontalAdvance(QStringLiteral("ao12"));
  for (const QString &name : lineNames()) {
    right = std::max(right, fm.horizontalAdvance(name));
  }
  return QRectF(rect()).adjusted(fm.horizontalAdvance(QStringLiteral("000.0")) +
                                     8,
                                 lineH / 2 + 2, -(right + 8), -(2 * lineH + 6));
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

void TrendChart::changeEvent(QEvent *e) {
  switch (e->type()) {
  case QEvent::PaletteChange:
  case QEvent::FontChange:
  case QEvent::StyleChange:
    invalidate();
    break;
  default:
    break;
  }
  QWidget::changeEvent(e);
}

void TrendChart::paintEvent(QPaintEvent *) {
  if (height() < 60) {
    return; // too squeezed to read
  }
  // Drawn again for a new size or pixel ratio (a hidden widget gets a
  // resize event on every grab, so the size is compared here instead).
  const qreal dpr = devicePixelRatioF();
  if (plot_.isNull() || plot_.devicePixelRatio() != dpr ||
      plot_.deviceIndependentSize() != QSizeF(size())) {
    plot_ = QPixmap((QSizeF(size()) * dpr).toSize());
    plot_.setDevicePixelRatio(dpr);
    plot_.fill(Qt::transparent);
    QPainter pp(&plot_);
    pp.setFont(font());
    renderPlot(pp);
  }
  QPainter p(this);
  p.drawPixmap(0, 0, plot_);
  std::size_t from = first();
  std::size_t n = solves_.size() - from;
  if (solves_.size() < 2 || n < 2) {
    return;
  }
  // The solve under the mouse: a line across the plot, and its values on
  // the read-out line under it (tooltips are off in the app), cut short
  // with "…" where the line is narrower.
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF area = this->area();
  const QColor axis = palette().color(QPalette::PlaceholderText);
  const QFontMetrics fm = p.fontMetrics();
  QRectF readout(area.left(), area.bottom() + 2, area.width(), fm.height());
  auto fitted = [&](const QString &text) {
    return fm.elidedText(text, Qt::ElideRight,
                         static_cast<int>(readout.width()));
  };
  if (hover_ >= 0 && static_cast<std::size_t>(hover_) >= from &&
      static_cast<std::size_t>(hover_) < solves_.size()) {
    double hx = area.left() + area.width() *
                                  static_cast<double>(
                                      static_cast<std::size_t>(hover_) - from) /
                                  static_cast<double>(n - 1);
    p.setPen(QPen(axis, 0.8));
    p.drawLine(QPointF(hx, area.top()), QPointF(hx, area.bottom()));
    p.setPen(palette().color(QPalette::Text));
    p.drawText(readout, Qt::AlignLeft | Qt::AlignVCenter,
               fitted(hoverText(static_cast<std::size_t>(hover_))
                          .replace('\n', QStringLiteral("    "))));
  } else {
    p.setPen(axis);
    p.drawText(readout, Qt::AlignLeft | Qt::AlignVCenter,
               fitted(tr("Point at the chart to read its values")));
  }
}

void TrendChart::renderPlot(QPainter &p) {
  p.setRenderHint(QPainter::Antialiasing);
  QRectF area = this->area();
  QColor axis = palette().color(QPalette::PlaceholderText);
  p.setPen(axis);
  std::size_t from = first();
  std::size_t n = solves_.size() - from;
  if (solves_.size() < 2 || n < 2) {
    p.drawText(rect(), Qt::AlignCenter, emptyText_);
    return;
  }
  bool splits = showsSeries();
  std::vector<double> times;
  times.reserve(n);
  double lo = 1e18, hi = 0;
  auto see = [&](double v) {
    if (!std::isnan(v)) {
      lo = std::min(lo, v);
      hi = std::max(hi, v);
    }
  };
  for (std::size_t i = from; i < solves_.size(); ++i) {
    std::int64_t t =
        effectiveTime(asSolve(solves_[i].timeMs, solves_[i].penalty));
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
  sorted.reserve(times.size());
  for (double v : times) {
    if (!std::isnan(v)) {
      sorted.push_back(v);
    }
  }
  if (!sorted.empty()) {
    auto at =
        sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() * 95 / 100);
    std::nth_element(sorted.begin(), at, sorted.end());
    hi = std::min(hi, *at * 1.15);
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
  const QFontMetrics fm = p.fontMetrics();
  const double lineH = fm.height();
  // Grid with labels, on round values.
  int decimals = step >= 1 ? 0 : step >= 0.1 ? 1 : 2;
  p.setPen(QPen(axis, 0.5, Qt::DotLine));
  for (double v = lo; v <= hi + step / 2; v += step) {
    double y = py(v);
    p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    p.drawText(QRectF(0, y - lineH / 2, area.left() - 4, lineH),
               Qt::AlignRight | Qt::AlignVCenter,
               QString::number(v, 'f', decimals));
  }
  // Each line is named at its right end, so that the lines aren't told
  // apart by colour alone.
  std::vector<std::pair<double, std::pair<QString, QColor>>> ends;
  auto drawSeries = [&](const std::vector<StatValue> &vals, const QString &name,
                        const QColor &c, double width) {
    QPainterPath path;
    std::vector<QPointF> run;
    std::optional<QPointF> last;
    // A run of values without a gap becomes one stroke.
    auto flush = [&] {
      std::vector<QPointF> pts = decimated(std::move(run), area.width());
      run.clear();
      for (std::size_t k = 0; k < pts.size(); ++k) {
        if (k == 0) {
          path.moveTo(pts[k]);
        } else {
          path.lineTo(pts[k]);
        }
      }
    };
    for (std::size_t i = 0; i < n; ++i) {
      std::size_t at = from + i;
      if (at >= vals.size() || !vals[at] || *vals[at] == kDnfTime) {
        flush();
        continue;
      }
      QPointF pt(px(i), py(static_cast<double>(*vals[at]) / 1000.0));
      last = pt;
      run.push_back(pt);
    }
    flush();
    p.setPen(QPen(c, width));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    if (last) {
      ends.push_back({last->y(), {name, c}});
    }
  };
  QColor single = palette().color(QPalette::WindowText);
  single.setAlpha(splits ? 60 : 110);
  p.setPen(Qt::NoPen);
  p.setBrush(single);
  // One dot per 2 px cell: dots closer than that draw the same picture,
  // so a long session draws far fewer of them.
  const auto cols = static_cast<std::size_t>(width() / 2 + 1);
  std::vector<bool> taken(cols * static_cast<std::size_t>(height() / 2 + 1));
  for (std::size_t i = 0; i < n; ++i) {
    if (std::isnan(times[i])) {
      continue;
    }
    const QPointF d(px(i), py(times[i]));
    const auto cx =
        static_cast<std::size_t>(std::clamp(d.x(), 0.0, 1.0 * width()) / 2);
    const auto cy =
        static_cast<std::size_t>(std::clamp(d.y(), 0.0, 1.0 * height()) / 2);
    const std::size_t cell = cy * cols + cx;
    if (!taken[cell]) {
      taken[cell] = true;
      p.drawEllipse(d, 2.2, 2.2);
    }
  }
  std::vector<std::pair<QString, QColor>> legend;
  if (splits) {
    for (const Series &s : series_) {
      QColor c = roleColour(s.colour);
      drawSeries(s.values, s.name, c, 1.6);
      legend.emplace_back(s.name, c);
    }
  } else if (averages_) {
    QColor ao5 = roleColour(Role::Series1);
    QColor ao12 = roleColour(Role::Series2);
    drawSeries(ao5_, "ao5", ao5, 1.8);
    drawSeries(ao12_, "ao12", ao12, 1.8);
    legend = {{"ao5", ao5}, {"ao12", ao12}};
  }
  // The names at the line ends, nudged apart where they would overlap.
  std::sort(ends.begin(), ends.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });
  double above = -1e9;
  for (auto &end : ends) {
    end.first = std::max(end.first, above + lineH);
    above = end.first;
  }
  for (const auto &[y, label] : ends) {
    p.setPen(label.second);
    p.drawText(QRectF(area.right() + 4, y - lineH / 2,
                      width() - area.right() - 4, lineH),
               Qt::AlignLeft | Qt::AlignVCenter, label.first);
  }
  legend.emplace_back(pointName_, axis);
  const QRectF bottomRow(area.left(), height() - lineH - 2, area.width(),
                         lineH);
  double x = area.left() + 4;
  for (const auto &[name, colour] : legend) {
    p.setPen(colour);
    p.drawText(QRectF(x, bottomRow.top(), width() - x, lineH),
               Qt::AlignLeft | Qt::AlignVCenter, name);
    x += fm.horizontalAdvance(name) + 14;
  }
  if (from > 0 || last_ > 0) {
    p.setPen(axis);
    p.drawText(bottomRow, Qt::AlignRight | Qt::AlignVCenter,
               tr("last %1 of %2").arg(n).arg(solves_.size()));
  }
}

QString TrendChart::hoverText(std::size_t at) const {
  const Solve s = asSolve(solves_[at].timeMs, solves_[at].penalty);
  QStringList lines;
  const QString value = moves_ ? valueText(effectiveTime(s))
                               : QString::fromStdString(formatSolve(s));
  if (at < pointLabels_.size()) {
    lines << tr("%1: %2").arg(pointLabels_[at], value);
  } else {
    lines << tr("#%1: %2").arg(at + 1).arg(value);
  }
  if (showsSeries()) {
    for (const Series &sr : series_) {
      if (at < sr.values.size() && sr.values[at]) {
        lines << QString("%1: %2").arg(sr.name, valueText(sr.values[at]));
      }
    }
  } else if (averages_) {
    if (at < ao5_.size() && ao5_[at]) {
      lines << QString("ao5: %1").arg(valueText(ao5_[at]));
    }
    if (at < ao12_.size() && ao12_[at]) {
      lines << QString("ao12: %1").arg(valueText(ao12_[at]));
    }
  }
  return lines.join("\n");
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
      invalidate();
    });
  }
  // Without the averages, the series are always drawn.
  if (!series_.empty() && averages_) {
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
      invalidate();
    });
  }
  menu.exec(e->globalPos());
}

} // namespace vcube
