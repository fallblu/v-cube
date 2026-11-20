#pragma once

#include "cube/cubie.hpp"
#include "cube/facelet.hpp"
#include "cube/move.hpp"
#include "cube/scramble.hpp"
#include "train/algset.hpp"
#include "train/ergonomics.hpp"
#include "train/f2l_search.hpp"
#include "train/solve_review.hpp"

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vcube {

// F2L mode's logic: what the F2L is for a method, the pair cases, the
// states the drills start from, and how pairs and their solutions are
// ranked. Everything is in the solver's frame (cross on D).

// --- the F2L of a method -----------------------------------------------------

struct F2LConfig {
  // Every edge is oriented for the F/B axis and stays so (ZZ): solutions
  // never use quarter turns of F or B, or wide turns.
  bool eo = true;
  // F2L - 1: one slot is left for the last-slot step (ZZ-CT's TSLE)...
  bool lastSlot = true;
  // ...which may be any of these (by Slot: FR, FL, BL, BR).
  std::array<bool, kSlots> lsSlots{true, true, false, false};
  // Moves besides R, U and L that solutions may use.
  bool dMoves = true;
  bool halfFB = true; // F2 and B2
  bool wide = true;   // without EO only: r, l, u, d

  PairMoves moves() const;
  Edges edges() const { return eo ? Edges::Oriented : Edges::Any; }
  bool leaves(Slot s) const {
    return lastSlot && lsSlots[static_cast<std::size_t>(s)];
  }
  friend bool operator==(const F2LConfig &, const F2LConfig &) = default;
};

// The methods offered: ZZ F2L - 1 (ZZ-CT), ZZ F2L (ZZ-OP, ZZ-A), CFOP F2L
// and CFOP F2L - 1.
enum class F2LMethod : std::uint8_t { ZZMinus1, ZZ, CFOP, CFOPMinus1 };
inline constexpr int kF2LMethods = 4;
std::string_view methodId(F2LMethod m); // "zz-1", "zz", "cfop", "cfop-1"
std::optional<F2LMethod> methodFromId(std::string_view id);
std::string_view methodName(F2LMethod m);
// Sets `eo` and `lastSlot` for the method; the rest is kept.
void applyMethod(F2LConfig &config, F2LMethod m);
F2LMethod methodOf(const F2LConfig &config);

inline constexpr std::array<Slot, kSlots> kF2LSlots = {Slot::FR, Slot::FL,
                                                       Slot::BL, Slot::BR};

bool crossSolved(const CubieCube &c);
bool edgesOriented(const CubieCube &c);
// The cross is done, and EO with it when the method has EO.
bool f2lBase(const CubieCube &c, const F2LConfig &config);
std::vector<Slot> unsolvedSlots(const CubieCube &c);
int solvedSlotCount(const CubieCube &c);
// The method's F2L is done: the base holds and every slot is solved, or
// (F2L - 1) all but one that may be left.
bool f2lDone(const CubieCube &c, const F2LConfig &config);
// The pairs still to solve at least: the unsolved slots, less the one
// that can be left.
int pairsLeft(const CubieCube &c, const F2LConfig &config);

// --- pair cases --------------------------------------------------------------

// Where a slot's corner and edge are, as seen for the front-right slot
// (other slots are mirrored onto it) and up to a U turn: `corner` is the
// corner's place * 3 + twist, `edge` the edge's place * 2 + flip. With
// the corner in the top layer it is at URF; with only the edge there, the
// edge is at UF.
struct PairCase {
  std::uint8_t corner = DFR * 3;
  std::uint8_t edge = FR * 2;
  friend auto operator<=>(const PairCase &, const PairCase &) = default;

  bool solved() const { return corner == DFR * 3 && edge == FR * 2; }
  bool cornerOnTop() const { return corner / 3 < 4; }
  bool edgeOnTop() const { return edge / 2 < 4; }
  // The slot the piece sits in (FR is the pair's own), if it is in one.
  std::optional<Slot> cornerSlot() const;
  std::optional<Slot> edgeSlot() const;
};

// The case of `target`'s pair in a state.
PairCase pairCaseOf(const CubieCube &state, Slot target);
// "URF1-UB0": the places and the twist and flip.
std::string caseKey(PairCase c);
std::optional<PairCase> caseFromKey(std::string_view key);
// A few words: "Corner up, edge UB" or "Corner in FL slot, edge in slot
// (flipped)".
std::string caseName(PairCase c);

// The groups the cases are listed in.
enum class PairGroup : std::uint8_t {
  BothTop,
  CornerTopEdgeSlot,
  EdgeTopCornerSlot,
  BothSlot,
  CornerElsewhere,
  EdgeElsewhere,
  BothElsewhere,
};
inline constexpr int kPairGroups = 7;
PairGroup groupOf(PairCase c);
std::string_view groupName(PairGroup g);

// Every case but the solved one, by group: 83 with EO (no edge is
// flipped), 167 without.
const std::vector<PairCase> &pairCases(Edges edges);

// --- cards -------------------------------------------------------------------

// One pair to solve.
struct PairSetup {
  CubieCube state;
  Slot target = Slot::FR;
  PairCase pcase;
};

// F2L mode's cards: a pair case for the front slots or for the back ones.
// A card's solutions are written for its side's own slot (FR or BR) and
// done mirrored left to right in the other one (FL or BL).
enum class PairSide : std::uint8_t { Front, Back };
struct PairCard {
  PairSide side = PairSide::Front;
  PairCase pcase;
  friend auto operator<=>(const PairCard &, const PairCard &) = default;
};
// "front/URF1-UB0", "back/URF1-UB0".
std::string cardKey(const PairCard &card);
std::optional<PairCard> cardFromKey(std::string_view key);
// FR or BR.
Slot sideSlot(PairSide side);
PairSide sideOf(Slot slot);
// "front", "back" (the slots: "FR / FL", "BR / BL").
std::string_view sideName(PairSide side);
// The case of `target`'s pair in a state, as a card.
PairCard pairCardOf(const CubieCube &state, Slot target);

// The cards in the order they are introduced: each case as listed, its
// front card then its back one (166 with EO, 334 without).
std::vector<std::string> pairCardPool(Edges edges);

// A card's case for its side's own slot, with the slots its pieces sit in
// open and the others solved: the state its solutions are chosen for.
std::optional<CubieCube> cardState(const PairCard &card, Edges edges, Rng &rng);
// A card's case to practise: the pair in one of the side's two slots, the
// slots its pieces sit in open and the others solved, except that the
// method's last slot may be left open (with `leaveOpen`).
std::optional<PairSetup> cardSetup(const PairCard &card,
                                   const F2LConfig &config, bool leaveOpen,
                                   Rng &rng);

// Moves written for the side's own slot as they are done for `slot`, and
// back: mirrored left to right for the side's other slot.
Alg sideToSlot(const Alg &alg, PairSide side, Slot slot);
inline Alg slotToSide(const Alg &alg, PairSide side, Slot slot) {
  return sideToSlot(alg, side, slot);
}
// A pair solution as solutions compare: the turns without rotations (see
// fixedFrame()), simplified, without the U turns at the start and the end,
// opposite faces in one order.
Alg comparableSolution(const Alg &moves);
// The one of `chosen` (written for the side's slot) that `moves`, done for
// `slot`, is; nullopt if none.
std::optional<std::size_t> matchChosen(const Alg &moves, PairSide side,
                                       Slot slot,
                                       const std::vector<Alg> &chosen);
// A chosen solution as it solves `slot`'s pair in `state`: mirrored for
// the slot, the U turn before it that the state needs put in; nullopt if no
// U turn makes it solve the pair (keeping the base and the solved slots).
std::optional<Alg> chosenFor(const CubieCube &state, Slot slot,
                             const Alg &chosen, const F2LConfig &config);
// Whether `alg` (with some U turn before it) solves `slot`'s pair from
// `state`, keeping the base and the slots that were solved: the U turn it
// needs (0-3), or nullopt.
std::optional<int> solvesPairWithAuf(const CubieCube &state, Slot slot,
                                     const Alg &alg, const F2LConfig &config);

// The mirror image that brings a slot to the front right (and back).
Mirror slotMirror(Slot s);
// The slot a slot of the front-right view is for `target`'s pair: where a
// case's "FL slot" is when the pair is, say, the back-left one.
Slot slotSeenFrom(Slot seen, Slot target);

// A picture of a case: the cross, the centres and the pair's two pieces
// where the case has them, the rest masked.
FaceletCube caseDiagram(PairCase c);

// A picture of a slot's pair in a state: the centres, the cross and the
// pair's two pieces where they are, the rest masked.
FaceletCube pairDiagram(const CubieCube &state, Slot slot);

// --- starting states ---------------------------------------------------------

// A random state with the cross (and EO, for `eo`) done, the `open` slots
// unsolved and the others solved.
CubieCube randomF2LState(const std::vector<Slot> &open, Edges edges, Rng &rng);

// A state for a whole F2L: every slot open.
CubieCube randomFullState(const F2LConfig &config, Rng &rng);

// --- blind spots -------------------------------------------------------------

// How many stickers of a piece the flat view shows (U, F and the strips
// of L and R beside them): `corner` at `place`.
int flatStickers(bool corner, int place);
// What looking for a slot's pair costs in a state: 0 when both pieces can
// be told from what the flat view shows, more for each piece that shows
// one sticker too few (kPartlyHidden) or none (kHidden).
inline constexpr double kHidden = 2.0;
inline constexpr double kPartlyHidden = 1.0;
double hiddenCost(const CubieCube &state, Slot slot);

// --- ranking -----------------------------------------------------------------

struct RankedSolution {
  Alg alg;
  std::string text;
  int htm = 0;
  double cost = 0.0;   // the profile's cost (and the wide-turn penalty)
  std::string setCase; // the reference set's case it is the alg of
};

struct AnalysisOptions {
  F2LConfig config;
  ErgoProfile profile;
  // A set of pair cases (see isPairSet()) whose alg for the pair is shown
  // beside the computed solutions; may be null.
  std::shared_ptr<const AlgSet> reference;
  std::size_t keep = 8;     // solutions kept per pair
  int extra = 2;            // moves over the shortest that are ranked
  int maxResults = 600;     // solutions costed per pair, shortest first
  double widePenalty = 1.0; // added for each wide turn
};

// What a way of solving a pair costs: the profile's cost of the moves as
// simplified, and the penalty for each wide turn.
double solutionCost(const Alg &alg, const AnalysisOptions &options);

// --- grading -----------------------------------------------------------------

// Costs closer than this are the same (they are shown to a tenth).
inline constexpr double kSameCost = 0.05;
// A cost this much over the best (a pair's) is far over.
inline constexpr double kFarOver = 2.0;

// How a cost compares with the best: the same, over it, or far over it
// (kFarOver for each of `pairs`).
enum class CostOver : std::uint8_t { Same, Over, Far };
CostOver costOver(double over, int pairs = 1);

// What a chosen solution (written for its side's slot) costs, as
// solutions compare (comparableSolution()).
double chosenCost(const Alg &chosen, const AnalysisOptions &options);

// A try at one pair, against the solutions chosen for it (not empty).
struct PairJudgement {
  std::optional<std::size_t> match; // the chosen solution it was
  Alg asChosen;          // the try as solutions compare, for the side's slot
  double cost = 0.0;     // of the try
  double cheapest = 0.0; // of the chosen ones
  double extra = 0.0;    // over the cheapest, never negative
  bool good = false;     // one of them, or as cheap: graded Good
};
// `moves` solved `target`'s pair, whose card is of `side`.
PairJudgement judgePair(const Alg &moves, PairSide side, Slot target,
                        const std::vector<Alg> &chosen,
                        const AnalysisOptions &options);

// A whole F2L against the cheapest way found from the start.
struct WholeJudgement {
  double extra = 0.0; // cost over it, never negative
  bool good = false;  // as good
  bool far = false;   // far over, for the pairs that were left
};
WholeJudgement judgeWhole(double cost, double cheapest, int pairsLeft);

struct PairAnalysis {
  Slot slot = Slot::FR;
  PairCase pcase;
  // Cheapest first: the solutions within `extra` moves of the shortest,
  // by cost, and the reference set's alg for the pair (marked),
  // wherever its cost puts it.
  std::vector<RankedSolution> solutions;
  int shortest = 0;
  bool found() const { return !solutions.empty(); }
  const RankedSolution *best() const {
    return solutions.empty() ? nullptr : &solutions.front();
  }
};

PairAnalysis analysePair(const CubieCube &state, Slot slot,
                         const AnalysisOptions &options,
                         const std::atomic<bool> *stop = nullptr);

// A pair as the one to solve next.
struct PairRank {
  PairAnalysis pair;
  double rest = 0.0;       // the cheapest way through the pairs after it
  double hidden = 0.0;     // hiddenCost() of the pair at the start
  double total = 0.0;      // pair cost + rest + hidden
  std::vector<Slot> order; // that way: this pair first
  int moves = 0;           // its moves in all
};

// Every pair that could be solved next, best first: each with its best
// solution followed by the cheapest way through the pairs the method
// still needs after it (each solved with its own best solution).
std::vector<PairRank> rankPairs(const CubieCube &state,
                                const AnalysisOptions &options,
                                const std::atomic<bool> *stop = nullptr);

// --- a solve's pairs ---------------------------------------------------------

// A stretch of moves that solved one or more slots: from the state
// `before` (as the solver sees it), ending with the base intact and more
// slots solved than at any time before.
struct F2LSegment {
  std::size_t begin = 0;
  std::size_t end = 0;
  std::vector<Slot> solved;
  CubieCube before;
};
std::vector<F2LSegment> f2lSegments(const CubieCube &start, const Alg &moves,
                                    const F2LConfig &config);

} // namespace vcube
