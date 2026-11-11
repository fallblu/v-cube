#pragma once

#include "cube/move.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace vcube {

// A guess at how hard an alg is to turn, as the sum of a cost for each
// move and a cost for each pair of moves in a row that turns awkwardly
// (a regrip, a change of axis). Lower is better. The numbers are a
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
  friend bool operator==(const ErgoProfile &, const ErgoProfile &) = default;
};

// The profiles v-cube ships: "Right-handed 2H" (the default) and
// "One-handed (right)".
const std::vector<ErgoProfile> &builtinErgoProfiles();

// Whether a rule side matches a move.
bool ergoMatches(std::string_view pattern, Move m);

struct ErgoScore {
  double total = 0.0;
  double moves = 0.0;       // the moves' own costs
  double transitions = 0.0; // axis changes and rules
};
ErgoScore ergoScore(const Alg &alg, const ErgoProfile &profile);

// Every move token a profile's table can price, in a fixed order: each
// face, wide turn and slice with its three turns, then the rotations.
const std::vector<std::string> &ergoMoveTokens();

} // namespace vcube
