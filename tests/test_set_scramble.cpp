#include "train/case_logic.hpp"
#include "train/set_scramble.hpp"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <set>

using namespace vcube;

namespace {

// Three OLL cases with nothing left free, so that a case is set up the
// same each time apart from its U turns.
AlgSet threeCases() {
  AlgSet set;
  set.id = "test";
  CaseGroup g{"Group", std::nullopt, {}};
  for (const auto &[id, alg] : {std::pair{"sune", "R U R' U R U2 R'"},
                                std::pair{"t", "R U R' U' R' F R F'"},
                                std::pair{"anti", "R U2 R' U' R U' R'"}}) {
    g.cases.push_back({id, id, stateSolvedBy(parseAlgOrEmpty(alg)), {alg}});
  }
  set.groups.push_back(g);
  return set;
}

// Each case as Drill sets it up.
std::map<std::string, CubieCube> displays(const AlgSet &set) {
  std::map<std::string, CubieCube> out;
  for (const auto &g : set.groups) {
    for (const auto &c : g.cases) {
      out[c.id] = analyzeCase(set, g, c).display;
    }
  }
  return out;
}

} // namespace

TEST_CASE("set scrambles: only the ticked cases are drawn") {
  AlgSet set = threeCases();
  Rng rng(7);
  SetScramblePool pool(set, {"t"});
  REQUIRE(pool.size() == 2);
  std::set<std::string> seen;
  for (int i = 0; i < 100; ++i) {
    SetScramble s = pool.draw(rng);
    CHECK(s.state.isValid());
    CHECK(s.caseId != "t");
    seen.insert(s.caseId);
  }
  CHECK(seen == std::set<std::string>{"sune", "anti"});
}

TEST_CASE("set scrambles: with every case unticked, all are drawn") {
  AlgSet set = threeCases();
  Rng rng(11);
  SetScramblePool pool(set, {"sune", "t", "anti"});
  REQUIRE(pool.size() == 3);
  std::set<std::string> seen;
  for (int i = 0; i < 100; ++i) {
    seen.insert(pool.draw(rng).caseId);
  }
  CHECK(seen == std::set<std::string>{"sune", "t", "anti"});
}

TEST_CASE("set scrambles: the U turns are the set's") {
  AlgSet set = threeCases();
  const auto shown = displays(set);
  Rng rng(3);

  // No U turn before or after: each case exactly as Drill shows it.
  set.aufBefore = false;
  set.aufAfter = false;
  SetScramblePool still(set, {});
  for (int i = 0; i < 50; ++i) {
    SetScramble s = still.draw(rng);
    REQUIRE(shown.count(s.caseId) == 1);
    CHECK(s.state == shown.at(s.caseId));
  }

  // With them, the case is turned some of the time.
  set.aufBefore = true;
  set.aufAfter = true;
  SetScramblePool turned(set, {});
  int moved = 0;
  for (int i = 0; i < 50; ++i) {
    SetScramble s = turned.draw(rng);
    CHECK(s.state.isValid());
    REQUIRE(shown.count(s.caseId) == 1);
    moved += s.state == shown.at(s.caseId) ? 0 : 1;
  }
  CHECK(moved > 0);
}

TEST_CASE("set scrambles: a case set up solved gives a random cube") {
  AlgSet set;
  set.id = "solved";
  set.aufBefore = false;
  set.aufAfter = false;
  set.groups.push_back({"Group", std::nullopt, {{"done", "Done", {}, {}}}});
  SetScramblePool pool(set, {});
  Rng rng(5);
  for (int i = 0; i < 10; ++i) {
    SetScramble s = pool.draw(rng);
    CHECK(s.caseId.empty());
    CHECK(s.state.isValid());
    CHECK(!s.state.isSolved());
  }
}
