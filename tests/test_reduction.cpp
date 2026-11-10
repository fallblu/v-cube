#include "io/algset_json.hpp"
#include "train/alg_follow.hpp"
#include "train/case_logic.hpp"
#include "train/families.hpp"
#include "train/reduction.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace vcube;

namespace {

const std::string kSune = "R U R' U R U2 R'";
const std::string kAntisune = "R U2 R' U' R U' R'";

AlgCase makeCase(const std::string &id, const std::string &solvedBy,
                 const std::string &alg) {
  AlgCase c;
  c.id = id;
  c.name = id;
  c.state = stateSolvedBy(parseAlgOrEmpty(solvedBy));
  c.algs = {alg};
  return c;
}

// A last-slot set for the front-right slot, a trigger at a time: the last
// F2L edge goes in while the last five corners are oriented.
//   one:   R U' R' finishes
//   two:   R U2 R' leaves one (a U turn off)
//   three: R U R' leaves sune (half a turn off)
AlgSet lastSlotSet() {
  AlgSet set;
  set.id = "last-slot";
  set.spec.free = *parseFree("LL:p DFR:p");
  set.reduction = true;
  CaseGroup g{"Cases", std::nullopt, {}};
  g.cases.push_back(makeCase("sune", kSune, kSune));
  g.cases.push_back(makeCase("antisune", kAntisune, kAntisune));
  g.cases.push_back(makeCase("one", "R U' R'", "R U' R'"));
  g.cases.push_back(makeCase("two", "R U2 R' U R U' R'", "R U2 R'"));
  g.cases.push_back(makeCase("three", "R U R' U2 " + kSune, "R U R'"));
  set.groups.push_back(g);
  return set;
}

const std::vector<Mirror> kMirrors = {
    {true, false}, {false, true}, {true, true}};

CubieCube done(const CubieCube &cube, const std::string &alg) {
  FaceletCube f = cube.toFacelets();
  f.apply(parseAlgOrEmpty(alg));
  f.normalizeOrientation();
  auto c = CubieCube::fromFacelets(f);
  REQUIRE(c);
  return *c;
}

} // namespace

TEST_CASE("alg text is mirrored move by move", "[reduction]") {
  CHECK(mirrorAlgText("U (R U' R')", {}) == "U (R U' R')");
  CHECK(mirrorAlgText("U (R U' R')", {true, false}) == "U' (L' U L)");
  CHECK(mirrorAlgText("U (R U' R')", {false, true}) == "U' (R' U R)");
  CHECK(mirrorAlgText("U (R U' R')", {true, true}) == "U (L U' L')");
  CHECK(mirrorAlgText("(U2) r U R' U' r' F R F'", {true, false}) ==
        "(U2) l' U' L U l F' L' F");
  // A group's repeat stays a repeat, and a written count stays written.
  CHECK(mirrorAlgText("(R U R' U')x3", {true, false}) == "(L' U' L U)x3");
  CHECK(mirrorAlgText("(R U R' U')x3", {false, true}) == "(R' U' R U)x3");
  CHECK(mirrorAlgText("(R U R' U') x3", {false, true}) == "(R' U' R U) x3'");
  CHECK(mirrorAlgText("R3 U2 Rw' x3", {true, false}) == "L3' U2 Lw x3");
  for (Mirror m : kMirrors) {
    for (const std::string a :
         {"(R U R' U')x3", "R3 U R U2' R'", "[R U R' U']2 Rw X' y2 M'"}) {
      INFO(a);
      Alg alg = parseAlgOrEmpty(a);
      Alg mirrored = parseAlgOrEmpty(mirrorAlgText(a, m));
      REQUIRE(mirrored.size() == alg.size());
      CHECK(stateSolvedBy(mirrored) == mirrorState(stateSolvedBy(alg), m));
      for (std::size_t k = 0; k < alg.size(); ++k) {
        CHECK(quarterTurns(mirrored[k]) == quarterTurns(alg[k]));
      }
    }
  }
  // What it does is the mirror image of what the alg does.
  for (Mirror m : kMirrors) {
    Alg alg = parseAlgOrEmpty(kSune);
    Alg mirrored = parseAlgOrEmpty(mirrorAlgText(kSune, m));
    CHECK(stateSolvedBy(mirrored) == mirrorState(stateSolvedBy(alg), m));
  }
}

TEST_CASE("a step's U turns join the U turn before it", "[reduction]") {
  CHECK(withAufBefore("U (R U' R')", 0) == "U (R U' R')");
  CHECK(withAufBefore("U (R U' R')", 1) == "U2 (R U' R')");
  CHECK(withAufBefore("U (R U' R')", 3) == "(R U' R')");
  CHECK(withAufBefore("(R U R')", 2) == "U2 (R U R')");
  CHECK(withAufBefore("(U) R U R'", 2) == "(U') R U R'");
  CHECK(withAufBefore("R U R'", 0) == "R U R'");
  CHECK(withAufBefore("U2", 1) == "(U) U2");
}

TEST_CASE("a step passes by stopping at another case", "[reduction]") {
  AlgSet set = lastSlotSet();
  const CaseGroup &g = set.groups[0];
  CaseAnalysis two = analyzeCase(set, g, *set.findCase("two"));
  REQUIRE(two.main);
  CHECK(two.checks[0].reducesTo == "one");
  CHECK(describeCheck(two.checks[0]) == "→ one, 3 moves");
  CHECK(analyzeCase(set, g, *set.findCase("three")).checks[0].reducesTo ==
        "sune");
  CHECK(analyzeCase(set, g, *set.findCase("one")).checks[0].reducesTo.empty());
  // An alg that leaves the case as it was, or the F2L broken, doesn't.
  AlgCase bad = *set.findCase("two");
  bad.algs = {"U2", "R U2"};
  CaseAnalysis b = analyzeCase(set, g, bad);
  CHECK_FALSE(b.main);
  // Only reduction sets take steps.
  set.reduction = false;
  CHECK_FALSE(analyzeCase(set, g, *set.findCase("two")).main);
}

TEST_CASE("steps left count each case's own steps", "[reduction]") {
  AlgSet set = lastSlotSet();
  auto steps = stepsLeft(set);
  CHECK(steps.at("sune") == 1);
  CHECK(steps.at("one") == 1);
  CHECK(steps.at("two") == 2);
  CHECK(steps.at("three") == 2);
  // Steps that only lead round in circles never reach the end.
  AlgCase &one = *set.findCase("one");
  one.algs = {"R U' R' U' R U2 R'"}; // back to two
  steps = stepsLeft(set);
  CHECK_FALSE(steps.contains("one"));
  CHECK_FALSE(steps.contains("two"));
  CHECK(steps.at("three") == 2);
}

TEST_CASE("a cube is followed through its cases to the end", "[reduction]") {
  AlgSet set = lastSlotSet();
  Rng rng(7);
  const PieceMask &free = set.spec.free;
  for (int i = 0; i < 20; ++i) {
    const AlgCase &two = *set.findCase("two");
    CubieCube cube =
        withRandomAufs(set, setUpCase(two.state, free, {}, true, rng), rng);
    auto view = seenCase(set, cube);
    REQUIRE(view);
    CHECK(view->caseId == "two");
    CHECK_FALSE(view->mirrored);
    Chain chain = solveChain(set, cube);
    REQUIRE(chain.complete);
    REQUIRE(chain.steps.size() == 2);
    CHECK(chain.steps[1].view.caseId == "one");
    CubieCube at = cube;
    for (const ChainStep &step : chain.steps) {
      at = done(at, step.alg);
    }
    CHECK(stepSolved(at, free));
  }
  // A cube with the step done shows no case.
  CHECK_FALSE(seenCase(set, CubieCube{}));
}

TEST_CASE("slot cases come up mirrored in a mirror world", "[reduction]") {
  AlgSet set = lastSlotSet();
  Rng rng(11);
  const PieceMask &free = set.spec.free;
  for (Mirror world : kMirrors) {
    PieceMask worldFree = mirrorMask(free, world);
    for (const char *id : {"one", "two", "three"}) {
      const AlgCase &c = *set.findCase(id);
      CHECK_FALSE(slotIndependent(c.state, free, world));
      CubieCube cube =
          withRandomAufs(set, setUpCase(c.state, free, world, true, rng), rng);
      auto view = seenCase(set, cube, world);
      REQUIRE(view);
      CHECK(view->caseId == id);
      CHECK(view->mirrored);
      CHECK(viewOfCase(set, set.groups[0], c, cube, world) == view);
      Chain chain = solveChain(set, cube, world);
      REQUIRE(chain.complete);
      CubieCube at = cube;
      for (const ChainStep &step : chain.steps) {
        at = done(at, step.alg);
      }
      CHECK(stepSolved(at, worldFree));
      // The world can be told from the cube.
      auto seen = worldOf(
          [&] {
            AlgSet mirrored = set;
            mirrored.mirrors = kMirrors;
            return mirrored;
          }(),
          cube);
      REQUIRE(seen);
      CHECK(seen->world == world);
    }
  }
}

TEST_CASE("cases that don't depend on the slot keep their algs",
          "[reduction]") {
  AlgSet set = lastSlotSet();
  Rng rng(13);
  const PieceMask &free = set.spec.free;
  const AlgCase &sune = *set.findCase("sune");
  for (Mirror world : kMirrors) {
    CHECK(slotIndependent(sune.state, free, world));
    CubieCube cube =
        withRandomAufs(set, setUpCase(sune.state, free, world, true, rng), rng);
    auto view = seenCase(set, cube, world);
    REQUIRE(view);
    CHECK(view->caseId == "sune");
    CHECK_FALSE(view->mirrored);
    std::vector<ViewAlg> algs =
        algsFor(set, set.groups[0], sune, analyzeCase(set, set.groups[0], sune),
                *view, cube, world);
    REQUIRE(algs.size() == 1);
    CHECK(algs[0].text.find('L') == std::string::npos);
    CHECK(stepSolved(done(cube, algs[0].text), mirrorMask(free, world)));
  }
  // A step that stops at one is followed by the alg as it is written:
  // three mirrored left-right is a left-hand trigger, then the case that
  // looks like an antisune, done as the set's antisune.
  Mirror leftRight{true, false};
  const AlgCase &three = *set.findCase("three");
  CubieCube cube = setUpCase(three.state, free, leftRight, false, rng);
  Chain chain = solveChain(set, cube, leftRight);
  REQUIRE(chain.complete);
  REQUIRE(chain.steps.size() == 2);
  CHECK(chain.steps[0].alg == "L' U' L");
  std::vector<ViewAlg> step =
      algsFor(set, set.groups[0], three, analyzeCase(set, set.groups[0], three),
              chain.steps[0].view, cube, leftRight);
  REQUIRE(step.size() == 1);
  CHECK(step[0].reducesTo == "antisune");
  CHECK(chain.steps[1].view.caseId == "antisune");
  CHECK_FALSE(chain.steps[1].view.mirrored);
}

TEST_CASE("a follower knows when an alg is done", "[reduction]") {
  FaceletCube start = stateSolvedBy(parseAlgOrEmpty(kSune)).toFacelets();
  AlgFollower f(start, {parseAlgOrEmpty(kSune)});
  CHECK_FALSE(f.begun());
  f.move({MoveType::U, 1});
  f.move({MoveType::U, 3});
  CHECK_FALSE(f.begun());
  for (Move m : parseAlgOrEmpty("R U R'")) {
    f.move(m);
  }
  CHECK(f.begun());
  CHECK_FALSE(f.finished());
  for (Move m : parseAlgOrEmpty("U R U2 R'")) {
    f.move(m);
  }
  CHECK(f.finished());
  f.move({MoveType::U, 1});
  CHECK(f.finished());
  CHECK_FALSE(f.leftAt());
}

TEST_CASE("reduction and mirrors are kept in set files", "[reduction]") {
  AlgSet set = lastSlotSet();
  set.mirrors = kMirrors;
  auto back = algSetFromJson(algSetToJson(set), nullptr);
  REQUIRE(back);
  CHECK(back->reduction);
  CHECK(back->mirrors == kMirrors);
  CHECK(back->worlds().size() == 4);
  set.reduction = false;
  set.mirrors.clear();
  QJsonObject o = algSetToJson(set);
  CHECK_FALSE(o.contains("reduction"));
  CHECK_FALSE(o.contains("mirrors"));
  back = algSetFromJson(o, nullptr);
  REQUIRE(back);
  CHECK_FALSE(back->reduction);
  CHECK(back->worlds() == std::vector<Mirror>{Mirror{}});
}

TEST_CASE("a free D corner shows its D sticker in orientation diagrams",
          "[reduction]") {
  // The DFR corner in the last layer, D sticker up.
  CubieCube c;
  std::swap(c.cp[URF], c.cp[DFR]);
  std::swap(c.cp[UFL], c.cp[ULB]);
  REQUIRE(c.isValid());
  DiagramMask mask{StickerMode::Orientation, StickerMode::Orientation,
                   StickerMode::Full, true};
  FaceletCube f = maskedFacelets(c, mask, *parseFree("LL:p DFR:p"));
  CHECK(f[kCornerFacelets[URF][0]] == static_cast<std::uint8_t>(Face::D));
  CHECK(f[kCornerFacelets[URF][1]] == kMaskedColour);
  CHECK(f[kCornerFacelets[DFR][0]] == static_cast<std::uint8_t>(Face::U));
  // By position, as last-layer sets have it, nothing changes.
  mask.byPiece = false;
  f = maskedFacelets(c, mask, {});
  CHECK(f[kCornerFacelets[URF][0]] == kMaskedColour);
}

TEST_CASE("the algs a case is set up with follow however the AUF is done",
          "[reduction]") {
  // An alg ending in a U turn, which joins the AUF after it.
  const std::string written = "R U R' U' R' F R2 U' R' U' R U R' F' U";
  AlgSet set;
  set.id = "pll";
  CaseGroup g{"Cases", std::nullopt, {}};
  g.cases.push_back(makeCase("t", written, written));
  set.groups.push_back(g);
  const CaseGroup &group = set.groups[0];
  const AlgCase &c = group.cases[0];
  CaseAnalysis analysis = analyzeCase(set, group, c);
  auto follow = [&](const CubieCube &cube, const std::string &moves) {
    INFO(moves);
    std::optional<CaseView> view = seenCase(set, cube);
    REQUIRE(view);
    std::vector<Alg> algs;
    for (const ViewAlg &a : algsFor(set, group, c, analysis, *view, cube, {})) {
      algs.push_back(parseAlgOrEmpty(a.text));
    }
    REQUIRE_FALSE(algs.empty());
    AlgFollower f(cube.toFacelets(), algs);
    for (Move m : parseAlgOrEmpty(moves)) {
      f.move(m);
    }
    CHECK_FALSE(f.deviated());
    CHECK(f.finished());
    CHECK(stepSolved(done(cube, moves), {}));
  };
  for (int before = 0; before < 4; ++before) {
    for (int after = 0; after < 4; ++after) {
      std::string pre = aufText(before);
      std::string post = aufText(after);
      CubieCube cube =
          stateSolvedBy(parseAlgOrEmpty(pre + " " + written + " " + post));
      // The alg as it is learned, then the AUF.
      follow(cube, pre + " " + written + " " + post);
      // The AUF turned too far and put right.
      follow(cube, pre + " " + written + " " + post + " U U'");
      follow(cube, pre + " " + written + " " + aufText(after + 1) + " U'");
    }
  }
  // Another layer once only U turns are left still leaves the alg.
  CubieCube cube = stateSolvedBy(parseAlgOrEmpty(written + " U"));
  std::optional<CaseView> view = seenCase(set, cube);
  REQUIRE(view);
  std::vector<Alg> algs;
  for (const ViewAlg &a : algsFor(set, group, c, analysis, *view, cube, {})) {
    algs.push_back(parseAlgOrEmpty(a.text));
  }
  AlgFollower f(cube.toFacelets(), algs);
  for (Move m : parseAlgOrEmpty("R U R' U' R' F R2 U' R' U' R U R' F' U D")) {
    f.move(m);
  }
  CHECK(f.deviated());
}
