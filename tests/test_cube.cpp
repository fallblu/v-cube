#include "cube/cubie.hpp"
#include "cube/facelet.hpp"

#include <catch2/catch_test_macros.hpp>

#include <random>
#include <string>
#include <vector>

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
  CubieCube t =
      cubieOf(parseAlgOrEmpty("R U R' U' R' F R2 U' R' U' R U R' F'"));
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

TEST_CASE("stickers no real cube has are rejected") {
  auto from = [](std::string text) {
    auto f = FaceletCube::fromString(text);
    REQUIRE(f);
    return CubieCube::fromFacelets(*f);
  };
  const std::string solved = FaceletCube().toString();
  CHECK(from(solved) == CubieCube{});
  // The U and D stickers of a corner swapped, the side colours left alone.
  std::string s = solved;
  s[27 + 2] = 'U'; // D3, on the DFR corner
  CHECK_FALSE(from(s));
  // Two stickers of one colour on an edge.
  s = solved;
  s[1] = 'B'; // U2, on the UB edge
  CHECK_FALSE(from(s));
  // A twisted corner on its own.
  s = solved;
  std::string corner = {s[8], s[9], s[20]}; // URF: U9, R1, F3
  s[8] = corner[1];
  s[9] = corner[2];
  s[20] = corner[0];
  CHECK_FALSE(from(s));
  // Any real cube is read back exactly.
  CubieCube t;
  t.apply(parseAlgOrEmpty("R U R' U' R' F R2 U' R' U' R U R' F'"));
  CHECK(CubieCube::fromFacelets(t.toFacelets()) == t);
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

namespace {
// The flat view's colours as face letters, one string per row.
std::vector<std::string> flatColours(const FaceletCube &f) {
  std::vector<std::string> rows;
  for (const auto &row : flatLayout()) {
    std::string text;
    for (int i : row) {
      text += "URFDLB"[f[i]];
    }
    rows.push_back(text);
  }
  return rows;
}

std::vector<std::string> flatAfter(const char *alg) {
  FaceletCube f;
  f.apply(parseAlgOrEmpty(alg));
  return flatColours(f);
}
} // namespace

TEST_CASE("the flat view shows U and F with the adjacent L and R stickers") {
  using Rows = std::vector<std::string>;
  // Sticker positions, as in Alg-Trainer's numbering.
  const auto &layout = flatLayout();
  CHECK(layout[0] == std::array<int, 5>{36, 0, 1, 2, 11});
  CHECK(layout[2] == std::array<int, 5>{38, 6, 7, 8, 9});
  CHECK(layout[3] == std::array<int, 5>{38, 18, 19, 20, 9});
  CHECK(layout[5] == std::array<int, 5>{44, 24, 25, 26, 15});

  CHECK(flatAfter("") ==
        Rows{"LUUUR", "LUUUR", "LUUUR", "LFFFR", "LFFFR", "LFFFR"});
  CHECK(flatAfter("U") ==
        Rows{"FUUUB", "FUUUB", "FUUUB", "FRRRB", "LFFFR", "LFFFR"});
  CHECK(flatAfter("R") ==
        Rows{"LUUFR", "LUUFR", "LUUFR", "LFFDR", "LFFDR", "LFFDR"});
  CHECK(flatAfter("F") ==
        Rows{"LUUUR", "LUUUR", "DLLLU", "DFFFU", "DFFFU", "DFFFU"});
  CHECK(flatAfter("L") ==
        Rows{"LBUUR", "LBUUR", "LBUUR", "LUFFR", "LUFFR", "LUFFR"});
  CHECK(flatAfter("y") ==
        Rows{"FUUUB", "FUUUB", "FUUUB", "FRRRB", "FRRRB", "FRRRB"});
  // Two turns: R brings F stickers to U's right column, which U then turns
  // to the front row; the side rows come from the old F, R and B rows.
  CHECK(flatAfter("R U") ==
        Rows{"FUUUB", "FUUUB", "DFFFU", "DRRRU", "LFFDR", "LFFDR"});
}
