#include "cube/scramble.hpp"
#include "train/step_solver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace vcube;

namespace {

// Each takes at least as many moves as the one before.
const std::vector<SolveStep> kOrdered = {SolveStep::EO, SolveStep::EOPlus1,
                                         SolveStep::EOPlus2, SolveStep::EOLine,
                                         SolveStep::EOCross};
const std::vector<SolveStep> kSolvable = {
    SolveStep::EO,     SolveStep::EOPlus1, SolveStep::EOPlus2,
    SolveStep::EOLine, SolveStep::EOCross, SolveStep::Cross};

// The step done held as the solver sees it (no rotations allowed).
bool doneAsHeld(const CubieCube &c, SolveStep step) {
  if (stepNeedsEo(step) &&
      std::any_of(c.eo.begin(), c.eo.end(), [](auto o) { return o != 0; })) {
    return false;
  }
  int cross = 0;
  for (Edge e : {DF, DR, DB, DL}) {
    cross += c.ep[e] == e && c.eo[e] == 0 ? 1 : 0;
  }
  switch (step) {
  case SolveStep::EO:
    return true;
  case SolveStep::EOPlus1:
    return cross >= 1;
  case SolveStep::EOPlus2:
    return cross >= 2;
  case SolveStep::EOLine:
    return c.ep[DF] == DF && c.ep[DB] == DB;
  case SolveStep::EOCross:
  case SolveStep::Cross:
    return cross == 4;
  case SolveStep::ZZF2L:
  case SolveStep::XEOCross:
  case SolveStep::CFOPF2L:
    break;
  }
  return false;
}

CubieCube after(const CubieCube &c, const Alg &alg) {
  CubieCube out = c;
  out.apply(alg);
  return out;
}

// Every solution has the optimal length, reaches the step and is distinct.
void checkOptimum(const CubieCube &c, SolveStep step, const StepOptimum &o) {
  REQUIRE_FALSE(o.solutions.empty());
  for (const Alg &s : o.solutions) {
    CHECK(static_cast<int>(s.size()) == o.length);
    CHECK(doneAsHeld(after(c, s), step));
    FaceletCube f = c.toFacelets();
    f.apply(s);
    CHECK(stepReached(f, step));
  }
  auto sorted = o.solutions;
  std::sort(sorted.begin(), sorted.end(), [](const Alg &a, const Alg &b) {
    return toString(a) < toString(b);
  });
  CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
}

} // namespace

TEST_CASE("a solved cube has every step done", "[step]") {
  for (SolveStep step : kSolvable) {
    auto o = solveStep(CubieCube{}, step);
    REQUIRE(o);
    CHECK(o->length == 0);
    REQUIRE(o->solutions.size() == 1);
    CHECK(o->solutions[0].empty());
    CHECK(stepDistance(CubieCube{}, step) == 0);
  }
  CHECK_FALSE(solveStep(CubieCube{}, SolveStep::ZZF2L));
  CHECK_FALSE(stepDistance(CubieCube{}, SolveStep::ZZF2L));
  CHECK_FALSE(hasOptimalSolver(SolveStep::ZZF2L));
  CHECK_FALSE(hasOptimalSolver(SolveStep::XEOCross));
  CHECK_FALSE(hasOptimalSolver(SolveStep::CFOPF2L));
  CHECK(hasOptimalSolver(SolveStep::EOCross));
  CHECK(hasOptimalSolver(SolveStep::EOLine));
  CHECK(hasOptimalSolver(SolveStep::Cross));
}

TEST_CASE("the cross alone ignores edge orientation", "[step]") {
  // F flips edges but leaves the cross a turn away; F2 doesn't flip.
  CHECK(stepDistance(cubieOf(parseAlgOrEmpty("F")), SolveStep::Cross) == 1);
  CHECK(stepDistance(cubieOf(parseAlgOrEmpty("F U F'")), SolveStep::Cross) ==
        0);
  CHECK(stepDistance(cubieOf(parseAlgOrEmpty("F U F'")), SolveStep::EOCross) >
        0);
  CHECK(stepDistance(cubieOf(parseAlgOrEmpty("R")), SolveStep::EOLine) == 0);
  CHECK(stepDistance(cubieOf(parseAlgOrEmpty("F")), SolveStep::EOLine) == 1);
  Rng rng(5);
  for (int n = 0; n < 6; ++n) {
    CubieCube c = randomState(PieceMask::all(), rng);
    auto cross = stepDistance(c, SolveStep::Cross);
    REQUIRE(cross);
    CHECK(*cross <= 8); // the cross is at most 8 moves
    CHECK(*cross <= *stepDistance(c, SolveStep::EOCross));
  }
}

TEST_CASE("one F2L pair is solved in <R, U, L>", "[step]") {
  // The FR pair out with R U R', the rest solved.
  CubieCube c = cubieOf(parseAlgOrEmpty("R U R'"));
  auto alg = solvePairRul(c, DFR, FR, {{DLF, FL}, {DBL, BL}, {DRB, BR}});
  REQUIRE(alg);
  CHECK(alg->size() == 3);
  CubieCube done = c;
  done.apply(*alg);
  CHECK(done.isSolved());
  // A longer one: the solution keeps the other slots.
  Alg setup = parseAlgOrEmpty("R U' R' U R U R' U2 R U2 R'");
  CubieCube s = cubieOf(setup);
  auto two = solvePairRul(s, DFR, FR, {{DLF, FL}, {DBL, BL}, {DRB, BR}});
  REQUIRE(two);
  CubieCube after = s;
  after.apply(*two);
  CHECK(after.cp[DFR] == DFR);
  CHECK(after.co[DFR] == 0);
  CHECK(after.ep[FR] == FR);
  CHECK(after.ep[BL] == BL);
  CHECK(after.ep[FL] == FL);
  CHECK(after.cp[DRB] == DRB);
  CHECK(after.ep[DR] == DR);
  CHECK(after.ep[DL] == DL);
  // Not a ZZ F2L state.
  CHECK_FALSE(solvePairRul(cubieOf(parseAlgOrEmpty("F")), DFR, FR, {}));
}

TEST_CASE("short scrambles are undone at most as long", "[step]") {
  for (const char *text :
       {"F", "F R", "B' D2 F", "F U' R2 B L'", "R F2 L' B"}) {
    Alg alg = parseAlgOrEmpty(text);
    CubieCube c = cubieOf(alg);
    for (SolveStep step : kSolvable) {
      auto o = solveStep(c, step);
      REQUIRE(o);
      CHECK(o->length <= static_cast<int>(alg.size()));
      checkOptimum(c, step, *o);
    }
  }
  // A single F quarter turn needs exactly one move for EO.
  CHECK(stepDistance(cubieOf(parseAlgOrEmpty("F")), SolveStep::EO) == 1);
  CHECK(stepDistance(cubieOf(parseAlgOrEmpty("R U")), SolveStep::EO) == 0);
}

TEST_CASE("random states are solved optimally and in order", "[step]") {
  Rng rng(11);
  for (int n = 0; n < 6; ++n) {
    CubieCube c = randomState(PieceMask::all(), rng);
    std::optional<int> previous;
    for (SolveStep step : kOrdered) {
      auto o = solveStep(c, step, 4);
      REQUIRE(o);
      checkOptimum(c, step, *o);
      CHECK(o->solutions.size() <= 4);
      CHECK(stepDistance(c, step) == o->length);
      if (previous) {
        CHECK(*previous <= o->length);
      }
      previous = o->length;
    }
    auto cross = solveStep(c, SolveStep::Cross, 4);
    REQUIRE(cross);
    checkOptimum(c, SolveStep::Cross, *cross);
    CHECK(stepDistance(c, SolveStep::EO) <= 7);
    CHECK(stepDistance(c, SolveStep::EOCross) <= 11);
  }
}

TEST_CASE("the solution cap reports more solutions", "[step]") {
  // EO of an F turn: F or F', but only one is kept.
  auto one = solveStep(cubieOf(parseAlgOrEmpty("F")), SolveStep::EO, 1);
  REQUIRE(one);
  CHECK(one->solutions.size() == 1);
  CHECK(one->more);
  // Many solutions exist for a full EO Cross of a random state.
  Rng rng(3);
  CubieCube c = randomState(PieceMask::all(), rng);
  auto capped = solveStep(c, SolveStep::EO, 1);
  auto all = solveStep(c, SolveStep::EO, 1000);
  REQUIRE(capped);
  REQUIRE(all);
  CHECK(capped->solutions.front() == all->solutions.front());
  CHECK(capped->more == (all->solutions.size() > 1));
  CHECK_FALSE(all->more);
}

TEST_CASE("held solving dedupes holds that pose the same problem", "[step]") {
  Alg scramble = parseAlgOrEmpty("R2 F' U B2 L D' F R' U2 B");
  Hold yellowTop{3, 2};
  Hold turnedY2{3, 5}; // yellow top, blue front: same cross and EO axis
  auto results = solveStepHeld(scramble, SolveStep::EOCross,
                               {kWcaHold, turnedY2, yellowTop});
  // White top green front, then yellow top blue front; yellow top green
  // front is the same problem as yellow top blue front.
  REQUIRE(results.size() == 2);
  CubieCube scrambled = cubieOf(scramble);
  for (const auto &r : results) {
    CubieCube held = solverState(scrambled, r.hold);
    checkOptimum(held, SolveStep::EOCross, r.optimum);
  }
  CHECK(results[0].optimum.length <= results[1].optimum.length);

  // EO only depends on the front-back axis.
  auto eo = solveStepHeld(scramble, SolveStep::EO,
                          {kWcaHold, Hold{1, 2}, Hold{3, 5}, Hold{0, 1}});
  CHECK(eo.size() == 2);
  CHECK(solveStepHeld(scramble, SolveStep::ZZF2L, {kWcaHold}).empty());

  auto best = heldDistance(scramble, SolveStep::EOCross, {kWcaHold, turnedY2});
  REQUIRE(best);
  CHECK(*best == results[0].optimum.length);
  CHECK(heldDistance(scramble, SolveStep::EOCross, {kWcaHold, turnedY2}) ==
        best); // from the cache
  CHECK_FALSE(heldDistance(scramble, SolveStep::EOCross, {}));
}

TEST_CASE("both EO axes add the hold turned a quarter", "[step]") {
  auto holds = withBothEoAxes({kWcaHold});
  REQUIRE(holds.size() == 2);
  CHECK(std::is_sorted(holds.begin(), holds.end()));
  for (Hold h : holds) {
    CHECK(h.top == 0);
  }
  CHECK(holds[0].front % 3 != holds[1].front % 3);
  // Any front with two tops stays the same eight holds.
  auto neutral = neutralHolds(kWcaHold, Neutrality::X2Y);
  CHECK(withBothEoAxes(neutral) == neutral);
}

TEST_CASE("bad edges are counted for each axis by position", "[step]") {
  for (int axis = 0; axis < 3; ++axis) {
    CHECK(badEdgeCount(CubieCube{}, axis) == 0);
  }
  auto badAt = [](const char *alg, int axis) {
    std::vector<Edge> out;
    auto bad = badEdges(cubieOf(parseAlgOrEmpty(alg)), axis);
    for (std::size_t i = 0; i < kEdges; ++i) {
      if (bad[i]) {
        out.push_back(static_cast<Edge>(i));
      }
    }
    return out;
  };
  // A quarter turn flips its layer's edges for its own axis only.
  CHECK(badAt("F", 0) == std::vector<Edge>{UF, DF, FR, FL});
  CHECK(badAt("F", 1).empty());
  CHECK(badAt("F", 2).empty());
  CHECK(badAt("R", 1) == std::vector<Edge>{UR, DR, FR, BR});
  CHECK(badAt("R", 0).empty());
  CHECK(badAt("R", 2).empty());
  CHECK(badAt("U", 2) == std::vector<Edge>{UR, UF, UL, UB});
  CHECK(badAt("U", 0).empty());
  CHECK(badAt("U", 1).empty());
  // Half turns and turns on other axes keep an axis's orientation.
  CHECK(badEdgeCount(cubieOf(parseAlgOrEmpty("U D2 R2 L' F2 B")), 1) == 4);
  CHECK(badEdgeCount(cubieOf(parseAlgOrEmpty("U R2 D' F2 L2 B2")), 1) == 0);
  CHECK(badEdgeCount(cubieOf(parseAlgOrEmpty("R L F2 B2 U2 D2")), 2) == 0);
}
