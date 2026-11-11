#pragma once

#include "cube/move.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vcube {

// The layer types a move set can hold: faces, wide turns and slices (no
// rotations), in MoveType order U .. S.
inline constexpr int kLayerTypes = 15;

// Which turns of each layer a move set allows. Quarter turns come in both
// directions together, so that a move set always holds the inverse of
// each of its moves.
struct LayerTurns {
  bool quarter = false; // R and R'
  bool half = false;    // R2
  bool any() const { return quarter || half; }
  friend bool operator==(LayerTurns, LayerTurns) = default;
};

// The moves an alg generator may use, or an alg filter allows (a
// "generator" in cubers' terms, such as <R, U>).
struct MoveSet {
  std::array<LayerTurns, kLayerTypes> layers{};

  static MoveSet allFaces();
  bool empty() const;
  bool has(MoveType t) const {
    return static_cast<int>(t) < kLayerTypes &&
           layers[static_cast<std::size_t>(t)].any();
  }
  // Every move of the set, in MoveType order, each with amount 1, 2 and
  // 3 as allowed.
  std::vector<Move> moves() const;
  // Whether every turn of `alg` is in the set; rotations are allowed
  // when `rotations` is set (and a move written R3 counts as R').
  bool contains(const Alg &alg, bool rotations = true) const;

  // Text form: layers separated by commas, each written as its move
  // ("R", "r", "M") for quarter and half turns, with "2" for half turns
  // only and "1" for quarter turns only: "R, U, F2". Wide turns may also be
  // written Rw.
  std::string toString() const;
  // "<R, U, F2>"
  std::string display() const;
  // Reads toString()'s text, with or without the angle brackets, and with
  // spaces instead of commas. nullopt on anything it can't read.
  static std::optional<MoveSet> parse(std::string_view text);

  friend bool operator==(const MoveSet &, const MoveSet &) = default;
};

struct MoveSetPreset {
  std::string name; // e.g. "<R, U>" or a user's own name
  MoveSet set;
};

// The presets v-cube ships with: the common generators for last-layer and
// last-slot algs, then all faces.
const std::vector<MoveSetPreset> &builtinMoveSets();

// Move counts of an alg as written (rotations free except in ETM):
// HTM: an outer or wide turn is 1, a slice 2. STM: any layer turn is 1.
// QTM: a quarter turn is 1 and a half turn 2, a slice twice that. ETM:
// every move, rotations too, is 1.
struct MoveCounts {
  int htm = 0;
  int stm = 0;
  int qtm = 0;
  int etm = 0;
};
MoveCounts moveCounts(const Alg &alg);

// The smallest preset-like move set an alg uses: each layer it turns, with
// the turns it makes of it (quarter, half). Rotations are left out.
MoveSet moveSetOf(const Alg &alg);

} // namespace vcube
