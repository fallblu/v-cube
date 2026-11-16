#pragma once

#include "cube/move.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace vcube {

// Faces in Kociemba order. A facelet's "colour" is the face whose centre
// colour it carries in the solved cube.
enum class Face : std::uint8_t { U, R, F, D, L, B };

inline constexpr int kFaceletCount = 54;
// Colour value for a sticker that a diagram should draw grey.
inline constexpr std::uint8_t kMaskedColour = 6;

// Integer 3D vector; x points right (R), y up (U), z to the front (F).
struct Vec3i {
  int x = 0, y = 0, z = 0;
  friend constexpr bool operator==(Vec3i, Vec3i) = default;
};

// Facelets are indexed U1..U9, R1..R9, F1..F9, D1..D9, L1..L9, B1..B9 as in
// Kociemba's cube definition; within a face, row-major in the usual net
// orientation (U seen from above with B at the top, D seen from below with
// F at the top, side faces with U at the top).
Vec3i faceletPosition(int index); // centre of the cubie the sticker is on
Vec3i faceletNormal(int index);   // outward direction of the sticker

// Facelet-level cube: which colour sits at every sticker position. Applies
// any move, including slices, wide moves and rotations, so it is the model
// behind the virtual cube.
class FaceletCube {
public:
  using Stickers = std::array<std::uint8_t, kFaceletCount>;

  FaceletCube(); // solved
  explicit FaceletCube(const Stickers &stickers) : s_(stickers) {}

  // From a 54-character string of face letters (URFDLB) or '.' for masked.
  static std::optional<FaceletCube> fromString(std::string_view text);
  std::string toString() const;

  const Stickers &stickers() const { return s_; }
  std::uint8_t operator[](int i) const {
    return s_[static_cast<std::size_t>(i)];
  }
  std::uint8_t &operator[](int i) { return s_[static_cast<std::size_t>(i)]; }

  void apply(Move m);
  void apply(const Alg &alg);

  // Every face shows a single colour (in any orientation).
  bool isSolved() const;
  // Centres are in the standard U R F D L B places.
  bool isStandardOrientation() const;

  // Rotates the whole cube so that the centres are in their standard places
  // and returns the rotation that was applied.
  Alg normalizeOrientation();

  friend bool operator==(const FaceletCube &, const FaceletCube &) = default;

private:
  Stickers s_{};
};

// Alg-Trainer's flat view: a grid 5 stickers wide and 6 tall showing the U
// face above the F face, with the adjacent L and R stickers at the sides
// (rows from top to bottom):
//   L1 U1 U2 U3 R3
//   L2 U4 U5 U6 R2
//   L3 U7 U8 U9 R1
//   L3 F1 F2 F3 R1
//   L6 F4 F5 F6 R4
//   L9 F7 F8 F9 R7
// L3 and R1 appear twice, as in Alg-Trainer. Entries are facelet indices.
inline constexpr int kFlatRows = 6;
inline constexpr int kFlatColumns = 5;
using FlatLayout = std::array<std::array<int, kFlatColumns>, kFlatRows>;
const FlatLayout &flatLayout();

// Whether a sticker at `pos` (a cubie centre) belongs to the layer(s) that
// the move type turns.
bool moveTurnsCubie(MoveType t, Vec3i pos);
// The sense of a single clockwise quarter turn as a right-handed rotation
// about the positive axis (+1 or -1).
int moveRotationSign(MoveType t);

// The 24 whole-cube orientations, each as a rotation sequence from the
// standard orientation (the first is empty).
const std::array<Alg, 24> &allRotations();

} // namespace vcube
