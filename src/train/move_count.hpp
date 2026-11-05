#pragma once

#include "train/stats.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace vcube {

// One scramble in Move Count mode: how many moves the solution to the step
// (e.g. EO Cross) took on a physical cube, or DNF when the step wasn't
// solved.
struct MoveCountAttempt {
  int moves = 0;
  bool dnf = false;
  std::string scramble;
  std::string comment;
  std::int64_t timestamp = 0; // Unix seconds when it was recorded
};

// The largest move count that can be entered.
inline constexpr int kMaxMoveCount = 999;

// The scramble types Move Count mode offers, by id: a full WCA scramble
// (for EO, EOLine, EO Cross or cross), EOLine solved, EO Cross solved (ZZ
// F2L) and cross solved (CFOP F2L).
const std::vector<std::string> &moveCountScrambleTypes();
bool isMoveCountScrambleType(const std::string &id);

// Move Count statistics are the timer's (WCA trimmed averages, means,
// best rolling averages), worked out on the attempts as solves of one
// "millisecond" per thousandth of a move. Their values are therefore in
// thousandths of a move, and kDnfTime stands for DNF.
inline constexpr std::int64_t kMoveUnit = 1000;
std::vector<Solve> attemptsAsSolves(std::span<const MoveCountAttempt> attempts);

// A statistic in thousandths of a move, rounded to `decimals` (0-2):
// "23", "23.33", "DNF", or "-" for nullopt.
std::string formatMoves(StatValue thousandths, int decimals = 2);
// An attempt as listed: "23", "DNF(23)", or "DNF" with no count.
std::string formatAttempt(const MoveCountAttempt &a);

} // namespace vcube
