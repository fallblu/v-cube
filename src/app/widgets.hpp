#pragma once

#include "app/colours.hpp"
#include "cube/cubie.hpp"
#include "cube/hold.hpp"
#include "cube/scramble.hpp"
#include "train/keys.hpp"
#include "train/stats.hpp"
#include "train/timer.hpp"

#include <QColor>
#include <QFuture>
#include <QLabel>
#include <QRectF>
#include <QSize>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace vcube {

// The big timer readout, coloured by timer state as in cstimer. While
// Space is held the colour isn't the only sign: a ring on each side of the
// time while it must still be held, a filled dot once letting go starts
// the timer, and the accessible description says the same in words.
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

protected:
  void paintEvent(QPaintEvent *e) override;

private:
  struct Shown {
    QString text;
    QColor colour;
  };

  int decimals_ = 2;
  QColor colour_;
  HoldCue cue_ = HoldCue::None;
  void setCue(HoldCue cue);
  std::int64_t lastInspection_ = -1; // for the 8 s and 12 s calls
  std::optional<Shown> beforeHold_;  // during a hold: what it replaced
  void show(const QString &text, const QColor &colour);
  void setColour(const QColor &c);
};

// Sizes from the settings: the scramble text of the pages (each label
// passed to markScrambleLabel()) and every TimeDisplay follow
// setDisplayFontSizes(), now and when they change.
void markScrambleLabel(QLabel *label);
void setDisplayFontSizes(int scramblePt, int timerPt);
// Runs `call` with 8 or 12 when a timer's inspection passes 8 s or 12 s.
void setInspectionCallHandler(std::function<void(int seconds)> call);

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
// Pointing at it shows a solve's values on the read-out line under the
// plot; the context menu picks how many of the last solves are charted
// and, when the chart has extra series (such as step splits), whether
// those are drawn instead of the averages.
class TrendChart : public QWidget {
  Q_OBJECT
public:
  explicit TrendChart(QWidget *parent = nullptr);
  void setSolves(const std::vector<Solve> &solves);
  // Shown until there are two values to chart.
  void setEmptyText(const QString &text);
  // The values are move counts: the axis is in whole moves.
  void setMoveCounts(bool on);
  // Where the choices from the context menu are kept (QSettings).
  void setSettingsKey(const QString &key);
  // Lines that can be drawn instead of the averages, one value per solve
  // (nullopt where a solve has none), e.g. the time of each solve's cross.
  struct Series {
    QString name;
    Role colour; // a chart series role, which follows the theme
    std::vector<StatValue> values;
  };
  void setSeries(std::vector<Series> series);
  QSize sizeHint() const override { return {400, 160}; }

protected:
  void paintEvent(QPaintEvent *) override;
  void mouseMoveEvent(QMouseEvent *e) override;
  void leaveEvent(QEvent *e) override;
  void contextMenuEvent(QContextMenuEvent *e) override;

private:
  QString hoverText(std::size_t at) const;
  std::vector<Solve> solves_;
  std::vector<Series> series_;
  QString emptyText_;
  QString settingsKey_;
  bool moves_ = false;
  int last_ = 0;            // solves charted: the last n, 0 for all
  bool showSeries_ = false; // the extra series instead of the averages
  int hover_ = -1;          // index into solves_

  // The first solve charted.
  std::size_t first() const;
  QRectF area() const;
  QString valueText(StatValue v) const;
};

// The tick step for an axis from `lo` to `hi` with about `ticks` ticks: 1,
// 2 or 5 times a power of ten, at least `minStep`.
double niceTickStep(double lo, double hi, int ticks = 4, double minStep = 0);

// Monotonic milliseconds since the app started.
std::int64_t monotonicMs();
// Unix seconds now, and of the start of the local day and the next one.
std::int64_t unixNow();
std::int64_t localDayStart();
std::int64_t localDayEnd();

} // namespace vcube
