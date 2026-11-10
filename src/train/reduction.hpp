#pragma once

#include "train/algset.hpp"
#include "train/case_logic.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace vcube {

// Mirror worlds and reduction sets.
//
// A set with mirrors is practised in several "worlds": as it is written and
// in each of its mirror images, e.g. a last-slot set written for the
// front-right slot also with the front-left, back-right or back-left slot
// open. In a mirror world the step leaves the mirrored pieces free, and a
// case comes up mirrored, with its algs mirrored to match (R U R' becomes
// L' U' L). A case that doesn't depend on the slot (the F2L is whole and
// only the last layer is left, say) comes up as it is written instead, so
// that its algs stay the same in every world.
//
// In a reduction set an alg may stop at another case of the set instead of
// finishing the step: each case holds the next step only, and the cases
// are followed one after another until the step is done.

// How a cube shows a case: which case, whether it is seen mirrored (so
// that its algs are done mirrored), and the U turn (in quarters) that
// lines the cube up with the case as it is stored. For a mirrored view
// that turn is in the stored case's frame: on the cube it goes the other
// way round.
struct CaseView {
  std::string caseId;
  bool mirrored = false;
  int before = 0;
  friend bool operator==(const CaseView &, const CaseView &) = default;
};

// Whether a case looks and solves the same whichever world it is in: no
// piece the step must solve is out of place, and the places where the
// world's free pieces differ from the set's are solved.
bool slotIndependent(const CubieCube &state, const PieceMask &free,
                     Mirror world);

// How `cube` shows the given case in `world`, if it does: the cube is the
// case up to the pieces the step leaves free and the U turns around it
// that the set allows.
std::optional<CaseView> viewOfCase(const AlgSet &set, const CaseGroup &group,
                                   const AlgCase &c, const CubieCube &cube,
                                   Mirror world);
// The case of the set that `cube` shows in `world`, in set order. Cases
// seen as they are written come before mirrored ones.
std::optional<CaseView> seenCase(const AlgSet &set, const CubieCube &cube,
                                 Mirror world = {});

// A case set up in `world`, without U turns around it: `state` is the case
// as it is drawn and `free` its step's free pieces, which are scrambled
// when `scramble` is set.
CubieCube setUpCase(const CubieCube &state, const PieceMask &free, Mirror world,
                    bool scramble, Rng &rng);

// An alg of a case as it is done from a cube showing the case: with the
// U turns it needs there written in, and mirrored for a mirrored view.
struct ViewAlg {
  std::string text;
  // The case the cube shows after the alg (a reduction step), or empty for
  // an alg that finishes the step.
  std::string reducesTo;
};
// The passing algs of the case, main alg first. `analysis` is the case's
// (see analyzeCase).
std::vector<ViewAlg> algsFor(const AlgSet &set, const CaseGroup &group,
                             const AlgCase &c, const CaseAnalysis &analysis,
                             const CaseView &view, const CubieCube &cube,
                             Mirror world);

// Steps left from each case of a reduction set by its own algs: 1 for an
// alg that finishes the step, else one more than from the case the alg
// stops at; the fewest over the case's passing algs. Cases that never
// reach the end (no passing alg, or algs that only lead round in circles)
// are left out. In a set that isn't a reduction set every case with a
// passing alg has 1.
std::map<std::string, int> stepsLeft(const AlgSet &set);

// The cases a cube goes through by the set's main algs, and what is done
// at each.
struct ChainStep {
  CaseView view;
  std::string alg; // as done from the cube at that point
};
struct Chain {
  std::vector<ChainStep> steps;
  bool complete = false; // the last step finishes the set's step
};
Chain solveChain(const AlgSet &set, const CubieCube &cube, Mirror world = {});

// The world in which `cube` shows one of the set's cases, of the worlds
// the set is practised in, and the case it shows.
struct WorldView {
  Mirror world;
  CaseView view;
};
std::optional<WorldView> worldOf(const AlgSet &set, const CubieCube &cube);

} // namespace vcube
