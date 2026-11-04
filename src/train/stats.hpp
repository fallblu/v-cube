#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vcube {

enum class Penalty : std::uint8_t { None, Plus2, Dnf };

struct Solve {
  std::int64_t timeMs = 0; // raw time, without the +2
  Penalty penalty = Penalty::None;
  std::string scramble;
  std::string comment;
  std::int64_t timestamp = 0; // Unix seconds when the solve finished
  std::string moves;          // virtual-cube solution, if any
};

// Time used for statistics: DNF sorts after everything.
inline constexpr std::int64_t kDnfTime =
    std::numeric_limits<std::int64_t>::max();
std::int64_t effectiveTime(const Solve &s);

// A statistic: nullopt when there are not enough solves; kDnfTime for DNF.
using StatValue = std::optional<std::int64_t>;

// WCA-style trimmed average of exactly these solves: the best and worst
// ceil(n/20) are dropped (one each for ao5/ao12), the rest averaged. DNF if
// more DNFs than can be trimmed. Needs at least 3 solves.
StatValue averageOf(std::span<const Solve> solves);
// Plain mean (mo3); any DNF makes it DNF.
StatValue meanOf(std::span<const Solve> solves);
// The current average/mean of the last n solves.
StatValue currentAverage(std::span<const Solve> all, std::size_t n);
StatValue currentMean(std::span<const Solve> all, std::size_t n);
// Best rolling average/mean of n over the session.
StatValue bestAverage(std::span<const Solve> all, std::size_t n);
StatValue bestMean(std::span<const Solve> all, std::size_t n);
// Rolling average of n ending at every solve (nullopt until n solves).
std::vector<StatValue> rollingAverages(std::span<const Solve> all,
                                       std::size_t n);

struct SessionSummary {
  int count = 0;
  int dnfs = 0;
  StatValue best;
  StatValue worst; // worst non-DNF
  StatValue mean;  // mean of non-DNF solves
};
SessionSummary summarize(std::span<const Solve> all);

// "12.34", "1:02.34", "1:01:02.34", or "DNF"; nullopt prints "-".
std::string formatTime(StatValue ms, int decimals = 2);
// Like formatTime, with "+" for +2 and "DNF(12.34)".
std::string formatSolve(const Solve &s, int decimals = 2);

} // namespace vcube
