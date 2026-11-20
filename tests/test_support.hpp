#pragma once

// Helpers shared by the unit tests.

#include "train/case_logic.hpp"
#include "train/solve_review.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace vcube::testing {

// The solved cube after `moves`, as facelets.
inline FaceletCube after(std::string_view moves) {
  FaceletCube f;
  f.apply(parseAlgOrEmpty(moves));
  return f;
}

// The cube after `alg` (rotations allowed); the test stops if it isn't a
// real cube.
inline CubieCube after(const CubieCube &c, const Alg &alg) {
  auto out = applyAlg(c, alg);
  REQUIRE(out);
  return *out;
}

// Three FR pair cases, and an FR alg as it is for another slot: mirrored.
inline const std::vector<std::string> kFrAlgs = {"R U' R'", "R U' R' U R U R'",
                                                 "R U2 R' U R U' R'"};

inline Alg slotAlg(const std::string &frAlg, Slot slot) {
  Alg alg = parseAlgOrEmpty(frAlg);
  if (slot == Slot::FL || slot == Slot::BL) {
    alg = mirrorM(alg);
  }
  if (slot == Slot::BR || slot == Slot::BL) {
    alg = mirrorS(alg);
  }
  return alg;
}

// The FR cases and their mirrors in the other slots, one group per slot,
// as an F2L set has them.
inline AlgSet pairSet() {
  AlgSet set;
  set.id = "pairs";
  set.spec.free = *parseFree("LL:p LLC:o");
  for (Slot s : {Slot::FR, Slot::FL, Slot::BR, Slot::BL}) {
    std::string name(slotName(s));
    CaseGroup g{name, std::nullopt, {}};
    for (std::size_t i = 0; i < kFrAlgs.size(); ++i) {
      Alg alg = slotAlg(kFrAlgs[i], s);
      g.cases.push_back({name + "-" + std::to_string(i + 1),
                         name + " " + std::to_string(i + 1),
                         stateSolvedBy(alg),
                         {toString(alg)}});
    }
    set.groups.push_back(g);
  }
  return set;
}

} // namespace vcube::testing
