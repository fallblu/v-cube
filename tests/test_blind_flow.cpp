#include "train/blind_flow.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <vector>

using namespace vcube;

namespace {

using QK = QuizFlow::Key;
using SK = SetupFlow::Key;
using TK = TraceFlow::Key;
using BK = BlindSolveFlow::Key;

const LetterScheme kSpeffz = LetterScheme::speffz();

// A press of a key (not repeated).
QuizFlow::Effects press(QuizFlow &f, QK k, std::int64_t now,
                        std::string_view text = {},
                        const LetterScheme &letters = kSpeffz) {
  return f.key(k, text, true, false, now, letters);
}

Move move(MoveType t, int amount = 1) {
  Move m;
  m.type = t;
  m.amount = static_cast<std::uint8_t>(amount);
  return m;
}

} // namespace

// --- letters ----------------------------------------------------------------

TEST_CASE("letterPool lists the stickers a drill asks about", "[blind]") {
  CHECK(letterPool(true, true, false).size() == 48);
  auto edges = letterPool(true, false, false);
  REQUIRE(edges.size() == 24);
  CHECK(edges.front() == LetterChoice{BldPiece::Edge, 0});
  auto corners = letterPool(false, true, false);
  REQUIRE(corners.size() == 24);
  CHECK(corners.front().kind == BldPiece::Corner);
  // Setups: no buffer piece stickers and no swap spot.
  auto setups = letterPool(true, true, true);
  CHECK(setups.size() == (24 - 2 - 1) + (24 - 3 - 1));
  for (const LetterChoice &c : setups) {
    CHECK(op::isTarget(c.kind, c.sticker));
    CHECK(c.sticker != op::swapSpot(c.kind));
  }
}

TEST_CASE("pickLetter never repeats the last letter and favours the weak",
          "[blind]") {
  Rng rng(7);
  std::vector<LetterChoice> pool = {{BldPiece::Edge, 0}, {BldPiece::Edge, 1}};
  std::vector<LetterRecord> records(2);
  for (int i = 0; i < 50; ++i) {
    CHECK(pickLetter(pool, records, pool[0], rng) == 1);
  }
  // One letter alone may come twice.
  std::vector<LetterChoice> one = {pool[0]};
  std::vector<LetterRecord> oneRecord(1);
  CHECK(pickLetter(one, oneRecord, pool[0], rng) == 0);
  // A letter always right and quick is asked less than one always missed.
  records[0] = {10, 1.0, 1000.0};
  records[1] = {10, 0.0, std::nullopt};
  int weak = 0;
  for (int i = 0; i < 400; ++i) {
    weak += pickLetter(pool, records, std::nullopt, rng) == 1;
  }
  CHECK(weak > 280); // weights 1 and 5
}

TEST_CASE("whereIs follows a sticker through moves", "[blind]") {
  FaceletCube ids = numberedFacelets();
  for (int f = 0; f < kFaceletCount; ++f) {
    CHECK(whereIs(ids, f) == f);
  }
  ids.apply(move(MoveType::U));
  std::set<int> places;
  for (int f = 0; f < kFaceletCount; ++f) {
    places.insert(whereIs(ids, f));
  }
  CHECK(places.size() == static_cast<std::size_t>(kFaceletCount));
  ids.apply(move(MoveType::U, 3));
  CHECK(whereIs(ids, 9) == 9);
}

TEST_CASE("AwayClock measures the time away once", "[blind]") {
  AwayClock c;
  CHECK_FALSE(c.back(100));
  c.leave(100);
  c.leave(150); // the first time is kept
  CHECK(c.away());
  CHECK(c.back(400) == 300);
  CHECK_FALSE(c.away());
  CHECK_FALSE(c.back(500));
}

// --- the letter quiz
// ------------------------------------------------------------

TEST_CASE("QuizFlow answers a typed letter at once when it is whole",
          "[blind]") {
  QuizFlow f;
  f.show({BldPiece::Edge, 2}, true, 1000, false); // C
  auto e = press(f, QK::Letter, 1800, "c");
  REQUIRE(e.answer);
  CHECK(e.answer->right);
  CHECK(e.answer->ms == 800);
  CHECK(e.autoNext);
  CHECK(f.item()->answered);
  CHECK(f.typed() == "c");
  // Space or Enter then goes on; other keys do nothing.
  CHECK_FALSE(press(f, QK::Letter, 1900, "x").next);
  CHECK(press(f, QK::Enter, 1900).next);
}

TEST_CASE("QuizFlow waits for Enter on a prefix of longer letters", "[blind]") {
  LetterScheme s = LetterScheme::speffz();
  s.edges[0] = "Ab"; // A..: "ab" and plain "a" can't both end at one key
  s.edges[1] = "Ac";
  QuizFlow f;
  f.show({BldPiece::Edge, 1}, true, 0, false);
  auto e = press(f, QK::Letter, 10, "a", s);
  CHECK_FALSE(e.answer);
  CHECK(e.showTyped);
  CHECK(f.typed() == "a");
  // Backspace corrects, Enter on nothing does nothing.
  e = press(f, QK::Backspace, 20, {}, s);
  CHECK(e.showTyped);
  CHECK(f.typed().empty());
  CHECK_FALSE(press(f, QK::Enter, 30, {}, s).answer);
  press(f, QK::Letter, 40, "A", s);
  e = press(f, QK::Letter, 50, "C", s); // whole and only "ac"
  REQUIRE(e.answer);
  CHECK(e.answer->right);

  // Enter answers a prefix: wrong here.
  f.show({BldPiece::Edge, 1}, true, 100, false);
  CHECK(f.typed().empty());
  press(f, QK::Letter, 110, "a", s);
  e = press(f, QK::Enter, 120, {}, s);
  REQUIRE(e.answer);
  CHECK_FALSE(e.answer->right);
  CHECK_FALSE(e.autoNext);
}

TEST_CASE("QuizFlow takes a letter no sticker starts with as wrong",
          "[blind]") {
  QuizFlow f;
  f.show({BldPiece::Edge, 0}, true, 0, false);
  auto e = press(f, QK::Letter, 10, "7");
  REQUIRE(e.answer);
  CHECK_FALSE(e.answer->right);
  CHECK_FALSE(e.autoNext);
}

TEST_CASE("QuizFlow gives up on Space, except just after moving on",
          "[blind]") {
  QuizFlow f;
  // No letter yet: any key shows one.
  CHECK(press(f, QK::Space, 0).next);
  f.show({BldPiece::Corner, 5}, true, 0, false);
  f.movedOn(1000); // shown by the quiz moving on by itself
  auto e = press(f, QK::Space, 1400);
  CHECK(e.used);
  CHECK_FALSE(e.answer); // meant for the letter before
  e = press(f, QK::Space, 1500);
  REQUIRE(e.answer);
  CHECK(e.givenUp);
  CHECK_FALSE(e.answer->right);
  // Releases and repeats are taken but do nothing.
  f.show({BldPiece::Corner, 6}, true, 2000, false);
  CHECK(f.key(QK::Space, {}, false, false, 2100, kSpeffz).used);
  CHECK_FALSE(f.key(QK::Space, {}, true, true, 2100, kSpeffz).answer);
  CHECK_FALSE(f.key(QK::Other, {}, true, false, 2100, kSpeffz).used);
}

TEST_CASE("QuizFlow answers a click on the letter's sticker", "[blind]") {
  QuizFlow f;
  f.show({BldPiece::Edge, 4}, false, 0, false);
  // Typing does nothing while a click is asked for.
  CHECK_FALSE(press(f, QK::Letter, 5, "e").answer);
  auto e = f.click(4, 300);
  REQUIRE(e.answer);
  CHECK(e.answer->right);
  CHECK(e.answer->ms == 300);
  CHECK(e.autoNext);
  CHECK_FALSE(f.click(4, 400).answer); // answered already
  f.show({BldPiece::Edge, 4}, false, 1000, false);
  e = f.click(std::nullopt, 1100);
  REQUIRE(e.answer);
  CHECK_FALSE(e.answer->right);
  f.show({BldPiece::Edge, 4}, true, 2000, false);
  CHECK_FALSE(f.click(4, 2100).used); // a typed letter isn't clicked
}

TEST_CASE("A letter's clock stops while away", "[blind]") {
  QuizFlow f;
  f.show({BldPiece::Edge, 2}, true, 1000, false);
  f.pause(1500);
  f.pause(1700);
  f.resume(4500); // away for 3000
  auto e = press(f, QK::Letter, 5000, "c");
  REQUIRE(e.answer);
  CHECK(e.answer->ms == 1000);

  // Shown while away: its clock starts on return.
  f.show({BldPiece::Edge, 3}, true, 6000, true);
  f.resume(9000);
  REQUIRE(f.item());
  CHECK(f.item()->shownAt == 9000);
  // An answered letter's clock doesn't stop.
  press(f, QK::Letter, 9100, "d");
  f.pause(9200);
  f.resume(9900);
  CHECK(f.item()->shownAt == 9000);

  f.reset();
  CHECK_FALSE(f.item());
}

// --- setup moves
// -------------------------------------------------------------------

TEST_CASE("SetupFlow checks a setup done on the cube", "[blind]") {
  SetupFlow f;
  CHECK(f.key(SK::Space, std::nullopt, true, false, 0).next);
  // Edge sticker X (DL): L2 brings it to UL.
  f.show({BldPiece::Edge, 23}, 0, false);
  auto e = f.key(SK::Move, move(MoveType::Y), true, false, 10);
  CHECK(e.onlyLayers);
  CHECK_FALSE(e.turned);
  e = f.key(SK::Move, move(MoveType::L), true, false, 20);
  REQUIRE(e.turned);
  CHECK_FALSE(e.answer);
  e = f.key(SK::Escape, std::nullopt, true, false, 30);
  CHECK(e.resetCube);
  CHECK(f.moves().empty());
  f.key(SK::Move, move(MoveType::L), true, false, 40);
  e = f.key(SK::Move, move(MoveType::L), true, false, 50);
  REQUIRE(e.answer);
  CHECK(e.answer->right);
  CHECK(e.answer->ms == 50);
  CHECK(f.target() == bldFacelet(BldPiece::Edge, op::kEdgeSwap));
  // Esc then asks the same letter again; Space goes on.
  e = f.key(SK::Escape, std::nullopt, true, false, 100);
  CHECK(e.again);
  CHECK(f.open());
  CHECK(f.item()->shownAt == 100);
  e = f.key(SK::Space, std::nullopt, true, false, 200);
  REQUIRE(e.answer);
  CHECK(e.skipped);
  CHECK(f.key(SK::Enter, std::nullopt, true, false, 300).next);
}

TEST_CASE("SetupFlow says when the swap spot is reached the wrong way",
          "[blind]") {
  SetupFlow f;
  // Edge sticker C (UF): U brings it to UL but moves the buffer.
  f.show({BldPiece::Edge, 2}, 0, false);
  auto e = f.key(SK::Move, move(MoveType::U), true, false, 10);
  REQUIRE(e.turned);
  CHECK_FALSE(e.answer);
  CHECK(e.notValid);
}

TEST_CASE("SetupFlow with typed setups", "[blind]") {
  SetupFlow f;
  f.setTyped(true);
  f.show({BldPiece::Edge, 23}, 0, false);
  CHECK(f.key(SK::Space, std::nullopt, true, false, 10).focusField);
  CHECK(f.key(SK::Enter, std::nullopt, true, false, 10).focusField);
  auto e = f.key(SK::Move, move(MoveType::L), true, false, 10);
  CHECK(e.used);
  CHECK_FALSE(e.turned);
  auto a = f.typedSetup(parseAlgOrEmpty("U2"), 500);
  REQUIRE(a);
  CHECK_FALSE(a->right);
  CHECK_FALSE(f.typedSetup(parseAlgOrEmpty("L2"), 600)); // answered
  f.show({BldPiece::Edge, 23}, 1000, false);
  a = f.typedSetup(parseAlgOrEmpty("L2"), 1200);
  REQUIRE(a);
  CHECK(a->right);
  CHECK(a->ms == 200);
  f.show({BldPiece::Edge, 23}, 2000, false);
  e = f.key(SK::Escape, std::nullopt, true, false, 2100);
  REQUIRE(e.answer);
  CHECK(e.skipped);
}

// --- memo tracing
// --------------------------------------------------------------------

TEST_CASE("TraceFlow waits for a scramble, traces and finishes", "[blind]") {
  TraceFlow f;
  CHECK_FALSE(f.key(TK::Other, false, true, false, false, 0).used);
  auto e = f.key(TK::Space, false, true, false, false, 0);
  CHECK(e.waiting);
  CHECK(f.waiting());
  CHECK(f.phase() == TraceFlow::Phase::Ready);
  CHECK_FALSE(f.finish(10));
  e = f.scrambleReady(1000);
  CHECK(e.start);
  CHECK(f.phase() == TraceFlow::Phase::Memo);
  CHECK_FALSE(f.scrambleReady(1100).start); // not waiting any more
  e = f.key(TK::Move, true, true, false, true, 1200);
  CHECK(e.rotate);
  e = f.key(TK::Move, false, true, false, true, 1200);
  CHECK(e.onlyRotations);
  CHECK(f.key(TK::Enter, false, true, false, true, 1300).focusFields);
  CHECK(f.key(TK::Tab, false, true, false, true, 1300).focusFields);
  CHECK(f.elapsed(1500) == 500);
  CHECK(f.finish(3000) == 2000);
  CHECK(f.phase() == TraceFlow::Phase::Done);
  // Done: rotations look, Space starts the next one.
  CHECK(f.key(TK::Move, true, true, false, true, 3100).look);
  e = f.key(TK::Space, false, true, false, true, 4000);
  CHECK(e.start);
  e = f.key(TK::Escape, false, true, false, true, 4100);
  CHECK(e.giveUp);
  CHECK(f.phase() == TraceFlow::Phase::Ready);
  CHECK_FALSE(f.key(TK::Move, true, true, false, true, 4200).look);
}

TEST_CASE("TraceFlow's clock stops while away", "[blind]") {
  TraceFlow f;
  f.key(TK::Space, false, true, false, true, 1000);
  f.pause(2000);
  f.resume(5000);
  CHECK(f.finish(6000) == 2000);
  // Not tracing: nothing stops.
  f.pause(7000);
  f.resume(8000);
  f.key(TK::Space, false, true, false, true, 9000);
  CHECK(f.elapsed(9500) == 500);
  f.reset();
  CHECK(f.phase() == TraceFlow::Phase::Ready);
}

// --- blind solves
// ----------------------------------------------------------------------

TEST_CASE("BlindSolveFlow: a virtual solve", "[blind]") {
  BlindSolveFlow f;
  BlindSolveFlow::Context c;
  CHECK(f.key(BK::Space, true, false, 0, c).waitForScramble);
  CHECK(f.phase() == BlindSolveFlow::Phase::Ready);
  c.scrambleReady = true;
  CHECK_FALSE(f.key(BK::Move, true, false, 0, c).turn); // Ready
  CHECK_FALSE(f.key(BK::Backspace, true, false, 0, c).used);
  CHECK_FALSE(f.key(BK::Other, true, false, 0, c).used);
  CHECK(f.key(BK::Space, true, false, 1000, c).startMemo);
  CHECK(f.isBusy());
  CHECK(f.key(BK::Move, true, false, 1100, c).turn);
  CHECK(f.key(BK::Space, true, false, 4000, c).startExecution);
  CHECK(f.memoMs() == 3000);
  CHECK(f.elapsed(5000) == 4000);
  CHECK(f.key(BK::Move, true, false, 5000, c).turn);
  auto e = f.key(BK::Space, true, false, 9000, c);
  REQUIRE(e.stop);
  CHECK(e.stop->timeMs == 8000);
  CHECK(e.stop->memoMs == 3000);
  CHECK_FALSE(f.isBusy());
  e = f.key(BK::Move, true, false, 9100, c);
  CHECK(e.look);
  CHECK_FALSE(e.turn);
  CHECK(f.key(BK::Space, true, false, 9200, c).next);
  CHECK(f.phase() == BlindSolveFlow::Phase::Ready);
}

TEST_CASE("BlindSolveFlow: Esc cancels memo or execution", "[blind]") {
  BlindSolveFlow f;
  BlindSolveFlow::Context c{true, 0, std::nullopt};
  CHECK_FALSE(f.key(BK::Escape, true, false, 0, c).cancel); // not busy
  f.key(BK::Space, true, false, 0, c);
  CHECK(f.key(BK::Escape, true, false, 10, c).cancel);
  CHECK(f.phase() == BlindSolveFlow::Phase::Ready);
  f.key(BK::Space, true, false, 20, c);
  f.key(BK::Space, true, false, 30, c);
  CHECK(f.key(BK::Escape, true, false, 40, c).cancel);
  // Leaving the page cancels too.
  f.key(BK::Space, true, false, 50, c);
  CHECK(f.cancel().cancel);
  CHECK_FALSE(f.isBusy());
}

TEST_CASE("BlindSolveFlow: a physical solve and its DNF toggle", "[blind]") {
  BlindSolveFlow f;
  f.setVirtual(false);
  BlindSolveFlow::Context c{true, 1, 500};
  CHECK_FALSE(f.key(BK::Move, true, false, 0, c).used);
  CHECK(f.key(BK::Space, true, false, 1000, c).startMemo);
  // Releases and repeats do nothing.
  CHECK_FALSE(f.key(BK::Space, false, false, 1100, c).startExecution);
  CHECK_FALSE(f.key(BK::Space, true, true, 1100, c).startExecution);
  CHECK(f.key(BK::Space, true, false, 2000, c).startExecution);
  // Any key stops it.
  CHECK(f.key(BK::Other, false, false, 2500, c).used);
  auto e = f.key(BK::Other, true, false, 3000, c);
  REQUIRE(e.stop);
  CHECK(e.stop->timeMs == 2000);
  CHECK(e.stop->memoMs == 1000);
  // Backspace: only the solve recorded, while it is the session's last.
  CHECK_FALSE(f.key(BK::Backspace, true, false, 3100, c).toggleDnf);
  f.recorded(1, 777);
  c.lastTimestamp = 777;
  CHECK(f.key(BK::Backspace, true, false, 3200, c).toggleDnf);
  CHECK(f.key(BK::Backspace, true, false, 3300, c).toggleDnf);
  BlindSolveFlow::Context other{true, 0, 777};
  CHECK_FALSE(f.key(BK::Backspace, true, false, 3400, other).toggleDnf);
  c.lastTimestamp = 778; // another solve came after it
  CHECK_FALSE(f.key(BK::Backspace, true, false, 3500, c).toggleDnf);
  c.lastTimestamp = 777;
  CHECK(f.key(BK::Space, true, false, 4000, c).next);
  CHECK_FALSE(f.key(BK::Backspace, true, false, 4100, c).toggleDnf);
  // Esc cancels the memo, and the execution (as any key would stop it).
  f.key(BK::Space, true, false, 5000, c);
  CHECK(f.key(BK::Escape, true, false, 5100, c).cancel);
  f.key(BK::Space, true, false, 6000, c);
  f.key(BK::Space, true, false, 6100, c);
  e = f.key(BK::Escape, true, false, 6200, c);
  CHECK(e.cancel);
  CHECK_FALSE(e.stop);
}
