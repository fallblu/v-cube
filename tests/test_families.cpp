#include "cube/scramble.hpp"
#include "io/algset_json.hpp"
#include "train/case_logic.hpp"
#include "train/families.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>

using namespace vcube;

namespace {

// Three FR pair cases and their mirrors in the other slots, one group per
// slot, as an F2L set has them.
const std::vector<std::string> kFrAlgs = {"R U' R'", "R U' R' U R U R'",
                                          "R U2 R' U R U' R'"};

AlgSet pairSet() {
  AlgSet set;
  set.id = "pairs";
  set.spec.free = *parseFree("LL:p LLC:o");
  struct Slot {
    std::string name;
    bool lr, fb;
  };
  for (const Slot &s : std::vector<Slot>{{"FR", false, false},
                                         {"FL", true, false},
                                         {"BR", false, true},
                                         {"BL", true, true}}) {
    CaseGroup g{s.name, std::nullopt, {}};
    for (std::size_t i = 0; i < kFrAlgs.size(); ++i) {
      Alg alg = parseAlgOrEmpty(kFrAlgs[i]);
      if (s.lr) {
        alg = mirrorM(alg);
      }
      if (s.fb) {
        alg = mirrorS(alg);
      }
      AlgCase c;
      c.id = s.name + "-" + std::to_string(i + 1);
      c.name = s.name + " " + std::to_string(i + 1);
      c.state = stateSolvedBy(alg);
      c.algs.push_back(toString(alg));
      g.cases.push_back(c);
    }
    set.groups.push_back(g);
  }
  return set;
}

} // namespace

TEST_CASE("mirrored states are real and mirror back", "[families]") {
  Rng rng(7);
  for (int i = 0; i < 50; ++i) {
    CubieCube c = randomState(PieceMask::all(), rng);
    for (auto [lr, fb] : {std::pair{true, false}, std::pair{false, true},
                          std::pair{true, true}}) {
      CubieCube m = mirrorState(c, lr, fb);
      CHECK(m.isValid());
      CHECK(mirrorState(m, lr, fb) == c);
    }
  }
  Alg alg = parseAlgOrEmpty("R U F' D2 L B'");
  CHECK(mirrorState(cubieOf(alg), true, false) == cubieOf(mirrorM(alg)));
  CHECK(mirrorState(cubieOf(alg), false, true) == cubieOf(mirrorS(alg)));
  CHECK(mirrorState(cubieOf(alg), true, true) ==
        cubieOf(mirrorS(mirrorM(alg))));
}

TEST_CASE("a case's pieces to solve leave out free pieces", "[families]") {
  PieceMask free = *parseFree("LL:p LLC:o");
  CasePieces p = casePieces(stateSolvedBy(parseAlgOrEmpty("R U R'")), free);
  CHECK(p.corners == std::vector<Corner>{DFR});
  CHECK(p.edges == std::vector<Edge>{FR});
  CHECK(casePieces(CubieCube{}, PieceMask{}) == CasePieces{});
}

TEST_CASE("pair cases form families with their three mirrors", "[families]") {
  AlgSet set = pairSet();
  CaseFamilies f = findFamilies(set);
  REQUIRE(f.families.size() == 3);
  for (std::size_t i = 0; i < 3; ++i) {
    std::string n = std::to_string(i + 1);
    CHECK(f.families[i] ==
          std::vector<std::string>{"FR-" + n, "FL-" + n, "BR-" + n, "BL-" + n});
    CHECK(f.representative("BL-" + n) == "FR-" + n);
  }
  REQUIRE(f.familyFor("FL-2"));
  CHECK(f.familyFor("FL-2")->size() == 4);
  CHECK_FALSE(f.familyFor("nope"));
  CHECK(f.representative("nope") == "nope");
}

TEST_CASE("families are found up to the U turns around a case", "[families]") {
  AlgSet set = pairSet();
  // The FL cases seen from another side.
  for (auto &c : set.groups[1].cases) {
    c.state = uTurn(3) * c.state * uTurn(2);
  }
  CaseFamilies f = findFamilies(set);
  CHECK(f.families.size() == 3);
  set.aufBefore = false;
  set.aufAfter = false;
  f = findFamilies(set);
  // Without U turns the FL cases no longer line up with the others.
  CHECK(f.families.size() == 6);
}

TEST_CASE("mirror-image PLLs share a family", "[families]") {
  QString error;
  auto pll = readAlgSetFile(
      QStringLiteral(VCUBE_SOURCE_DIR "/resources/algsets/pll.json"), &error);
  REQUIRE(pll);
  CaseFamilies f = findFamilies(*pll);
  auto same = [&](const std::string &a, const std::string &b) {
    return f.familyOf.at(a) == f.familyOf.at(b);
  };
  CHECK(same("Ja", "Jb"));
  CHECK(same("Aa", "Ab"));
  CHECK(same("Ua", "Ub"));
  CHECK(same("Ra", "Rb"));
  CHECK(same("Na", "Nb"));
  CHECK_FALSE(same("Ja", "T"));
  CHECK(f.familyFor("H")->size() == 1);
  CHECK(f.familyFor("T")->size() == 1);
  std::size_t total = 0;
  for (const auto &family : f.families) {
    total += family.size();
  }
  CHECK(total == 21);
}
