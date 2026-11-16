#include "cube/solver.hpp"
#include "io/library.hpp"
#include "io/move_counts.hpp"
#include "io/sessions.hpp"
#include "test_files.hpp"
#include "train/case_logic.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <string>
#include <vector>

using namespace vcube;
using namespace vcube::testing;

TEST_CASE("built-in sets load with the expected cases and no algs") {
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
  // Its file was moved aside, not deleted.
  CHECK(QDir(lib.deletedDirectory())
            .entryList({"2look-pll.*.json"}, QDir::Files)
            .size() == 1);
  CHECK(lib.find("pll")->findCase("T")->algs.size() == 1);
  CHECK(lib.progress("pll")["T"].attempts.size() == 1);
  REQUIRE(lib.restoreBuiltin("pll"));
  CHECK(lib.find("pll")->findCase("T")->algs.size() == 1);
}

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
  std::string id = lib.add(copy)->id;
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

TEST_CASE("a stray copy of a set file never overwrites the set") {
  QTemporaryDir tmp;
  {
    AlgLibrary lib(tmp.path(), kBuiltins);
    lib.load();
  }
  // "pll - Copy.json" sorts before "pll.json" and has the same id.
  QByteArray original = readText(tmp.filePath("pll.json"));
  QByteArray stale = original;
  stale.replace("\"name\": \"PLL\"", "\"name\": \"Stale PLL\"");
  REQUIRE(stale != original);
  writeText(tmp.filePath("pll - Copy.json"), stale);
  AlgLibrary lib(tmp.path(), kBuiltins);
  lib.load();
  REQUIRE(lib.errors().size() == 1);
  CHECK(lib.errors().front().contains("pll - Copy.json"));
  CHECK(lib.find("pll")->name == "PLL");
  CHECK(lib.sets().size() == 4);
  lib.find("pll")->findCase("T")->algs.push_back("R U R' U' R' F R2 U' R' U' "
                                                 "R U R' F'");
  REQUIRE(lib.save(*lib.find("pll")));
  CHECK(readText(tmp.filePath("pll - Copy.json")) == stale);
  CHECK(readText(tmp.filePath("pll.json")) != original);

  // A set in a file of another name is saved back to that file.
  QFile::rename(tmp.filePath("oll.json"), tmp.filePath("my oll.json"));
  AlgLibrary again(tmp.path(), kBuiltins);
  again.load();
  REQUIRE(again.find("oll"));
  CHECK(again.pathFor("oll") == tmp.filePath("my oll.json"));
  REQUIRE(again.save(*again.find("oll")));
  CHECK_FALSE(QFile::exists(tmp.filePath("oll.json")));
  REQUIRE(again.remove("oll"));
  CHECK_FALSE(QFile::exists(tmp.filePath("my oll.json")));
}

TEST_CASE("of two files with one set id, one loads and the other is named") {
  QTemporaryDir tmp;
  QByteArray mine;
  {
    AlgLibrary lib(tmp.path(), kBuiltins);
    lib.load();
    AlgSet set = *lib.find("pll");
    set.builtin = false;
    set.id = "mine";
    set.name = "Mine";
    REQUIRE(lib.add(set));
    mine = readText(tmp.filePath("mine.json"));
  }
  // The file named for the id wins even when another sorts before it.
  QByteArray other = mine;
  other.replace("\"name\": \"Mine\"", "\"name\": \"Other\"");
  REQUIRE(other != mine);
  writeText(tmp.filePath("a mine.json"), other);
  {
    AlgLibrary lib(tmp.path(), kBuiltins);
    lib.load();
    REQUIRE(lib.errors().size() == 1);
    CHECK(lib.errors().front().contains("a mine.json"));
    CHECK(lib.errors().front().contains("\"mine\""));
    CHECK(lib.find("mine")->name == "Mine");
    CHECK(lib.pathFor("mine") == tmp.filePath("mine.json"));
    CHECK(lib.sets().size() == 5);
  }
  // Neither named for the id: the first by name loads, and saving it
  // leaves the other alone.
  REQUIRE(QFile::rename(tmp.filePath("mine.json"), tmp.filePath("z.json")));
  AlgLibrary lib(tmp.path(), kBuiltins);
  lib.load();
  REQUIRE(lib.errors().size() == 1);
  CHECK(lib.errors().front().startsWith("z.json"));
  CHECK(lib.find("mine")->name == "Other");
  CHECK(lib.pathFor("mine") == tmp.filePath("a mine.json"));
  lib.find("mine")->name = "Renamed";
  REQUIRE(lib.save(*lib.find("mine")));
  CHECK(readText(tmp.filePath("z.json")) == mine);
  // A set added with a taken id gets one of its own.
  AlgSet copy = *lib.find("mine");
  AlgSet *added = lib.add(copy);
  REQUIRE(added);
  CHECK(added->id != "mine");
  CHECK(lib.find("mine")->name == "Renamed");
}

TEST_CASE("deleted built-in sets can be restored") {
  QTemporaryDir tmp;
  AlgLibrary lib(tmp.path(), kBuiltins);
  lib.load();
  CHECK(lib.missingBuiltins().empty());
  lib.progress("oll")["oll27"].record({100, 1500, false});
  lib.saveProgress("oll");
  REQUIRE(lib.remove("oll"));
  REQUIRE(lib.missingBuiltins().size() == 1);
  CHECK(lib.missingBuiltins().front().id == "oll");
  REQUIRE(lib.restoreBuiltin("oll"));
  REQUIRE(lib.find("oll"));
  CHECK(lib.find("oll")->builtin);
  CHECK(lib.progress("oll").empty());
  CHECK(lib.missingBuiltins().empty());
}

TEST_CASE("restoring a built-in keeps progress its set left behind") {
  QTemporaryDir tmp;
  {
    AlgLibrary lib(tmp.path(), kBuiltins);
    lib.load();
    lib.progress("oll")["oll27"].record({100, 1500, false});
    lib.saveProgress("oll");
    finishBackgroundWrites();
  }
  // The set's file is lost; its progress file stays.
  REQUIRE(QFile::remove(tmp.filePath("oll.json")));
  AlgLibrary lib(tmp.path(), kBuiltins);
  lib.load();
  REQUIRE(lib.missingBuiltins().size() == 1);
  REQUIRE(lib.restoreBuiltin("oll"));
  CHECK(lib.progress("oll").empty());
  CHECK_FALSE(QFile::exists(tmp.filePath("progress/oll.json")));
  CHECK(filesStarting(tmp.filePath("deleted/progress"), "oll.").size() == 1);
}

TEST_CASE("removing an earlier session keeps the current one current") {
  SessionStore store(QTemporaryDir().filePath("sessions.json"));
  store.sessions()[0].name = "a";
  store.addSession("b");
  store.addSession("c");
  store.setCurrentIndex(1);
  store.removeSession(0);
  CHECK(store.current().name == "b");
  store.removeSession(1); // a later one
  CHECK(store.current().name == "b");
  store.removeSession(0); // the last one: an empty session is left
  CHECK(store.sessions().size() == 1);
  CHECK(store.currentIndex() == 0);
}

TEST_CASE("a lost install marker doesn't bring deleted built-ins back") {
  QTemporaryDir tmp;
  {
    AlgLibrary lib(tmp.path(), kBuiltins);
    lib.load();
    REQUIRE(lib.remove("2look-pll"));
  }
  REQUIRE(QFile::remove(tmp.filePath("installed-builtins.json")));
  AlgLibrary lib(tmp.path(), kBuiltins);
  lib.load();
  CHECK_FALSE(lib.find("2look-pll"));
  REQUIRE(lib.errors().size() == 1);
  CHECK(lib.errors().front().contains("2-look PLL"));
  CHECK(lib.missingBuiltins().size() == 1);
  // The marker is written again, so it is said once.
  AlgLibrary again(tmp.path(), kBuiltins);
  again.load();
  CHECK(again.errors().isEmpty());
  // A fresh library still gets every built-in.
  QTemporaryDir fresh;
  AlgLibrary first(fresh.path(), kBuiltins);
  first.load();
  CHECK(first.sets().size() == 4);
}

TEST_CASE("restoring doesn't overwrite a user case that shares an id") {
  QTemporaryDir tmp;
  AlgLibrary lib(tmp.path(), kBuiltins);
  lib.load();
  AlgSet &pll = *lib.find("pll");
  std::erase_if(pll.groups.front().cases,
                [](const AlgCase &c) { return c.id == "T"; });
  // The user's own case named T, which isn't the T perm.
  AlgCase mine;
  mine.id = "T";
  mine.name = "T";
  mine.state = stateSolvedBy(parseAlgOrEmpty("R U R' U R U2 R'"));
  mine.algs = {"R U R' U R U2 R'"};
  pll.groups.front().cases.push_back(mine);
  lib.progress("pll")["T"].record({100, 1500, false});
  int cases = pll.caseCount();
  REQUIRE(lib.restoreBuiltin("pll"));
  const AlgSet &after = *lib.find("pll");
  CHECK(after.caseCount() == cases + 1);
  CHECK(after.findCase("T")->state == mine.state);
  CHECK(after.findCase("T")->algs == mine.algs);
  CHECK(lib.progress("pll").contains("T"));
  // The T perm is back beside it.
  bool tperm = false;
  for (const auto &g : after.groups) {
    for (const auto &c : g.cases) {
      tperm = tperm ||
              (c.id != "T" &&
               checkCaseAlg(after, g, c, "R U R' U' R' F R2 U' R' U' R U R' F'")
                   .passes());
    }
  }
  CHECK(tperm);
}

TEST_CASE("a set that can't be saved or deleted stays as it was") {
  QTemporaryDir tmp;
  AlgLibrary lib(tmp.path(), kBuiltins);
  lib.load();
  std::size_t before = lib.sets().size();
  auto writable = QFile::permissions(tmp.path());
  QFile::setPermissions(tmp.path(),
                        QFileDevice::ReadOwner | QFileDevice::ExeOwner);
  QFile probe(tmp.filePath("probe"));
  if (probe.open(QIODevice::WriteOnly)) {
    probe.close();
    probe.remove();
    QFile::setPermissions(tmp.path(), writable);
    SKIP("the directory stays writable (running as root?)");
  }
  AlgSet s;
  s.id = "mine";
  s.name = "Mine";
  QString error;
  CHECK_FALSE(lib.add(s, &error));
  CHECK_FALSE(error.isEmpty());
  CHECK(lib.sets().size() == before);
  error.clear();
  CHECK_FALSE(lib.remove("pll", &error));
  CHECK_FALSE(error.isEmpty());
  CHECK(lib.find("pll"));
  QFile::setPermissions(tmp.path(), writable);
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
  // Reviewed in Learn, so introduced (when is unknown).
  CHECK(loaded["Ua"].card.introduced != 0);
  CHECK_FALSE(loaded["Ua"].card.marked);

  // Before cards knew when they were introduced: a reviewed card was at
  // its first Learn attempt, a card in review that never was reviewed had
  // been marked learned, and a queued one wasn't yet.
  auto noIntro = QJsonDocument::fromJson(R"({"version": 1, "cases": {
    "learned": {"card": {"state": 1, "reps": 1, "due": 900},
                "attempts": [[50, 900, 0, "drill"], [700, 0, 0, "learn"]]},
    "marked": {"card": {"state": 2, "reps": 0, "due": 5000}, "attempts": []},
    "queued": {"card": {"state": 1, "reps": 0, "due": 0}, "attempts": []}}})");
  SetProgress intro = progressFromJson(noIntro.object());
  CHECK(intro["learned"].card.introduced == 700);
  CHECK_FALSE(intro["learned"].card.marked);
  CHECK(intro["marked"].card.introduced != 0);
  CHECK(intro["marked"].card.marked);
  CHECK(intro["queued"].card.introduced == 0);
  // And kept from then on.
  intro["queued"].card.introduced = 12345;
  SetProgress again = progressFromJson(progressToJson(intro));
  CHECK(again["queued"].card.introduced == 12345);
  CHECK(again["marked"].card.marked);
  CHECK_FALSE(again["learned"].card.marked);
}

TEST_CASE("progress keeps recognition times, suspension and burial") {
  SetProgress p;
  Attempt learn{100, 0, false, AttemptMode::Learn};
  learn.recognitionMs = 850;
  learn.executionMs = 2100;
  p["T"].record(learn);
  Attempt recog{200, 0, true, AttemptMode::Learn};
  recog.recognitionMs = 1200;
  recog.recognitionOnly = true;
  p["T"].record(recog);
  p["T"].record({300, 2500, false, AttemptMode::PhysicalDrill});
  p["T"].card.suspended = true;
  p["U"].card.buriedUntil = 99999;
  SetProgress back = progressFromJson(progressToJson(p));
  REQUIRE(back["T"].attempts.size() == 3);
  CHECK(back["T"].attempts[0].recognitionMs == 850);
  CHECK(back["T"].attempts[0].executionMs == 2100);
  CHECK_FALSE(back["T"].attempts[0].recognitionOnly);
  CHECK(back["T"].attempts[1].recognitionMs == 1200);
  CHECK(back["T"].attempts[1].recognitionOnly);
  CHECK(back["T"].attempts[2].recognitionMs == 0);
  CHECK(back["T"].attempts[2].timeMs == 2500);
  CHECK(back["T"].card.suspended);
  CHECK_FALSE(back["U"].card.suspended);
  CHECK(back["U"].card.buriedUntil == 99999);
  CHECK(back["T"].card.buriedUntil == 0);
  // The four-element attempts of earlier files still read the same.
  QJsonArray row = progressToJson(p)
                       .value("cases")
                       .toObject()
                       .value("T")
                       .toObject()
                       .value("attempts")
                       .toArray()
                       .at(2)
                       .toArray();
  CHECK(row.size() == 4);
}

TEST_CASE("progress keeps the drill archive, and files without one load") {
  SetProgress p;
  DrillArchive kept;
  kept.bestMs = 812;
  kept.days = {{19700, 12, 2, 20000, 900}, {19702, 3, 3, 0, 0}};
  p["T"].archive[AttemptMode::VirtualDrill] = kept;
  p["T"].record({300, 2500, false, AttemptMode::VirtualDrill});
  p["U"].record({300, 2500, false, AttemptMode::PhysicalDrill});
  QJsonObject json = progressToJson(p);
  // Nothing more is written for a case without an archive.
  CHECK_FALSE(json.value("cases").toObject().value("U").toObject().contains(
      "drillArchive"));
  SetProgress back = progressFromJson(json);
  CHECK(back["T"].archive.at(AttemptMode::VirtualDrill) == kept);
  CHECK(*back["T"].best(AttemptMode::VirtualDrill) == 812);
  CHECK(back["U"].archive.empty());
  CHECK(back["T"].days(AttemptMode::VirtualDrill).size() == 3);
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
  CHECK(back[0].solves[1].moveTimes ==
        std::vector<std::int64_t>{100, 250, 400});
  CHECK(back[0].solves[0].moveTimes.empty());

  s.scrambleType = "lsll";
  s.solves[1].moves = "r U R'";
  QByteArray cs = sessionsToCstimer({s});
  // cstimer tells its own format from other timers' by this start.
  CHECK(cs.startsWith("{\"session1\":"));
  QJsonObject csObject = QJsonDocument::fromJson(cs).object();
  QString sessionData =
      csObject.value("properties").toObject().value("sessionData").toString();
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

TEST_CASE("cstimer files in older and odd shapes still import") {
  QString error;
  // Sessions stored as JSON text, sessionData as an object, the type under
  // the older "scr", and no names or ranks.
  auto doc = QJsonDocument::fromJson(R"({
    "session2": "[[[0, 5000], \"R\", \"\", 1700000000]]",
    "session1": [[[0, 7000], "U", "", 1700000001]],
    "session10": [[[0, 8000], "F", "", 1700000002]],
    "sessionx": [[[0, 1], "B", "", 1]],
    "properties": {"sessionData": {"1": {"scr": "pll"}, "10": {"name": 12}}}
  })");
  auto imported = sessionsFromCstimer(doc.object(), &error);
  REQUIRE(imported);
  REQUIRE(imported->sessions.size() == 3);
  // Without ranks, in number order.
  CHECK(imported->sessions[0].name == "Session 1");
  CHECK(imported->sessions[0].scrambleType == "pll");
  CHECK(imported->sessions[1].name == "Session 2");
  CHECK(imported->sessions[1].solves.front().timeMs == 5000);
  CHECK(imported->sessions[2].name == "12");
  CHECK(imported->sessions[2].scrambleType == "333");
  for (const Session &s : imported->sessions) {
    CHECK(s.cube == CubeKind::Physical);
  }

  // Solves that can't be read are left out; a session of only those is
  // skipped as empty. Solutions that don't read, or are for another puzzle,
  // leave the solve without moves.
  doc = QJsonDocument::fromJson(R"({
    "session1": [
      [[0], "R", "", 1],
      "not a solve",
      [[0, 1000], "U", "", 2, ["R@10 junk", "333"]],
      [[0, 1000], "U", "", 3, ["R@10 U@20", "222"]],
      [[0, 1000], "U", "", 4, ["R@x", "333"]],
      [[0, 1000], "U", "", 5, ["", "333"]],
      [[4000, 1000], "U", "", 6]
    ],
    "session2": [[[0]]],
    "properties": {"sessionData": "not json"}
  })");
  imported = sessionsFromCstimer(doc.object(), &error);
  REQUIRE(imported);
  REQUIRE(imported->sessions.size() == 1);
  CHECK(imported->emptySkipped == 1);
  const auto &solves = imported->sessions[0].solves;
  REQUIRE(solves.size() == 5);
  for (const Solve &s : solves) {
    CHECK(s.moves.empty());
    CHECK(s.moveTimes.empty());
  }
  CHECK(solves.back().penalty == Penalty::Plus2); // any positive penalty

  // Only sessions of other puzzles: none to import, but not an error.
  doc = QJsonDocument::fromJson(R"({
    "session1": [[[0, 1000], "R", "", 1]],
    "properties": {"sessionData": {"1": {"name": "Pyra", "opt": {"scrType": "pyrso"}}}}
  })");
  imported = sessionsFromCstimer(doc.object(), &error);
  REQUIRE(imported);
  CHECK(imported->sessions.empty());
  CHECK(imported->others == std::vector<std::string>{"Pyra"});

  // Not an object at all.
  CHECK_FALSE(sessionsFromCstimerFile("[1, 2]", &error));
  CHECK(error.contains("not a cstimer export"));
  CHECK_FALSE(sessionsFromCstimerFile("", &error));
  CHECK_FALSE(sessionsFromCstimer(QJsonObject{}, &error));
}

TEST_CASE("exports to cstimer keep penalties, rotations and untimed moves") {
  Session a;
  a.name = "A";
  a.scrambleType = "xcross"; // cstimer has no such type: 3x3
  a.solves.push_back({1000, Penalty::Dnf, "R", "", 1, "x y R2 M'", {}});
  a.solves.push_back({2000, Penalty::Plus2, "U", "", 2, "Rw U", {5}});
  a.solves.push_back({3000, Penalty::None, "F", "", 3, "", {}});
  Session empty;
  empty.name = "Empty";
  QByteArray cs = sessionsToCstimer({a, empty});
  QJsonObject o = QJsonDocument::fromJson(cs).object();
  QJsonArray one = o.value("session1").toArray();
  REQUIRE(one.size() == 3);
  CHECK(one.at(0).toArray().at(0).toArray().at(0).toInt() == -1);
  CHECK(one.at(1).toArray().at(0).toArray().at(0).toInt() == 2000);
  CHECK(one.at(2).toArray().at(0).toArray().at(0).toInt() == 0);
  // Rotations stay lower case; moves without a time for each are at 0.
  CHECK(one.at(0).toArray().at(4).toArray().at(0).toString() ==
        "x@0 y@0 R2@0 M'@0");
  CHECK(one.at(1).toArray().at(4).toArray().at(0).toString() == "Rw@0 U@0");
  CHECK(one.at(2).toArray().size() == 4); // no moves, no solution
  CHECK(o.value("session2").toArray().isEmpty());
  QJsonObject data = QJsonDocument::fromJson(o.value("properties")
                                                 .toObject()
                                                 .value("sessionData")
                                                 .toString()
                                                 .toUtf8())
                         .object();
  CHECK(data.value("1")
            .toObject()
            .value("opt")
            .toObject()
            .value("scrType")
            .toString() == "333");
  CHECK(data.value("2").toObject().value("name").toString() == "Empty");
  CHECK(data.value("2").toObject().value("rank").toInt() == 2);

  // Back in: the empty session is skipped and the rest is as it was.
  QString error;
  auto back = sessionsFromCstimerFile(cs, &error);
  REQUIRE(back);
  CHECK(back->emptySkipped == 1);
  REQUIRE(back->sessions.size() == 1);
  const auto &solves = back->sessions[0].solves;
  REQUIRE(solves.size() == 3);
  CHECK(solves[0].penalty == Penalty::Dnf);
  CHECK(solves[0].timeMs == 1000);
  CHECK(solves[0].moves == "x y R2 M'");
  CHECK(solves[1].penalty == Penalty::Plus2);
  CHECK(solves[1].moves == "r U");
  CHECK(solves[2].moves.empty());
}
