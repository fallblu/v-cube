#include "train/eo_cross.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <numeric>

using namespace vcube;

TEST_CASE("EO Cross kinds have keys", "[eocross]") {
  CHECK(eoKindKey({EoDrill::Edges, SolveStep::EO, 6}) == "edges/6");
  CHECK(eoKindKey({EoDrill::Axis, SolveStep::EOPlus2, 6}) == "axis/eo+2");
  CHECK(eoKindKey({EoDrill::Plan, SolveStep::EOLine, 4}) == "plan/eoline/4");
  CHECK(eoKindKey({EoDrill::Solve, SolveStep::EOCross, 0}) ==
        "solve/eocross/0");
  CHECK(eoKindKey({EoDrill::Full, SolveStep::EO, 12}) == "full/12");
  for (int d = 0; d < kEoDrills; ++d) {
    for (SolveStep step : eoCrossSteps()) {
      for (int bad : kBadEdgeKinds) {
        EoKind k = normalized({static_cast<EoDrill>(d), step, bad});
        CHECK(eoKindFromKey(eoKindKey(k)) == k);
      }
    }
  }
  for (const char *key :
       {"", "edges", "edges/5", "edges/14", "edges/6/1", "axis", "axis/zzf2l",
        "axis/eo/2", "solve/eocross", "solve/cross/4", "full/x", "pair/2"}) {
    CAPTURE(key);
    CHECK_FALSE(eoKindFromKey(key));
  }
}

TEST_CASE("the bad edge shares are those of all states", "[eocross]") {
  double sum = 0.0;
  for (int bad : kBadEdgeKinds) {
    sum += badEdgeShare(bad);
  }
  CHECK(std::abs(sum - 1.0) < 1e-9);
  CHECK(badEdgeShare(6) == 924.0 / 2048.0);
  CHECK(badEdgeShare(3) == 0.0);
}

TEST_CASE("random EO states have the bad edges asked for", "[eocross]") {
  Rng rng(7);
  for (int bad : kBadEdgeKinds) {
    for (int i = 0; i < 20; ++i) {
      CubieCube c = randomEoState(bad, rng);
      REQUIRE(c.isValid());
      CHECK(badEdgeCount(c, 0) == bad);
    }
  }
  CHECK(randomEoState(-1, rng).isValid());
}

TEST_CASE("kinds that go badly come up more often", "[eocross]") {
  EoCrossStats stats;
  std::array<double, 7> fresh =
      badEdgeWeights(stats, EoDrill::Solve, SolveStep::EOCross);
  // Untried: by their flattened shares, 6 bad edges the most.
  CHECK(std::max_element(fresh.begin(), fresh.end()) - fresh.begin() == 3);
  CHECK(fresh[0] > 0.0);
  CHECK(fresh[0] == fresh[6]);
  PairAttempt good, bad;
  good.ms = bad.ms = 5000;
  good.moves = good.bestMoves = bad.bestMoves = 8;
  bad.moves = 12;
  bad.excess = 4;
  for (int i = 0; i < 8; ++i) {
    stats.add({EoDrill::Solve, SolveStep::EOCross, 4}, good);
    stats.add({EoDrill::Solve, SolveStep::EOCross, 8}, bad);
  }
  std::array<double, 7> w =
      badEdgeWeights(stats, EoDrill::Solve, SolveStep::EOCross);
  CHECK(w[4] > 4 * w[2]); // same share, 8 bad edges going badly
  CHECK(eoSummary(stats, {EoDrill::Solve, SolveStep::EOCross, 8}).weak);
  CHECK_FALSE(eoSummary(stats, {EoDrill::Solve, SolveStep::EOCross, 4}).weak);
  // Another drill or step has its own tries.
  CHECK(badEdgeWeights(stats, EoDrill::Plan, SolveStep::EOCross) == fresh);
  CHECK(badEdgeWeights(stats, EoDrill::Solve, SolveStep::EO) == fresh);
  CHECK(eoMeanMs(stats, EoDrill::Solve, SolveStep::EOCross) == 5000.0);
  Rng rng(3);
  std::array<int, 13> seen{};
  for (int i = 0; i < 400; ++i) {
    ++seen[static_cast<std::size_t>(
        pickBadEdges(stats, EoDrill::Solve, SolveStep::EOCross, rng))];
  }
  CHECK(seen[8] > seen[4]);
  CHECK(seen[1] + seen[3] + seen[5] + seen[7] + seen[9] + seen[11] == 0);
  // The newest tries are kept.
  for (int i = 0; i < 80; ++i) {
    stats.add({EoDrill::Edges, SolveStep::EO, 2}, good);
  }
  CHECK(stats.tries({EoDrill::Edges, SolveStep::EOCross, 2}).size() ==
        EoCrossStats::kPerKind);
}

TEST_CASE("a step is done in a hold that is allowed", "[eocross]") {
  ColourNeutrality fixed; // white top, green front
  std::vector<Hold> holds = eoHolds(fixed);
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
  CubieCube scrambled = randomEoState(6, rng);
  ColourNeutrality n = ColourNeutrality::preset(kWcaHold, Neutrality::X2Y);
  std::vector<Hold> holds = eoHolds(n);
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
    CubieCube state =
        randomEoState(kBadEdgeKinds[static_cast<std::size_t>(1 + i % 5)], rng);
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
