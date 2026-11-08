#pragma once

#include "cube/cubie.hpp"
#include "train/algset.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace vcube {

// Mirror families: the cases of a set that are one case mirrored left-right
// (across the M plane: R <-> L), front-back (across the S plane: F <-> B)
// or both. For F2L pairs, a front-right case, its front-left and back-right
// mirrors and its back-left double mirror form one family, and their algs
// are mirrors of each other (mirrorM, mirrorS).

// The state mirrored: each sticker moves to its mirror position and takes
// the mirrored face's colour (R <-> L for left-right, F <-> B for
// front-back). The result is a real cube state.
CubieCube mirrorState(const CubieCube &c, bool leftRight, bool frontBack);

// The pieces a case leaves to solve: those not solved in its state whose
// home positions the step doesn't leave free (by permutation).
struct CasePieces {
  std::vector<Corner> corners;
  std::vector<Edge> edges;
  friend bool operator==(const CasePieces &, const CasePieces &) = default;
};
CasePieces casePieces(const CubieCube &state, const PieceMask &free);

struct CaseFamilies {
  // Each family's case ids in set order; every case is in one family
  // (alone when no mirror of it is in the set). Families are in the order
  // of their first case.
  std::vector<std::vector<std::string>> families;
  std::map<std::string, std::size_t> familyOf; // case id -> family index

  // The family of a case: its members, or nullptr for an unknown id.
  const std::vector<std::string> *familyFor(const std::string &caseId) const;
  // The case whose progress stands for the family: its first case.
  std::string representative(const std::string &caseId) const;
};

// Groups each case with the cases of the set that are its mirror images:
// case B is a mirror of case A when B's pieces to solve are A's mirrored and
// sit where A's mirrored state has them, up to a U turn before the alg.
CaseFamilies findFamilies(const AlgSet &set);

} // namespace vcube
