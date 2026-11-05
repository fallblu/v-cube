#pragma once

#include "cube/cubie.hpp"
#include "cube/scramble.hpp"

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
  bool aufBefore = true;      // drills turn U randomly before the case...
  bool aufAfter = true;       // ...and after it
  double targetSeconds = 3.0; // SRS grading reference time per case
  std::vector<CaseGroup> groups;

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

// Text form of a PieceMask used in alg-set files: space-separated tokens
// "<pieces>:<flags>", where <pieces> is LL, LLC (LL corners), LLE (LL
// edges), F2L, ALL or a single position name (URF, FR, ...), and <flags>
// is any of p (permutation free) and o (orientation free). Empty = none.
std::string freeToString(const PieceMask &m);
std::optional<PieceMask> parseFree(std::string_view text);

std::string_view stickerModeName(StickerMode m);
std::optional<StickerMode> parseStickerMode(std::string_view s);

// Facelets of `state` with the stickers hidden by `mask` set to
// kMaskedColour. `free` (the step's free pieces) matters for masks by piece.
FaceletCube maskedFacelets(const CubieCube &state, const DiagramMask &mask,
                           const PieceMask &free = {});

} // namespace vcube
