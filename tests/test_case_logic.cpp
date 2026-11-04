#include "train/algset.hpp"
#include "train/case_logic.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace vcube;

namespace {

AlgSet ollLikeSet() {
  AlgSet set;
  set.id = "test";
  set.spec.free = *parseFree("LL:p");
  CaseGroup g{"Group", std::nullopt, {}};
  AlgCase sune;
  sune.id = "sune";
  sune.name = "Sune";
  sune.state = stateSolvedBy(parseAlgOrEmpty("R U R' U R U2 R'"));
  g.cases.push_back(sune);
  AlgCase t;
  t.id = "t";
  t.name = "T";
  t.state = stateSolvedBy(parseAlgOrEmpty("R U R' U' R' F R F'"));
  g.cases.push_back(t);
  set.groups.push_back(g);
  return set;
}

} // namespace

TEST_CASE("free-piece specs round-trip through text") {
  for (std::string text : {"", "LL:p", "LL:p LLC:o", "LLE:p", "ALL:po",
                           "LL:po DFR:po FR:po"}) {
    auto m = parseFree(text);
    REQUIRE(m);
    CHECK(freeToString(*m) == text);
  }
  CHECK_FALSE(parseFree("LL"));
  CHECK_FALSE(parseFree("XX:p"));
  CHECK_FALSE(parseFree("LL:q"));
}

TEST_CASE("algs are fitted with the AUFs they need") {
  CubieCube tperm = stateSolvedBy(parseAlgOrEmpty("R U R' U' R' F R2 U' R' U' R U R' F'"));
  PieceMask none;
  auto fit = fitAlg(tperm, parseAlgOrEmpty("R U R' U' R' F R2 U' R' U' R U R' F'"), none);
  REQUIRE(fit);
  CHECK(fit->before == 0);
  CHECK(fit->after == 0);
  // The same case seen after a U2 needs a U2 first.
  CubieCube turned = tperm * uTurn(2);
  fit = fitAlg(turned, parseAlgOrEmpty("R U R' U' R' F R2 U' R' U' R U R' F'"), none);
  REQUIRE(fit);
  CHECK(fit->before == 2);
  // Jb solves T's case? No: different case.
  CHECK_FALSE(fitAlg(tperm, parseAlgOrEmpty("R U R' F' R U R' U' R' F R2 U' R'"), none));
  // A rotated version of the alg still fits.
  CHECK(fitAlg(tperm, parseAlgOrEmpty("y2 L U L' U' L' B L2 U' L' U' L U L' B' y2"), none));
  CHECK(fitAlg(tperm, parseAlgOrEmpty("y2 L U L' U' L' B L2 U' L' U' L U L' B'"), none));
  CHECK(withAufs("R U R'", {3, 2}) == "(U') R U R' (U2)");
}

TEST_CASE("drill states keep the case and can be solved with an AUF") {
  AlgSet set = ollLikeSet();
  Rng rng(5);
  const auto &group = set.groups[0];
  for (int i = 0; i < 30; ++i) {
    for (const auto &c : group.cases) {
      CubieCube s = drillState(set, group, c, rng);
      Alg alg = parseAlgOrEmpty(c.id == "sune" ? "R U R' U R U2 R'" : "R U R' U' R' F R F'");
      auto fit = fitAlg(s, alg, set.spec.free);
      REQUIRE(fit);
      CubieCube after = s * uTurn(fit->before);
      after.apply(alg);
      CHECK(stepSolved(after, set.spec.free));
      CHECK_FALSE(stepSolved(s, set.spec.free));
    }
  }
}

TEST_CASE("pasted algs are matched to their cases") {
  AlgSet set = ollLikeSet();
  auto report = importAlgs(set,
                           "Sune: R U R' U R U2 R' / y2 L U L' U L U2 L'\n"
                           "1. F R U R' U' F'\n"
                           "(U2) R U R' U' R' F R F'\n"
                           "R U R' U R U2 R'\n"
                           "R U Q\n",
                           false);
  CHECK(report.added == 3);
  CHECK(report.duplicates == 1);
  CHECK(report.unmatched.size() == 1);
  CHECK(report.invalid.size() == 1);
  CHECK(set.findCase("sune")->algs.size() == 2);
  CHECK(set.findCase("t")->algs.size() == 1);

  report = importAlgs(set, "Line: F R U R' U' F'\n", true);
  CHECK(report.created == 1);
  CHECK(set.caseCount() == 3);
  CHECK(set.findCase("Line"));
}

TEST_CASE("masked facelets hide what the step ignores") {
  CubieCube s = stateSolvedBy(parseAlgOrEmpty("R U R' U R U2 R'"));
  DiagramMask oll{StickerMode::Orientation, StickerMode::Orientation, StickerMode::Full};
  FaceletCube f = maskedFacelets(s, oll);
  int grey = 0, top = 0;
  for (int i = 0; i < kFaceletCount; ++i) {
    grey += f[i] == kMaskedColour;
    top += f[i] == 0;
  }
  CHECK(top == 9);
  CHECK(grey == 12); // the 12 LL side stickers that are not U-coloured
}
