#pragma once

#include "cube/cubie.hpp"
#include "cube/move.hpp"

#include <chrono>
#include <optional>

namespace vcube {

// Kociemba's two-phase algorithm. Phase 1 brings the cube into the subgroup
// <U, D, R2, L2, F2, B2> (corners and edges oriented, slice edges in the
// slice); phase 2 solves it within that subgroup. Pruning tables are built
// on first use (well under a second with optimisation on) and shared by all
// threads.
class TwoPhaseSolver {
public:
  using Clock = std::chrono::steady_clock;

  // Builds the move and pruning tables now rather than on first solve.
  static void warmUp();

  // A face-turn solution of at most `maxLength` moves, or nullopt if none
  // was found before `deadline`. The cube must be valid.
  static std::optional<Alg> solve(const CubieCube &cube, int maxLength,
                                  Clock::time_point deadline);

  // A solution, trying for <= 21 moves within `budget` and then accepting
  // longer ones. Always succeeds for a valid cube.
  static Alg
  solve(const CubieCube &cube,
        std::chrono::milliseconds budget = std::chrono::milliseconds(500));
};

} // namespace vcube
