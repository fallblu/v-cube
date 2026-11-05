#include "train/alg_follow.hpp"
#include "train/case_logic.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace vcube;

namespace {

Alg alg(std::string_view text) {
  ParseResult r = parseAlg(text);
  REQUIRE(r);
  return r.alg;
}

// The case `algs[0]` solves, set up after `setup` (e.g. a U turn that the
// solver has to undo first).
AlgFollower follower(const std::vector<std::string> &algs,
                     std::string_view setup = "") {
  FaceletCube start = stateSolvedBy(alg(algs.front())).toFacelets();
  start.apply(alg(setup));
  std::vector<Alg> parsed;
  for (const auto &a : algs) {
    parsed.push_back(alg(a));
  }
  return AlgFollower(start, parsed);
}

void doMoves(AlgFollower &f, std::string_view moves) {
  for (Move m : alg(moves)) {
    f.move(m);
  }
}

const std::string kSune = "R U R' U R U2 R'";

} // namespace

TEST_CASE("an alg's opening runs to its first turn off the U-D axis") {
  auto rest = [](std::string_view a) {
    return toString(algOpening(alg(a)).rest);
  };
  CHECK(rest("U2 R U R'") == "R U R'");
  CHECK(rest("y U' R U R'") == "R U R'");
  CHECK(toString(algOpening(alg("y U' R U R'")).rotation) == "y");
  // d turns the U layer and the cube.
  CHECK(toString(algOpening(alg("d R U R'")).rotation) == "y'");
  // x tilts the cube for the rest of the alg.
  CHECK(rest("x R' U R'") == "R' U R'");
  CHECK(toString(algOpening(alg("x R' U R'")).rotation) == "x");
  // After z the U layer is on the right.
  CHECK(rest("z R U") == "U");
  // D turns are part of the opening, the U turns among them are the AUF.
  AlgOpening d = algOpening(alg("D U2 R U R' D'"));
  CHECK(d.d == 1);
  CHECK(d.dCost == 1);
  CHECK(toString(d.rest) == "R U R' D'");
  AlgOpening e = algOpening(alg("E' R"));
  CHECK(e.d == 1);
  CHECK(e.dCost == 2);
  CHECK(algOpening(alg("U2 y")).rest.empty());
}

TEST_CASE("U-D axis turns are told apart however the cube is held") {
  FaceletCube cube;
  auto ud = [&](MoveType t, int q) {
    return udTurn(cube, {t, static_cast<std::uint8_t>(q)});
  };
  REQUIRE(ud(MoveType::U, 3));
  CHECK(ud(MoveType::U, 3)->u == 3);
  CHECK(ud(MoveType::Y, 1));
  CHECK(ud(MoveType::X, 1));
  REQUIRE(ud(MoveType::Dw, 2));
  CHECK(ud(MoveType::Dw, 2)->u == 2);
  CHECK(ud(MoveType::Dw, 2)->d == 0);
  REQUIRE(ud(MoveType::D, 1));
  CHECK(ud(MoveType::D, 1)->d == 1);
  REQUIRE(ud(MoveType::E, 1));
  CHECK(ud(MoveType::E, 1)->u == 1);
  CHECK(ud(MoveType::E, 1)->d == 3);
  CHECK_FALSE(ud(MoveType::R, 1));
  CHECK_FALSE(ud(MoveType::M, 1));
  cube.apply(alg("x"));
  REQUIRE(ud(MoveType::B, 1)); // the U layer is at the back
  CHECK(ud(MoveType::B, 1)->u == 1);
  CHECK_FALSE(ud(MoveType::U, 1));
}

TEST_CASE("doing the alg as written follows it") {
  AlgFollower f = follower({kSune});
  CHECK(f.onAlg());
  doMoves(f, kSune);
  CHECK(f.onAlg());
  CHECK_FALSE(f.deviated());
  CHECK(f.moves().size() == 7);
}

TEST_CASE("U turns and rotations before the alg are free") {
  // The case is set up a U off, so it needs a U' first.
  AlgFollower f = follower({kSune}, "U");
  doMoves(f, "y U2 y' U U' U " + kSune);
  CHECK(f.onAlg());
  // The alg can also be done from a side where it solves the case.
  AlgFollower h = follower({"M2 U M2 U2 M2 U M2"});
  doMoves(h, "y M2 U M2 U2 M2 U M2");
  CHECK(h.onAlg());
}

TEST_CASE("the same turns done another way still follow the alg") {
  SECTION("half turns as two quarter turns either way") {
    AlgFollower f = follower({kSune});
    doMoves(f, "R U R' U R U' U' R'");
    CHECK(f.onAlg());
    AlgFollower g = follower({kSune});
    doMoves(g, "R U R' U R U U R'");
    CHECK(g.onAlg());
  }
  SECTION("but not halfway through a half turn") {
    AlgFollower f = follower({"R2 U R2"});
    doMoves(f, "R");
    CHECK_FALSE(f.deviated());
    CHECK_FALSE(f.onAlg());
    doMoves(f, "R");
    CHECK(f.onAlg());
  }
  SECTION("wide turns as a face and a slice turn, or a face and a rotation") {
    const std::string t = "r U R' U' r' F R F'";
    AlgFollower f = follower({t});
    doMoves(f, "R M' U R' U' M R' F R F'");
    CHECK(f.onAlg());
    AlgFollower g = follower({t});
    doMoves(g, "L x U R' U' r' F R F'");
    CHECK(g.onAlg());
  }
  SECTION("with or without the alg's rotations") {
    const std::string aa = "x R' U R' D2 R U' R' D2 R2 x'";
    AlgFollower f = follower({aa});
    doMoves(f, "R' F R' B2 R F' R' B2 R2");
    CHECK(f.onAlg());
    AlgFollower g = follower({aa});
    doMoves(g, "x R' U R' D2 R U' R' D2 R2");
    CHECK(g.onAlg());
    // Tilting first and then doing the alg's own x tilts twice.
    AlgFollower h = follower({aa});
    doMoves(h, "x x R' U");
    CHECK(h.deviated());
  }
  SECTION("opposite faces in either order") {
    AlgFollower f = follower({"R L' U2 R' L"});
    doMoves(f, "L' R U' U' L R'");
    CHECK(f.onAlg());
  }
  SECTION("the AUF before or after D turns at the start") {
    const std::string a = "D R U R' D' R U' R'";
    AlgFollower f = follower({a}, "U2");
    doMoves(f, "D U2 R U R' D' R U' R'");
    CHECK(f.onAlg());
    AlgFollower g = follower({a}, "U2");
    doMoves(g, "U2 D R U R' D' R U' R'");
    CHECK(g.onAlg());
    AlgFollower h = follower({"U2 " + a}, "U2");
    doMoves(h, "D U' U' R U R' D' R U' R'");
    CHECK(h.onAlg());
  }
  SECTION("a wide U-D turn as a face and a slice turn") {
    AlgFollower f = follower({"u R U R' u'"});
    doMoves(f, "U E' R U R' u'");
    CHECK(f.onAlg());
  }
  SECTION("without the U turn that ends an alg") {
    AlgFollower f = follower({"R U R' U D'"});
    doMoves(f, "R U R' D'");
    CHECK_FALSE(f.deviated());
    CHECK(f.onAlg());
    AlgFollower g = follower({"R U R' U D'"});
    doMoves(g, "R U R' D' U");
    CHECK(g.onAlg());
  }
  SECTION("any of the case's algs") {
    AlgFollower f = follower({kSune, "y' R' U' R U' R' U2 R"});
    doMoves(f, "y' R' U' R U' R' U2 R");
    CHECK(f.onAlg());
  }
}

TEST_CASE("a wrong turn leaves the algs") {
  SECTION("on another axis, at once") {
    AlgFollower f = follower({kSune});
    doMoves(f, "R F");
    CHECK(f.deviated());
  }
  SECTION("the wrong way, by the next turn") {
    AlgFollower f = follower({kSune});
    doMoves(f, "R U'");
    CHECK_FALSE(f.deviated());
    doMoves(f, "R'");
    CHECK(f.deviated());
  }
  SECTION("even when it is undone") {
    AlgFollower f = follower({kSune});
    doMoves(f, "R U R' U' U R U2 R'");
    CHECK(f.deviated());
    CHECK_FALSE(f.onAlg());
  }
  SECTION("or turned on round") {
    AlgFollower f = follower({kSune});
    doMoves(f, "R U' U' U' R'");
    CHECK(f.deviated());
  }
  SECTION("so does turning another layer to start") {
    AlgFollower f = follower({kSune});
    doMoves(f, "D");
    CHECK(f.deviated());
    // F could start the alg done from another side, F F can't.
    AlgFollower g = follower({kSune});
    doMoves(g, "F");
    CHECK_FALSE(g.deviated());
    doMoves(g, "F");
    CHECK(g.deviated());
  }
  SECTION("or the D layer the wrong way") {
    AlgFollower f = follower({"D R U R' D'"});
    doMoves(f, "D'");
    CHECK(f.deviated());
    AlgFollower g = follower({"D R U R' D'"});
    doMoves(g, "D D' D");
    CHECK(g.deviated());
  }
  SECTION("or going on after the alg") {
    AlgFollower f = follower({kSune});
    doMoves(f, kSune + " U");
    CHECK(f.deviated());
  }
}

TEST_CASE("starting over counts once the alg was begun") {
  AlgFollower f = follower({kSune}, "U");
  doMoves(f, "U' U");
  f.restart();
  CHECK_FALSE(f.deviated());
  doMoves(f, "U' R U");
  f.restart();
  CHECK(f.deviated());
  doMoves(f, "U' " + kSune);
  CHECK(f.deviated());
  CHECK_FALSE(f.onAlg());
}

TEST_CASE("starting over after D turns counts too") {
  AlgFollower f = follower({"D R U R' D'"});
  doMoves(f, "D");
  CHECK_FALSE(f.deviated());
  f.restart();
  CHECK(f.deviated());
}

TEST_CASE("changing the algs checks the moves again") {
  AlgFollower f = follower({kSune});
  doMoves(f, "R U2 R'");
  CHECK(f.deviated());
  f.setAlgs({alg("R U2 R' U' R U' R'")});
  CHECK_FALSE(f.deviated());
  CHECK(f.onAlg());
}
