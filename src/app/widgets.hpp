#pragma once

#include "cube/cubie.hpp"
#include "cube/scramble.hpp"
#include "train/stats.hpp"
#include "train/timer.hpp"

#include <QFuture>
#include <QLabel>
#include <QWidget>

#include <functional>

namespace vcube {

// The big timer readout, coloured by timer state as in cstimer.
class TimeDisplay : public QLabel {
  Q_OBJECT
public:
  explicit TimeDisplay(QWidget *parent = nullptr);
  void setDecimals(int d) { decimals_ = d; }
  // Shows the timer's state at `now` (ms on its clock).
  void showTimer(const SolveTimer &timer, std::int64_t now);
  void showTime(std::int64_t ms, Penalty penalty = Penalty::None);
  void showText(const QString &text, const QColor &colour = {});

private:
  int decimals_ = 2;
  void setColour(const QColor &c);
};

// A scramble worked out off the GUI thread.
struct GeneratedScramble {
  CubieCube state;
  QString text;
};

// Runs `makeState` and a two-phase solve on the thread pool.
QFuture<GeneratedScramble>
generateScramble(std::function<CubieCube(Rng &)> makeState);

// Times and rolling averages of a session, drawn with QPainter.
class TrendChart : public QWidget {
  Q_OBJECT
public:
  explicit TrendChart(QWidget *parent = nullptr);
  void setSolves(const std::vector<Solve> &solves);
  QSize sizeHint() const override { return {400, 160}; }

protected:
  void paintEvent(QPaintEvent *) override;

private:
  std::vector<Solve> solves_;
};

// Monotonic milliseconds since the app started.
std::int64_t monotonicMs();
// Unix seconds now, and of the start of the local day.
std::int64_t unixNow();
std::int64_t localDayStart();

} // namespace vcube
