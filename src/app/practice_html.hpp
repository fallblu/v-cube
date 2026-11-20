#pragma once

#include "cube/move.hpp"
#include "train/practice.hpp"
#include "train/srs.hpp"

#include <QColor>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace vcube {

// What the practice modes (see PracticePage) say about a solve, as rich
// text: the lines of the verdict, the Inspection clock and the Timed tab's
// splits, from plain values.

// How a solve went: what is kept and graded, and how it is said.
struct PracticeOutcome {
  PracticeResult result;
  QString verdict;   // one line of rich text
  Alg best;          // the best solution from the start; empty if none
  QString bestText;  // how it is written (default: the alg)
  QString bestLabel; // what the best is called (default: "Best")
};

// The parts of a solve, for the Timed tab's splits: each named, ending
// after a move of the solve (an index into the solve's moves, exclusive).
struct PracticeSplit {
  QString name;
  std::size_t end = 0;
};

// `text` on a line of its own in small print, or nothing for no text.
QString smallLine(const QString &text);
// "12.34", as the timer shows a time with `decimals`.
QString clockText(std::int64_t ms, int decimals);
// "9.3 s".
QString tenthsText(std::int64_t ms);

// "Best: <alg>" (or the outcome's own label), or nothing to show.
QString bestLine(const PracticeOutcome &o);
// The grade a try gave its card and when the card comes back (`due`).
QString gradeLine(Grade grade, std::int64_t due, std::int64_t now,
                  std::int64_t dayEnd);

// A timed solve.
struct TimedResult {
  int moves = 0;
  std::int64_t ms = 0;
  bool dnf = false;
  std::optional<double> meanBefore; // the group's mean before it
};
// The result of a timed solve is about the time: its moves and turns a
// second, and how it compares with the recent ones.
QString timedLine(const TimedResult &r, int decimals);

// "looked 9.3 s", in red when over the limit (`limitMs`, 0 for none);
// nothing when no time was looked.
QString inspectionLine(std::int64_t lookedMs, std::int64_t limitMs);
// The Inspection clock after `usedMs`: counting down to the limit and then
// up over it, or the time looked once it has stopped (`running` false).
// `text` is the colour while nothing is amiss.
QString inspectionClock(std::int64_t usedMs, std::int64_t limitMs, bool running,
                        const QColor &text);
// What the Inspection tab says before its first state.
QString inspectionReadyPrompt(std::int64_t limitMs);

// The lines of a verdict, and whether to work it out is still under way.
struct PracticeVerdict {
  bool waiting = false; // for the best solutions
  QString result;       // the verdict or the timed line
  QString best;
  QString grade;
  QString note; // said once under it, in `muted`
};
QString verdictHtml(const PracticeVerdict &v, const QString &muted);

// Where the time of a solve went, a row for each part and one for what
// came after them. `moveMs` is when each move was made, since the solve
// began; nothing when there are no parts or the times don't match the
// moves.
QString splitsHtml(const std::vector<PracticeSplit> &parts, const Alg &moves,
                   const std::vector<std::int64_t> &moveMs, int decimals,
                   const QString &muted);
// How efficient a timed solve was (its verdict and best solution), for
// the details.
QString efficiencyHtml(const PracticeOutcome &o);

} // namespace vcube
