#pragma once

#include "cube/cubie.hpp"
#include "cube/move.hpp"

#include <array>
#include <compare>
#include <optional>
#include <random>
#include <string_view>
#include <vector>

namespace vcube {

// A way of holding the cube while solving, by the colours on top and in
// front. Colours are the WCA faces they belong to: 0 white (U), 1 red (R),
// 2 green (F), 3 yellow (D), 4 orange (L), 5 blue (B).
//
// Scrambles are always applied holding white on top and green in front.
// The app's logic works in the solver's frame (U on top): a step such as
// the cross is solved on D, a last-layer case sits on U. Solving held
// another way means turning the cube (e.g. x2) after scrambling, so the
// scramble must set up the step "turned the other way" for the solver to
// find it where they hold it.
struct Hold {
  int top = 0;
  int front = 2;

  friend auto operator<=>(const Hold &, const Hold &) = default;
};

inline constexpr Hold kWcaHold{0, 2};

// Top and front are neither the same colour nor opposite.
bool isValidHold(Hold h);
// The 24 holds, ordered by top colour, then front colour.
const std::array<Hold, 24> &allHolds();

// The whole-cube rotation that turns the cube from the WCA hold (white top,
// green front) to `h`, e.g. "z2" for yellow top, green front.
Alg holdRotation(Hold h);
// The hold the cube is in after `rotation` from the WCA hold.
Hold holdAfter(const Alg &rotation);
// The colour each face shows when the cube is held as `h`, by face
// (U R F D L B): colours[face] = colour.
std::array<int, 6> holdColours(Hold h);

// The state to scramble to (holding white top, green front) so that, held
// as `h`, the cube shows `state` (in the solver's frame).
CubieCube heldState(const CubieCube &state, Hold h);
// The inverse: what the solver sees held as `h` after scrambling to
// `scrambled` holding white top, green front.
CubieCube solverState(const CubieCube &scrambled, Hold h);

// Common degrees of colour neutrality, as the rotations allowed on top of
// a main hold: Y means any of y, y2, y' (any front with the same top),
// X2Y means x2 and y (either of two opposite tops, any front), and so on.
enum class Neutrality { Fixed, Y2, Y, X2, X2Y2, X2Y, Full };
inline constexpr int kNeutralityCount = 7;

// "fixed", "y2", "y", "x2", "x2y2", "x2y", "full".
std::string_view neutralityName(Neutrality n);

// The holds `n` allows starting from `main`, sorted.
std::vector<Hold> neutralHolds(Hold main, Neutrality n);

// The colour neutrality used for scrambles and cases in every mode: the
// holds a scramble may be meant for, one picked at random each time, and
// the main hold, which diagrams are drawn in.
struct ColourNeutrality {
  Hold main = kWcaHold;
  std::vector<Hold> holds{kWcaHold}; // sorted, never empty

  static ColourNeutrality preset(Hold main, Neutrality n);

  // Sorts the holds, drops invalid or repeated ones, and falls back to the
  // main hold when none are left.
  void normalize();
  // A hold picked uniformly at random.
  Hold pick(std::mt19937_64 &rng) const;
  // Only one hold.
  bool isFixed() const { return holds.size() == 1; }
  // The preset that gives exactly these holds from the main hold, if any.
  std::optional<Neutrality> matchingPreset() const;

  friend bool operator==(const ColourNeutrality &,
                         const ColourNeutrality &) = default;
};

} // namespace vcube
