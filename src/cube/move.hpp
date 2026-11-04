#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vcube {

// Every layer or rotation that a 3x3 move can turn. Face turns come first
// in Kociemba's U R F D L B order so that `static_cast<int>(type) < 6` means
// "outer face turn".
enum class MoveType : std::uint8_t {
  U, R, F, D, L, B,       // outer faces
  Uw, Rw, Fw, Dw, Lw, Bw, // wide (two layers)
  M, E, S,                // slices
  X, Y, Z,                // whole-cube rotations
};

inline constexpr int kMoveTypeCount = 18;

// One move: a layer type turned 1 (clockwise), 2 (half) or 3 (counter-
// clockwise) quarter turns.
struct Move {
  MoveType type = MoveType::U;
  std::uint8_t amount = 1; // 1, 2 or 3

  friend constexpr bool operator==(Move, Move) = default;
};

using Alg = std::vector<Move>;

constexpr bool isFaceTurn(MoveType t) { return static_cast<int>(t) < 6; }
constexpr bool isRotation(MoveType t) {
  return t == MoveType::X || t == MoveType::Y || t == MoveType::Z;
}

// 0 = x (R/L), 1 = y (U/D), 2 = z (F/B).
int moveAxis(MoveType t);

// "U", "Rw" is written "r", "M", "x" ...
std::string_view moveTypeName(MoveType t);
std::string toString(Move m);
std::string toString(const Alg &alg);

// Error from parseAlg, with the character offset where parsing failed.
struct ParseError {
  std::string message;
  std::size_t position = 0;
};

// Parses standard 3x3 notation:
//   - faces U R F D L B, wide moves as Rw or r, slices M E S, rotations x y z
//     (X Y Z accepted too), suffixes 2, ', 2' and the Unicode primes;
//   - grouping with optional repetition: (R U R' U')3, (R U)2';
//   - commutators [A, B] = A B A' B' and conjugates [A: B] = A B A';
//   - comments from "//" to end of line; "." and "*" are ignored.
// AUF parentheses such as "(U) R U R'" are simply a group.
struct ParseResult {
  Alg alg;
  std::optional<ParseError> error;
  explicit operator bool() const { return !error.has_value(); }
};
ParseResult parseAlg(std::string_view text);

// Parses or returns an empty alg on error.
Alg parseAlgOrEmpty(std::string_view text);

Alg invert(const Alg &alg);

// Mirrors left-right (across the M plane): R <-> L', U -> U', ...
Alg mirrorM(const Alg &alg);
// Mirrors front-back (across the S plane): F <-> B', U -> U', ...
Alg mirrorS(const Alg &alg);

// Merges consecutive moves of the same type (R R -> R2, R R' -> nothing) and
// moves of the same type separated only by moves on the parallel axis that
// commute with them (U D U -> U2 D).
Alg simplify(const Alg &alg);

// Number of moves in HTM, counting each non-rotation token once.
int moveCount(const Alg &alg);

} // namespace vcube
