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

TEST_CASE("every scramble type gives valid states of its own kind") {
  const auto &types = scrambleTypes();
  REQUIRE_FALSE(types.empty());
  CHECK(types.front().id == "333");
  CHECK(types.front().mask == PieceMask::all());
  CHECK_FALSE(findScrambleType("no-such-type"));
  Rng rng(21);
  for (std::size_t i = 0; i < types.size(); ++i) {
    const ScrambleType &type = types[i];
    CAPTURE(type.id);
    CHECK(findScrambleType(type.id) == &type);
    for (std::size_t j = i + 1; j < types.size(); ++j) {
      CHECK(types[j].id != type.id);
    }
    CHECK_FALSE(type.mask.isEmpty());
    // The masks are already normalized.
    PieceMask normal = type.mask;
    normal.normalize();
    CHECK(normal == type.mask);
    for (int n = 0; n < 30; ++n) {
      CubieCube s = randomStateOf(type, rng);
      REQUIRE(s.isValid());
      CHECK_FALSE(s.isSolved());
      // A U turn after the state may move the last layer as a whole.
      bool within = withinMask(s, type.mask);
      for (int q = 1; q < 4 && type.aufAfter && !within; ++q) {
        within = withinMask(
            s * cubieOf(parseAlgOrEmpty(q == 1 ? "U" : (q == 2 ? "U2" : "U'"))),
            type.mask);
      }
      CHECK(within);
      // Never one face turn from solved.
      for (const char *turn :
           {"U", "U2", "U'", "D", "D2", "D'", "R", "R2", "R'", "L", "L2", "L'",
            "F", "F2", "F'", "B", "B2", "B'"}) {
        CHECK_FALSE((s * cubieOf(parseAlgOrEmpty(turn))).isSolved());
      }
    }
  }
}

TEST_CASE("orientation-only and permutation-only types keep the rest") {
  Rng rng(4);
  auto check = [&rng](const char *id, auto &&holds) {
    const ScrambleType *type = findScrambleType(id);
    REQUIRE(type);
    for (int n = 0; n < 30; ++n) {
      CubieCube s = randomStateOf(*type, rng);
      CAPTURE(id, n);
      CHECK(holds(s));
    }
  };
  check("zbll", [](const CubieCube &s) {
    return s.eo == std::array<std::uint8_t, 12>{};
  });
  check("eoline", [](const CubieCube &s) {
    return s.eo == std::array<std::uint8_t, 12>{} && s.ep[DF] == DF &&
           s.ep[DB] == DB;
  });
  check("corners", [](const CubieCube &s) {
    return s.ep == CubieCube{}.ep && s.eo == CubieCube{}.eo;
  });
  check("edges", [](const CubieCube &s) {
    return s.cp == CubieCube{}.cp && s.co == CubieCube{}.co;
  });
  check("epll", [](const CubieCube &s) {
    return s.cp == CubieCube{}.cp && s.co == CubieCube{}.co &&
           s.eo == CubieCube{}.eo;
  });
  check("cll", [](const CubieCube &s) {
    return s.ep == CubieCube{}.ep && s.eo == CubieCube{}.eo;
  });
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
