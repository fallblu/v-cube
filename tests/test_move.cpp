#include "cube/move.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace vcube;

namespace {
std::string roundTrip(std::string_view s) {
  return toString(parseAlgOrEmpty(s));
}
} // namespace

TEST_CASE("parses face, wide, slice and rotation moves") {
  CHECK(roundTrip("R U R' U'") == "R U R' U'");
  CHECK(roundTrip("Rw r2 Uw' x y2 z' M E2 S'") == "r r2 u' x y2 z' M E2 S'");
  CHECK(roundTrip("R2' U3 F4 B1") == "R2 U' B");
  CHECK(roundTrip("R\xE2\x80\x99 U`") == "R' U'");
}

TEST_CASE("parses groups, repetition, commutators and conjugates") {
  CHECK(roundTrip("(R U R' U')2") == "R U R' U' R U R' U'");
  CHECK(roundTrip("(R U)2'") == "U' R' U' R'");
  CHECK(roundTrip("[R, U]") == "R U R' U'");
  CHECK(roundTrip("[F: R U R' U']") == "F R U R' U' F'");
  CHECK(roundTrip("[R: [U, R']]") == "R U R' U' R R'");
  CHECK(roundTrip("(U) R U R' // comment\n U'") == "U R U R' U'");
}

TEST_CASE("reports parse errors") {
  CHECK_FALSE(parseAlg("R U Q"));
  CHECK_FALSE(parseAlg("(R U"));
  CHECK_FALSE(parseAlg("[R U]"));
  CHECK(parseAlg("R U Q").error->position == 4);
}

TEST_CASE("inverts, mirrors and simplifies") {
  CHECK(toString(invert(parseAlgOrEmpty("R U2 F'"))) == "F U2 R'");
  CHECK(toString(mirrorM(parseAlgOrEmpty("R U R' F x M r"))) ==
        "L' U' L F' x M l'");
  CHECK(toString(mirrorS(parseAlgOrEmpty("F R B' z S"))) == "B' R' F z S");
  CHECK(toString(simplify(parseAlgOrEmpty("R R U U' D U2 D'"))) == "R2 U2");
  CHECK(toString(simplify(parseAlgOrEmpty("R L R'"))) == "L");
  CHECK(moveCount(parseAlgOrEmpty("x R U y R'")) == 3);
}
