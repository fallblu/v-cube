#include "cube/solver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <random>

using namespace vcube;

TEST_CASE("two-phase solver solves random states in at most 21-24 moves") {
  std::mt19937 rng(42);
  for (int i = 0; i < 25; ++i) {
    CubieCube s;
    for (int n = 0; n < 40; ++n) {
      s.apply(Move{static_cast<MoveType>(rng() % 6),
                   static_cast<std::uint8_t>(1 + rng() % 3)});
    }
    REQUIRE(s.isValid());
    Alg sol = TwoPhaseSolver::solve(s);
    CubieCube c = s;
    c.apply(sol);
    CHECK(c.isSolved());
    CHECK(sol.size() <= 24);
  }
}

TEST_CASE("solver handles solved trivial and superflip cubes") {
  CHECK(TwoPhaseSolver::solve(CubieCube{}).empty());
  CubieCube r = cubieOf(parseAlgOrEmpty("R"));
  CHECK(toString(TwoPhaseSolver::solve(r)) == "R'");
  CubieCube flip;
  flip.eo.fill(1);
  Alg sol = TwoPhaseSolver::solve(flip);
  CubieCube c = flip;
  c.apply(sol);
  CHECK(c.isSolved());
}

TEST_CASE("near-solved states get short solutions without using the budget") {
  TwoPhaseSolver::warmUp();
  CubieCube t =
      cubieOf(parseAlgOrEmpty("R U R' U' R' F R2 U' R' U' R U R' F'"));
  auto start = std::chrono::steady_clock::now();
  Alg sol = TwoPhaseSolver::solve(t, std::chrono::milliseconds(2000));
  auto took = std::chrono::steady_clock::now() - start;
  CubieCube c = t;
  c.apply(sol);
  CHECK(c.isSolved());
  CHECK(sol.size() <= 14);
  // It stops once shorter solutions stop coming, long before the fifth of
  // the budget that used to be spent looking for them.
  CHECK(took < std::chrono::milliseconds(300));
}

namespace {

std::vector<CubieCube> invalidCubes() {
  std::vector<CubieCube> out;
  CubieCube twist; // one corner twisted
  twist.co[URF] = 1;
  out.push_back(twist);
  CubieCube flip; // one edge flipped
  flip.eo[UF] = 1;
  out.push_back(flip);
  CubieCube parity; // two edges swapped
  std::swap(parity.ep[UF], parity.ep[UR]);
  out.push_back(parity);
  CubieCube twice; // the same corner twice
  twice.cp[URF] = twice.cp[UFL];
  out.push_back(twice);
  CubieCube range; // a piece that doesn't exist
  range.ep[UB] = 12;
  out.push_back(range);
  CubieCube overTwisted;
  overTwisted.co[URF] = 3;
  overTwisted.co[UFL] = 0;
  out.push_back(overTwisted);
  return out;
}

} // namespace

TEST_CASE("the solver refuses cubes that can't be reached by moves") {
  for (const CubieCube &c : invalidCubes()) {
    CHECK_FALSE(c.isValid());
    CHECK_FALSE(TwoPhaseSolver::solve(
        c, 22, TwoPhaseSolver::Clock::now() + std::chrono::seconds(1)));
    CHECK(TwoPhaseSolver::solve(c, std::chrono::milliseconds(50)).empty());
  }
  // Two swaps (corners and edges) together are reachable again.
  CubieCube both;
  std::swap(both.ep[UF], both.ep[UR]);
  std::swap(both.cp[URF], both.cp[UFL]);
  CHECK(both.isValid());
}

TEST_CASE("the solver keeps to its length limit and deadline") {
  TwoPhaseSolver::warmUp();
  auto later = TwoPhaseSolver::Clock::now() + std::chrono::seconds(5);
  CubieCube three = cubieOf(parseAlgOrEmpty("R U F"));
  // Within the limit: a solution no longer than it.
  auto sol = TwoPhaseSolver::solve(three, 3, later);
  REQUIRE(sol);
  CHECK(sol->size() <= 3);
  CubieCube c = three;
  c.apply(*sol);
  CHECK(c.isSolved());
  // Too short a limit: none.
  CHECK_FALSE(TwoPhaseSolver::solve(three, 2, later));
  CHECK_FALSE(TwoPhaseSolver::solve(three, 0, later));
  // Solved needs no moves, whatever the limit.
  auto none = TwoPhaseSolver::solve(CubieCube{}, 0, later);
  REQUIRE(none);
  CHECK(none->empty());
  // A limit past the solver's own depth is held to it.
  sol = TwoPhaseSolver::solve(three, 1000, later);
  REQUIRE(sol);
  c = three;
  c.apply(*sol);
  CHECK(c.isSolved());

  // A deadline already passed stops a search that can't finish at once:
  // hardly any random state has a solution of 12 moves or fewer.
  std::mt19937 rng(3);
  CubieCube s;
  for (int n = 0; n < 40; ++n) {
    s.apply(Move{static_cast<MoveType>(rng() % 6),
                 static_cast<std::uint8_t>(1 + rng() % 3)});
  }
  auto start = std::chrono::steady_clock::now();
  CHECK_FALSE(TwoPhaseSolver::solve(s, 12, TwoPhaseSolver::Clock::now()));
  CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
  // The shortest budget still gives a solution.
  Alg quick = TwoPhaseSolver::solve(s, std::chrono::milliseconds(1));
  c = s;
  c.apply(quick);
  CHECK(c.isSolved());
}
