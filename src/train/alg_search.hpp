#pragma once

#include "cube/cubie.hpp"
#include "cube/scramble.hpp"
#include "train/case_logic.hpp"
#include "train/move_set.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace vcube {

// An alg generator: every alg of a move set that solves a case, shortest
// first, as a depth-first search with iterative deepening (IDA*).
//
// What "solves" means is fitAlg()'s check: the case, a U turn before it if
// `aufBefore`, the alg, and a U turn after it if `aufAfter` leave the cube
// solved except for the `free` pieces. Lengths count every layer turn as
// one move (STM; the same as HTM without slices).
//
// The search prunes with pattern databases: the distance to the goal of
// the corners alone (where the corners whose place is free count as one
// kind of piece), and of one or two groups of edges, under the move
// set's turns. Tables are built when a search first needs them (a few
// seconds for all six faces and a fully solved goal, much less for
// restricted move sets) and kept for later searches with the same moves
// and goal, so batches over a set reuse them.
//
// Wide and slice turns are followed with the way the cube is held: after
// r, a U turn turns the layer that was in front. Algs never turn the same
// layer twice in a row, and turn layers of one axis (R, L, r, M ...) in one
// order only. With a U turn allowed before (after) the case, an alg never
// starts (ends) with a U turn, which would only be that AUF.

struct SearchGoal {
  CubieCube state; // the case, as seen before its alg
  PieceMask free;  // what the alg may leave unsolved (normalized)
  bool aufBefore = true;
  bool aufAfter = true;
};

struct SearchOptions {
  MoveSet moves = MoveSet::allFaces();
  int maxLength = 14; // never longer than this
  // >= 0: stop after the shortest length plus this many moves.
  int extra = 2;
  int maxResults = 200; // stop after this many algs
  int threads = 0;      // 0: one per hardware thread
};

struct SearchResult {
  Alg alg;
  std::string text; // toString(alg)
  AufFit fit;       // the U turns it needs, as fitAlg() finds them
  int length = 0;   // moves (STM)
};

enum class SearchEnd : std::uint8_t {
  Done,       // every alg up to the length limit was found
  Capped,     // stopped at maxResults
  Stopped,    // stopped from outside
  Unsolvable, // the move set can't solve the case
};

struct SearchOutcome {
  SearchEnd end = SearchEnd::Done;
  int shortest = -1; // length of the shortest alg, -1 if none
  int deepest = 0;   // the last length searched in full
  int found = 0;     // algs found
  std::uint64_t nodes = 0;
};

struct SearchCallbacks {
  // Each alg as it is found, from a worker thread (one call at a time).
  std::function<void(const SearchResult &)> result;
  // Between steps: a message such as "Building tables…" or "Searching 11
  // moves…", from the calling thread.
  std::function<void(const std::string &)> progress;
};

// Runs a search to the end (on the calling thread plus workers). `stop`
// can be set from any thread to end it early.
SearchOutcome searchAlgs(const SearchGoal &goal, const SearchOptions &options,
                         const std::atomic<bool> &stop,
                         const SearchCallbacks &callbacks = {});

} // namespace vcube
