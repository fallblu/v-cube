#pragma once

#include "cube/cubie.hpp"
#include "cube/facelet.hpp"
#include "cube/move.hpp"
#include "cube/scramble.hpp"
#include "train/f2l_stats.hpp"
#include "train/solve_review.hpp"

#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace vcube {

// TSLE mode's logic. TSLE is ZZ-CT's last-slot step: with EO done and
// three slots solved, the last slot's edge is put in and the five corners
// left (the last layer's and the slot's) are oriented, wherever they are.
// It is done a trigger at a time: a U turn (the AUF) and one of the slot's
// three triggers (R U R', R U' R', R U2 R' for the front-right slot),
// until the step is done or only an OCLL is left.
//
// Everything here is seen for the front-right slot; the other slots are
// its mirror images (see TsleSolver for the way back).

// --- states and cases --------------------------------------------------------

// Where the slot's edge is and how the five corner places are twisted.
struct TsleState {
  // UR, UF, UL, UB (as Edge) or kInSlot.
  std::uint8_t edge = kInSlot;
  // The twists at URF, UFL, ULB, UBR and DFR (they add up to 0 mod 3).
  std::array<std::uint8_t, 5> twist{};
  static constexpr std::uint8_t kInSlot = 4;

  friend auto operator<=>(const TsleState &, const TsleState &) = default;
  // 0 to kTsleStates - 1.
  int index() const;
  static TsleState fromIndex(int index);
  bool done() const { return edge == kInSlot && twist == decltype(twist){}; }
  // The edge is in and the slot's corner place is oriented: an OCLL is
  // left (or nothing).
  bool ocll() const { return edge == kInSlot && twist[4] == 0 && !done(); }
};
inline constexpr int kTsleStates = 5 * 81;

// The state of a cube whose last slot is `slot`, seen for the front-right
// slot; nullopt unless EO, the cross and the other three slots are done.
std::optional<TsleState> tsleStateOf(const CubieCube &cube, Slot slot);
// A random cube in a state, for a slot: the five corners and the top
// layer's edges are anywhere the state allows.
CubieCube randomTsleCube(TsleState state, Slot slot, Rng &rng);

// The state after `quarters` U turns.
TsleState tsleTurned(TsleState state, int quarters);
// The state after a U turn of `auf` quarters and the trigger R U^turn R'.
TsleState tsleAfter(TsleState state, int auf, int turn);

// A case is a state up to U turns, named by the turn that has the edge at
// UF (or, with the edge in the slot, the first of its turns).
TsleState tsleCase(TsleState state);
// "UF-N-0120": the edge (UF, or FR in the slot), the slot's corner place
// (N oriented, CW or CCW) and the twists at URF, UFL, ULB, UBR.
std::string tsleKey(TsleState c);
std::optional<TsleState> tsleFromKey(std::string_view key);
// "UF CW 0120", or an OCLL's name ("Sune").
std::string tsleName(TsleState c);
// The 104 cases (all but the solved one), by steps left, then key.
const std::vector<TsleState> &tsleCases();
// The fewest steps to the end, where a step is a trigger or the OCLL that
// ends a chain: 0 when done, 1 for an OCLL. The same for every state of a
// case and for every slot.
int tsleStepsLeft(TsleState state);
inline constexpr int kTsleLevels = 3; // the most steps a case takes

// A picture of a case for the front-right slot: the F2L, the slot's edge,
// and of the five corners and the other edges only the top and bottom
// colours.
FaceletCube tsleDiagram(TsleState state);

// --- the OCLLs ---------------------------------------------------------------

struct OcllAlg {
  std::string id; // "sune", "antisune", "h", "pi", "headlights", ...
  std::string name;
  std::string alg; // as written, for any slot
  friend bool operator==(const OcllAlg &, const OcllAlg &) = default;
};
// The seven OCLLs with the algs the mode starts with.
const std::vector<OcllAlg> &defaultOcllAlgs();
// Whether an alg solves the OCLL `id` from some U turn, keeping the F2L.
bool ocllAlgWorks(std::string_view id, const Alg &alg);

// --- what to do from a state -------------------------------------------------

// The end of a chain: an OCLL alg, with the U turn before it.
struct TsleFinish {
  std::string id;
  std::string name;
  Alg alg; // for the solver's slot, the OCLL alg as it is written
  int moves = 0;
};

// One trigger from a state.
struct TsleOption {
  int auf = 0;  // quarter U turns before it
  int turn = 1; // the U turn inside it: 1, 2 or 3
  Alg alg;      // for the solver's slot: the AUF and the trigger
  TsleState next;
  int stepsAfter = 0;
  int moves = 0; // of this step: the trigger, and 1 for an AUF
  // Moves to the end this way, going on as well as can be (and joining
  // the OCLL alg to the trigger where their turns cancel).
  int total = 0;
  bool good = false; // on a way to the end with the fewest steps
  bool best = false; // ...and the fewest moves of those
  // When the trigger leaves an OCLL: the alg to finish with.
  std::optional<TsleFinish> finish;
};

// TSLE for one last slot, with the OCLL algs chains end with. OCLL algs
// are done as they are written whichever the slot is; triggers are the
// slot's own (L' U L for the front-left slot, and so on).
class TsleSolver {
public:
  // Algs that don't solve their OCLL are replaced by the default ones.
  explicit TsleSolver(Slot slot,
                      const std::vector<OcllAlg> &algs = defaultOcllAlgs());

  Slot slot() const { return slot_; }
  // Moves to the end, done as well as can be.
  int movesLeft(TsleState state) const;
  // The twelve triggers from a state, good ones first, by total; none for
  // a state that is done or an OCLL.
  std::vector<TsleOption> options(TsleState state) const;
  // The OCLL alg from an OCLL state.
  std::optional<TsleFinish> finish(TsleState state) const;
  // The best way to the end: each step's moves.
  std::vector<Alg> bestPath(TsleState state) const;
  // Moves of the front-right view as they are done for the slot.
  Alg forSlot(const Alg &alg) const;

private:
  Slot slot_;
  std::vector<OcllAlg> algs_;
  std::array<int, kTsleStates> total_{};
  std::array<std::optional<TsleFinish>, kTsleStates> finish_{};

  // The OCLL finishes of a state, each as the moves in the front-right
  // view (the U turn and the alg).
  struct WorldFinish {
    std::size_t alg = 0;
    Alg moves;
  };
  std::array<std::vector<WorldFinish>, kTsleStates> finishes_{};

  TsleOption option(TsleState state, int auf, int turn) const;
  TsleFinish finishFrom(const WorldFinish &f) const;
};

// --- progress ----------------------------------------------------------------

// What a step that wasn't a good trigger adds to an attempt's excess (a
// good one adds its moves over the best).
inline constexpr double kWrongTrigger = 3.0;

struct TsleProgress {
  // By tsleKey(): the last tries of each case (excess in moves).
  std::map<std::string, std::vector<PairAttempt>> cases;
  // The cases Guided intro has been through. OCLLs need no introduction.
  std::set<std::string> introduced;
  std::vector<FullAttempt> fulls;
  // OCLL algs changed from the default ones, by id.
  std::map<std::string, std::string> ocll;

  static constexpr std::size_t kPerCase = 50;
  static constexpr std::size_t kFulls = 500;
  void add(const std::string &key, const PairAttempt &a);
  void add(const FullAttempt &a);
  bool isIntroduced(TsleState c) const;
  // The OCLL algs in use: the default ones with the changed ones put in.
  std::vector<OcllAlg> ocllAlgs() const;
  friend bool operator==(const TsleProgress &, const TsleProgress &) = default;
};

// A level is the cases with that many steps left (the OCLLs aside). It is
// solid when every case of it is introduced and three quarters of them
// at least have been tried and aren't weak.
bool tsleLevelSolid(const TsleProgress &progress, int level);
// The deepest level open: 1 at least, and each further one once the level
// before it is solid.
int tsleOpenLevel(const TsleProgress &progress);
// The next case to introduce: the first one not introduced of the open
// levels, shallowest first; nullopt when there is none.
std::optional<TsleState> tsleNextNew(const TsleProgress &progress);

} // namespace vcube
