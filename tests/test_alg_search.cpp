#include "train/alg_search.hpp"
#include "train/algset.hpp"
#include "train/case_logic.hpp"
#include "train/move_set.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <functional>
#include <set>

using namespace vcube;

namespace {

SearchGoal goalFor(const char *alg, const char *free, bool before = true,
                   bool after = true) {
  SearchGoal g;
  g.state = stateSolvedBy(parseAlgOrEmpty(alg));
  g.free = *parseFree(free);
  g.aufBefore = before;
  g.aufAfter = after;
  return g;
}

std::vector<SearchResult> run(const SearchGoal &g, const char *moves,
                              int maxLength, int extra = 0,
                              int maxResults = 1000,
                              SearchOutcome *outcome = nullptr) {
  SearchOptions o;
  o.moves = *MoveSet::parse(moves);
  o.maxLength = maxLength;
  o.extra = extra;
  o.maxResults = maxResults;
  o.threads = 4;
  std::vector<SearchResult> out;
  std::atomic<bool> stop{false};
  SearchCallbacks cb;
  cb.result = [&](const SearchResult &r) { out.push_back(r); };
  SearchOutcome got = searchAlgs(g, o, stop, cb);
  if (outcome) {
    *outcome = got;
  }
  return out;
}

std::set<std::string> texts(const std::vector<SearchResult> &rs) {
  std::set<std::string> s;
  for (const auto &r : rs) {
    s.insert(r.text);
  }
  return s;
}

} // namespace

TEST_CASE("move sets read and write their text form", "[movesets]") {
  auto s = MoveSet::parse("<R, U, F2>");
  REQUIRE(s);
  CHECK(s->toString() == "U, R, F2");
  CHECK(s->display() == "<U, R, F2>");
  CHECK(MoveSet::parse(s->toString()) == s);
  CHECK(MoveSet::parse("R U r M1")->toString() == "U, R, r, M1");
  CHECK(MoveSet::parse("Rw U")->has(MoveType::Rw));
  CHECK_FALSE(MoveSet::parse("R Q"));
  CHECK_FALSE(MoveSet::parse(""));
  CHECK_FALSE(MoveSet::parse("R3"));

  CHECK(s->contains(parseAlgOrEmpty("R U R' F2 U2")));
  CHECK_FALSE(s->contains(parseAlgOrEmpty("R U F")));
  CHECK(s->contains(parseAlgOrEmpty("y R U R'")));
  CHECK_FALSE(s->contains(parseAlgOrEmpty("y R U R'"), false));
  CHECK(s->contains(parseAlgOrEmpty("R3 U")));
  CHECK(moveSetOf(parseAlgOrEmpty("R U2 R' F2")).toString() == "U2, R1, F2");

  CHECK(builtinMoveSets().front().name == "<U, R>");
  CHECK(builtinMoveSets().back().set == MoveSet::allFaces());
}

TEST_CASE("move counts in four metrics", "[movesets]") {
  auto c = moveCounts(parseAlgOrEmpty("R U2 M' r x"));
  CHECK(c.htm == 5);
  CHECK(c.stm == 4);
  CHECK(c.qtm == 6);
  CHECK(c.etm == 5);
}

TEST_CASE("the generator finds Sune in <R, U>", "[search]") {
  SearchOutcome out;
  auto rs = run(goalFor("R U R' U R U2 R'", "LL:p"), "R U", 9, 2, 1000, &out);
  CHECK(out.end == SearchEnd::Done);
  CHECK(out.shortest == 7);
  CHECK(texts(rs).contains("R U R' U R U2 R'"));
  for (const auto &r : rs) {
    CHECK(r.length >= 7);
    CHECK(r.length <= 9);
    CHECK(r.alg.front().type != MoveType::U);
    CHECK(r.alg.back().type != MoveType::U);
    auto fit = fitAlg(stateSolvedBy(parseAlgOrEmpty("R U R' U R U2 R'")), r.alg,
                      *parseFree("LL:p"));
    REQUIRE(fit);
    CHECK(fit->before == r.fit.before);
    CHECK(fit->after == r.fit.after);
  }
}

TEST_CASE("the generator finds every alg a brute force does", "[search]") {
  // Every <R, U> sequence of up to 8 moves in the generator's canonical
  // form (no layer twice in a row, not starting or ending with U).
  SearchGoal g = goalFor("R U2 R' U' R U' R'", "LL:p");
  std::set<std::string> brute;
  std::vector<Move> moves = MoveSet::parse("R U")->moves();
  Alg path;
  std::function<void(int)> walk = [&](int left) {
    if (!path.empty() && path.back().type != MoveType::U &&
        fitAlg(g.state, path, g.free)) {
      // Leave out algs with a shorter alg in them as a start.
      bool earlier = false;
      for (std::size_t n = 1; n < path.size() && !earlier; ++n) {
        Alg prefix(path.begin(), path.begin() + static_cast<long>(n));
        for (int b : {0, 1, 2, 3}) {
          CubieCube s = g.state * uTurn(b) * cubieOf(prefix);
          earlier = earlier || stepSolved(s, g.free);
        }
      }
      if (!earlier) {
        brute.insert(toString(path));
      }
    }
    if (left == 0) {
      return;
    }
    for (Move m : moves) {
      if (path.empty() && m.type == MoveType::U) {
        continue;
      }
      if (!path.empty() && path.back().type == m.type) {
        continue;
      }
      path.push_back(m);
      walk(left - 1);
      path.pop_back();
    }
  };
  walk(8);
  auto rs = run(g, "R U", 8, -1);
  CHECK(texts(rs) == brute);
  CHECK_FALSE(brute.empty());
}

TEST_CASE("the generator follows wide turns and slices", "[search]") {
  auto rs = run(goalFor("r U R' U' r' F R F'", "LL:p"), "R U F r", 8);
  CHECK(texts(rs).contains("r U R' U' r' F R F'"));
  auto ms = run(goalFor("M2 U M U2 M' U M2", ""), "R U M", 7);
  CHECK(texts(ms).contains("M2 U M U2 M' U M2"));
}

TEST_CASE("the generator keeps to the case's AUF settings", "[search]") {
  // Without a U turn allowed before, Antisune from the other side doesn't
  // count.
  auto rs = run(goalFor("R U R' U R U2 R'", "LL:p", false, false), "R U", 7);
  for (const auto &r : rs) {
    CHECK(r.fit.before == 0);
    CHECK(r.fit.after == 0);
  }
  CHECK(texts(rs).contains("R U R' U R U2 R'"));
  CHECK_FALSE(texts(rs).contains("R' U2 R U R' U R"));
}

TEST_CASE("the generator says when the moves can't solve a case", "[search]") {
  SearchOutcome out;
  auto rs = run(goalFor("F", ""), "R U", 10, 0, 10, &out);
  CHECK(rs.empty());
  CHECK(out.end == SearchEnd::Unsolvable);
}

TEST_CASE("the generator stops at the result cap and when told to",
          "[search]") {
  SearchOutcome out;
  auto rs = run(goalFor("R U R' U'", ""), "R U F", 12, -1, 3, &out);
  CHECK(rs.size() == 3);
  CHECK(out.end == SearchEnd::Capped);

  SearchOptions o;
  o.moves = MoveSet::allFaces();
  std::atomic<bool> stop{true};
  auto stopped = searchAlgs(goalFor("R U R' U'", ""), o, stop);
  CHECK(stopped.end == SearchEnd::Stopped);
  CHECK(stopped.found == 0);
}
