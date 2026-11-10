#pragma once

#include "cube/cubie.hpp"
#include "cube/move.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// Coordinates and move tables shared by the solvers (the two-phase solver
// and the step solver). Everything is inline so that it is compiled with
// the solvers' optimisation (see src/cube/CMakeLists.txt); include it only
// from the optimised solver sources.
namespace vcube::coord {

// Moves are numbered face * 3 + (amount - 1), faces in U R F D L B order.
inline constexpr int kMoves = 18;
inline constexpr int kTwist = 2187; // 3^7: the eighth corner's twist follows
inline constexpr int kFlip = 2048;  // 2^11: the twelfth edge's flip follows

// The face turn of a move number.
inline Move moveOf(int m) {
  return Move{static_cast<MoveType>(m / 3),
              static_cast<std::uint8_t>(m % 3 + 1), 0};
}

// The cubie cube of each move number.
inline std::array<CubieCube, kMoves> moveCubies() {
  std::array<CubieCube, kMoves> out{};
  for (int f = 0; f < 6; ++f) {
    CubieCube c;
    for (int a = 0; a < 3; ++a) {
      c = c * faceTurnCubie(static_cast<Face>(f));
      out[static_cast<std::size_t>(f * 3 + a)] = c;
    }
  }
  return out;
}

// Canonical move order: never the same face twice in a row, and opposite
// faces in one order only (U before D, R before L, F before B). -1 for no
// previous face.
inline constexpr bool skipFace(int face, int prevFace) {
  return prevFace >= 0 &&
         (face == prevFace || (face % 3 == prevFace % 3 && face < prevFace));
}

// Corner twist of the first seven corners, base 3.
inline int twistOf(const CubieCube &c) {
  int t = 0;
  for (std::size_t i = 0; i < 7; ++i) {
    t = t * 3 + c.co[i];
  }
  return t;
}

inline void setTwist(CubieCube &c, int t) {
  int sum = 0;
  for (std::size_t i = 7; i-- > 0;) {
    c.co[i] = static_cast<std::uint8_t>(t % 3);
    sum += c.co[i];
    t /= 3;
  }
  c.co[7] = static_cast<std::uint8_t>((3 - sum % 3) % 3);
}

// Edge flip of the first eleven edges, base 2.
inline int flipOf(const CubieCube &c) {
  int f = 0;
  for (std::size_t i = 0; i < 11; ++i) {
    f = f * 2 + c.eo[i];
  }
  return f;
}

inline void setFlip(CubieCube &c, int f) {
  int sum = 0;
  for (std::size_t i = 11; i-- > 0;) {
    c.eo[i] = static_cast<std::uint8_t>(f % 2);
    sum += c.eo[i];
    f /= 2;
  }
  c.eo[11] = static_cast<std::uint8_t>(sum % 2);
}

// A move table: table[coord * kMoves + move] is the coordinate after the
// move, for each of `moves` (the others stay 0). `set` puts a coordinate
// value on a solved cube, `get` reads it back.
template <typename Set, typename Get, typename Moves>
std::vector<std::uint16_t> buildMoveTable(int size, Set set, Get get,
                                          const Moves &moves) {
  const std::array<CubieCube, kMoves> cubes = moveCubies();
  std::vector<std::uint16_t> table(static_cast<std::size_t>(size * kMoves), 0);
  for (int i = 0; i < size; ++i) {
    CubieCube c;
    set(c, i);
    for (int m : moves) {
      CubieCube n = c * cubes[static_cast<std::size_t>(m)];
      table[static_cast<std::size_t>(i * kMoves + m)] =
          static_cast<std::uint16_t>(get(n));
    }
  }
  return table;
}

// The 18 move numbers.
inline const std::array<int, kMoves> &allMoves() {
  static const std::array<int, kMoves> moves = [] {
    std::array<int, kMoves> out{};
    for (int i = 0; i < kMoves; ++i) {
      out[static_cast<std::size_t>(i)] = i;
    }
    return out;
  }();
  return moves;
}

// The flip move table for all 18 moves, built once and shared.
inline const std::vector<std::uint16_t> &flipMoveTable() {
  static const std::vector<std::uint16_t> table =
      buildMoveTable(kFlip, setFlip, flipOf, allMoves());
  return table;
}

} // namespace vcube::coord
