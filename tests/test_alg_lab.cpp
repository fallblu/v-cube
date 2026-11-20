#include "io/alglab_store.hpp"
#include "io/json_values.hpp"
#include "train/alg_lab.hpp"
#include "train/case_logic.hpp"
#include "train/case_picker.hpp"
#include "train/ergonomics.hpp"
#include "train/f2l.hpp"

#include "test_files.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
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

TEST_CASE("an alg's own attempts give the statistics of them all", "[alglab]") {
  LabCaseKey k1{"", "1"}, k2{"pll", "T"};
  std::vector<LabAttempt> as;
  // Two cases with an alg of the same text, sessions and cubes mixed.
  for (int i = 0; i < 40; ++i) {
    LabAttempt a =
        attempt(i % 3 == 0 ? k2 : k1, i % 4 == 0 ? "B" : "A",
                1000 + (i * 37) % 500, i % 5 < 2 ? "s1" : "s2", i % 11 == 0);
    a.cube = i % 7 == 0 ? CubeKind::Virtual : CubeKind::Physical;
    a.deviated = i % 13 == 0;
    a.timestamp = i;
    as.push_back(a);
  }
  WarmupOf warm = [](std::string_view s) { return s == "s1" ? 2 : 1; };
  LabAttemptGroups all(as), one(as, k1);
  CHECK(all.of(k1, "C").empty());
  CHECK(all.of({"pll", "U"}, "A").empty());
  CHECK(one.of(k2, "A").empty());
  CHECK(one.of(k1, "A").size() == all.of(k1, "A").size());
  std::size_t total = 0;
  std::vector<LabWindow> windows = {{},
                                    {LabWindow::Kind::Session, "s2", 0},
                                    {LabWindow::Kind::Recent, "", 4}};
  for (const LabCaseKey &k : {k1, k2}) {
    for (const std::string alg : {"A", "B"}) {
      total += all.of(k, alg).size();
      for (CubeKind cube : {CubeKind::Physical, CubeKind::Virtual}) {
        for (const LabWindow &w : windows) {
          CHECK(labTimed(all.of(k, alg), k, alg, cube, w, warm) ==
                labTimed(as, k, alg, cube, w, warm));
          LabStats own = labStats(all.of(k, alg), k, alg, cube, w, 10, warm);
          LabStats whole = labStats(as, k, alg, cube, w, 10, warm);
          CHECK(own.attempts == whole.attempts);
          CHECK(own.used == whole.used);
          CHECK(own.timed == whole.timed);
          CHECK(own.dnfs == whole.dnfs);
          CHECK(own.deviated == whole.deviated);
          CHECK(own.mean == whole.mean);
          CHECK(own.median == whole.median);
          CHECK(own.best == whole.best);
          CHECK(own.worst == whole.worst);
          CHECK(own.sd == whole.sd);
          CHECK(own.ao5 == whole.ao5);
          CHECK(own.ao12 == whole.ao12);
        }
      }
    }
  }
  CHECK(total == as.size());
}

TEST_CASE("lab setups put the alg's case in front of it", "[alglab]") {
  Rng rng(3);
  CubieCube sune = stateSolvedBy(parseAlgOrEmpty("R U R' U R U2 R'"));
  PieceMask oll = *parseFree("LL:p");
  for (int i = 0; i < 20; ++i) {
    // An alg that needs a U turn first: from the setup it fits without.
    CubieCube s = labSetup(sune, "U R U R' U R U2 R'", oll, Aufs::Both,
                           LabAuf::Done, rng);
    auto fit = fitAlg(s, parseAlgOrEmpty("R U R' U R U2 R'"), oll, Aufs::After);
    CHECK(fit);
  }
  PieceMask none;
  CubieCube t = stateSolvedBy(parseAlgOrEmpty("R U R' U' R' F R2 U' R' U' "
                                              "R U R' F'"));
  CubieCube s = labSetup(t, "R U R' U' R' F R2 U' R' U' R U R' F'", none,
                         Aufs::Both, LabAuf::Done, rng);
  CHECK(fitAlg(s, parseAlgOrEmpty("R U R' U' R' F R2 U' R' U' R U R' F'"), none,
               Aufs::None));
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

TEST_CASE("an Alg Lab file edited by hand reads what can be used",
          "[alglab][io]") {
  QJsonObject o = testing::parsedJson(R"({
    "format": "v-cube.alglab", "version": 2, "current": 0,
    "sessions": [
      {"name": "High", "reps": 5000, "warmup": -1, "trim": 99,
       "cube": "x", "order": "zigzag", "entries": [
         {"case": "", "alg": "R U"},
         {"case": "set:pll/T", "alg": ""},
         {"case": "set:pll/T", "alg": "R U R'"}]},
      {"id": "keep", "name": "Low", "reps": 0, "warmup": 500, "trim": -5}],
    "cases": [{"id": "", "name": "No id"}, {"id": "1", "name": "Kept"}],
    "candidates": [
      {"case": "nonsense", "algs": [{"alg": "R", "source": "typed"}]},
      {"case": "lab:1", "algs": [{"alg": "", "source": "typed"},
                                 {"alg": "R U", "source": "x"}]}],
    "attempts": [
      {"case": "set:/T", "alg": "R", "time": 1000},
      {"case": "lab:1", "alg": "R U", "cube": "x", "time": 900}],
    "moveSets": [{"name": "Bad", "moves": "Q"},
                 {"name": "Good", "moves": "R U"}]})")
                      .object();
  REQUIRE_FALSE(o.isEmpty());
  AlgLabData d = algLabFromJson(o);

  REQUIRE(d.sessions.size() == 2);
  const LabSession &high = d.sessions[0];
  // A session without an id gets a fresh one.
  CHECK(high.id.size() == 12);
  CHECK(high.reps == 1000);
  CHECK(high.warmup == 0);
  CHECK(high.trimPercent == 40);
  CHECK(high.cube == CubeKind::Physical);
  CHECK(high.order == LabOrder::Interleaved);
  // Entries without a readable case or an alg are dropped.
  REQUIRE(high.entries.size() == 1);
  CHECK(high.entries[0].key == LabCaseKey{"pll", "T"});
  CHECK(high.entries[0].alg == "R U R'");
  const LabSession &low = d.sessions[1];
  CHECK(low.id == "keep");
  CHECK(low.reps == 1);
  CHECK(low.warmup == 100);
  CHECK(low.trimPercent == 0);

  // A case without an id is dropped.
  REQUIRE(d.cases.size() == 1);
  CHECK(d.cases[0].id == "1");

  // Candidates of a case key that can't be read are dropped, and so are
  // candidates without an alg; an unknown source reads as typed.
  REQUIRE(d.candidates.size() == 1);
  CHECK(d.candidates[0].key == LabCaseKey{"", "1"});
  REQUIRE(d.candidates[0].candidates.size() == 1);
  CHECK(d.candidates[0].candidates[0].alg == "R U");
  CHECK(d.candidates[0].candidates[0].source == CandidateSource::Typed);

  // So are attempts of a case key that can't be read; an unknown cube
  // reads as physical.
  REQUIRE(d.attempts.size() == 1);
  CHECK(d.attempts[0].key == LabCaseKey{"", "1"});
  CHECK(d.attempts[0].timeMs == 900);
  CHECK(d.attempts[0].cube == CubeKind::Physical);

  // A move set whose moves can't be read is dropped.
  REQUIRE(d.moveSets.size() == 1);
  CHECK(d.moveSets[0].name == "Good");
  CHECK(d.moveSets[0].set == *MoveSet::parse("R U"));
}

TEST_CASE("Alg Lab candidates and attempts can be removed", "[alglab][io]") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString path = QDir(dir.path()).filePath("alglab.json");
  AlgLabStore store(path);
  REQUIRE(store.load());
  const LabCaseKey t{"pll", "T"};
  const LabCaseKey mine{"", "1"};
  CHECK(store.addCandidate(t, {"R U R'", CandidateSource::Typed, 0}));
  CHECK(store.addCandidate(t, {"R U2 R'", CandidateSource::Pasted, 0}));
  CHECK(store.addCandidate(mine, {"F R F'", CandidateSource::Typed, 0}));
  CHECK(store.removeCandidate(t, "R U R'"));
  CHECK_FALSE(store.removeCandidate(t, "R U R'"));
  CHECK_FALSE(store.removeCandidate(t, "U"));
  CHECK_FALSE(store.removeCandidate({"pll", "Y"}, "R U2 R'"));
  REQUIRE(store.candidates(t).size() == 1);
  CHECK(store.candidates(t)[0].alg == "R U2 R'");
  store.removeCandidates(mine);
  CHECK(store.candidates(mine).empty());
  CHECK(store.candidates(t).size() == 1);

  store.addAttempt(attempt(t, "R U2 R'", 1000));
  store.addAttempt(attempt(t, "R U2 R'", 2000));
  store.addAttempt(attempt(t, "R U2 R'", 3000));
  const std::size_t n = store.attempts().size();
  CHECK(store.attempt(n) == nullptr);
  store.removeAttempt(n);
  CHECK(store.attempts().size() == n);
  store.removeAttempt(0);
  REQUIRE(store.attempts().size() == 2);
  REQUIRE(store.attempt(0) != nullptr);
  CHECK(store.attempt(0)->timeMs == 2000);
  CHECK(store.attempt(1)->timeMs == 3000);
  CHECK(store.attempt(2) == nullptr);

  store.current().warmup = 3;
  const std::string id = store.current().id;
  REQUIRE(store.sessionById(id) != nullptr);
  CHECK(store.sessionById(id) == &store.current());
  CHECK(store.warmupOf(id) == 3);
  CHECK(store.sessionById("gone") == nullptr);
  CHECK(store.warmupOf("gone") == 0);

  REQUIRE(store.save());
  AlgLabStore again(path);
  REQUIRE(again.load());
  CHECK(again.data() == store.data());
  CHECK(again.candidates(mine).empty());
  REQUIRE(again.candidates(t).size() == 1);
  CHECK(again.candidates(t)[0].source == CandidateSource::Pasted);
  REQUIRE(again.attempts().size() == 2);
  CHECK(again.attempts()[0].timeMs == 2000);
  CHECK(again.warmupOf(id) == 3);
}

TEST_CASE("Alg Lab times and timestamps are kept within range",
          "[alglab][io]") {
  AlgLabData d;
  d.candidates = {{{"pll", "T"}, {{"R U R'", CandidateSource::Typed, 1}}}};
  d.attempts = {attempt({"pll", "T"}, "R U R'", 1000)};
  QJsonObject o = algLabToJson(d);
  QJsonArray attempts = o["attempts"].toArray();
  QJsonObject huge = attempts[0].toObject();
  huge["time"] = 9e18;
  huge["timestamp"] = -5;
  attempts[0] = huge;
  attempts.append(huge);
  o["attempts"] = attempts;
  QJsonArray candidates = o["candidates"].toArray();
  QJsonObject c = candidates[0].toObject();
  QJsonArray algs = c["algs"].toArray();
  QJsonObject alg = algs[0].toObject();
  alg["added"] = 1e300;
  algs[0] = alg;
  c["algs"] = algs;
  candidates[0] = c;
  o["candidates"] = candidates;

  AlgLabData back = algLabFromJson(o);
  REQUIRE(back.attempts.size() == 2);
  for (const LabAttempt &a : back.attempts) {
    CHECK(a.timeMs == kMaxFileNumber);
    CHECK(a.timestamp == 0);
  }
  REQUIRE(back.candidates.size() == 1);
  CHECK(back.candidates[0].candidates[0].added == kMaxFileNumber);
  // Two of them add up without overflowing.
  CHECK(back.attempts[0].timeMs + back.attempts[1].timeMs > 0);
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

namespace {

// Each value's id reads back as that value, and no two share an id.
template <typename E, typename Id, typename FromId>
void checkIds(std::initializer_list<E> values, Id id, FromId fromId) {
  std::set<std::string_view> seen;
  for (E v : values) {
    INFO(id(v));
    CHECK(fromId(id(v)) == v);
    CHECK(seen.insert(id(v)).second);
  }
  CHECK_FALSE(fromId("nonsense"));
}

} // namespace

TEST_CASE("the ids kept in files read back as their values", "[io]") {
  using S = CandidateSource;
  checkIds({S::Set, S::Generated, S::Pasted, S::Typed}, candidateSourceId,
           candidateSourceFromId);
  checkIds({LabOrder::Interleaved, LabOrder::Blocked}, labOrderId,
           labOrderFromId);
  checkIds({CubeKind::Virtual, CubeKind::Physical}, cubeKindId, cubeKindFromId);
  checkIds({PickMode::Random, PickMode::Weighted, PickMode::InOrder,
            PickMode::Recap},
           pickModeId, pickModeFromId);
  using M = F2LMethod;
  checkIds({M::ZZMinus1, M::ZZ, M::CFOP, M::CFOPMinus1}, methodId,
           methodFromId);

  // Every source is written and read back in a file.
  AlgLabData d;
  CaseCandidates cc{{"pll", "T"}, {}};
  for (S s : {S::Set, S::Generated, S::Pasted, S::Typed}) {
    cc.candidates.push_back({std::string(candidateSourceId(s)), s, 1});
  }
  d.candidates = {cc};
  d.profiles = builtinErgoProfiles();
  CHECK(algLabFromJson(algLabToJson(d)) == d);
}

TEST_CASE("Alg Lab candidates are kept by case, each alg once",
          "[alglab][io]") {
  QTemporaryDir dir;
  AlgLabStore store(QDir(dir.path()).filePath("alglab.json"));
  LabCaseKey t{"pll", "T"}, y{"pll", "Y"}, lab{"", "1"};
  CHECK(store.addCandidate(t, {"R U", CandidateSource::Generated, 0}));
  CHECK(store.addCandidate(y, {"R U", CandidateSource::Generated, 0}));
  CHECK(store.addCandidate(t, {"R U'", CandidateSource::Typed, 0}));
  CHECK_FALSE(store.addCandidate(t, {"R U", CandidateSource::Typed, 1}));
  CHECK(store.addCandidate(lab, {"F", CandidateSource::Pasted, 0}));
  REQUIRE(store.candidates(t).size() == 2);
  CHECK(store.candidates(t)[1].alg == "R U'");
  CHECK(store.candidates(y).size() == 1);
  CHECK(store.candidates({"pll", "Z"}).empty());

  CHECK(store.removeCandidate(t, "R U"));
  CHECK_FALSE(store.removeCandidate(t, "R U"));
  CHECK_FALSE(store.removeCandidate({"pll", "Z"}, "R U"));
  CHECK(store.addCandidate(t, {"R U", CandidateSource::Typed, 2}));
  CHECK(store.candidates(t).back().alg == "R U");

  // The cases after one that is forgotten are still found.
  store.removeCandidates(t);
  CHECK(store.candidates(t).empty());
  CHECK(store.candidates(lab).size() == 1);
  CHECK_FALSE(store.addCandidate(lab, {"F", CandidateSource::Typed, 3}));
  CHECK(store.addCandidate(t, {"R2", CandidateSource::Typed, 4}));
  AlgLabData d = store.data();
  REQUIRE(d.candidates.size() == 3);
  CHECK(d.candidates[0].key == y);
  CHECK(d.candidates[1].key == lab);
  CHECK(d.candidates[2].key == t);

  // A case listed twice (a file edited by hand) is read as one.
  QJsonObject o = algLabToJson(d);
  QJsonArray list = o["candidates"].toArray();
  list.append(QJsonObject{{"case", "set:pll/Y"},
                          {"algs", QJsonArray{QJsonObject{{"alg", "R U"}},
                                              QJsonObject{{"alg", "U2"}}}}});
  o["candidates"] = list;
  AlgLabData read = algLabFromJson(o);
  REQUIRE(read.candidates.size() == 4);
  REQUIRE(store.save());
  QFile f(store.path());
  REQUIRE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
  f.write(QJsonDocument(o).toJson());
  f.close();
  AlgLabStore again(store.path());
  REQUIRE(again.load());
  REQUIRE(again.candidates(y).size() == 2);
  CHECK(again.candidates(y)[1].alg == "U2");
  CHECK(again.data().candidates.size() == 3);
}

namespace {

void checkWritesAfresh(AlgLabStore &store) {
  REQUIRE(store.save());
  QFile f(store.path());
  REQUIRE(f.open(QIODevice::ReadOnly));
  CHECK(
      f.readAll() ==
      QJsonDocument(algLabToJson(store.data())).toJson(QJsonDocument::Compact));
}

} // namespace

TEST_CASE("Alg Lab is written right however it changes between saves",
          "[alglab][io]") {
  QTemporaryDir dir;
  AlgLabStore store(QDir(dir.path()).filePath("alglab.json"));
  LabCaseKey t{"pll", "T"}, y{"pll", "Y"};
  store.addCandidate(t, {"R U", CandidateSource::Generated, 0});
  store.addCandidate(y, {"R U'", CandidateSource::Generated, 0});
  store.addAttempt(attempt(t, "R U", 1000));
  store.addAttempt(attempt(y, "R U'", 1100));
  checkWritesAfresh(store);
  store.addCandidate(t, {"R2", CandidateSource::Typed, 1});
  store.addAttempt(attempt(t, "R2", 900));
  checkWritesAfresh(store);
  store.attempt(0)->dnf = true;
  checkWritesAfresh(store);
  store.removeAttempt(1);
  store.removeCandidate(t, "R U");
  checkWritesAfresh(store);
  store.removeCandidates(t);
  checkWritesAfresh(store);
  store.cases().push_back({store.newCaseId(), "x", "R", "", true, true});
  store.addLabSession("Two");
  checkWritesAfresh(store);
}
