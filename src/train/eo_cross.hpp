#pragma once

#include "cube/cubie.hpp"
#include "cube/facelet.hpp"
#include "cube/hold.hpp"
#include "cube/move.hpp"
#include "train/move_count.hpp"
#include "train/step_solver.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace vcube {

// What EO Cross mode knows of the start of a ZZ solve: the steps it
// practises, the holds a step may be done in, and where the moves of an EO
// Cross went.

// The steps the mode practises, in the order a solve does them: EO, EOLine,
// EO+1, EO+2, EO Cross.
const std::vector<SolveStep> &eoCrossSteps();

// The hold in which the cube shows `step` done, among those that pose the
// same problem as one of `allowed`: the hold the cube is in now if it is
// one, else one with the same top colour, else the first; nullopt when the
// step isn't done in any.
std::optional<Hold> doneHold(const FaceletCube &cube, SolveStep step,
                             const std::vector<Hold> &allowed);
// The index of the optimum among `ranked` (solveStepHeld's) that poses the
// problem `hold` does.
std::optional<std::size_t> rankOfHold(const std::vector<HeldOptimum> &ranked,
                                      Hold hold, SolveStep step);

// Where the moves of an EO Cross went: the solve cut where EO, EO+1, EO+2
// and EO Cross were first done as held in `hold`, each part with its moves,
// how far EO Cross then was at best, and what the part lost: its moves
// less how much nearer EO Cross it brought the cube. Steps done by the
// same move share a part (named for the last of them).
struct EoPart {
  SolveStep step = SolveStep::EO;
  std::size_t begin = 0; // moves of the solution, [begin, end)
  std::size_t end = 0;
  int moves = 0; // HTM of the part, simplified
  int toGo = 0;  // the optimum to EO Cross after it
  int lost = 0;
};
struct EoBreakdown {
  int optimal = 0; // from the start, in `hold`
  std::vector<EoPart> parts;
  bool done = false; // EO Cross was reached
};
EoBreakdown eoBreakdown(const FaceletCube &start, const Alg &solution,
                        Hold hold);

} // namespace vcube
