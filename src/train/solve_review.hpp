#pragma once

#include "cube/cubie.hpp"
#include "cube/facelet.hpp"
#include "cube/hold.hpp"
#include "cube/scramble.hpp"
#include "train/algset.hpp"
#include "train/move_count.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vcube {

// Reviewing solves done on the virtual cube: where each step was done, and
// how the F2L pairs went compared with an alg set of pair cases (such as a
// ZZ F2L set: one slot's pair to solve per case, the other slots kept).
//
// A solve is its start (the scrambled cube, held white top and green front)
// and its solution, the moves as done (turns and rotations, as
// VirtualAttempt::solution() keeps them).

// When a solve first had a step done.
struct StepMark {
  SolveStep step = SolveStep::EO;
  std::size_t moves = 0; // moves of the solution done by then
  int htm = 0;           // their HTM (htmCount)
  // A hold in which the step is done then: the cross colour on the bottom
  // (top = its opposite) and the EO axis front-back. For EO alone the top
  // is any colour off that axis.
  Hold hold;
};

// The steps of solveSteps() the solve did at some point, in that order,
// each at the first move after which stepReached() holds (0 when the
// start already has it). Their HTM is solutionHtm() of the moves by then,
// simplified or not, as the solve's move count is taken.
std::vector<StepMark> stepMarks(const FaceletCube &start, const Alg &solution,
                                bool simplified = true);
// The same for some of the steps only (in the order given), which is
// quicker.
std::vector<StepMark> stepMarks(const FaceletCube &start, const Alg &solution,
                                const std::vector<SolveStep> &steps,
                                bool simplified = true);

// F2L slots in the solver's frame (cross on D), by their corner and edge.
enum class Slot : std::uint8_t { FR, FL, BL, BR };
inline constexpr int kSlots = 4;
std::string_view slotName(Slot s); // "FR", "FL", "BL", "BR"
Corner slotCorner(Slot s);         // DFR, DLF, DBL, DRB
Edge slotEdge(Slot s);             // FR, FL, BL, BR
bool slotSolved(const CubieCube &c, Slot s);
// EO Cross held as the solver sees it: every edge oriented for F/B and the
// four cross edges solved.
bool eoCrossSolved(const CubieCube &c);

// One pair that could be solved next, as a reference set solves it.
struct PairOption {
  Slot slot = Slot::FR;
  std::string caseId; // the set's case for the pair now; empty if none
  std::string caseName;
  // The shortest passing alg of the case, with the U turn before it that
  // this state needs written in (withAufs), e.g. "(U') R U R'".
  std::string alg;
  int moves = 0; // its HTM, counting that U turn; 0 without a case
};

// The options of a ZZ F2L state (EO Cross done, in the solver's frame):
// one per unsolved slot, in Slot order. A case matches when its pair's
// corner and edge are where the state has them, up to a U turn before the
// alg, and one of its passing algs, done from the state, solves the slot
// and keeps EO Cross and every slot that was solved. A set with mirrors
// also matches its cases in each mirror image, with the algs mirrored (a
// set written for the front-right slot covers the other three). Without a
// reference set, or for a pair no case matches, the option has no case.
std::vector<PairOption> pairOptions(const CubieCube &state,
                                    const AlgSet *reference);

// Whether every case of the set leaves one slot's pair to solve (its
// corner, its edge or both) and has EO Cross done (as a ZZ F2L set's cases
// do), so that it can be a reference for pairOptions().
bool isPairSet(const AlgSet &set);

// A stretch of a solve that solved one or more F2L slots.
struct PairSegment {
  std::size_t begin = 0;           // moves of the solution done when it began
  std::size_t end = 0;             // ...and when it ended
  int htm = 0;                     // HTM of the moves in [begin, end)
  std::vector<Slot> solved;        // the slots it solved
  std::vector<PairOption> options; // the pairs that could be done then
};

// A solve's F2L split into pairs. From the first move after which EO Cross
// is done (held in the hold of that StepMark), a segment ends each time
// more slots are solved, with EO Cross done, than at any point before.
// Without `eo` the same is done from the cross alone (CFOP), and the
// options have no case unless the edges happen to be oriented.
struct F2LBreakdown {
  bool found = false;      // the solve reached EO Cross (or the cross)
  std::size_t eoCross = 0; // moves done when it did
  Hold hold;               // the hold the slots are seen in
  std::vector<PairSegment> segments;
  bool complete = false; // every slot was solved in the end
};
F2LBreakdown f2lBreakdown(const FaceletCube &start, const Alg &solution,
                          const AlgSet *reference, bool eo = true,
                          bool simplified = true);

// The shortest <R, U, L> solution of a slot's pair from a ZZ F2L state (in
// the solver's frame) that keeps EO Cross and the slots solved there; see
// solvePairRul(). nullopt if none is found within `maxDepth` moves.
std::optional<Alg> optimalPair(const CubieCube &state, Slot slot,
                               int maxDepth = 14);

// The phases of a whole solve, for step splits: the cross (EO Cross when
// EO Cross was done before the F2L, as in ZZ, else the cross, as in CFOP),
// each F2L pair, and the last layer. A phase runs from the end of the one
// before (the solve's start for the first) to the move that finished it;
// the last layer runs to the end of the solve.
struct SolvePhase {
  enum class Kind : std::uint8_t { Cross, Pair, LastLayer };
  Kind kind = Kind::Cross;
  std::string name;       // "EO Cross", "Cross", "FR", "FR + FL", "LL"
  std::size_t begin = 0;  // moves of the solution done when it began
  std::size_t end = 0;    // ...and when it ended
  int htm = 0;            // its moves (solutionHtm of [begin, end))
  std::int64_t ms = 0;    // how long it took
  std::int64_t endMs = 0; // when it ended, from the start of the solve
};
struct SolveSplits {
  bool zz = false; // EO Cross came first
  std::vector<SolvePhase> phases;
  // The sums by kind; nullopt when a kind wasn't reached (the F2L counts
  // only when it was finished).
  std::optional<std::int64_t> crossMs, f2lMs, llMs;
  int crossHtm = 0, f2lHtm = 0, llHtm = 0;
};
// `times` are milliseconds from the start of the solve to each move of the
// solution (as VirtualAttempt::solutionTimes() keeps them), and `totalMs`
// the solve's time. Without times (a solve saved before they were kept,
// or with a different number of times than moves) the phases have no
// times.
SolveSplits solveSplits(const FaceletCube &start, const Alg &solution,
                        const std::vector<std::int64_t> &times,
                        std::int64_t totalMs, bool simplified = true);

// Moves per second, or 0 without time.
double tps(int htm, std::int64_t ms);

// A reconstruction of a solve as text, e.g. for a forum post or
// alg.cubing.net: the scramble, then the solution a phase per line with
// the phase's name, moves and (with times) time after "//".
std::string reconstruction(const Alg &scramble, const Alg &solution,
                           const SolveSplits &splits);
// The solve on alg.cubing.net, its moves commented with the phases.
std::string reconstructionUrl(const Alg &scramble, const Alg &solution,
                              const SolveSplits &splits,
                              const std::string &title);

// The pairs of a ZZ F2L state solved with a reference set's algs in the
// order that takes the fewest moves (every order of the unsolved slots is
// tried, each pair with its shortest alg).
struct PairPlan {
  std::vector<PairOption> steps;
  int moves = 0;
  bool complete = false; // a case was found for every pair on the way
};
PairPlan cheapestPairOrder(const CubieCube &state, const AlgSet &reference);

// A random ZZ F2L practice state in the solver's frame: EO Cross solved,
// the `open` slots' pieces and the last layer scrambled (with every edge
// oriented) and the other slots solved.
CubieCube randomPairState(const std::vector<Slot> &open, Rng &rng);
// The same with `count` (1-4) slots picked at random.
CubieCube randomPairState(int count, Rng &rng);

} // namespace vcube
