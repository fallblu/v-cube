#include "app/lab_chart.hpp"

#include "app/widgets.hpp"
#include "train/stats.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace vcube {

namespace {

double quantile(std::vector<std::int64_t> v, double q) {
  std::sort(v.begin(), v.end());
  double at = q * static_cast<double>(v.size() - 1);
  auto lo = static_cast<std::size_t>(std::floor(at));
  auto hi = static_cast<std::size_t>(std::ceil(at));
  double f = at - static_cast<double>(lo);
  return static_cast<double>(v[lo]) * (1 - f) + static_cast<double>(v[hi]) * f;
}

} // namespace

LabChart::LabChart(QWidget *parent) : QWidget(parent) {
  setMouseTracking(true);
  setMinimumHeight(140);
}

void LabChart::setSeries(std::vector<Series> series) {
  series_ = std::move(series);
  hover_ = -1;
  update();
}

void LabChart::setMode(Mode m) {
  mode_ = m;
  hover_ = -1;
  update();
}

void LabChart::setDecimals(int d) {
  decimals_ = d;
  update();
}

void LabChart::setEmptyText(const QString &text) {
  emptyText_ = text;
  update();
}

void LabChart::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QPalette &pal = palette();
  const QColor ink = pal.color(QPalette::WindowText);
  QColor muted = ink;
  muted.setAlphaF(0.55f);
  QColor grid = ink;
  grid.setAlphaF(0.12f);
  dots_.clear();

  std::int64_t lo = std::numeric_limits<std::int64_t>::max(), hi = 0;
  std::size_t longest = 0;
  for (const auto &s : series_) {
    for (auto t : s.times) {
      lo = std::min(lo, t);
      hi = std::max(hi, t);
    }
    longest = std::max(longest, s.times.size());
  }
  if (series_.empty() || hi == 0) {
    p.setPen(muted);
    p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, emptyText_);
    return;
  }

  // Legend across the top, the algs in order.
  QFontMetrics fm(font());
  const int lineH = fm.height();
  double x = 8, y = 4;
  for (const auto &s : series_) {
    QString name = fm.elidedText(s.name, Qt::ElideRight, 220);
    int w = 14 + fm.horizontalAdvance(name) + 14;
    if (x + w > width() - 8 && x > 8) {
      x = 8;
      y += lineH + 2;
    }
    p.setPen(Qt::NoPen);
    p.setBrush(s.colour);
    p.drawRoundedRect(QRectF(x, y + lineH / 2.0 - 4, 8, 8), 2, 2);
    p.setPen(ink);
    p.drawText(QPointF(x + 12, y + fm.ascent()), name);
    x += w;
  }
  const double top = y + lineH + 10;
  const double left = fm.horizontalAdvance("00.00") + 14;
  const double bottom = height() - (mode_ == Mode::Trend ? lineH + 8 : 10);
  const double right = width() - 10;
  if (bottom - top < 30 || right - left < 40) {
    return;
  }

  // Time axis: rounded ticks around the times charted.
  double lof = static_cast<double>(lo) / 1000.0;
  double hif = static_cast<double>(hi) / 1000.0;
  double pad = std::max(0.05, (hif - lof) * 0.08);
  lof = std::max(0.0, lof - pad);
  hif += pad;
  double step = niceTickStep(lof, hif, 4, 0.01);
  double from = std::floor(lof / step) * step;
  double to = std::ceil(hif / step) * step;
  auto yOf = [&](double secs) {
    return bottom - (secs - from) / (to - from) * (bottom - top);
  };
  p.setPen(QPen(grid, 1));
  for (double t = from; t <= to + step / 2; t += step) {
    double yy = yOf(t);
    p.setPen(QPen(grid, 1));
    p.drawLine(QPointF(left, yy), QPointF(right, yy));
    p.setPen(muted);
    p.drawText(QRectF(0, yy - lineH / 2.0, left - 6, lineH),
               Qt::AlignRight | Qt::AlignVCenter,
               QString::number(t, 'f', step < 0.1 ? 2 : 1));
  }

  if (mode_ == Mode::Spread) {
    const double band = (right - left) / static_cast<double>(series_.size());
    for (std::size_t i = 0; i < series_.size(); ++i) {
      const auto &s = series_[i];
      if (s.times.empty()) {
        continue;
      }
      double cx = left + band * (static_cast<double>(i) + 0.5);
      double half = std::min(28.0, band * 0.3);
      // The middle half of the times as a box, with the median across it.
      double q1 = quantile(s.times, 0.25) / 1000.0;
      double q3 = quantile(s.times, 0.75) / 1000.0;
      double med = quantile(s.times, 0.5) / 1000.0;
      QColor fill = s.colour;
      fill.setAlphaF(0.18f);
      p.setPen(QPen(s.colour, 1.5));
      p.setBrush(fill);
      p.drawRoundedRect(QRectF(cx - half, yOf(q3), 2 * half, yOf(q1) - yOf(q3)),
                        4, 4);
      p.setPen(QPen(s.colour, 2.5));
      p.drawLine(QPointF(cx - half, yOf(med)), QPointF(cx + half, yOf(med)));
      // Each time as a dot, spread sideways so that equal times show.
      p.setPen(QPen(pal.color(QPalette::Base), 1.5));
      p.setBrush(s.colour);
      for (std::size_t k = 0; k < s.times.size(); ++k) {
        double jitter =
            (static_cast<double>((k * 7919) % 97) / 96.0 - 0.5) * half * 1.4;
        QPointF at(cx + jitter, yOf(static_cast<double>(s.times[k]) / 1000.0));
        p.drawEllipse(at, 4, 4);
        dots_.push_back({at, i, s.times[k]});
      }
    }
  } else {
    double span = std::max<double>(1, static_cast<double>(longest) - 1);
    auto xOf = [&](std::size_t k) {
      return left + static_cast<double>(k) / span * (right - left);
    };
    p.setPen(muted);
    p.drawText(QRectF(left, bottom + 4, right - left, lineH), Qt::AlignCenter,
               tr("attempt"));
    for (std::size_t i = 0; i < series_.size(); ++i) {
      const auto &s = series_[i];
      QPainterPath path;
      for (std::size_t k = 0; k < s.times.size(); ++k) {
        QPointF at(xOf(k), yOf(static_cast<double>(s.times[k]) / 1000.0));
        if (k == 0) {
          path.moveTo(at);
        } else {
          path.lineTo(at);
        }
        dots_.push_back({at, i, s.times[k]});
      }
      p.setPen(QPen(s.colour, 2));
      p.setBrush(Qt::NoBrush);
      p.drawPath(path);
      p.setPen(QPen(pal.color(QPalette::Base), 1.5));
      p.setBrush(s.colour);
      for (std::size_t k = 0; k < s.times.size(); ++k) {
        p.drawEllipse(
            QPointF(xOf(k), yOf(static_cast<double>(s.times[k]) / 1000.0)), 3,
            3);
      }
    }
  }

  if (hover_ >= 0 && static_cast<std::size_t>(hover_) < dots_.size()) {
    const Dot &d = dots_[static_cast<std::size_t>(hover_)];
    p.setPen(QPen(ink, 2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(d.at, 7, 7);
    QString text = series_[d.series].name + ": " +
                   QString::fromStdString(formatTime(d.ms, decimals_));
    QRectF box(0, 0, fm.horizontalAdvance(text) + 12, lineH + 6);
    box.moveTopLeft(d.at + QPointF(10, -box.height() - 6));
    if (box.right() > width()) {
      box.moveRight(d.at.x() - 10);
    }
    if (box.top() < 0) {
      box.moveTop(d.at.y() + 10);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(pal.color(QPalette::ToolTipBase));
    p.drawRoundedRect(box, 4, 4);
    p.setPen(pal.color(QPalette::ToolTipText));
    p.drawText(box, Qt::AlignCenter, text);
  }
}

void LabChart::mouseMoveEvent(QMouseEvent *e) {
  int best = -1;
  double bestD = 12 * 12;
  for (std::size_t i = 0; i < dots_.size(); ++i) {
    QPointF d = dots_[i].at - e->position();
    double dist = d.x() * d.x() + d.y() * d.y();
    if (dist < bestD) {
      bestD = dist;
      best = static_cast<int>(i);
    }
  }
  if (best != hover_) {
    hover_ = best;
    update();
  }
}

void LabChart::leaveEvent(QEvent *) {
  if (hover_ != -1) {
    hover_ = -1;
    update();
  }
}

} // namespace vcube
