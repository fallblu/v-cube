#pragma once

#include <array>
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

  friend bool operator==(const Solve &, const Solve &) = default;
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
enum class Rolling : std::uint8_t { Average, Mean };
std::optional<std::size_t> bestAverageStart(std::span<const Solve> all,
                                            std::size_t n,
                                            Rolling rolling = Rolling::Average,
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

// A statistic of a session by its name: "single" (a solve), "moN" (the
// mean of N) or "aoN" (the average of N), e.g. "mo3" or "ao12".
struct StatId {
  enum class Kind : std::uint8_t { Single, Mean, Average };
  Kind kind = Kind::Single;
  std::size_t n = 1;
};
// nullopt for any other text (or N of 0).
std::optional<StatId> parseStatId(std::string_view id);

// The solves behind a statistic: `count` solves from index `from`.
struct StatSpan {
  std::size_t from = 0;
  std::size_t count = 0;
};
// The current one (the last solve, or the last N) or the best one (the
// first best solve, or the start of the best rolling mean or average);
// nullopt when there are too few solves.
enum class StatPick : std::uint8_t { Current, Best };
std::optional<StatSpan> statSpan(std::span<const Solve> all, StatId id,
                                 StatPick pick, std::int64_t unit = kWcaUnit);

// One statistic's current and best values.
struct StatLine {
  std::string id;
  StatValue current;
  StatValue best;
};
// What a session's statistics panel shows: the statistics named in `ids`
// (see parseStatId(); an unknown name gives no values) and the summary.
struct SessionStats {
  std::vector<StatLine> lines;
  SessionSummary summary;
};
SessionStats sessionStats(std::span<const Solve> all,
                          std::span<const std::string_view> ids,
                          std::int64_t unit = kWcaUnit);

// A session's statistics kept from one solve to the next, so that a solve
// added at the end only has its own windows looked at: sessionStats() of
// kIds, the rolling ao5 and ao12 of every solve and newBests(). Any other
// change to the solves (one deleted, a penalty changed, another session or
// unit) needs reset().
class RunningStats {
public:
  static constexpr std::array<std::string_view, 5> kIds = {
      "single", "mo3", "ao5", "ao12", "ao100"};

  // Works everything out afresh.
  void reset(std::span<const Solve> all, std::int64_t unit = kWcaUnit);
  // `all` is the solves last seen with one more at the end. Gives
  // newBests(all, unit()).
  std::vector<NewBest> add(std::span<const Solve> all);

  // The number of solves seen.
  std::size_t size() const { return size_; }
  std::int64_t unit() const { return unit_; }
  // sessionStats(all, kIds, unit()).
  const SessionStats &stats() const { return stats_; }
  // rollingAverages(all, 5, unit()) and rollingAverages(all, 12, unit()).
  const std::vector<StatValue> &ao5() const { return ao5_; }
  const std::vector<StatValue> &ao12() const { return ao12_; }

private:
  std::int64_t unit_ = kWcaUnit;
  std::size_t size_ = 0;
  SessionStats stats_;
  std::vector<StatValue> ao5_;
  std::vector<StatValue> ao12_;
  // What the summary's mean is of: the sum of the non-DNF times, truncated
  // to the unit, and how many there are.
  std::int64_t validSum_ = 0;
  std::int64_t valid_ = 0;
};

// "12.34", "1:02.34", "1:01:02.34", or "DNF"; nullopt prints "-".
std::string formatTime(StatValue ms, int decimals = 2);
// Like formatTime, with "+" for +2 and "DNF(12.34)".
std::string formatSolve(const Solve &s, int decimals = 2);

} // namespace vcube
