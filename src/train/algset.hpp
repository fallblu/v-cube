#pragma once

#include "cube/cubie.hpp"
#include "cube/scramble.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vcube {

// How a diagram draws a group of pieces.
enum class StickerMode : std::uint8_t {
  Full,        // real colours
  Orientation, // only stickers of the top colour; the rest grey
  Hidden,      // all grey
};

// Which stickers a case diagram shows.
struct DiagramMask {
  StickerMode llCorners = StickerMode::Full;
  StickerMode llEdges = StickerMode::Full;
  StickerMode rest = StickerMode::Full; // F2L pieces
  // false: the modes apply to the positions (whatever sits in the last
  // layer). true: they apply to the pieces wherever they are, so that an
  // F2L piece in the top layer keeps its colours; F2L pieces the step
  // leaves free are drawn like last-layer pieces.
  bool byPiece = false;
  friend bool operator==(const DiagramMask &, const DiagramMask &) = default;
};

enum class DiagramView : std::uint8_t {
  TopLayer,       // top-down last-layer view with side stickers
  Iso3D,          // the cube seen from the top-front-right corner
  Iso3DFrontLeft, // ...from the top-front-left corner
  Iso3DBackLeft,  // ...from the top-back-left corner
  Iso3DBackRight, // ...from the top-back-right corner
};

// What a case's algs achieve: the pieces in `free` may be left unsolved
// (e.g. for OLL, the last layer's permutation), the rest must be solved.
struct StepSpec {
  PieceMask free;
  DiagramMask mask;
  DiagramView view = DiagramView::TopLayer;
  friend bool operator==(const StepSpec &, const StepSpec &) = default;
};

// A mirror image of a case: left-right (across the M plane, R <-> L),
// front-back (across the S plane, F <-> B) or both, which is the case
// turned half way round the U-D axis. An F2L case for the front-right
// slot is, mirrored, the case for the front-left, back-right or back-left
// slot.
struct Mirror {
  bool leftRight = false;
  bool frontBack = false;
  bool none() const { return !leftRight && !frontBack; }
  friend bool operator==(const Mirror &, const Mirror &) = default;
};

struct AlgCase {
  std::string id; // unique within its set
  std::string name;
  CubieCube state;               // the case as seen before its alg
  std::vector<std::string> algs; // algs[0] is the main alg
};

struct CaseGroup {
  std::string name;
  std::optional<StepSpec> spec; // overrides the set's spec when present
  std::vector<AlgCase> cases;
};

struct AlgSet {
  std::string id;
  std::string name;
  std::string description;
  bool builtin = false; // case definitions shipped with the app
  StepSpec spec;
  bool aufBefore = true;      // Learn and Drill turn U randomly before the
  bool aufAfter = true;       // case, and after it
  double targetSeconds = 3.0; // SRS grading reference time per case
  // Learn walks through new cases (a steppable demo, then the alg done
  // with each next move shown) and shows the next move as a hint during
  // learning steps.
  bool guided = true;
  // Learn takes each case with its mirror images as one family: one card
  // per family, a random member set up each time (see families.hpp).
  bool families = false;
  // The cases are solved a step at a time: an alg may stop at another case
  // of the set instead of finishing the step, and that case's alg carries
  // on from there (see reduction.hpp). Learn then asks for one step per
  // case and follows the cases through to the end.
  bool reduction = false;
  // Learn and Drill also set the cases up in these mirror images, with the
  // algs mirrored to match, so that a set written for one F2L slot is
  // practised for the others too.
  std::vector<Mirror> mirrors;
  std::vector<CaseGroup> groups;

  // The ways a case is set up: as it is, and in each mirror image.
  std::vector<Mirror> worlds() const;

  const StepSpec &specFor(const CaseGroup &g) const {
    return g.spec ? *g.spec : spec;
  }
  int caseCount() const;
  AlgCase *findCase(std::string_view caseId);
  const AlgCase *findCase(std::string_view caseId) const;
  const CaseGroup *groupOf(std::string_view caseId) const;
  // A case id not used yet in this set, based on `base`.
  std::string uniqueCaseId(std::string_view base) const;
};

// Moves the cases `ids` (kept in set order) into group `group`, before the
// case `before`, or at the group's end if `before` is empty or not in that
// group. Returns false if nothing moved.
bool moveCases(AlgSet &set, const std::vector<std::string> &ids,
               std::size_t group, std::string_view before = {});
// Moves the cases one place up (`direction` < 0) or down, past the next
// case that isn't moving; at the edge of their group they go to the end of
// the group before it (or the start of the one after). Returns false if
// nothing moved.
bool shiftCases(AlgSet &set, const std::vector<std::string> &ids,
                int direction);
// Moves group `from` to place `to` of the new order. Returns false if
// nothing moved.
bool moveGroup(AlgSet &set, std::size_t from, std::size_t to);

// Text form of a PieceMask used in alg-set files: space-separated tokens
// "<pieces>:<flags>", where <pieces> is LL, LLC (LL corners), LLE (LL
// edges), F2L, ALL or a single position name (URF, FR, ...), and <flags>
// is any of p (permutation free) and o (orientation free). Empty = none.
std::string freeToString(const PieceMask &m);
// The mask is normalised (PieceMask::normalize()): pieces whose places are
// free trade places, so if one of them may also be left twisted or flipped,
// all of them may. Checks and drill scrambles then agree on what is free.
// `widened`, if given, is set when that freed more than the text says.
std::optional<PieceMask> parseFree(std::string_view text,
                                   bool *widened = nullptr);

std::string_view stickerModeName(StickerMode m);
std::optional<StickerMode> parseStickerMode(std::string_view s);

// Facelets of `state` with the stickers hidden by `mask` set to
// kMaskedColour. `free` (the step's free pieces) matters for masks by piece.
FaceletCube maskedFacelets(const CubieCube &state, const DiagramMask &mask,
                           const PieceMask &free = {});

} // namespace vcube
