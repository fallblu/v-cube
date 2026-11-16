#pragma once

#include "cube/cubie.hpp"
#include "cube/move.hpp"
#include "train/solve_review.hpp"

#include <atomic>
#include <cstdint>
#include <vector>

namespace vcube {

// Every short solution of one F2L pair: the moves that solve a slot's
// corner and edge from a state with the cross done, keeping the cross and
// every slot that was solved. Slots that aren't solved are free to be
// used, and so is the last layer.
//
// The moves are R, U and L, with more as asked for. Without quarter turns
// of F and B (and without wide turns) no solution changes the edge
// orientation of the F/B axis, so a state with EO done keeps it. No
// solution has a slice or a rotation; wide turns are followed with the way
// the cube is held, and a solution ends with the cross on the bottom
// again.

struct PairMoves {
  bool d = true;          // D, D', D2
  bool halfFB = true;     // F2, B2
  bool quarterFB = false; // F, F', B, B' (these change EO)
  bool wide = false;      // r, l, u, d (these change EO)
  friend bool operator==(const PairMoves &, const PairMoves &) = default;
};

struct PairSearchOptions {
  PairMoves moves;
  int extra = 2;      // stop after the shortest length plus this
  int maxLength = 12; // never longer than this
  int maxResults = 2000;
  std::uint64_t maxNodes = 40'000'000;
};

struct PairSearchResult {
  std::vector<Alg> algs; // shortest first
  int shortest = -1;     // -1: none found
  // Every solution up to the last length was found (no cap was reached
  // and the search wasn't stopped).
  bool complete = true;
};

// The solutions of `target`'s pair from `state` (in the solver's frame,
// cross on D and solved; empty otherwise, or when the slot is solved).
// Solutions never pass through a state that already has the pair solved,
// so none ends with a turn that isn't needed.
PairSearchResult searchPair(const CubieCube &state, Slot target,
                            const PairSearchOptions &options = {},
                            const std::atomic<bool> *stop = nullptr);

} // namespace vcube
