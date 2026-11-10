#include "cube/solver.hpp"
#include "io/library.hpp"
#include "io/move_counts.hpp"
#include "io/sessions.hpp"
#include "train/case_logic.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
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

TEST_CASE("diagram views and masks by piece round-trip") {
  AlgSet set;
  set.id = "views";
  set.spec.mask.byPiece = true;
  set.spec.view = DiagramView::Iso3DFrontLeft;
  for (DiagramView v :
       {DiagramView::TopLayer, DiagramView::Iso3D, DiagramView::Iso3DFrontLeft,
        DiagramView::Iso3DBackLeft, DiagramView::Iso3DBackRight}) {
    StepSpec spec;
    spec.view = v;
    set.groups.push_back({"g", spec, {}});
  }
  QJsonObject o = algSetToJson(set);
  CHECK(o.value("mask").toObject().value("by").toString() == "piece");
  CHECK(o.value("view").toString() == "3d-front-left");
  auto back = algSetFromJson(o, nullptr);
  REQUIRE(back);
  CHECK(back->spec == set.spec);
  REQUIRE(back->groups.size() == set.groups.size());
  for (std::size_t i = 0; i < set.groups.size(); ++i) {
    CHECK(back->groups[i].spec == set.groups[i].spec);
  }
  // Masks by position, the default, aren't written out.
  StepSpec plain;
  set.spec = plain;
  CHECK_FALSE(algSetToJson(set).value("mask").toObject().contains("by"));
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

namespace {

void writeText(const QString &path, const QByteArray &text) {
  QFile f(path);
  REQUIRE(f.open(QIODevice::WriteOnly));
  f.write(text);
}

QByteArray readText(const QString &path) {
  QFile f(path);
  return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// Files in `dir` whose names start with `prefix`.
QStringList filesStarting(const QString &dir, const QString &prefix) {
  return QDir(dir).entryList({prefix + "*"}, QDir::Files);
}

} // namespace

TEST_CASE("unreadable session files are set aside, not saved over") {
  QTemporaryDir tmp;
  QString path = tmp.filePath("sessions.json");
  writeText(path, "{\"sessions\": [");
  SessionStore store(path);
  QString error;
  CHECK_FALSE(store.load(&error));
  CHECK_FALSE(error.isEmpty());
  REQUIRE_FALSE(store.setAsidePath().isEmpty());
  CHECK(readText(store.setAsidePath()) == "{\"sessions\": [");
  CHECK(store.sessions().size() == 1);
  CHECK(store.save());
  CHECK(filesStarting(tmp.path(), "sessions.json.unreadable-").size() == 1);

  QString counts = tmp.filePath("movecounts.json");
  writeText(counts, "not json");
  MoveCountStore moves(counts);
  CHECK_FALSE(moves.load(&error));
  CHECK(readText(moves.setAsidePath()) == "not json");
  CHECK(moves.sessions().size() == defaultMoveCountSessions().size());
}

TEST_CASE("unreadable progress is set aside and reported") {
  QTemporaryDir tmp;
  {
    AlgLibrary lib(tmp.path(), kBuiltins);
    lib.load();
  }
  QString progress = tmp.filePath("progress/pll.json");
  writeText(progress, "{\"cases\": {\"T\": ");
  AlgLibrary lib(tmp.path(), kBuiltins);
  lib.load();
  REQUIRE(lib.errors().size() == 1);
  CHECK(lib.errors().front().contains("pll.json"));
  CHECK(lib.progress("pll").empty());
  QStringList kept =
      filesStarting(tmp.filePath("progress"), "pll.json.unreadable-");
  REQUIRE(kept.size() == 1);
  lib.progress("pll")["T"].record({100, 1500, false});
  CHECK(lib.saveProgress("pll"));
  CHECK(readText(tmp.filePath("progress/" + kept.front())) ==
        "{\"cases\": {\"T\": ");
}

TEST_CASE("set ids that share a file name are kept apart") {
  QTemporaryDir tmp;
  AlgLibrary lib(tmp.path(), kBuiltins);
  lib.load();
  CHECK(lib.idTaken("pll"));
  CHECK(lib.idTaken("PLL"));
  CHECK(lib.idTaken("p.l.l") == false);
  AlgSet copy = *lib.find("pll");
  copy.id = "PLL";
  copy.name = "My PLL";
  std::string id = lib.add(copy).id;
  CHECK(id != "PLL");
  CHECK(lib.find("pll")->name == "PLL");
  AlgLibrary again(tmp.path(), kBuiltins);
  again.load();
  CHECK(again.find("pll"));
  CHECK(again.find(id));
}

TEST_CASE("restoring built-in cases keeps the user's cases and settings") {
  QTemporaryDir tmp;
  AlgLibrary lib(tmp.path(), kBuiltins);
  lib.load();
  AlgSet &pll = *lib.find("pll");
  pll.targetSeconds = 1.5;
  pll.aufAfter = false;
  pll.findCase("T")->algs.push_back("R U R' U' R' F R2 U' R' U' R U R' F'");
  pll.findCase("T")->name = "Renamed";
  std::erase_if(pll.groups.front().cases,
                [](const AlgCase &c) { return c.id == "Ua"; });
  AlgCase mine;
  mine.id = "mine";
  mine.name = "Mine";
  mine.algs = {"R U R'"};
  pll.groups.back().cases.push_back(mine);
  pll.groups.push_back({"My group", std::nullopt, {}});
  int cases = pll.caseCount();
  REQUIRE(lib.restoreBuiltin("pll"));
  const AlgSet &after = *lib.find("pll");
  CHECK(after.caseCount() == cases + 1); // Ua is back
  CHECK(after.findCase("Ua"));
  CHECK(after.findCase("mine"));
  CHECK(after.groups.back().name == "My group");
  CHECK(after.findCase("T")->name != "Renamed");
  CHECK(after.findCase("T")->algs.size() == 1);
  CHECK(after.targetSeconds == 1.5);
  CHECK_FALSE(after.aufAfter);
}

TEST_CASE("a new case doesn't inherit a deleted case's progress") {
  QTemporaryDir tmp;
  AlgLibrary lib(tmp.path(), kBuiltins);
  lib.load();
  lib.progress("pll")["T"].record({100, 1500, false});
  lib.progress("pll")["H"].record({100, 1500, false});
  lib.saveProgress("pll");
  lib.forgetProgress("pll", {"T", "unknown"});
  AlgLibrary again(tmp.path(), kBuiltins);
  again.load();
  CHECK_FALSE(again.progress("pll").contains("T"));
  CHECK(again.progress("pll").contains("H"));
}

TEST_CASE("progress keeps the attempt mode and reads old files as drill") {
  SetProgress p;
  p["T"].card.state = CardState::Review;
  p["T"].record({100, 1500, false, AttemptMode::PhysicalDrill});
  p["T"].record({200, 0, true, AttemptMode::Learn});
  p["T"].record({300, 2500, true, AttemptMode::VirtualDrill});
  SetProgress back = progressFromJson(progressToJson(p));
  REQUIRE(back["T"].attempts.size() == 3);
  CHECK(back["T"].attempts[0].mode == AttemptMode::PhysicalDrill);
  CHECK(back["T"].attempts[0].timeMs == 1500);
  CHECK(back["T"].attempts[1].mode == AttemptMode::Learn);
  CHECK(back["T"].attempts[1].failed);
  CHECK(back["T"].attempts[2].mode == AttemptMode::VirtualDrill);
  CHECK(back["T"].attempts[2].timeMs == 2500);
  CHECK(back["T"].attempts[2].failed);
  CHECK(back["T"].card.state == CardState::Review);

  // A file written before attempts had a mode.
  auto old = QJsonDocument::fromJson(R"({"version": 1, "cases": {"Ua": {
    "card": {"state": 2, "step": 0, "ease": 2.5, "interval": 4, "due": 5000,
             "reps": 3, "lapses": 0},
    "attempts": [[100, 2100, 0], [200, 1900, 1]]}}})");
  SetProgress loaded = progressFromJson(old.object());
  REQUIRE(loaded["Ua"].attempts.size() == 2);
  CHECK(loaded["Ua"].attempts[0].mode == AttemptMode::PhysicalDrill);
  CHECK(loaded["Ua"].attempts[1].mode == AttemptMode::PhysicalDrill);
  CHECK(loaded["Ua"].attempts[1].failed);
  CHECK(loaded["Ua"].card.state == CardState::Review);
  CHECK(loaded["Ua"].best(AttemptMode::PhysicalDrill) == 2100);
}

TEST_CASE("sessions round-trip and convert to and from cstimer") {
  Session s;
  s.name = "Main";
  s.solves.push_back({12340, Penalty::None, "R U", "nice", 1700000000, "", {}});
  s.solves.push_back({9000, Penalty::Plus2, "F", "", 1700000100, "R U R'", {}});
  s.solves.push_back({20000, Penalty::Dnf, "D", "", 1700000200, "", {}});
  std::size_t current = 0;
  auto back = sessionsFromJson(sessionsToJson({s}, 0), &current);
  REQUIRE(back.size() == 1);
  CHECK(back[0].solves.size() == 3);
  CHECK(back[0].solves[1].penalty == Penalty::Plus2);
  CHECK(back[0].solves[1].moves == "R U R'");
  CHECK(back[0].cube == CubeKind::Virtual); // a new session's default
  s.cube = CubeKind::Physical;
  CHECK(sessionsFromJson(sessionsToJson({s}, 0))[0].cube == CubeKind::Physical);
  // Saved before sessions had a cube: the timer was for physical cubes.
  auto old = QJsonDocument::fromJson(
      R"({"version": 1, "sessions": [{"name": "A", "solves": []}]})");
  CHECK(sessionsFromJson(old.object())[0].cube == CubeKind::Physical);

  s.solves[1].moveTimes = {100, 250, 400};
  back = sessionsFromJson(sessionsToJson({s}, 0));
  CHECK(back[0].solves[1].moveTimes == std::vector<std::int64_t>{100, 250, 400});
  CHECK(back[0].solves[0].moveTimes.empty());

  s.scrambleType = "lsll";
  s.solves[1].moves = "r U R'";
  QByteArray cs = sessionsToCstimer({s});
  // cstimer tells its own format from other timers' by this start.
  CHECK(cs.startsWith("{\"session1\":"));
  QJsonObject csObject = QJsonDocument::fromJson(cs).object();
  QString sessionData = csObject.value("properties")
                            .toObject()
                            .value("sessionData")
                            .toString();
  CHECK(sessionData.contains("\"scrType\":\"lsll2\""));
  QJsonArray solution =
      csObject.value("session1").toArray().at(1).toArray().at(4).toArray();
  CHECK(solution.at(0).toString() == "Rw@100 U@250 R'@400");
  CHECK(solution.at(1).toString() == "333");
  QString error;
  auto imported = sessionsFromCstimerFile(cs, &error);
  REQUIRE(imported);
  REQUIRE(imported->sessions.size() == 1);
  const Session &in = imported->sessions[0];
  CHECK(in.name == "Main");
  CHECK(in.scrambleType == "lsll");
  CHECK(in.solves[0].timeMs == 12340);
  CHECK(in.solves[2].penalty == Penalty::Dnf);
  CHECK(in.solves[1].moves == "r U R'");
  CHECK(in.solves[1].moveTimes == std::vector<std::int64_t>{100, 250, 400});

  // A file cut off is reported as such, not as having no sessions.
  CHECK_FALSE(sessionsFromCstimerFile(cs.left(cs.size() / 2), &error));
  CHECK(error.contains("complete JSON"));
  CHECK_FALSE(sessionsFromCstimerFile("{\"other\": 1}", &error));
  CHECK(error.contains("no cstimer sessions"));

  // A cstimer export as it is written by cstimer itself.
  auto doc = QJsonDocument::fromJson(R"({
    "session1": [[[0, 10500], "R U F", "", 1700000000], [[2000, 9000], "U", "c", 1700000001]],
    "session2": [[[-1, 30000], "D2", "", 1700000002, ["R@10 U@500", "333"]]],
    "session3": [],
    "session4": [[[0, 4000], "R U", "", 1700000003]],
    "properties": {"sessionData": "{\"1\":{\"name\":\"3x3\",\"opt\":{},\"rank\":2},\"2\":{\"name\":\"OH\",\"opt\":{\"scrType\":\"333oh\"},\"rank\":1},\"3\":{\"name\":\"empty\",\"rank\":3},\"4\":{\"name\":\"2x2\",\"opt\":{\"scrType\":\"222so\"},\"rank\":4}}"}
  })");
  imported = sessionsFromCstimer(doc.object(), &error);
  REQUIRE(imported);
  REQUIRE(imported->sessions.size() == 2);
  CHECK(imported->emptySkipped == 1);
  CHECK(imported->others == std::vector<std::string>{"2x2"});
  const auto &list = imported->sessions;
  CHECK(list[0].name == "OH");
  CHECK(list[0].scrambleType == "333");
  CHECK(list[0].cube == CubeKind::Physical);
  CHECK(list[0].solves[0].penalty == Penalty::Dnf);
  CHECK(list[0].solves[0].moves == "R U");
  CHECK(list[0].solves[0].moveTimes == std::vector<std::int64_t>{10, 500});
  CHECK(list[1].solves[1].penalty == Penalty::Plus2);
  CHECK(list[1].solves[1].comment == "c");

  CHECK(cstimerScrambleType("xcross") == "333");
  CHECK(cstimerScrambleType("eoline") == "eoline");
  CHECK(scrambleTypeFromCstimer("pyrso") == std::nullopt);
  CHECK(scrambleTypeFromCstimer("zbll") == "zbll");
  CHECK(scrambleTypeFromCstimer("roux") == "333");
}
