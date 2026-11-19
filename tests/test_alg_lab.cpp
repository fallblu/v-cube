#include "io/alglab_store.hpp"
#include "train/alg_lab.hpp"
#include "train/case_logic.hpp"
#include "train/ergonomics.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

using namespace vcube;

namespace {

LabAttempt attempt(const LabCaseKey &key, const std::string &alg,
                   std::int64_t ms, const std::string &session = "s1",
                   bool dnf = false) {
  LabAttempt a;
  a.key = key;
  a.alg = alg;
  a.cube = CubeKind::Physical;
  a.timeMs = ms;
  a.dnf = dnf;
  a.session = session;
  return a;
}

} // namespace

TEST_CASE("ergonomic scores add move costs, axis changes and rules",
          "[ergonomics]") {
  ErgoProfile p;
  p.moves = {{"R", 1.0}, {"U", 1.0}, {"F", 2.0}, {"R2", 1.5}};
  p.otherMove = 5.0;
  p.axisChange = 0.5;
  p.rules = {{"R2", "F*", 1.0}, {"*", "B", 0.25}};
  auto s = ergoScore(parseAlgOrEmpty("R U R2 F B"), p);
  CHECK(s.moves == Catch::Approx(1 + 1 + 1.5 + 2 + 5));
  // R>U, U>R2, R2>F axis changes; F>B same axis; R2 F and * B rules.
  CHECK(s.transitions == Catch::Approx(0.5 * 3 + 1.0 + 0.25));
  CHECK(s.total == Catch::Approx(s.moves + s.transitions));

  CHECK(ergoMatches("R*", parseAlgOrEmpty("R'").front()));
  CHECK_FALSE(ergoMatches("R*", parseAlgOrEmpty("r").front()));
  CHECK(ergoMatches("r*", parseAlgOrEmpty("Rw2").front()));
  CHECK(ergoMatches("R'", parseAlgOrEmpty("R3").front()));

  CHECK(ergoMoveTokens().size() == 54);
}

TEST_CASE("lab case keys and alg keys", "[alglab]") {
  LabCaseKey k{"pll", "T"};
  CHECK(k.text() == "set:pll/T");
  CHECK(LabCaseKey::parse("set:pll/T") == k);
  CHECK(LabCaseKey::parse("lab:7") == LabCaseKey{"", "7"});
  CHECK_FALSE(LabCaseKey::parse("set:pll"));
  CHECK_FALSE(LabCaseKey::parse("nope"));
  CHECK(labAlgKey(" (R U  R' U')  ") == "R U R' U'");
  CHECK(labAlgKey("R3 U") == "R' U");
  CHECK(labAlgKey("R U )") == "R U )");

  LabCase c{"1", "Sune", "R U2 R' U' R U' R'", "LL:p", true, true};
  CHECK(fitAlg(labCaseState(c), parseAlgOrEmpty("R U R' U R U2 R'"),
               labCaseFree(c)));
}

TEST_CASE("test sessions pick the next alg", "[alglab]") {
  LabSession s;
  s.id = "s1";
  s.reps = 2;
  LabCaseKey k{"", "1"};
  s.entries = {{k, "A"}, {k, "B"}, {k, "C"}};
  Rng rng(1);
  std::vector<LabAttempt> as;

  SECTION("blocked") {
    s.order = LabOrder::Blocked;
    CHECK(nextEntry(s, as, std::nullopt, rng) == 0u);
    as.push_back(attempt(k, "A", 1000));
    CHECK(nextEntry(s, as, 0u, rng) == 0u);
    as.push_back(attempt(k, "A", 1000));
    CHECK(nextEntry(s, as, 0u, rng) == 1u);
  }
  SECTION("interleaved: every alg once a round, never twice in a row") {
    std::optional<std::size_t> last;
    std::vector<int> order;
    for (int i = 0; i < 6; ++i) {
      auto n = nextEntry(s, as, last, rng);
      REQUIRE(n);
      if (last) {
        CHECK(*n != *last);
      }
      order.push_back(static_cast<int>(*n));
      as.push_back(attempt(k, s.entries[*n].alg, 1000));
      last = n;
      auto done = repsDone(s, as);
      int lo = *std::min_element(done.begin(), done.end());
      int hi = *std::max_element(done.begin(), done.end());
      CHECK(hi - lo <= 1);
    }
    CHECK_FALSE(nextEntry(s, as, last, rng));
    // Attempts from another session don't count.
    as.push_back(attempt(k, "A", 1000, "other"));
    CHECK(repsDone(s, as) == std::vector<int>{2, 2, 2});
  }
}

TEST_CASE("lab statistics leave out warm-up, deviations and the trim",
          "[alglab]") {
  LabCaseKey k{"", "1"};
  std::vector<LabAttempt> as = {
      attempt(k, "A", 5000), // warm-up of s1
      attempt(k, "A", 1000),       attempt(k, "A", 1200),
      attempt(k, "A", 1100),       attempt(k, "A", 900, "s1", true), // DNF
      attempt(k, "B", 700),        // another alg
      attempt(k, "A", 4000, "s2"), // warm-up of s2
      attempt(k, "A", 1300, "s2"),
  };
  LabAttempt dev = attempt(k, "A", 600, "s2");
  dev.deviated = true;
  as.push_back(dev);
  WarmupOf warm = [](std::string_view) { return 1; };

  LabStats all = labStats(as, k, "A", CubeKind::Physical, {}, 0, warm);
  CHECK(all.attempts == 8);
  CHECK(all.deviated == 1);
  CHECK(all.dnfs == 1);
  CHECK(all.used == 4);
  CHECK(all.best == 1000);
  CHECK(all.worst == 1300);
  CHECK(all.mean == 1150);
  CHECK(all.median == 1150);
  REQUIRE(all.sd);
  CHECK(*all.sd == Catch::Approx(129.1).epsilon(0.01));
  CHECK(all.ao5); // 5 timed attempts: 4 times and a DNF
  CHECK(all.dnfRate() == Catch::Approx(0.2));
  // The trim leaves times out of the mean, not out of the DNF rate.
  LabStats quarter = labStats(as, k, "A", CubeKind::Physical, {}, 25, warm);
  CHECK(quarter.used == 2);
  CHECK(quarter.dnfRate() == Catch::Approx(0.2));

  LabWindow s2{LabWindow::Kind::Session, "s2", 0};
  LabStats second = labStats(as, k, "A", CubeKind::Physical, s2, 0, warm);
  CHECK(second.used == 1);
  CHECK(second.best == 1300);

  // A deviation included counts again.
  as.back().include = true;
  CHECK(labStats(as, k, "A", CubeKind::Physical, {}, 0, warm).best == 600);

  // Without warm-up and with a 25% trim of 8 times, 2 go from each end.
  std::vector<LabAttempt> eight;
  for (int t : {1000, 1100, 1200, 1300, 1400, 1500, 1600, 9000}) {
    eight.push_back(attempt(k, "A", t));
  }
  LabStats trimmed = labStats(eight, k, "A", CubeKind::Physical, {}, 25);
  CHECK(trimmed.used == 4);
  CHECK(trimmed.mean == 1350);
  CHECK(trimmed.best == 1000); // best and worst are of every time

  LabWindow recent{LabWindow::Kind::Recent, "", 3};
  CHECK(labStats(eight, k, "A", CubeKind::Physical, recent, 0).used == 3);
}

TEST_CASE("lab setups put the alg's case in front of it", "[alglab]") {
  Rng rng(3);
  CubieCube sune = stateSolvedBy(parseAlgOrEmpty("R U R' U R U2 R'"));
  PieceMask oll = *parseFree("LL:p");
  for (int i = 0; i < 20; ++i) {
    // An alg that needs a U turn first: from the setup it fits without.
    CubieCube s =
        labSetup(sune, "U R U R' U R U2 R'", oll, true, true, false, rng);
    auto fit = fitAlg(s, parseAlgOrEmpty("R U R' U R U2 R'"), oll, false, true);
    CHECK(fit);
  }
  PieceMask none;
  CubieCube t = stateSolvedBy(parseAlgOrEmpty("R U R' U' R' F R2 U' R' U' "
                                              "R U R' F'"));
  CubieCube s = labSetup(t, "R U R' U' R' F R2 U' R' U' R U R' F'", none, true,
                         true, false, rng);
  CHECK(fitAlg(s, parseAlgOrEmpty("R U R' U' R' F R2 U' R' U' R U R' F'"), none,
               false, false));
}

TEST_CASE("the Alg Lab file keeps everything", "[alglab][io]") {
  AlgLabData d;
  LabSession s;
  s.id = "abc";
  s.name = "T perms";
  s.cube = CubeKind::Virtual;
  s.order = LabOrder::Blocked;
  s.reps = 5;
  s.warmup = 2;
  s.trimPercent = 10;
  s.randomAuf = true;
  s.entries = {{{"pll", "T"}, "R U R' U'"}, {{"", "1"}, "R U2 R'"}};
  d.sessions = {s, LabSession{}};
  d.sessions[1].id = "def";
  d.current = 1;
  d.cases = {{"1", "Mine", "R U R'", "LL:p", false, true}};
  d.candidates = {
      {{"pll", "T"}, {{"R U R'", CandidateSource::Generated, 1700000000}}}};
  LabAttempt a;
  a.key = {"pll", "T"};
  a.alg = "R U R' U'";
  a.cube = CubeKind::Virtual;
  a.timeMs = 1830;
  a.dnf = true;
  a.deviated = true;
  a.include = true;
  a.session = "abc";
  a.timestamp = 1700000001;
  a.moves = "R U R' U'";
  d.attempts = {a, attempt({"", "1"}, "R U2 R'", 900, "def")};
  d.attempts[1].scramble = "R U F";
  d.profiles = builtinErgoProfiles();
  d.moveSets = {{"Mine", *MoveSet::parse("R U F2")}};

  AlgLabData back = algLabFromJson(algLabToJson(d));
  CHECK(back == d);

  // A file without profiles gets the built-in ones.
  QJsonObject o = algLabToJson(d);
  o.remove("profiles");
  CHECK(algLabFromJson(o).profiles == builtinErgoProfiles());

  // A grip profile keeps its tricks, turned off ones too.
  ErgoProfile mine = builtinErgoProfiles().front();
  mine.name = "Mine";
  mine.grips->tricks[1].on = false;
  mine.grips->tricks[2].from = gripBit(-1) | gripBit(2);
  mine.grips->regrip = 2.0;
  d.profiles = {mine, builtinErgoProfiles()[1]};
  CHECK(algLabFromJson(algLabToJson(d)).profiles == d.profiles);

  // Costs that can't be used (not numbers, below zero, nan written as
  // null) are left out or put back to their defaults.
  o = algLabToJson(d);
  QJsonArray edited = o["profiles"].toArray();
  QJsonObject bad = edited[1].toObject();
  bad["moves"] = QJsonObject{{"R", QJsonValue()}, {"U", -1}, {"F", 3.5}};
  bad["other"] = -4;
  bad["rules"] = QJsonArray{
      QJsonObject{{"first", "R"}, {"second", "U"}, {"cost", QJsonValue()}},
      QJsonObject{{"first", "R"}, {"second", "F"}, {"cost", 0.5}}};
  edited[1] = bad;
  o["profiles"] = edited;
  ErgoProfile read = algLabFromJson(o).profiles[1];
  CHECK(read.moves == std::map<std::string, double>{{"F", 3.5}});
  CHECK(read.otherMove == 2.0);
  REQUIRE(read.rules.size() == 1);
  CHECK(read.rules.front().cost == 0.5);
  CHECK(validCost(0.0));
  CHECK_FALSE(validCost(std::nan("")));
  CHECK_FALSE(validCost(std::numeric_limits<double>::infinity()));
  CHECK_FALSE(validCost(-0.5));

  // A version 1 file gets the built-in grip profile first.
  o = algLabToJson(d);
  QJsonArray moveOnly;
  moveOnly.append(o["profiles"].toArray()[1]);
  o["profiles"] = moveOnly;
  o["version"] = 1;
  auto old = algLabFromJson(o).profiles;
  REQUIRE(old.size() == 2);
  CHECK(old[0] == builtinErgoProfiles().front());
  CHECK(old[1] == builtinErgoProfiles()[1]);
  o["version"] = 2;
  CHECK(algLabFromJson(o).profiles.size() == 1);

  QTemporaryDir dir;
  AlgLabStore store(QDir(dir.path()).filePath("alglab.json"));
  REQUIRE(store.load());
  CHECK(store.sessions().size() == 1);
  CHECK_FALSE(store.current().id.empty());
  CHECK(store.addCandidate({"", "1"}, {"R U", CandidateSource::Typed, 0}));
  CHECK_FALSE(
      store.addCandidate({"", "1"}, {"R U", CandidateSource::Typed, 0}));
  store.addAttempt(attempt({"", "1"}, "R U", 1000));
  store.cases().push_back({store.newCaseId(), "x", "R", "", true, true});
  LabSession &added = store.addLabSession("Two");
  CHECK(added.id != store.sessions().front().id);
  REQUIRE(store.save());
  AlgLabStore again(QDir(dir.path()).filePath("alglab.json"));
  REQUIRE(again.load());
  CHECK(again.data() == store.data());
  CHECK(again.candidates({"", "1"}).size() == 1);
  CHECK(again.clearAttempts({"", "1"}, "R U") == 1);
  CHECK(again.newCaseId() == "2");
}

TEST_CASE("profiles are told apart by name", "[alglab][io]") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  AlgLabStore store(QDir(dir.path()).filePath("alglab.json"));
  REQUIRE(store.load());
  const std::string first = store.profiles().front().name;
  CHECK(store.uniqueProfileName("Mine") == "Mine");
  CHECK(store.uniqueProfileName(first) == first + " 2");
  // A profile may keep its own name when it is edited.
  CHECK(store.uniqueProfileName(first, 0) == first);

  // Two of one name in a file are told apart when it is read.
  ErgoProfile twin = store.profiles().front();
  store.profiles().push_back(twin);
  store.profiles().push_back(twin);
  REQUIRE(store.save());
  AlgLabStore again(QDir(dir.path()).filePath("alglab.json"));
  REQUIRE(again.load());
  std::vector<std::string> names;
  for (const ErgoProfile &p : again.profiles()) {
    names.push_back(p.name);
  }
  std::sort(names.begin(), names.end());
  CHECK(std::adjacent_find(names.begin(), names.end()) == names.end());
  CHECK(std::find(names.begin(), names.end(), first + " 2") != names.end());
  CHECK(std::find(names.begin(), names.end(), first + " 3") != names.end());
}
