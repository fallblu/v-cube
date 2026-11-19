#pragma once

#include "cube/move.hpp"
#include "train/grips.hpp"

#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vcube {

// A cost a profile may hold: a finite number from 0 up. (nan would make
// every score using it nan, and can't be written to a file.)
inline constexpr double kMaxCost = 1000.0;
inline bool validCost(double v) {
  return std::isfinite(v) && v >= 0.0 && v <= kMaxCost;
}

// A guess at how hard an alg is to turn. Lower is better. A grip profile
// (one with a GripModel) scores the cheapest way to turn the alg with its
// fingertricks and regrips (see grips.hpp); a move profile scores the sum
// of a cost for each move and a cost for each pair of moves in a row that
// turns awkwardly (a regrip, a change of axis). The numbers are a
// profile's: the user tunes them to their hands.

// A rule for a pair of moves in a row: the second move after the first
// costs `cost` more. Each side is a move as written ("R2", "F'"), a layer
// with any turn ("R*"), or any move ("*").
struct ErgoRule {
  std::string first;
  std::string second;
  double cost = 0.0;
  friend bool operator==(const ErgoRule &, const ErgoRule &) = default;
};

struct ErgoProfile {
  std::string name;
  // Cost of each move as written ("R", "R'", "R2", "r", "M'", "x" ...).
  std::map<std::string, double> moves;
  double otherMove = 2.0;  // a move the table doesn't list
  double axisChange = 0.0; // each turn on another axis than the last turn
  std::vector<ErgoRule> rules;
  // A grip profile scores with this instead of the table and rules.
  std::optional<GripModel> grips;
  friend bool operator==(const ErgoProfile &, const ErgoProfile &) = default;
};

// The profiles v-cube ships: "Two-handed grips" (the default, a grip
// profile), "Right-handed 2H" and "One-handed (right)".
inline constexpr std::string_view kGripProfileName = "Two-handed grips";
const std::vector<ErgoProfile> &builtinErgoProfiles();

// Whether a rule side matches a move.
bool ergoMatches(std::string_view pattern, Move m);

struct ErgoScore {
  double total = 0.0;
  double moves = 0.0;       // the moves' (or tricks') own costs
  double transitions = 0.0; // axis changes and rules (or regrips ...)
};
ErgoScore ergoScore(const Alg &alg, const ErgoProfile &profile);
// The total alone, quicker for a grip profile.
double ergoTotal(const Alg &alg, const ErgoProfile &profile);

// Every move token a profile's table can price, in a fixed order: each
// face, wide turn and slice with its three turns, then the rotations.
const std::vector<std::string> &ergoMoveTokens();

} // namespace vcube
