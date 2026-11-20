#include "cube/scramble.hpp"
#include "io/move_counts.hpp"
#include "train/move_count.hpp"

#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

using namespace vcube;
using namespace vcube::testing;

namespace {
std::vector<MoveCountAttempt> attempts(std::initializer_list<int> counts) {
  std::vector<MoveCountAttempt> out;
  for (int c : counts) {
    MoveCountAttempt a;
    a.moves = c < 0 ? -c : c;
    a.dnf = c < 0;
    out.push_back(a);
  }
  return out;
}
} // namespace

TEST_CASE("move count statistics use WCA averages in thousandths of a move") {
  auto a = attempts({7, 9, 8, 6, 12});
  auto s = attemptsAsSolves(a);
  REQUIRE(s.size() == 5);
  CHECK(averageOf(s) == 8000); // drops 6 and 12
  CHECK(meanOf(std::span(s).first(3)) == 8000);
  CHECK(currentMean(s, 3) == 8670); // (8 + 6 + 12) / 3, in hundredths
  CHECK(formatMoves(currentMean(s, 3)) == "8.67");
  CHECK(formatMoves(currentMean(s, 3), 1) == "8.7");
  CHECK(formatMoves(currentMean(s, 3), 0) == "9");
  CHECK(formatMoves(8000) == "8.00");
  CHECK(formatMoves(8005) == "8.01");
  CHECK(formatMoves(std::nullopt) == "-");
  CHECK(formatMoves(kDnfTime) == "DNF");
  CHECK(formatMoves(summarize(s).best, 0) == "6");

  auto withDnf = attemptsAsSolves(attempts({7, -9, 8, 6, 12}));
  CHECK(averageOf(withDnf) == 9000); // DNF is the dropped worst: 7, 8, 12
  CHECK(meanOf(std::span(withDnf).first(3)) == kDnfTime);
  CHECK(summarize(withDnf).dnfs == 1);
}

TEST_CASE("move count attempts are listed as counts or DNF") {
  MoveCountAttempt a;
  a.moves = 23;
  CHECK(formatAttempt(a) == "23");
  a.dnf = true;
  CHECK(formatAttempt(a) == "DNF(23)");
  a.moves = 0;
  CHECK(formatAttempt(a) == "DNF");
}

TEST_CASE("Move Count offers a few existing scramble types") {
  CHECK(moveCountScrambleTypes() ==
        std::vector<std::string>{"333", "eoline", "eocross", "f2l"});
  for (const auto &id : moveCountScrambleTypes()) {
    CHECK(findScrambleType(id) != nullptr);
  }
  CHECK(isMoveCountScrambleType("eocross"));
  CHECK_FALSE(isMoveCountScrambleType("pll"));
  CHECK_FALSE(isMoveCountScrambleType("set:oll"));
}

TEST_CASE("move count sessions round-trip through JSON") {
  MoveCountSession s;
  s.name = "EO Cross";
  s.scrambleType = "eocross";
  s.attempts.push_back({9, false, "R U F", "nice", 1700000000, "", true});
  s.attempts.push_back({0, true, "D2 L", "", 1700000100, "", true});
  MoveCountSession other;
  other.name = "Old";
  other.scrambleType = "pll"; // not offered: read back as 333
  std::size_t current = 0;
  auto json = moveCountsToJson({s, other}, 1);
  CHECK(json.value("format").toString() == "v-cube.movecount");
  auto back = moveCountsFromJson(json, &current);
  CHECK(current == 1);
  REQUIRE(back.size() == 2);
  CHECK(back[0].name == "EO Cross");
  CHECK(back[0].scrambleType == "eocross");
  REQUIRE(back[0].attempts.size() == 2);
  CHECK(back[0].attempts[0].moves == 9);
  CHECK_FALSE(back[0].attempts[0].dnf);
  CHECK(back[0].attempts[0].scramble == "R U F");
  CHECK(back[0].attempts[0].comment == "nice");
  CHECK(back[0].attempts[0].timestamp == 1700000000);
  CHECK(back[0].attempts[1].dnf);
  CHECK(back[1].scrambleType == "333");
}

TEST_CASE("the move count store starts with a virtual session per step") {
  QTemporaryDir dir;
  QString path = dir.filePath("movecounts.json");
  MoveCountStore store(path);
  REQUIRE(store.load());
  const std::size_t steps = solveSteps().size();
  REQUIRE(store.sessions().size() == steps);
  for (std::size_t i = 0; i < steps; ++i) {
    const MoveCountSession &s = store.sessions()[i];
    CHECK(s.step == solveSteps()[i]);
    CHECK(s.name == stepName(s.step));
    CHECK(s.cube == CubeKind::Virtual);
    CHECK(s.scrambleType == defaultScrambleType(s.step));
  }

  store.setCurrentIndex(2);
  store.current().attempts.push_back({14, false, "F2 U", "", 1, "", true});
  store.addSession("Mine").scrambleType = "333";
  CHECK(store.currentIndex() == steps);
  REQUIRE(store.save());

  MoveCountStore again(path);
  REQUIRE(again.load());
  REQUIRE(again.sessions().size() == steps + 1);
  CHECK(again.currentIndex() == steps);
  CHECK(again.sessions()[2].attempts.size() == 1);
  CHECK(again.sessions()[2].attempts[0].moves == 14);

  for (std::size_t i = 0; i < steps + 1; ++i) {
    again.removeSession(0);
  }
  REQUIRE(again.sessions().size() == 1); // never empty
  CHECK(again.sessions()[0].attempts.empty());
  CHECK(again.currentIndex() == 0);

  QFile bad(path);
  REQUIRE(bad.open(QIODevice::WriteOnly));
  bad.write("{not json");
  bad.close();
  QString error;
  CHECK_FALSE(MoveCountStore(path).load(&error));
  CHECK_FALSE(error.isEmpty());
}

TEST_CASE("move count steps are named and found by name") {
  CHECK(solveSteps().size() == 9);
  for (SolveStep s : solveSteps()) {
    CHECK(stepFromId(stepId(s)) == s);
    CHECK(stepFromName(stepName(s)) == s);
  }
  CHECK(stepFromName("eo cross") == SolveStep::EOCross);
  CHECK(stepFromName("ZZ-F2L") == SolveStep::ZZF2L);
  CHECK(stepFromName("EO Line") == SolveStep::EOLine);
  CHECK(stepFromName("CFOP F2L") == SolveStep::CFOPF2L);
  CHECK(stepFromName("EO Block") == std::nullopt);
  // The ids of the first steps are as they were saved.
  CHECK(stepId(SolveStep::ZZF2L) == "zzf2l");
  CHECK(static_cast<int>(SolveStep::ZZF2L) == 4);
  CHECK(defaultScrambleType(SolveStep::ZZF2L) == "eocross");
  CHECK(defaultScrambleType(SolveStep::CFOPF2L) == "f2l");
  CHECK(defaultScrambleType(SolveStep::EO) == "333");
  CHECK(stepNeedsEo(SolveStep::XEOCross));
  CHECK_FALSE(stepNeedsEo(SolveStep::Cross));
}

TEST_CASE("EOLine, the cross, XEOCross and CFOP F2L are checked") {
  CHECK(stepReached(after(""), SolveStep::EOLine));
  CHECK(stepReached(after("R"), SolveStep::EOLine));   // DF and DB stay
  CHECK(stepReached(after("R"), SolveStep::XEOCross)); // orange bottom
  CHECK(stepReached(after("R U R'"), SolveStep::XEOCross));
  CHECK_FALSE(stepReached(after("R U R'"), SolveStep::ZZF2L));
  CHECK(stepReached(after("R U R' U R U2 R'"), SolveStep::CFOPF2L));
  // Two flipped last layer edges: the cross and the F2L are done, but no
  // EO on either axis around them.
  CubieCube c;
  c.eo[UR] = 1;
  c.eo[UF] = 1;
  FaceletCube flipped = c.toFacelets();
  CHECK(stepReached(flipped, SolveStep::Cross));
  CHECK(stepReached(flipped, SolveStep::CFOPF2L));
  CHECK_FALSE(stepReached(flipped, SolveStep::EOCross));
  CHECK_FALSE(stepReached(flipped, SolveStep::ZZF2L));
  CHECK_FALSE(stepReached(flipped, SolveStep::EOLine));
}

TEST_CASE("simplified move counts take out cancelling turns") {
  CHECK(solutionHtm(parseAlgOrEmpty("U D U'")) == 1);
  CHECK(solutionHtm(parseAlgOrEmpty("U D U'"), false) == 3);
  CHECK(solutionHtm(parseAlgOrEmpty("R x R'")) == 0);
  CHECK(solutionHtm(parseAlgOrEmpty("R x R'"), false) == 2);
  CHECK(solutionHtm(parseAlgOrEmpty("R y R'")) == 2);
  CHECK(solutionHtm(parseAlgOrEmpty("M2 R")) == 3);
  VirtualAttempt a;
  a.load(parseAlgOrEmpty("U D U'"));
  a.start();
  for (Move m : parseAlgOrEmpty("U D' U'")) {
    CHECK(a.turn(m));
  }
  CHECK(a.cube().isSolved());
  CHECK(toString(a.solution()) == "U D' U'"); // kept as done
  CHECK(a.count() == 1);
  CHECK(a.count(false) == 3);
}

TEST_CASE("a virtual attempt keeps when each move was turned") {
  VirtualAttempt a;
  a.load(parseAlgOrEmpty("R U"));
  a.setClock(1000);
  CHECK(a.turn(parseAlgOrEmpty("y").front(), 1200)); // before the start: 0
  CHECK(a.turn(parseAlgOrEmpty("y'").front(), 1300));
  a.start();
  // A joined move has the time of its last turn.
  for (auto [m, t] : std::vector<std::pair<const char *, int>>{
           {"U", 1500}, {"U", 1600}, {"R", 1700}, {"R'", 1800}, {"R'", 1900}}) {
    CHECK(a.turn(parseAlgOrEmpty(m).front(), t));
  }
  CHECK(toString(a.solution()) == "U2 R'");
  CHECK(a.solutionTimes() == std::vector<std::int64_t>{600, 900});
}

TEST_CASE("EO is edges solvable without F or B quarter turns, on any axis") {
  CHECK(stepReached(after(""), SolveStep::EO));
  CHECK(stepReached(after("R U L' D2 F2 B2 R'"), SolveStep::EO));
  // F flips four edges for the F/B axis, but they are still oriented for
  // the R/L axis (held with a y), so EO counts there.
  CHECK(stepReached(after("F"), SolveStep::EO));
  // F, R and U turns flip edges for every axis.
  CHECK_FALSE(stepReached(after("F R U"), SolveStep::EO));
  // However the cube is turned.
  CHECK(stepReached(after("x y R U L'"), SolveStep::EO));
}

TEST_CASE("EO+1, EO+2 and EO Cross need that many cross edges") {
  // R L keeps EO and leaves two cross edges on every side: DF and DB on D.
  FaceletCube f = after("R L");
  CHECK(stepReached(f, SolveStep::EOPlus1));
  CHECK(stepReached(f, SolveStep::EOPlus2));
  CHECK_FALSE(stepReached(f, SolveStep::EOCross));
  // R alone leaves the cross on L (held with L at the bottom).
  CHECK(stepReached(after("R"), SolveStep::EOCross));
  // A solved cube held another way has its cross on any colour.
  CHECK(stepReached(after("z2"), SolveStep::EOCross));
  CHECK(stepReached(after("y x"), SolveStep::ZZF2L));
  // The F2L solved apart from the last layer, edges oriented: ZZ F2L.
  CHECK(stepReached(after("R U R' U R U2 R'"), SolveStep::ZZF2L));
  // A U-layer edge flipped (an F turn sends edges up flipped): not.
  CHECK_FALSE(stepReached(after("F R U R' U' F'"), SolveStep::ZZF2L));
}

TEST_CASE("EOLine is EO with the DF and DB edges solved") {
  CHECK(stepReached(after("R L"), SolveStep::EOLine));
  // R2 moves DR but not DF or DB.
  CHECK(stepReached(after("R2"), SolveStep::EOLine));
  // F2 B2 moves DF and DB, but UL and DL make a line held with L down.
  CHECK(stepReached(after("F2 B2"), SolveStep::EOLine));
  CHECK_FALSE(stepReached(after("F R U"), SolveStep::EOLine));
}

TEST_CASE("step progress is the furthest hold's") {
  CHECK(stepProgress(after("")) == StepProgress{0, 4, true});
  // R L: EO and the line on D, DR and DL off.
  CHECK(stepProgress(after("R L")) == StepProgress{0, 2, true});
  // F R U flips edges for every axis; some hold is still best.
  StepProgress p = stepProgress(after("F R U"));
  CHECK(p.badEdges > 0);
  CHECK(p.badEdges < 12);
}

TEST_CASE("HTM counts slices twice and rotations not at all") {
  CHECK(htmCount(parseAlgOrEmpty("R U2 r M' x y E S")) == 3 + 6);
  CHECK(htmCount({}) == 0);
}

TEST_CASE("a virtual attempt turns only the whole cube until it starts") {
  VirtualAttempt a;
  a.load(parseAlgOrEmpty("R U F"));
  CHECK_FALSE(a.turn(parseAlgOrEmpty("R").front()));
  CHECK(a.turn(parseAlgOrEmpty("y").front()));
  CHECK(a.moves().size() == 1);
  a.start();
  for (Move m : parseAlgOrEmpty("y' F' U U U R' R R' L L'")) {
    CHECK(a.turn(m));
  }
  CHECK(a.cube().isSolved());
  CHECK(toString(a.solution()) == "F' U' R'");
  CHECK(a.count() == 3);
  a.restart();
  CHECK_FALSE(a.started());
  CHECK(a.moves().empty());
  CHECK_FALSE(a.cube().isSolved());
  a.setFree(true);
  CHECK(a.turn(parseAlgOrEmpty("R").front()));
}

TEST_CASE("a virtual attempt can start from a cube set up directly") {
  FaceletCube start;
  start.apply(parseAlgOrEmpty("R U R' U'"));
  VirtualAttempt a;
  a.load(start);
  CHECK_FALSE(a.turn(parseAlgOrEmpty("U").front()));
  CHECK(a.turn(parseAlgOrEmpty("y").front()));
  a.restart();
  CHECK(a.cube() == start);
  a.start();
  for (Move m : parseAlgOrEmpty("U R U' R'")) {
    CHECK(a.turn(m));
  }
  CHECK(a.cube().isSolved());
}

TEST_CASE("move count sessions keep their cube, step and solutions") {
  MoveCountSession s;
  s.name = "Mine";
  s.cube = CubeKind::Virtual;
  s.step = SolveStep::EOPlus2;
  MoveCountAttempt a;
  a.moves = 7;
  a.scramble = "R U";
  a.solution = "y F R";
  s.attempts.push_back(a);
  auto back = moveCountsFromJson(moveCountsToJson({s}, 0));
  REQUIRE(back.size() == 1);
  CHECK(back[0].cube == CubeKind::Virtual);
  CHECK(back[0].step == SolveStep::EOPlus2);
  REQUIRE(back[0].attempts.size() == 1);
  CHECK(back[0].attempts[0].solution == "y F R");
  CHECK(back[0].attempts[0].counted);
}

TEST_CASE("move count files from before cubes and steps read as physical") {
  QJsonObject old = QJsonDocument::fromJson(R"({
    "format": "v-cube.movecount", "version": 1, "current": 0,
    "sessions": [
      {"name": "EO Cross", "scrambleType": "333", "attempts": [
        {"moves": 9, "scramble": "R", "timestamp": 1}]},
      {"name": "Practice", "scrambleType": "eocross", "attempts": []},
      {"name": "Mine", "scrambleType": "333", "attempts": []}]})")
                        .object();
  auto s = moveCountsFromJson(old);
  REQUIRE(s.size() == 3);
  CHECK(s[0].cube == CubeKind::Physical);
  CHECK(s[0].step == SolveStep::EOCross); // from the name
  CHECK(s[0].attempts[0].counted);
  CHECK(s[1].step == SolveStep::ZZF2L); // from the scramble type
  CHECK(s[2].step == SolveStep::EOCross);
}

TEST_CASE("untimed sessions take any scramble type and uncounted solves") {
  QTemporaryDir dir;
  QString path = dir.filePath("untimed.json");
  MoveCountStore store(path, SolveLog::Untimed);
  REQUIRE(store.load());
  REQUIRE(store.sessions().size() == 1);
  CHECK(store.current().cube == CubeKind::Virtual);
  store.current().scrambleType = "set:pll";
  MoveCountAttempt physical;
  physical.counted = false;
  physical.scramble = "R U";
  store.current().attempts.push_back(physical);
  REQUIRE(store.save());
  QFile f(path);
  REQUIRE(f.open(QIODevice::ReadOnly));
  QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
  CHECK(o.value("format").toString() == "v-cube.untimed");
  MoveCountStore again(path, SolveLog::Untimed);
  REQUIRE(again.load());
  CHECK(again.current().scrambleType == "set:pll");
  REQUIRE(again.current().attempts.size() == 1);
  CHECK_FALSE(again.current().attempts[0].counted);
  CHECK(formatAttempt(again.current().attempts[0]) == "-");
  CHECK(attemptsAsSolves(again.current().attempts).empty());
}

TEST_CASE("move counts and scramble types from a file are checked") {
  QJsonObject o = QJsonDocument::fromJson(R"({
    "format": "v-cube.movecount", "version": 1, "current": 0,
    "sessions": [
      {"name": "Mine", "scrambleType": "set:pll", "attempts": [
        {"moves": -4, "scramble": "R"},
        {"moves": 100000, "scramble": "R"},
        {"moves": "x", "scramble": "R"},
        {"scramble": "R"}]}]})")
                      .object();
  REQUIRE_FALSE(o.isEmpty());
  auto s = moveCountsFromJson(o);
  REQUIRE(s.size() == 1);
  // Move Count doesn't offer the type: it reads as a plain scramble.
  CHECK(s[0].scrambleType == "333");
  REQUIRE(s[0].attempts.size() == 4);
  CHECK(s[0].attempts[0].moves == 0);
  CHECK(s[0].attempts[1].moves == kMaxMoveCount);
  CHECK(kMaxMoveCount == 999);
  CHECK(s[0].attempts[2].moves == 0);
  for (std::size_t i = 0; i < 3; ++i) {
    CHECK(s[0].attempts[i].counted);
  }
  CHECK_FALSE(s[0].attempts[3].counted);

  // Untimed takes any scramble type, and checks the counts the same way.
  auto untimed = moveCountsFromJson(o, nullptr, SolveLog::Untimed);
  REQUIRE(untimed.size() == 1);
  CHECK(untimed[0].scrambleType == "set:pll");
  REQUIRE(untimed[0].attempts.size() == 4);
  CHECK(untimed[0].attempts[0].moves == 0);
  CHECK(untimed[0].attempts[1].moves == kMaxMoveCount);
  CHECK(untimed[0].attempts[2].moves == 0);
}
