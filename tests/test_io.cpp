#include "cube/solver.hpp"
#include "io/library.hpp"
#include "io/sessions.hpp"
#include "train/case_logic.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <string>
#include <vector>

using namespace vcube;

namespace {
const QString kBuiltins = QStringLiteral(VCUBE_SOURCE_DIR "/resources/algsets");
}

TEST_CASE("built-in sets load with the expected cases and no algs") {
  AlgLibrary lib(QTemporaryDir().path() + "/never-used", kBuiltins);
  std::map<std::string, int> expected = {
      {"pll", 21}, {"oll", 57}, {"2look-oll", 10}, {"2look-pll", 6}};
  for (const QString &file : QDir(kBuiltins).entryList({"*.json"})) {
    QString error;
    auto set = readAlgSetFile(kBuiltins + "/" + file, &error);
    INFO(error.toStdString());
    REQUIRE(set);
    CHECK(set->builtin);
    CHECK(set->caseCount() == expected.at(set->id));
    for (const auto &g : set->groups) {
      for (const auto &c : g.cases) {
        CHECK(c.algs.empty());
        CHECK(c.state.isValid());
        CHECK_FALSE(stepSolved(c.state, set->specFor(g).free));
      }
    }
  }
}

TEST_CASE("built-in cases are distinct and accept standard algs") {
  auto pll = readAlgSetFile(kBuiltins + "/pll.json", nullptr);
  auto oll = readAlgSetFile(kBuiltins + "/oll.json", nullptr);
  REQUIRE(pll);
  REQUIRE(oll);
  // Every case is told apart: a case's own state never fits another case.
  for (const AlgSet *set : {&*pll, &*oll}) {
    std::vector<const AlgCase *> all;
    for (const auto &g : set->groups) {
      for (const auto &c : g.cases) {
        all.push_back(&c);
      }
    }
    for (const auto *a : all) {
      for (const auto *b : all) {
        if (a == b) {
          continue;
        }
        bool same = false;
        for (int x = 0; x < 4 && !same; ++x) {
          for (int y = 0; y < 4 && !same; ++y) {
            CubieCube s = uTurn(x) * a->state * uTurn(y);
            same = withinMask(s * b->state.inverse(), set->spec.free) ||
                   (s.co == b->state.co && s.eo == b->state.eo &&
                    !set->spec.free.co[0] && set->spec.free.cp[0]);
          }
        }
        CHECK_FALSE(same);
      }
    }
  }
  AlgSet p = *pll;
  auto report = importAlgs(p,
                           "T: R U R' U' R' F R2 U' R' U' R U R' F'\n"
                           "M2 U M U2 M' U M2\n"
                           "x R2 D2 R U R' D2 R U' R x'\n",
                           false);
  CHECK(report.added == 3);
  CHECK(p.findCase("T")->algs.size() == 1);
  CHECK(p.findCase("Ua")->algs.size() == 1);
  CHECK(p.findCase("Ab")->algs.size() == 1);
  AlgSet o = *oll;
  report = importAlgs(o, "R U R' U R U2 R'\nF R U R' U' F'\n", false);
  CHECK(report.added == 2);
  CHECK(o.findCase("oll27")->algs.size() == 1);
  CHECK(o.findCase("oll45")->algs.size() == 1);
}

namespace {

// `alg` (outer face turns) as written after the cube is turned by
// `rotation`, so that "rotation + result" turns the same layers as `alg`.
Alg afterRotation(const Alg &alg, MoveType rotation, int quarters) {
  // Where each face (U R F D L B) ends up after one quarter of the rotation.
  static const MoveType x[] = {MoveType::B, MoveType::R, MoveType::U,
                               MoveType::F, MoveType::L, MoveType::D};
  static const MoveType y[] = {MoveType::U, MoveType::F, MoveType::L,
                               MoveType::D, MoveType::B, MoveType::R};
  static const MoveType z[] = {MoveType::R, MoveType::D, MoveType::F,
                               MoveType::L, MoveType::U, MoveType::B};
  const MoveType *to = rotation == MoveType::X   ? x
                       : rotation == MoveType::Y ? y
                                                 : z;
  Alg out = alg;
  for (int q = 0; q < quarters; ++q) {
    for (Move &m : out) {
      m.type = to[static_cast<int>(m.type)];
    }
  }
  return out;
}

// `alg` written to start with the cube rotations of `prefix`.
std::string withPrefix(const std::string &prefix, const Alg &alg) {
  Alg body = alg;
  for (const Move &r : parseAlgOrEmpty(prefix)) {
    if (isRotation(r.type)) {
      body = afterRotation(body, r.type, r.amount);
    }
  }
  return prefix.empty() ? toString(body) : prefix + " " + toString(body);
}

} // namespace

TEST_CASE("built-in cases keep their last layer on top whatever the main "
          "alg starts with") {
  // Algs that start by tilting the cube (x, z) still solve a last-layer
  // case held the usual way up: only y turns and U turns reorient it.
  const char *prefixes[] = {"",     "y",        "y2",  "y'",     "x",
                            "x'",   "x2",       "z",   "z'",     "y x'",
                            "x y",  "z' y'",    "U x", "U2 y z", "x2 z2",
                            "x x'", "x2 z2 U'", "y U"};
  for (const QString &file : QDir(kBuiltins).entryList({"*.json"})) {
    auto set = readAlgSetFile(kBuiltins + "/" + file, nullptr);
    REQUIRE(set);
    for (const auto &g : set->groups) {
      const PieceMask &free = set->specFor(g).free;
      for (const auto &c : g.cases) {
        Alg base = TwoPhaseSolver::solve(c.state);
        AlgCase withAlgs = c;
        std::optional<CubieCube> firstDisplay;
        for (const char *prefix : prefixes) {
          std::string alg = withPrefix(prefix, base);
          INFO(set->id << " " << c.id << ": " << alg);
          REQUIRE(checkAlg(c.state, alg, free, set->aufBefore, set->aufAfter)
                      .passes());
          AlgCase one = c;
          one.algs = {alg};
          CaseAnalysis a = analyzeCase(*set, g, one);
          REQUIRE(a.main);
          // Only the U layer is unsolved, as in the stored case.
          Unsolved left = unsolvedPieces(a.display, PieceMask{});
          CHECK(left.cornersMisplaced[2] + left.cornersTwisted[2] == 0);
          CHECK(left.edgesMisplaced[1] + left.edgesFlipped[1] == 0);
          CHECK(left.edgesMisplaced[2] + left.edgesFlipped[2] == 0);
          // The alg as shown solves the case as shown with no U first.
          CHECK(fitAlg(a.display, parseAlgOrEmpty(a.mainAlg), free, false,
                       set->aufAfter));
          // Adding more algs leaves the case shown as its first alg has it.
          withAlgs.algs.push_back(alg);
          CaseAnalysis all = analyzeCase(*set, g, withAlgs);
          CHECK(all.main == 0u);
          if (!firstDisplay) {
            firstDisplay = a.display;
          }
          CHECK(all.display == *firstDisplay);
        }
      }
    }
  }
}

TEST_CASE("a tilted Aa perm alg shows the case the usual way up") {
  auto pll = readAlgSetFile(kBuiltins + "/pll.json", nullptr);
  REQUIRE(pll);
  AlgSet set = *pll;
  auto report = importAlgs(set, "x R' U R' D2 R U' R' D2 R2 x'\n", false);
  REQUIRE(report.added == 1);
  const CaseGroup *group = nullptr;
  const AlgCase *aa = nullptr;
  for (const auto &g : set.groups) {
    for (const auto &c : g.cases) {
      if (c.id == "Aa") {
        group = &g;
        aa = &c;
      }
    }
  }
  REQUIRE(aa);
  CaseAnalysis a = analyzeCase(set, *group, *aa);
  REQUIRE(a.main);
  CHECK(a.mainAlg == "x R' U R' D2 R U' R' D2 R2 x'");
  // Three U-layer corners cycle; nothing else moves.
  Unsolved left = unsolvedPieces(a.display, PieceMask{});
  CHECK(left.cornersMisplaced[0] == 3);
  CHECK(left.corners() == 3);
  CHECK(left.edges() == 0);
}

TEST_CASE("library installs built-ins once and saves changes") {
  QTemporaryDir tmp;
  {
    AlgLibrary lib(tmp.path(), kBuiltins);
    lib.load();
    CHECK(lib.errors().isEmpty());
    REQUIRE(lib.sets().size() == 4);
    CHECK(lib.sets()[0].id == "pll");
    lib.find("pll")->findCase("T")->algs.push_back(
        "R U R' U' R' F R2 U' R' U' R U R' F'");
    lib.save(*lib.find("pll"));
    lib.remove("2look-pll");
    lib.progress("pll")["T"].record({100, 1500, false});
    lib.saveProgress("pll");
  }
  AlgLibrary lib(tmp.path(), kBuiltins);
  lib.load();
  CHECK(lib.sets().size() == 3); // removed built-in stays removed
  CHECK(lib.find("pll")->findCase("T")->algs.size() == 1);
  CHECK(lib.progress("pll")["T"].attempts.size() == 1);
  REQUIRE(lib.restoreBuiltin("pll"));
  CHECK(lib.find("pll")->findCase("T")->algs.size() == 1);
}

TEST_CASE("progress keeps the attempt mode and reads old files as drill") {
  SetProgress p;
  p["T"].card.state = CardState::Review;
  p["T"].record({100, 1500, false, AttemptMode::Drill});
  p["T"].record({200, 0, true, AttemptMode::Learn});
  SetProgress back = progressFromJson(progressToJson(p));
  REQUIRE(back["T"].attempts.size() == 2);
  CHECK(back["T"].attempts[0].mode == AttemptMode::Drill);
  CHECK(back["T"].attempts[0].timeMs == 1500);
  CHECK(back["T"].attempts[1].mode == AttemptMode::Learn);
  CHECK(back["T"].attempts[1].failed);
  CHECK(back["T"].card.state == CardState::Review);

  // A file written before attempts had a mode.
  auto old = QJsonDocument::fromJson(R"({"version": 1, "cases": {"Ua": {
    "card": {"state": 2, "step": 0, "ease": 2.5, "interval": 4, "due": 5000,
             "reps": 3, "lapses": 0},
    "attempts": [[100, 2100, 0], [200, 1900, 1]]}}})");
  SetProgress loaded = progressFromJson(old.object());
  REQUIRE(loaded["Ua"].attempts.size() == 2);
  CHECK(loaded["Ua"].attempts[0].mode == AttemptMode::Drill);
  CHECK(loaded["Ua"].attempts[1].mode == AttemptMode::Drill);
  CHECK(loaded["Ua"].attempts[1].failed);
  CHECK(loaded["Ua"].card.state == CardState::Review);
  CHECK(loaded["Ua"].best() == 2100);
}

TEST_CASE("sessions round-trip and convert to and from cstimer") {
  Session s;
  s.name = "Main";
  s.solves.push_back({12340, Penalty::None, "R U", "nice", 1700000000, ""});
  s.solves.push_back({9000, Penalty::Plus2, "F", "", 1700000100, "R U R'"});
  s.solves.push_back({20000, Penalty::Dnf, "D", "", 1700000200, ""});
  std::size_t current = 0;
  auto back = sessionsFromJson(sessionsToJson({s}, 0), &current);
  REQUIRE(back.size() == 1);
  CHECK(back[0].solves.size() == 3);
  CHECK(back[0].solves[1].penalty == Penalty::Plus2);
  CHECK(back[0].solves[1].moves == "R U R'");

  QJsonObject cs = sessionsToCstimer({s});
  QString error;
  auto imported = sessionsFromCstimer(cs, &error);
  REQUIRE(imported);
  REQUIRE(imported->size() == 1);
  CHECK((*imported)[0].name == "Main");
  CHECK((*imported)[0].solves[0].timeMs == 12340);
  CHECK((*imported)[0].solves[2].penalty == Penalty::Dnf);

  // A cstimer export as it is written by cstimer itself.
  auto doc = QJsonDocument::fromJson(R"({
    "session1": [[[0, 10500], "R U F", "", 1700000000], [[2000, 9000], "U", "c", 1700000001]],
    "session2": [[[-1, 30000], "D2", "", 1700000002, ["R U", "333"]]],
    "properties": {"sessionData": "{\"1\":{\"name\":\"3x3\",\"opt\":{},\"rank\":2},\"2\":{\"name\":\"OH\",\"opt\":{\"scrType\":\"333oh\"},\"rank\":1}}"}
  })");
  imported = sessionsFromCstimer(doc.object(), &error);
  REQUIRE(imported);
  REQUIRE(imported->size() == 2);
  CHECK((*imported)[0].name == "OH");
  CHECK((*imported)[0].solves[0].penalty == Penalty::Dnf);
  CHECK((*imported)[1].solves[1].penalty == Penalty::Plus2);
  CHECK((*imported)[1].solves[1].comment == "c");
}
