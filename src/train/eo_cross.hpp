#pragma once

#include "cube/cubie.hpp"
#include "cube/facelet.hpp"
#include "cube/hold.hpp"
#include "cube/move.hpp"
#include "cube/scramble.hpp"
#include "train/f2l_stats.hpp"
#include "train/move_count.hpp"
#include "train/step_solver.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vcube {

// EO Cross mode: the start of a ZZ solve practised a part at a time on the
// virtual cube.
//
//   Bad edges       count the bad edges of a state for an axis or two
//   Axis choice     turn the cube to the hold (cross colour and EO axis)
//                   the step is shortest from
//   Plan blind      look at the cube, then do the step without seeing it
//   Step solutions  solve the step in view, then compare with the optimum
//   Full EO Cross   a whole EO Cross, then where the moves went
//
// The states are random ones of a kind: a number of edges bad for the
// front-back axis as the cube is first shown (0, 2, ... 12). How each kind
// went is kept per drill and step, and the kinds that go badly come up
// more often than a scramble would give them.

enum class EoDrill : std::uint8_t { Edges, Axis, Plan, Solve, Full };
inline constexpr int kEoDrills = 5;
// "edges", "axis", "plan", "solve", "full".
std::string_view eoDrillId(EoDrill d);
// The drill is for a step chosen in the bar (the others have a step of
// their own).
bool hasStep(EoDrill d);

// The steps the mode practises, in the order a solve does them: EO, EOLine,
// EO+1, EO+2, EO Cross.
const std::vector<SolveStep> &eoCrossSteps();

// The kinds of state: how many edges are bad.
inline constexpr std::array<int, 7> kBadEdgeKinds{0, 2, 4, 6, 8, 10, 12};
// The share of all states with that many bad edges (0 for an odd count).
double badEdgeShare(int bad);

// What a list of tries is kept for: a drill, the step where the drill has
// one (Axis choice, Plan blind, Step solutions; EO Cross otherwise) and the
// bad edges where the drill has kinds (all but Axis choice; -1 otherwise).
struct EoKind {
  EoDrill drill = EoDrill::Solve;
  SolveStep step = SolveStep::EOCross;
  int bad = -1;
  friend bool operator==(const EoKind &, const EoKind &) = default;
};
// A kind as its drill keeps it (the step or the bad edges it doesn't use
// set as above).
EoKind normalized(EoKind k);
// "edges/6", "axis/eocross", "plan/eo+2/6", "solve/eocross/4", "full/8".
std::string eoKindKey(const EoKind &k);
std::optional<EoKind> eoKindFromKey(std::string_view key);

// The tries of each kind, by eoKindKey(). A try is written as F2L mode
// writes a pair's: `moves` against `bestMoves` with `excess` what was lost.
//   Bad edges: moves the count answered for F/B, bestMoves the right one,
//     excess how far off the answers were in all
//   Axis choice: moves the optimum of the hold picked, bestMoves the best
//     hold's
//   the others: the moves done and the optimum over the holds allowed;
//     gaveUp also for a step that wasn't done
struct EoCrossStats {
  std::map<std::string, std::vector<PairAttempt>> kinds;

  static constexpr std::size_t kPerKind = 50;
  void add(const EoKind &kind, const PairAttempt &a);
  const std::vector<PairAttempt> &tries(const EoKind &kind) const;
  friend bool operator==(const EoCrossStats &, const EoCrossStats &) = default;
};

// The mean time of the tries (not given up) of a drill and step, over all
// their kinds: what a kind's own time is compared with.
double eoMeanMs(const EoCrossStats &stats, EoDrill drill, SolveStep step);
CaseSummary eoSummary(const EoCrossStats &stats, const EoKind &kind);
// How often each of kBadEdgeKinds should come up in a drill and step: its
// share of all states, flattened so that the rare kinds are met too, times
// how badly it has gone (summarizeCase's weight).
std::array<double, 7> badEdgeWeights(const EoCrossStats &stats, EoDrill drill,
                                     SolveStep step);
int pickBadEdges(const EoCrossStats &stats, EoDrill drill, SolveStep step,
                 Rng &rng);

// A random state, in the solver's frame, with exactly `bad` edges bad for
// the F/B axis (any number when `bad` is negative or odd).
CubieCube randomEoState(int bad, Rng &rng);

// The holds a step may be done in: the neutrality's with both EO axes.
std::vector<Hold> eoHolds(const ColourNeutrality &neutrality);
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
