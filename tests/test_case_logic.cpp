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
  for (std::string text :
       {"", "LL:p", "LL:p LLC:o", "LLE:p", "ALL:po", "LL:po DFR:po FR:po"}) {
    auto m = parseFree(text);
    REQUIRE(m);
    CHECK(freeToString(*m) == text);
  }
  CHECK_FALSE(parseFree("LL"));
  CHECK_FALSE(parseFree("XX:p"));
  CHECK_FALSE(parseFree("LL:q"));
}

TEST_CASE("algs are fitted with the AUFs they need") {
  CubieCube tperm =
      stateSolvedBy(parseAlgOrEmpty("R U R' U' R' F R2 U' R' U' R U R' F'"));
  PieceMask none;
  auto fit = fitAlg(
      tperm, parseAlgOrEmpty("R U R' U' R' F R2 U' R' U' R U R' F'"), none);
  REQUIRE(fit);
  CHECK(fit->before == 0);
  CHECK(fit->after == 0);
  // The same case seen after a U2 needs a U2 first.
  CubieCube turned = tperm * uTurn(2);
  fit = fitAlg(turned, parseAlgOrEmpty("R U R' U' R' F R2 U' R' U' R U R' F'"),
               none);
  REQUIRE(fit);
  CHECK(fit->before == 2);
  // Jb solves T's case? No: different case.
  CHECK_FALSE(fitAlg(
      tperm, parseAlgOrEmpty("R U R' F' R U R' U' R' F R2 U' R'"), none));
  // A rotated version of the alg still fits.
  CHECK(fitAlg(tperm,
               parseAlgOrEmpty("y2 L U L' U' L' B L2 U' L' U' L U L' B' y2"),
               none));
  CHECK(fitAlg(
      tperm, parseAlgOrEmpty("y2 L U L' U' L' B L2 U' L' U' L U L' B'"), none));
  CHECK(withAufs("R U R'", {3, 2}) == "(U') R U R' (U2)");
}

TEST_CASE("the case an alg solves allows for the alg's cube rotations") {
  PieceMask none;
  for (const char *text : {"R U R' U' R' F R2 U' R' U' R U R' F'",
                           "x R U R' U' R' F R2 U' R' U' R U R' F'",
                           "z' y R U R' U' R' F R2 U' R' U' R U R' F'",
                           "R U R' U' x' R' F R2 U' R' U' R U R' F' x",
                           "x R2 D2 R U R' D2 R U' R x'"}) {
    INFO(text);
    Alg alg = parseAlgOrEmpty(text);
    auto fit = fitAlg(stateSolvedBy(alg), alg, none);
    REQUIRE(fit);
    CHECK(fit->before == 0);
    CHECK(fit->after == 0);
  }
}

TEST_CASE("drill states keep the case and can be solved with an AUF") {
  AlgSet set = ollLikeSet();
  Rng rng(5);
  const auto &group = set.groups[0];
  for (int i = 0; i < 30; ++i) {
    for (const auto &c : group.cases) {
      CubieCube s = drillState(set, group, c, rng);
      Alg alg = parseAlgOrEmpty(c.id == "sune" ? "R U R' U R U2 R'"
                                               : "R U R' U' R' F R F'");
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
  DiagramMask oll{StickerMode::Orientation, StickerMode::Orientation,
                  StickerMode::Full};
  FaceletCube f = maskedFacelets(s, oll);
  int grey = 0, top = 0;
  for (int i = 0; i < kFaceletCount; ++i) {
    grey += f[i] == kMaskedColour;
    top += f[i] == 0;
  }
  CHECK(top == 9);
  CHECK(grey == 12); // the 12 LL side stickers that are not U-coloured
}

TEST_CASE("masks by piece keep F2L pieces in the top layer coloured") {
  // The case R U' R' solves has the FR pair in the top layer, and an LL
  // corner and edge in the FR slot.
  CubieCube s = stateSolvedBy(parseAlgOrEmpty("R U' R'"));
  DiagramMask byPosition{StickerMode::Hidden, StickerMode::Hidden,
                         StickerMode::Full};
  DiagramMask byPiece = byPosition;
  byPiece.byPiece = true;
  auto colouredAt = [](const FaceletCube &f, std::size_t pos, bool corner) {
    int n = 0;
    if (corner) {
      for (auto i : kCornerFacelets[pos]) {
        n += f[i] != kMaskedColour;
      }
    } else {
      for (auto i : kEdgeFacelets[pos]) {
        n += f[i] != kMaskedColour;
      }
    }
    return n;
  };
  std::size_t cornerAt = 0, edgeAt = 0;
  for (std::size_t i = 0; i < kCorners; ++i) {
    cornerAt = s.cp[i] == DFR ? i : cornerAt;
  }
  for (std::size_t i = 0; i < kEdges; ++i) {
    edgeAt = s.ep[i] == FR ? i : edgeAt;
  }
  REQUIRE(cornerAt < 4);
  REQUIRE(edgeAt < 4);
  FaceletCube pos = maskedFacelets(s, byPosition);
  CHECK(colouredAt(pos, cornerAt, true) == 0);
  CHECK(colouredAt(pos, DFR, true) == 3); // an LL corner, by position
  FaceletCube piece = maskedFacelets(s, byPiece);
  CHECK(colouredAt(piece, cornerAt, true) == 3);
  CHECK(colouredAt(piece, edgeAt, false) == 2);
  CHECK(colouredAt(piece, DFR, true) == 0);
  CHECK(colouredAt(piece, FR, false) == 0);
  // F2L pieces the step leaves free are drawn like LL pieces.
  PieceMask free = *parseFree("LL:p LLC:o DRB:po BR:p");
  FaceletCube freeBr = maskedFacelets(CubieCube{}, byPiece, free);
  CHECK(colouredAt(freeBr, DRB, true) == 0);
  CHECK(colouredAt(freeBr, BR, false) == 0);
  CHECK(colouredAt(freeBr, DFR, true) == 3);
}

TEST_CASE("what an alg leaves unsolved is counted and described by layer") {
  PieceMask none;
  PieceMask oll = *parseFree("LL:p");
  // The Sune case under OLL: three twisted corners, all in the U layer.
  CubieCube sune = stateSolvedBy(parseAlgOrEmpty("R U R' U R U2 R'"));
  Unsolved u = unsolvedPieces(sune, oll);
  CHECK(u.cornersTwisted[0] == 3);
  CHECK(u.corners() == 3);
  CHECK(u.edges() == 0);
  CHECK(describeUnsolved(u) == "leaves 3 corners twisted (U layer)");
  // The T perm with everything to solve: two corners and two edges swapped.
  CubieCube tperm =
      stateSolvedBy(parseAlgOrEmpty("R U R' U' R' F R2 U' R' U' R U R' F'"));
  u = unsolvedPieces(tperm, none);
  CHECK(u.cornersMisplaced[0] == 2);
  CHECK(u.edgesMisplaced[0] == 2);
  CHECK(describeUnsolved(u) ==
        "leaves 2 corners and 2 edges unsolved (U layer)");
  // ... but nothing under a step that leaves the LL permutation free.
  CHECK(unsolvedPieces(tperm, oll).none());
  CHECK(describeUnsolved(unsolvedPieces(tperm, oll)) == "solves the step");
  // Pieces in other layers.
  CubieCube sexy = stateSolvedBy(parseAlgOrEmpty("R U R' U'"));
  u = unsolvedPieces(sexy, none);
  CHECK(u.cornersMisplaced[2] + u.cornersTwisted[2] > 0);
  CHECK(u.edgesMisplaced[1] + u.edgesFlipped[1] > 0);
  Unsolved made;
  made.cornersMisplaced[0] = 1;
  made.edgesMisplaced[2] = 2;
  CHECK(describeUnsolved(made) ==
        "leaves 1 corner and 2 edges unsolved (U and D layers)");
  made.edgesFlipped[1] = 1;
  CHECK(describeUnsolved(made) ==
        "leaves 1 corner and 3 edges unsolved (U, middle and D layers)");
  Unsolved flips;
  flips.edgesFlipped[0] = 2;
  CHECK(describeUnsolved(flips) == "leaves 2 edges flipped (U layer)");
}

TEST_CASE("alg checks respect the step and the set's AUF settings") {
  PieceMask none;
  PieceMask oll = *parseFree("LL:p");
  const std::string t = "R U R' U' R' F R2 U' R' U' R U R' F'";
  CubieCube tperm = stateSolvedBy(parseAlgOrEmpty(t));
  AlgCheck c = checkAlg(tperm, t, none, true, true);
  CHECK(c.passes());
  CHECK(describeCheck(c) == "14 moves");
  // PLL algs needing a U turn before and after still pass.
  CubieCube turned = uTurn(1) * tperm * uTurn(3);
  c = checkAlg(turned, t, none, true, true);
  REQUIRE(c.passes());
  CHECK(describeCheck(c) == "(U) … (U'), 14 moves");
  // ... unless the set doesn't turn U around the case.
  c = checkAlg(turned, t, none, false, true);
  CHECK_FALSE(c.passes());
  CHECK(describeCheck(c).starts_with("leaves "));
  c = checkAlg(turned, t, none, true, false);
  CHECK_FALSE(c.passes());
  // OLL algs pass whatever permutation they leave in the last layer.
  CubieCube sune = stateSolvedBy(parseAlgOrEmpty("R U R' U R U2 R'"));
  CubieCube sunePermuted = sune * tperm;
  REQUIRE(unsolvedPieces(sunePermuted * sune.inverse(), oll).none());
  CHECK(checkAlg(sunePermuted, "R U R' U R U2 R'", oll, true, true).passes());
  CHECK(
      checkAlg(sunePermuted, "y' R' U2 R U R' U R", oll, true, true).passes());
  // Failing algs say what is left.
  c = checkAlg(sune, "R U R' U' R' F R F'", oll, true, true);
  CHECK_FALSE(c.passes());
  CHECK(describeCheck(c) == describeUnsolved(c.unsolved));
  CHECK(c.unsolved.corners() + c.unsolved.edges() > 0);
  c = checkAlg(sune, "R U Q", oll, true, true);
  CHECK(describeCheck(c) == "can't read at character 5");
  CHECK(describeCheck(checkAlg(sune, "  ", oll, true, true)) == "empty");
}

namespace {

// The main alg written as `prefix + base` for a case that `prefix + base`
// solves exactly; checks the display orientation strips the prefix.
void checkDisplay(const std::string &prefix, const std::string &base,
                  const PieceMask &free) {
  INFO(prefix << " | " << base);
  std::string main = prefix.empty() ? base : prefix + " " + base;
  CubieCube canonical = stateSolvedBy(parseAlgOrEmpty(main));
  auto d = displayOrientation(canonical, main, free, true, true);
  REQUIRE(d);
  CHECK(d->alg == base);
  // The alg without its prefix solves the case as shown, with no U turn
  // before (or after) it.
  auto fit = fitAlg(d->state, parseAlgOrEmpty(base), free, false, false);
  CHECK(fit);
  if (free.isEmpty()) {
    CHECK(d->state == stateSolvedBy(parseAlgOrEmpty(base)));
  }
}

} // namespace

TEST_CASE("cases are shown from the angle their main alg starts from") {
  PieceMask none;
  PieceMask oll = *parseFree("LL:p");
  const std::string t = "R U R' U' R' F R2 U' R' U' R U R' F'";
  const std::string jb = "R U R' F' R U R' U' R' F R2 U' R'";
  const std::string sune = "R U R' U R U2 R'";
  const std::string t45 = "F R U R' U' F'";
  for (const char *prefix :
       {"", "y", "y'", "y2", "U", "U'", "U2", "y U", "(U2) y'", "U y2"}) {
    checkDisplay(prefix, t, none);
    checkDisplay(prefix, jb, none);
    checkDisplay(prefix, sune, oll);
    checkDisplay(prefix, t45, oll);
  }
  // Without a prefix the stored case is kept as it is.
  CubieCube tperm = stateSolvedBy(parseAlgOrEmpty(t));
  auto d = displayOrientation(tperm, t, none, true, true);
  REQUIRE(d);
  CHECK(d->state == tperm);
  CHECK(d->alg == t);
  // A case stored at another angle is turned so the alg needs no U first.
  CubieCube turned = tperm * uTurn(2);
  d = displayOrientation(turned, t, none, true, true);
  REQUIRE(d);
  CHECK(d->alg == t);
  CHECK(fitAlg(d->state, parseAlgOrEmpty(t), none, false, true));
  // Leading U turns are part of the alg when the set doesn't turn U first.
  CubieCube uSetup = stateSolvedBy(parseAlgOrEmpty("U " + t));
  d = displayOrientation(uSetup, "U " + t, none, false, true);
  REQUIRE(d);
  CHECK(d->alg == "U " + t);
  CHECK(d->state == uSetup);
  // Algs that don't solve the case give no orientation.
  CHECK_FALSE(displayOrientation(tperm, sune, none, true, true));
  // Tilting rotations stay in the alg: the case is still shown with its
  // last layer on top, turned only by the leading y.
  for (const char *prefix : {"", "y", "y2 U"}) {
    std::string alg =
        std::string(prefix) + (*prefix ? " " : "") + "x R' U R' D2 R U' x'";
    INFO(alg);
    CubieCube s = stateSolvedBy(parseAlgOrEmpty(alg));
    d = displayOrientation(s, alg, oll, true, true);
    REQUIRE(d);
    CHECK(d->alg == "x R' U R' D2 R U' x'");
  }
  // Notation is kept as written.
  d = displayOrientation(stateSolvedBy(parseAlgOrEmpty("y (R U R' U')3")),
                         "y (R U R' U')3", none, true, true);
  REQUIRE(d);
  CHECK(d->alg == "(R U R' U')3");
}

TEST_CASE("case analysis picks the first passing alg as the main alg") {
  AlgSet set = ollLikeSet();
  AlgCase &sune = set.groups[0].cases[0];
  sune.algs = {"R U R' U' R' F R F'", "R U Q", "y R U R' U R U2 R'",
               "R U R' U R U2 R'"};
  CaseAnalysis a = analyzeCase(set, set.groups[0], sune);
  REQUIRE(a.checks.size() == 4);
  CHECK_FALSE(a.checks[0].passes());
  CHECK(a.checks[1].parseError);
  CHECK(a.passing == std::vector<std::size_t>{2, 3});
  CHECK(a.main == 2u);
  CHECK(a.failing() == 2);
  CHECK(a.mainAlg == "R U R' U R U2 R'");
  CHECK(fitAlg(a.display, parseAlgOrEmpty(a.mainAlg), set.spec.free, false,
               true));
  // No passing alg: the stored case, no main alg.
  sune.algs = {"R U Q"};
  a = analyzeCase(set, set.groups[0], sune);
  CHECK_FALSE(a.main);
  CHECK(a.display == sune.state);
  CHECK(a.mainAlg.empty());
}
