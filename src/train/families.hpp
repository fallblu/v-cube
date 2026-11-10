#pragma once

#include "cube/cubie.hpp"
#include "train/algset.hpp"
#include "train/srs.hpp"

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
inline CubieCube mirrorState(const CubieCube &c, Mirror m) {
  return mirrorState(c, m.leftRight, m.frontBack);
}
// The mask of the mirrored positions.
PieceMask mirrorMask(const PieceMask &mask, Mirror m);
// An alg's text with every move mirrored (R U R' is L' U' L left-right and
// L U L' both ways), keeping its brackets and spacing.
std::string mirrorAlgText(std::string_view text, Mirror m);

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
  // Each family's id, under which its progress is kept: "family:" and the
  // place of the pieces of the first of its cases' mirror images and U
  // turns (see findFamilies), so that it stays the same whichever of the
  // cases are in the set and in whatever order.
  std::vector<std::string> ids;
  std::map<std::string, std::size_t> familyOfId; // family id -> index

  // The family of a case, or of a family id: its members, or nullptr for
  // an unknown id.
  const std::vector<std::string> *familyFor(const std::string &id) const;
  // The id whose progress stands for the family of a case (or the family
  // id itself); an unknown id stands for itself.
  std::string representative(const std::string &caseId) const;
};

// Groups each case with the cases of the set that are its mirror images:
// case B is a mirror of case A when B's pieces to solve are A's mirrored and
// sit where A's mirrored state has them, up to a U turn before the alg.
CaseFamilies findFamilies(const AlgSet &set);

// Moves a family's card kept under one of its cases, as it was before
// families had ids (under the family's first case), to the family's id:
// for a family with no progress of its own, the card of its case reviewed
// most (in set order on a tie), with that case's Learn attempts. The
// case's drill attempts stay. Whether anything changed.
bool adoptFamilyCards(const CaseFamilies &families, SetProgress &progress);

} // namespace vcube
