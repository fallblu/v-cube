#include "train/eo_cross.hpp"

#include "cube/scramble.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <numeric>

using namespace vcube;

TEST_CASE("a step is done in a hold that is allowed", "[eocross]") {
  ColourNeutrality fixed; // white top, green front
  std::vector<Hold> holds = withBothEoAxes(fixed.holds);
  CHECK(holds == std::vector<Hold>{{0, 1}, {0, 2}});
  FaceletCube solved;
  // Held as it is: yellow cross on the bottom.
  CHECK(doneHold(solved, SolveStep::EOCross, holds) == Hold{0, 2});
  FaceletCube turned = solved;
  turned.apply(parseAlgOrEmpty("y"));
  auto h = doneHold(turned, SolveStep::EOCross, holds);
  REQUIRE(h);
  CHECK(h->top == 0);
  // EO alone on the third axis isn't one of this neutrality's.
  FaceletCube f;
  f.apply(parseAlgOrEmpty("F"));
  // F breaks EO for F/B but not for R/L.
  CHECK(doneHold(f, SolveStep::EO, {{0, 2}}) == std::nullopt);
  CHECK(doneHold(f, SolveStep::EO, holds).has_value());
  // A cross on another colour doesn't count.
  FaceletCube u;
  u.apply(parseAlgOrEmpty("U"));
  CHECK(doneHold(u, SolveStep::EOCross, {{0, 2}}).has_value());
  CHECK(doneHold(u, SolveStep::EOCross, {{1, 2}}) == std::nullopt);
}

TEST_CASE("holds are ranked by their optimum", "[eocross]") {
  Rng rng(11);
  CubieCube scrambled = randomState(PieceMask::all(), rng);
  ColourNeutrality n = ColourNeutrality::preset(kWcaHold, Neutrality::X2Y);
  std::vector<Hold> holds = withBothEoAxes(n.holds);
  std::vector<HeldOptimum> ranked =
      solveStepHeld(scrambled, SolveStep::EOCross, holds, 4);
  REQUIRE(ranked.size() == 4); // two crosses, two axes each
  for (std::size_t i = 0; i < ranked.size(); ++i) {
    if (i) {
      CHECK(ranked[i - 1].optimum.length <= ranked[i].optimum.length);
    }
    // Each solution does the step in its hold.
    FaceletCube f = scrambled.toFacelets();
    f.apply(holdRotation(ranked[i].hold));
    f.apply(ranked[i].optimum.solutions.front());
    CHECK(doneHold(f, SolveStep::EOCross, {ranked[i].hold}) == ranked[i].hold);
    CHECK(rankOfHold(ranked, ranked[i].hold, SolveStep::EOCross) == i);
    Hold y2 = holdAfter([&] {
      Alg a = holdRotation(ranked[i].hold);
      a.push_back(parseAlgOrEmpty("y2").front());
      return a;
    }());
    CHECK(rankOfHold(ranked, y2, SolveStep::EOCross) == i);
  }
  CHECK_FALSE(rankOfHold(ranked, Hold{1, 2}, SolveStep::EOCross));
}

TEST_CASE("an EO Cross is broken down by where its moves went", "[eocross]") {
  Rng rng(5);
  for (int i = 0; i < 6; ++i) {
    CubieCube state = randomState(PieceMask::all(), rng);
    FaceletCube start = state.toFacelets();
    auto optimum = solveStep(state, SolveStep::EOCross, 1);
    REQUIRE(optimum);
    // An optimal solution loses nothing anywhere.
    EoBreakdown best = eoBreakdown(start, optimum->solutions.front(), kWcaHold);
    CHECK(best.done);
    CHECK(best.optimal == optimum->length);
    REQUIRE_FALSE(best.parts.empty());
    CHECK(best.parts.back().step == SolveStep::EOCross);
    CHECK(best.parts.back().toGo == 0);
    int moves = 0;
    for (const EoPart &p : best.parts) {
      CHECK(p.lost == 0);
      moves += p.moves;
    }
    CHECK(moves == optimum->length);

    // EO first, then the cross: the parts' losses add up to the moves
    // over the optimum.
    auto eo = solveStep(state, SolveStep::EO, 1);
    REQUIRE(eo);
    CubieCube after = state;
    after.apply(eo->solutions.front());
    auto rest = solveStep(after, SolveStep::EOCross, 1);
    REQUIRE(rest);
    Alg solution = eo->solutions.front();
    solution.insert(solution.end(), rest->solutions.front().begin(),
                    rest->solutions.front().end());
    EoBreakdown two = eoBreakdown(start, solution, kWcaHold);
    CHECK(two.done);
    REQUIRE_FALSE(two.parts.empty());
    CHECK(two.parts.front().end == eo->solutions.front().size());
    int lost = 0, total = 0;
    for (const EoPart &p : two.parts) {
      CHECK(p.lost >= 0);
      lost += p.lost;
      total += p.moves;
    }
    CHECK(total == eo->length + rest->length);
    CHECK(lost == total - optimum->length);
    // Only the first part can lose: the rest was optimal from there.
    for (std::size_t p = 1; p < two.parts.size(); ++p) {
      CHECK(two.parts[p].lost == 0);
    }
  }
  // Not finished, and nothing to do.
  FaceletCube f;
  f.apply(parseAlgOrEmpty("R U F"));
  EoBreakdown none = eoBreakdown(f, parseAlgOrEmpty("U"), kWcaHold);
  CHECK_FALSE(none.done);
  EoBreakdown solved = eoBreakdown(FaceletCube(), {}, kWcaHold);
  CHECK(solved.done);
  CHECK(solved.parts.empty());
  CHECK(solved.optimal == 0);
}
