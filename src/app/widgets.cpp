#include "app/widgets.hpp"

#include "app/colours.hpp"

#include "cube/solver.hpp"

#include <QApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QPainter>
#include <QPainterPath>
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
}

void TrendChart::setEmptyText(const QString &text) {
  emptyText_ = text;
  update();
}

void TrendChart::setSolves(const std::vector<Solve> &solves) {
  solves_ = solves;
  update();
}

void TrendChart::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  QRectF area = QRectF(rect()).adjusted(44, 10, -10, -20);
  QColor axis = palette().color(QPalette::PlaceholderText);
  p.setPen(axis);
  if (height() < 60) {
    return; // too squeezed to read
  }
  if (solves_.size() < 2) {
    p.drawText(rect(), Qt::AlignCenter, emptyText_);
    return;
  }
  std::vector<double> times;
  double lo = 1e18, hi = 0;
  for (const auto &s : solves_) {
    std::int64_t t = effectiveTime(s);
    double v = t == kDnfTime ? std::nan("") : static_cast<double>(t) / 1000.0;
    times.push_back(v);
    if (!std::isnan(v)) {
      lo = std::min(lo, v);
      hi = std::max(hi, v);
    }
  }
  if (hi <= 0) {
    return;
  }
  // Clip extreme outliers so the interesting range stays readable.
  std::vector<double> sorted;
  for (double v : times) {
    if (!std::isnan(v)) {
      sorted.push_back(v);
    }
  }
  std::sort(sorted.begin(), sorted.end());
  hi = std::min(hi, sorted[sorted.size() * 95 / 100] * 1.15);
  lo = std::max(0.0, lo - (hi - lo) * 0.05);
  if (hi - lo < 0.5) {
    hi = lo + 0.5;
  }
  std::size_t n = times.size();
  auto px = [&](std::size_t i) {
    return area.left() +
           area.width() * static_cast<double>(i) / static_cast<double>(n - 1);
  };
  auto py = [&](double v) {
    return area.bottom() - area.height() * (std::min(v, hi) - lo) / (hi - lo);
  };
  // Grid with labels.
  p.setPen(QPen(axis, 0.5, Qt::DotLine));
  for (int k = 0; k <= 4; ++k) {
    double v = lo + (hi - lo) * k / 4.0;
    double y = py(v);
    p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    p.drawText(QRectF(0, y - 8, 40, 16), Qt::AlignRight | Qt::AlignVCenter,
               QString::number(v, 'f', 1));
  }
  auto drawSeries = [&](const std::vector<StatValue> &vals, const QColor &c,
                        double width) {
    QPainterPath path;
    bool started = false;
    for (std::size_t i = 0; i < n; ++i) {
      if (!vals[i] || *vals[i] == kDnfTime) {
        started = false;
        continue;
      }
      QPointF pt(px(i), py(static_cast<double>(*vals[i]) / 1000.0));
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
  single.setAlpha(110);
  p.setPen(Qt::NoPen);
  p.setBrush(single);
  for (std::size_t i = 0; i < n; ++i) {
    if (!std::isnan(times[i])) {
      p.drawEllipse(QPointF(px(i), py(times[i])), 2.2, 2.2);
    }
  }
  drawSeries(rollingAverages(solves_, 5), QColor(0x3d, 0x8e, 0xf0), 1.8);
  drawSeries(rollingAverages(solves_, 12), QColor(0xff, 0x8a, 0x3d), 1.8);
  p.setPen(QColor(0x3d, 0x8e, 0xf0));
  p.drawText(QPointF(area.left() + 4, height() - 5), "ao5");
  p.setPen(QColor(0xff, 0x8a, 0x3d));
  p.drawText(QPointF(area.left() + 40, height() - 5), "ao12");
  p.setPen(single);
  p.drawText(QPointF(area.left() + 84, height() - 5), tr("single"));
}

} // namespace vcube
