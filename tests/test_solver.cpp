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
