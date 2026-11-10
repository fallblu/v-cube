#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vcube {

enum class Penalty : std::uint8_t { None, Plus2, Dnf };

// The cube a session's solves are done on: the keyboard-turned virtual
// cube or a physical one.
enum class CubeKind : std::uint8_t { Virtual, Physical };
// "virtual" or "physical".
std::string_view cubeKindId(CubeKind k);
// nullopt for any other text.
std::optional<CubeKind> cubeKindFromId(std::string_view id);

struct Solve {
  std::int64_t timeMs = 0; // raw time, without the +2
  Penalty penalty = Penalty::None;
  std::string scramble;
  std::string comment;
  std::int64_t timestamp = 0; // Unix seconds when the solve finished
  std::string moves;          // virtual-cube solution, if any
  // Milliseconds from the start of the solve to each move of `moves` (as
  // parsed), if they were kept; empty for solves from before they were.
  std::vector<std::int64_t> moveTimes;
};

// Time used for statistics: DNF sorts after everything.
inline constexpr std::int64_t kDnfTime =
    std::numeric_limits<std::int64_t>::max();
std::int64_t effectiveTime(const Solve &s);

// A statistic: nullopt when there are not enough solves; kDnfTime for DNF.
using StatValue = std::optional<std::int64_t>;

// Averages and means are worked out as the WCA does (regulation 9f1): each
// time is first truncated to `unit` (10 ms: hundredths of a second), and
// the average is rounded to the nearest `unit`, halves up. Shown with as
// many decimals as `unit` has, the value is then exact (formatTime
// truncates, which leaves it as it is). statUnit() gives the unit for a
// number of decimals.
inline constexpr std::int64_t kWcaUnit = 10;
// 1000 for 0 decimals, 100 for 1, 10 for 2, 1 for 3 or more.
std::int64_t statUnit(int decimals);

// WCA-style trimmed average of exactly these solves: the best and worst
// ceil(n/20) are dropped (one each for ao5/ao12), the rest averaged. DNF if
// more DNFs than can be trimmed. Needs at least 3 solves.
StatValue averageOf(std::span<const Solve> solves,
                    std::int64_t unit = kWcaUnit);
// Plain mean (mo3); any DNF makes it DNF.
StatValue meanOf(std::span<const Solve> solves, std::int64_t unit = kWcaUnit);
// The current average/mean of the last n solves.
StatValue currentAverage(std::span<const Solve> all, std::size_t n,
                         std::int64_t unit = kWcaUnit);
StatValue currentMean(std::span<const Solve> all, std::size_t n,
                      std::int64_t unit = kWcaUnit);
// Best rolling average/mean of n over the session.
StatValue bestAverage(std::span<const Solve> all, std::size_t n,
                      std::int64_t unit = kWcaUnit);
StatValue bestMean(std::span<const Solve> all, std::size_t n,
                   std::int64_t unit = kWcaUnit);
// Rolling average of n ending at every solve (nullopt until n solves).
std::vector<StatValue> rollingAverages(std::span<const Solve> all,
                                       std::size_t n,
                                       std::int64_t unit = kWcaUnit);
// The index of the first solve of the best rolling average (or mean) of n,
// nullopt when there are fewer than n solves.
std::optional<std::size_t> bestAverageStart(std::span<const Solve> all,
                                            std::size_t n, bool mean = false,
                                            std::int64_t unit = kWcaUnit);
// The solves an average of these solves drops as best and worst (indices
// into `solves`): none for 3 solves, else ceil(n/20) of each.
std::vector<std::size_t> trimmedIndices(std::span<const Solve> solves);

// The statistics the last solve set a new best for: "single", "mo3",
// "ao5", "ao12" and "ao100", each when it is better than every earlier
// value of the same statistic (a first value isn't a new best). DNF is
// never one.
struct NewBest {
  std::string name;
  std::int64_t value = 0;
};
std::vector<NewBest> newBests(std::span<const Solve> all,
                              std::int64_t unit = kWcaUnit);

struct SessionSummary {
  int count = 0;
  int dnfs = 0;
  StatValue best;
  StatValue worst; // worst non-DNF
  StatValue mean;  // mean of non-DNF solves, rounded as averages are
};
SessionSummary summarize(std::span<const Solve> all,
                         std::int64_t unit = kWcaUnit);

// "12.34", "1:02.34", "1:01:02.34", or "DNF"; nullopt prints "-".
std::string formatTime(StatValue ms, int decimals = 2);
// Like formatTime, with "+" for +2 and "DNF(12.34)".
std::string formatSolve(const Solve &s, int decimals = 2);

} // namespace vcube
