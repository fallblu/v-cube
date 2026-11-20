#pragma once

#include "cube/facelet.hpp"
#include "cube/move.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace vcube {

// Corner and edge positions in Kociemba's order.
enum Corner : std::uint8_t { URF, UFL, ULB, UBR, DFR, DLF, DBL, DRB };
enum Edge : std::uint8_t { UR, UF, UL, UB, DR, DF, DL, DB, FR, FL, BL, BR };

inline constexpr int kCorners = 8;
inline constexpr int kEdges = 12;
inline constexpr std::array<std::string_view, kCorners> kCornerNames = {
    "URF", "UFL", "ULB", "UBR", "DFR", "DLF", "DBL", "DRB"};
inline constexpr std::array<std::string_view, kEdges> kEdgeNames = {
    "UR", "UF", "UL", "UB", "DR", "DF", "DL", "DB", "FR", "FL", "BL", "BR"};

// Facelet indices of each corner/edge position. The first facelet of a
// corner is its U or D sticker; edges list U/D (or F/B for slice edges)
// first.
extern const std::array<std::array<std::uint8_t, 3>, kCorners> kCornerFacelets;
extern const std::array<std::array<std::uint8_t, 2>, kEdges> kEdgeFacelets;

// Piece-level cube in a fixed orientation (centres never move). cp[i] is the
// corner piece at position i and co[i] its twist (0..2); likewise ep/eo for
// edges (flip 0..1).
struct CubieCube {
  std::array<std::uint8_t, kCorners> cp{0, 1, 2, 3, 4, 5, 6, 7};
  std::array<std::uint8_t, kCorners> co{};
  std::array<std::uint8_t, kEdges> ep{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
  std::array<std::uint8_t, kEdges> eo{};

  friend bool operator==(const CubieCube &, const CubieCube &) = default;

  bool isSolved() const { return *this == CubieCube{}; }

  // The cube after doing `a` then `b` (starting from solved).
  static CubieCube multiply(const CubieCube &a, const CubieCube &b);
  CubieCube operator*(const CubieCube &b) const { return multiply(*this, b); }
  CubieCube inverse() const;

  // Face turns (U R F D L B). Other moves turn the pieces as cubieOf()
  // does: a slice or wide move as the face turns it amounts to with the
  // centres kept still, and a rotation not at all.
  void apply(Move m);
  void apply(const Alg &alg);

  // Valid = reachable by moves: permutations, matching parity, twist and
  // flip sums.
  bool isValid() const;
  int cornerParity() const;
  int edgeParity() const;

  FaceletCube toFacelets() const;
  // Converts a cube whose centres are in standard orientation; nullopt if
  // the stickers are not those of a real cube (a piece with stickers no
  // piece has, a piece twice, a twisted corner or flipped edge alone, ...).
  static std::optional<CubieCube> fromFacelets(const FaceletCube &f);
  // For stickers that are a real cube by construction (e.g. a real cube
  // turned, mirrored or recoloured): throws std::logic_error if they
  // aren't, rather than carrying on with a wrong cube.
  static CubieCube fromRealFacelets(const FaceletCube &f);
};

// Whether the piece of a position is home and not flipped or twisted.
inline bool edgeSolved(const CubieCube &c, Edge e) {
  return c.ep[e] == e && c.eo[e] == 0;
}
inline bool cornerSolved(const CubieCube &c, Corner k) {
  return c.cp[k] == k && c.co[k] == 0;
}

// Where a piece is and how it is turned there, as one value: a corner's
// place * 3 + twist, an edge's place * 2 + flip.
inline int cornerValue(const CubieCube &c, Corner piece) {
  for (std::size_t i = 0; i < kCorners; ++i) {
    if (c.cp[i] == piece) {
      return static_cast<int>(i) * 3 + c.co[i];
    }
  }
  return 0;
}
inline int edgeValue(const CubieCube &c, Edge piece) {
  for (std::size_t i = 0; i < kEdges; ++i) {
    if (c.ep[i] == piece) {
      return static_cast<int>(i) * 2 + c.eo[i];
    }
  }
  return 0;
}

// Cubie cube of one face quarter turn.
const CubieCube &faceTurnCubie(Face f);

// Cubie cube of any alg (slices and rotations allowed): the alg is applied
// to a facelet cube, re-oriented, and converted.
CubieCube cubieOf(const Alg &alg);

// U-layer turn by `quarters` (0..3).
CubieCube uTurn(int quarters);

// A cube turned any way round (e.g. after rotations while solving), as
// the solver sees it with the centres in their places.
std::optional<CubieCube> centredCubie(const FaceletCube &cube);
// The cube after `uBefore` U turns and then `alg` (rotations allowed), as
// centredCubie() sees it.
std::optional<CubieCube> applyAlg(const CubieCube &cube, const Alg &alg,
                                  int uBefore = 0);

} // namespace vcube
