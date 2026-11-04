#include "cube/cubie.hpp"
#include "cube/facelet.hpp"

#include <catch2/catch_test_macros.hpp>

#include <random>

using namespace vcube;

namespace {
Alg randomFaceAlg(std::mt19937 &rng, int n) {
  Alg a;
  for (int i = 0; i < n; ++i) {
    a.push_back({static_cast<MoveType>(rng() % 6),
                 static_cast<std::uint8_t>(1 + rng() % 3)});
  }
  return a;
}
} // namespace

TEST_CASE("facelet moves have order four and known effects") {
  for (int t = 0; t < kMoveTypeCount; ++t) {
    FaceletCube c;
    Move m{static_cast<MoveType>(t), 1};
    c.apply(m);
    CHECK(c != FaceletCube{});
    c.apply(m);
    c.apply(m);
    c.apply(m);
    CHECK(c == FaceletCube{});
  }
  FaceletCube c;
  c.apply(parseAlgOrEmpty("R"));
  // R takes the front's right column up to U.
  CHECK(c.toString().substr(0, 9) == "UUFUUFUUF");
  FaceletCube u;
  u.apply(parseAlgOrEmpty("U"));
  // U takes the R face's top row to F.
  CHECK(u.toString().substr(18, 3) == "RRR");
}

TEST_CASE("sexy move has order six; T-perm and superflip behave") {
  FaceletCube c;
  for (int i = 0; i < 6; ++i) {
    c.apply(parseAlgOrEmpty("R U R' U'"));
  }
  CHECK(c.isSolved());
  CubieCube t = cubieOf(parseAlgOrEmpty("R U R' U' R' F R2 U' R' U' R U R' F'"));
  CHECK(t.isValid());
  CHECK(t.cornerParity() == 1);
  CHECK(t.co == std::array<std::uint8_t, 8>{});
  CubieCube t2 = t * t;
  CHECK(t2.isSolved());
}

TEST_CASE("slices and rotations equal their outer-turn equivalents") {
  auto same = [](std::string_view a, std::string_view b) {
    return cubieOf(parseAlgOrEmpty(a)) == cubieOf(parseAlgOrEmpty(b));
  };
  CHECK(same("M", "R L'"));
  CHECK(same("E", "U D'"));
  CHECK(same("S", "F' B"));
  CHECK(same("r", "L"));
  CHECK(same("u", "D"));
  CHECK(same("f", "B"));
  CHECK(same("x y z", ""));
  CHECK(same("x U", "F x"));
  CHECK(same("x R", "R x"));
  FaceletCube f;
  f.apply(parseAlgOrEmpty("x y'"));
  CHECK(f.isSolved());
  CHECK_FALSE(f.isStandardOrientation());
  f.normalizeOrientation();
  CHECK(f == FaceletCube{});
}

TEST_CASE("cubie and facelet models agree on random face-turn algs") {
  std::mt19937 rng(1234);
  for (int n = 0; n < 200; ++n) {
    Alg a = randomFaceAlg(rng, 25);
    CubieCube c;
    c.apply(a);
    FaceletCube f;
    f.apply(a);
    REQUIRE(c.isValid());
    CHECK(c.toFacelets() == f);
    CHECK(CubieCube::fromFacelets(f) == c);
    CHECK((c * c.inverse()).isSolved());
    CubieCube inv;
    inv.apply(invert(a));
    CHECK(inv == c.inverse());
  }
}

TEST_CASE("there are 24 distinct rotations") {
  const auto &rots = allRotations();
  for (std::size_t i = 0; i < rots.size(); ++i) {
    for (std::size_t j = i + 1; j < rots.size(); ++j) {
      FaceletCube a, b;
      a.apply(rots[i]);
      b.apply(rots[j]);
      CHECK(a != b);
    }
  }
}
