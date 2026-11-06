#pragma once

#include "cube/cubie.hpp"
#include "cube/hold.hpp"
#include "cube/scramble.hpp"
#include "train/stats.hpp"
#include "train/timer.hpp"

#include <QFuture>
#include <QLabel>
#include <QWidget>

#include <functional>
#include <optional>

namespace vcube {

// The big timer readout, coloured by timer state as in cstimer.
class TimeDisplay : public QLabel {
  Q_OBJECT
public:
  explicit TimeDisplay(QWidget *parent = nullptr);
  void setDecimals(int d) { decimals_ = d; }
  // Shows the timer's state at `now` (ms on its clock). A hold let go of
  // before it was ready brings back what was shown before it.
  void showTimer(const SolveTimer &timer, std::int64_t now);
  void showTime(std::int64_t ms, Penalty penalty = Penalty::None);
  void showText(const QString &text, const QColor &colour = {});

private:
  struct Shown {
    QString text;
    QColor colour;
  };

  int decimals_ = 2;
  QColor colour_;
  std::optional<Shown> beforeHold_; // during a hold: what it replaced
  void show(const QString &text, const QColor &colour);
  void setColour(const QColor &c);
};

// A state to scramble to, in the solver's frame, and the alg set case it
// sets up (if any).
struct ScrambleSetup {
  CubieCube state;
  std::string setId;
  std::string caseId;
};

// A scramble worked out off the GUI thread. The text is applied holding
// white top and green front; turned to `hold`, the cube shows `state`.
struct GeneratedScramble {
  CubieCube state; // in the solver's frame
  QString text;
  std::string setId;  // for a scramble from an alg set: the set
  std::string caseId; // and the case it sets up
  Hold hold = kWcaHold;
};

// Runs `makeState`, picks a hold allowed by `neutrality` and solves for
// the scramble on the thread pool.
QFuture<GeneratedScramble>
generateScramble(std::function<ScrambleSetup(Rng &)> makeState,
                 ColourNeutrality neutrality);

// Times and rolling averages of a session, drawn with QPainter. Values
// are charted in thousandths (seconds for times, moves for move counts).
class TrendChart : public QWidget {
  Q_OBJECT
public:
  explicit TrendChart(QWidget *parent = nullptr);
  void setSolves(const std::vector<Solve> &solves);
  // Shown until there are two values to chart.
  void setEmptyText(const QString &text);
  QSize sizeHint() const override { return {400, 160}; }

protected:
  void paintEvent(QPaintEvent *) override;

private:
  std::vector<Solve> solves_;
  QString emptyText_;
};

// Monotonic milliseconds since the app started.
std::int64_t monotonicMs();
// Unix seconds now, and of the start of the local day and the next one.
std::int64_t unixNow();
std::int64_t localDayStart();
std::int64_t localDayEnd();

} // namespace vcube
