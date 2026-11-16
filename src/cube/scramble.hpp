#pragma once

#include "cube/cubie.hpp"
#include "cube/move.hpp"

#include <array>
#include <random>
#include <string>
#include <vector>

namespace vcube {

using Rng = std::mt19937_64;

// A generator seeded from std::random_device.
Rng makeRng();

// Which pieces a random state may disturb, by position. A position whose
// permutation is free trades pieces with the other free positions; a free
// orientation is twisted or flipped at random. Positions whose permutation
// is free should all share the same orientation freedom (per piece type) for
// the set of such states to be closed under composition; normalize() makes
// sure of that.
struct PieceMask {
  std::array<bool, kCorners> cp{};
  std::array<bool, kCorners> co{};
  std::array<bool, kEdges> ep{};
  std::array<bool, kEdges> eo{};

  static PieceMask all();

  // Builders for common groups of positions.
  PieceMask &corners(std::initializer_list<Corner> cs, bool perm, bool ori);
  PieceMask &edges(std::initializer_list<Edge> es, bool perm, bool ori);
  PieceMask &lastLayer(bool perm, bool cornerOri, bool edgeOri);

  // Makes orientation freedom uniform across the free-permutation positions
  // of each piece type.
  void normalize();

  bool isEmpty() const;
  friend bool operator==(const PieceMask &, const PieceMask &) = default;
};

// A uniformly random state among those that only disturb `mask`'s pieces
// (respecting parity and orientation sums).
CubieCube randomState(const PieceMask &mask, Rng &rng);

// Whether a state only disturbs pieces that `mask` lets it.
bool withinMask(const CubieCube &state, const PieceMask &mask);

// A move sequence that takes a solved cube to `state` (inverse of a
// two-phase solution).
Alg scrambleToState(const CubieCube &state);

// A named random-state scramble kind, as in cstimer's 3x3 subsets.
struct ScrambleType {
  std::string id;
  std::string name;
  PieceMask mask;
  bool aufAfter = false; // random U turn after the subset state
};

// Built-in types; the first is the full WCA random-state scramble.
const std::vector<ScrambleType> &scrambleTypes();
const ScrambleType *findScrambleType(const std::string &id);

// Random state of a scramble type (never solved; never solvable in < 2).
CubieCube randomStateOf(const ScrambleType &type, Rng &rng);

} // namespace vcube
