#pragma once

#include "cube/scramble.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace vcube {

// What F2L mode keeps of the pairs solved in it: for each pair case (by
// caseKey()) how the last attempts went, so that the cases solved badly
// come up more often; how the pairs picked in Pair choice compared with
// the best pick; and the Full F2L solves.

enum class F2LDrill : std::uint8_t { Pair, Choice, Full };

struct PairAttempt {
  std::int64_t timestamp = 0; // Unix seconds
  std::int64_t ms = 0;        // from the first look to the pair solved
  int moves = 0;              // HTM, as simplified
  int bestMoves = 0;          // of the best solution
  double excess = 0.0;        // cost over the best solution's (0 or more)
  bool hint = false;          // a hint was shown
  bool gaveUp = false;
  F2LDrill drill = F2LDrill::Pair;
  friend bool operator==(const PairAttempt &, const PairAttempt &) = default;
};

struct ChoiceAttempt {
  std::int64_t timestamp = 0;
  int rank = 1;        // the pick's place among the pairs, 1 the best
  int of = 1;          // how many there were
  double regret = 0.0; // its plan's cost over the best pair's
  friend bool operator==(const ChoiceAttempt &,
                         const ChoiceAttempt &) = default;
};

struct FullAttempt {
  std::int64_t timestamp = 0;
  std::int64_t ms = 0;
  int moves = 0;
  int pairs = 0;
  double excess = 0.0; // the pairs' costs over their best solutions, summed
  friend bool operator==(const FullAttempt &, const FullAttempt &) = default;
};

struct F2LStats {
  std::map<std::string, std::vector<PairAttempt>> cases;
  std::vector<ChoiceAttempt> choices;
  std::vector<FullAttempt> fulls;

  // The newest attempts are kept: this many per case, and of the others.
  static constexpr std::size_t kPerCase = 50;
  static constexpr std::size_t kOthers = 500;
  void add(const std::string &caseKey, const PairAttempt &a);
  void add(const ChoiceAttempt &a);
  void add(const FullAttempt &a);
  friend bool operator==(const F2LStats &, const F2LStats &) = default;
};

// How a case has gone lately (its last kRecent attempts).
struct CaseSummary {
  int attempts = 0;    // in all
  double excess = 0.0; // mean cost over the best; a hint or giving up adds
  double meanMs = 0.0; // of the attempts not given up
  double hints = 0.0;  // share with a hint or given up
  double weight = 0.0; // how often it should come up
  bool weak = false;
};
inline constexpr std::size_t kRecentAttempts = 8;
// What a hint and giving up add to an attempt's excess.
inline constexpr double kHintExcess = 2.0;
inline constexpr double kGaveUpExcess = 4.0;
// A case never tried comes up as often as one this far over the best.
inline constexpr double kUnseenWeight = 3.0;
// From this weight on a case tried is called weak.
inline constexpr double kWeakWeight = 3.0;

// The mean time of every pair attempt not given up (0 without any).
double meanPairMs(const F2LStats &stats);
// `overallMs` is meanPairMs(): a case slower than that weighs more.
CaseSummary summarizeCase(const std::vector<PairAttempt> &attempts,
                          double overallMs);

// An index picked with the chances the weights give (0 for none).
std::size_t pickWeighted(const std::vector<double> &weights, Rng &rng);

} // namespace vcube
