#pragma once

#include "cube/cubie.hpp"
#include "cube/hold.hpp"
#include "cube/move.hpp"
#include "train/move_count.hpp"

#include <array>
#include <optional>
#include <vector>

namespace vcube {

// Optimal solutions for the inspection steps of ZZ: EO, EO+1, EO+2, EOLine
// and EO Cross, and for CFOP's cross. XEOCross and the F2L are far too long
// to solve optimally here, so they have no solver.
//
// A step is solved in the solver's frame, as stepReached() checks it held
// one way: the cross on D and the edges oriented for the F/B axis (they can
// be solved without F or B quarter turns). EO+1 and EO+2 need any one or
// two of the four cross edges (DF, DR, DB, DL) solved as well.
//
// IDA* over the 18 face turns (no slice, wide or rotation moves), with
// pruning tables on the edge orientation and the cross edges, built on
// first use and shared by all threads.

// Whether the step has an optimal solver (all but XEOCross, ZZ F2L and CFOP
// F2L).
bool hasOptimalSolver(SolveStep step);

// The optimal solutions of a step: every face-turn solution of the
// shortest length, up to a cap. Solutions are in a fixed order (by move
// index, U R F D L B, then quarter, half, counter-clockwise), never turn
// the same face twice in a row, and turn opposite faces in one order only
// (U before D, R before L, F before B), so no two are the same moves.
struct StepOptimum {
  int length = 0;
  std::vector<Alg> solutions; // at most the cap asked for
  bool more = false;          // more optimal solutions exist than are kept
};

// The optimal solutions for `step` of `cube` (in the solver's frame), at
// most `maxSolutions` (at least one is always kept). nullopt for ZZ F2L. A
// step that is already done gives length 0 and one empty solution.
std::optional<StepOptimum> solveStep(const CubieCube &cube, SolveStep step,
                                     int maxSolutions = 8);

// The optimal length only, which stops at the first solution.
std::optional<int> stepDistance(const CubieCube &cube, SolveStep step);

// One hold's optimum for a scramble: held as `hold` after the scramble was
// applied holding white top and green front (the solution is done after
// turning the cube with holdRotation(hold)).
struct HeldOptimum {
  Hold hold;
  StepOptimum optimum;
};

// The optimum for each of `holds`, for a scramble applied holding white
// top and green front. Holds that pose the same problem for the step are
// solved once, under the first of them in `holds`: for EO+1, EO+2, EOLine
// and EO Cross a hold and the same hold turned y2 (same cross, same EO
// axis); for EO every hold with the same front-back axis; for the cross
// every hold with the same top. Sorted by length, then in the order of
// `holds`. Empty for the steps without a solver.
std::vector<HeldOptimum> solveStepHeld(const Alg &scramble, SolveStep step,
                                       const std::vector<Hold> &holds,
                                       int maxSolutions = 8);

// The optimal length for a scramble over `holds` (the best of
// solveStepHeld's lengths), stopping at first solutions; nullopt for ZZ
// F2L or no holds. Results are cached by scramble, step and holds, so the
// Move Count list can ask for every attempt.
std::optional<int> heldDistance(const Alg &scramble, SolveStep step,
                                const std::vector<Hold> &holds);

// The holds of a colour neutrality with both EO axes allowed: each hold and
// the same hold turned a quarter (y), so that the front-back axis may be
// either of the two around the top colour. Sorted, without repeats.
std::vector<Hold> withBothEoAxes(const std::vector<Hold> &holds);

// The shortest solution of one F2L pair (the slot of `corner` and `edge`,
// e.g. DFR and FR) from a ZZ F2L state in the solver's frame (EO Cross
// done), in <R, U, L> so that EO stays, keeping the cross and the slots in
// `keep` (by their corner and edge) solved. nullopt if there is none
// within `maxDepth` moves, or the state isn't a ZZ F2L state.
std::optional<Alg>
solvePairRul(const CubieCube &state, Corner corner, Edge edge,
             const std::vector<std::pair<Corner, Edge>> &keep,
             int maxDepth = 14);

// Edge orientation of a cube in the solver's frame, for one axis: 0 the
// F/B axis (ZZ's EO as held), 1 the R/L axis, 2 the U/D axis. An edge is
// bad when it needs a quarter turn of a face on that axis to be solved, by
// position (true for the edge at that position).
std::array<bool, kEdges> badEdges(const CubieCube &cube, int axis);
int badEdgeCount(const CubieCube &cube, int axis);

} // namespace vcube
