#include "cube/facelet.hpp"
#include "cube/hold.hpp"
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

TEST_CASE("keeps the quarter turns a move is written with") {
  Alg a = parseAlgOrEmpty("R3 U3' F2' L R'");
  REQUIRE(a.size() == 5);
  CHECK(a[0] == Move{MoveType::R, 3, 3});
  CHECK(a[1] == Move{MoveType::U, 1, 3});
  CHECK(quarterTurns(a[0]) == 3);
  CHECK(quarterTurns(a[1]) == 3);
  CHECK(quarterTurns(a[2]) == 2);
  CHECK(quarterTurns(a[3]) == 1);
  CHECK(quarterTurns(a[4]) == 1);
  CHECK(a[4] == Move{MoveType::R, 3});
  CHECK(toString(a) == "R' U F2 L R'");
  // Undoing three quarter turns takes three the other way.
  CHECK(invert(parseAlgOrEmpty("U R3"))[0] == Move{MoveType::R, 1, 3});
  CHECK(mirrorM(parseAlgOrEmpty("R3"))[0] == Move{MoveType::L, 1, 3});
  CHECK(simplify(parseAlgOrEmpty("R3 R2"))[0] == Move{MoveType::R, 1});
}

TEST_CASE("parses groups, repetition, commutators and conjugates") {
  CHECK(roundTrip("(R U R' U')2") == "R U R' U' R U R' U'");
  CHECK(roundTrip("(R U)2'") == "U' R' U' R'");
  CHECK(roundTrip("[R, U]") == "R U R' U'");
  CHECK(roundTrip("[F: R U R' U']") == "F R U R' U' F'");
  CHECK(roundTrip("[R: [U, R']]") == "R U R' U' R R'");
  CHECK(roundTrip("(U) R U R' // comment\n U'") == "U R U R' U'");
}

TEST_CASE("reads the spaces and primes of copied text") {
  // No-break space, zero-width space and byte order mark.
  CHECK(roundTrip("\xEF\xBB\xBFR\xC2\xA0U\xE2\x80\x8BR'") == "R U R'");
  // Curly quotes, prime, acute accent.
  CHECK(roundTrip("R\xE2\x80\x98 U\xE2\x80\xB2 F\xC2\xB4") == "R' U' F'");
  // The prime may come before the 2.
  CHECK(roundTrip("R'2 U2'") == "R2 U2");
}

TEST_CASE("a prime and a count are measured as parseAlg reads them") {
  CHECK(primeLength("'2") == 1);
  CHECK(primeLength("` U") == 1);
  CHECK(primeLength("\xE2\x80\x99 U") == 3);
  CHECK(primeLength("\xC2\xB4") == 2);
  CHECK(primeLength("2'") == 0);
  CHECK(primeLength("") == 0);
  CHECK(amountLength("") == 0);
  CHECK(amountLength(" U") == 0);
  CHECK(amountLength("2 U") == 1);
  CHECK(amountLength("' U") == 1);
  CHECK(amountLength("2' U") == 2);
  CHECK(amountLength("'2 U") == 2);
  CHECK(amountLength("12` U") == 3);
  CHECK(amountLength("\xE2\x80\xB2"
                     "2 U") == 4);
  CHECK(amountLength("2\xE2\x80\xB2 U") == 4);
  // One prime only, as parseAlg reads it: the second isn't part of it.
  CHECK(amountLength("'2'") == 2);
  CHECK(amountLength("''") == 1);
}

TEST_CASE("cleaned alg text has plain spaces and primes and no ends") {
  // No-break space, zero-width space, byte order mark and ideographic
  // space: one plain space each.
  CHECK(cleanAlgText("R\xC2\xA0U") == "R U");
  CHECK(cleanAlgText("R\xE2\x80\x8BU") == "R U");
  CHECK(cleanAlgText("R\xEF\xBB\xBFU") == "R U");
  CHECK(cleanAlgText("R\xE3\x80\x80U") == "R U");
  // Curly quotes, prime and acute accent: a plain prime.
  CHECK(cleanAlgText("R\xE2\x80\x99 U\xE2\x80\x98 L\xE2\x80\xB2 D\xC2\xB4") ==
        "R' U' L' D'");
  // A backtick is read as a prime, and written as one.
  CHECK(cleanAlgText("R` U2` (R U)`") == "R' U2' (R U)'");
  // The ends are trimmed, Unicode spaces too.
  CHECK(cleanAlgText(" \t\r\nR U\n\r\t ") == "R U");
  CHECK(cleanAlgText("\xEF\xBB\xBFR U\xC2\xA0") == "R U");
  CHECK(cleanAlgText(" \t\xC2\xA0\r\n").empty());
  CHECK(cleanAlgText("").empty());
  // Spaces inside are kept as typed: labAlgKey(), not this, makes two
  // spellings of one alg the same.
  CHECK(cleanAlgText("R  U\xC2\xA0\xC2\xA0R'") == "R  U  R'");
  // The text still reads as the same moves.
  const char *raw = "\xEF\xBB\xBF(R\xE2\x80\x99 U\xE2\x80\x8B"
                    "F)2\xE3\x80\x80"
                    "D\xC2\xB4 ";
  REQUIRE(parseAlg(raw));
  CHECK(parseAlgOrEmpty(cleanAlgText(raw)) == parseAlgOrEmpty(raw));
  CHECK(cleanAlgText(raw) == "(R' U F)2 D'");
}

TEST_CASE("reads common ways of writing a repetition") {
  CHECK(roundTrip("(R U R' U')x2") == "R U R' U' R U R' U'");
  CHECK(roundTrip("(R U)*2 F") == "R U R U F");
  CHECK(roundTrip("[R, U] * 2") == "R U R' U' R U R' U'");
  CHECK(roundTrip("(R U)\xC3\x97"
                  "2") == "R U R U");
  CHECK(roundTrip("(R U)x2'") == "U' R' U' R'");
  // A rotation after a space, and a lone '*', are what they were.
  CHECK(roundTrip("(R U) x2") == "R U x2");
  CHECK(roundTrip("(R U)* F") == "R U F");
  CHECK(roundTrip("(R U) * F") == "R U F");
}

TEST_CASE("reports parse errors") {
  CHECK_FALSE(parseAlg("R U Q"));
  CHECK_FALSE(parseAlg("(R U"));
  CHECK_FALSE(parseAlg("[R U]"));
  CHECK(parseAlg("R U Q").error->position == 4);
  // Positions count characters, and the message quotes the character.
  ParseResult r = parseAlg("R\xE2\x80\x99 U\xE2\x80\x99 \xC3\xA9");
  REQUIRE_FALSE(r);
  CHECK(r.error->position == 6);
  CHECK(r.error->message == "unexpected character '\xC3\xA9'");
}

TEST_CASE("refuses algs too big to be typos") {
  CHECK_FALSE(parseAlg("(((R U)1000)1000)1000"));
  CHECK_FALSE(parseAlg("R1001"));
  CHECK(roundTrip("R1000 U999") == "U'");
  std::string deep(200000, '(');
  CHECK_FALSE(parseAlg(deep + "R"));
  std::string nested;
  for (int i = 0; i < 64; ++i) {
    nested = "(" + nested + "R)";
  }
  CHECK(parseAlg(nested));
  CHECK_FALSE(parseAlg("(" + nested + ")"));
  CHECK(parseAlg("(R U R' U')1000").alg.size() == 4000);
}

TEST_CASE("inverts, mirrors and simplifies") {
  CHECK(toString(invert(parseAlgOrEmpty("R U2 F'"))) == "F U2 R'");
  CHECK(toString(mirrorM(parseAlgOrEmpty("R U R' F x M r"))) ==
        "L' U' L F' x M l'");
  CHECK(toString(mirrorS(parseAlgOrEmpty("F R B' z S"))) == "B' R' F z S");
  CHECK(toString(simplify(parseAlgOrEmpty("R R U U' D U2 D'"))) == "R2 U2");
  CHECK(toString(simplify(parseAlgOrEmpty("R L R'"))) == "L");
  CHECK(htmCount(parseAlgOrEmpty("x R U y R'")) == 3);
}

TEST_CASE("the move count is HTM, with slice turns counted twice") {
  CHECK(htmCount(parseAlgOrEmpty("M2 U M2 U2 M2 U M2")) == 11);
  CHECK(htmCount(parseAlgOrEmpty("r U R' E S' x")) == 7);
}

TEST_CASE("turns are written without rotations") {
  // The same turns of the cube, the centres kept still.
  const char *const algs[] = {"y R U R'",     "x2 R U' r",  "z U M2 U'",
                              "R E' F S2 B'", "y' x l' d2", "M' U M U2 R"};
  for (const char *text : algs) {
    INFO(text);
    Alg alg = parseAlgOrEmpty(text);
    if (alg.empty()) {
      continue;
    }
    Alg fixed = fixedFrame(alg);
    for (Move m : fixed) {
      CHECK(isFaceTurn(m.type));
    }
    FaceletCube a, b;
    a.apply(alg);
    b.apply(fixed);
    CHECK(heldCubie(a, kWcaHold) == heldCubie(b, kWcaHold));
  }
  CHECK(toString(fixedFrame(parseAlgOrEmpty("y R U R'"))) == "B U B'");
  CHECK(toString(fixedFrame(parseAlgOrEmpty("r U"))) == "L F");
}
