#include "cube/scramble.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace vcube;

TEST_CASE("scrambles reach their target state") {
  Rng rng(7);
  for (const auto &type : scrambleTypes()) {
    CAPTURE(type.id);
    for (int i = 0; i < 4; ++i) {
      CubieCube s = randomStateOf(type, rng);
      CHECK_FALSE(s.isSolved());
      Alg scr = scrambleToState(s);
      CubieCube c;
      c.apply(scr);
      CHECK(c == s);
    }
  }
}

TEST_CASE("random states stay within their piece masks") {
  Rng rng(9);
  const ScrambleType *pll = findScrambleType("pll");
  REQUIRE(pll);
  for (int i = 0; i < 50; ++i) {
    CubieCube s = randomStateOf(*pll, rng);
    CHECK(s.co == std::array<std::uint8_t, 8>{});
    CHECK(s.eo == std::array<std::uint8_t, 12>{});
    for (std::size_t p = 4; p < 8; ++p) {
      CHECK(s.cp[p] == p);
    }
  }
  PieceMask cross = findScrambleType("f2l")->mask;
  for (int i = 0; i < 50; ++i) {
    CubieCube s = randomState(cross, rng);
    CHECK(s.isValid());
    CHECK(withinMask(s, cross));
    for (Edge e : {DR, DF, DL, DB}) {
      CHECK(s.ep[e] == e);
      CHECK(s.eo[e] == 0);
    }
  }
}
