#pragma once

#include "cube/facelet.hpp"
#include "cube/move.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace vcube {

// Corner and edge positions in Kociemba's order.
enum Corner : std::uint8_t { URF, UFL, ULB, UBR, DFR, DLF, DBL, DRB };
enum Edge : std::uint8_t { UR, UF, UL, UB, DR, DF, DL, DB, FR, FL, BL, BR };

inline constexpr int kCorners = 8;
inline constexpr int kEdges = 12;

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

// Cubie cube of one face quarter turn.
const CubieCube &faceTurnCubie(Face f);

// Cubie cube of any alg (slices and rotations allowed): the alg is applied
// to a facelet cube, re-oriented, and converted.
CubieCube cubieOf(const Alg &alg);

// U-layer turn by `quarters` (0..3).
CubieCube uTurn(int quarters);

} // namespace vcube
