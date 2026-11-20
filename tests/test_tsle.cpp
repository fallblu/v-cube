#include "train/f2l.hpp"
#include "train/tsle.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

using namespace vcube;

namespace {

CubieCube after(const CubieCube &c, const Alg &alg) {
  FaceletCube f = c.toFacelets();
  f.apply(alg);
  auto out = centredCubie(f);
  REQUIRE(out);
  return *out;
}

} // namespace

TEST_CASE("TSLE has the sheet's cases", "[tsle]") {
  const std::vector<TsleState> &cases = tsleCases();
  CHECK(cases.size() == 104);
  std::map<int, int> byLevel;
  int oclls = 0;
  std::set<std::string> keys, names;
  for (TsleState c : cases) {
    CHECK(tsleCase(c) == c);
    CHECK(keys.insert(tsleKey(c)).second);
    CHECK(names.insert(tsleName(c)).second);
    CHECK(tsleFromKey(tsleKey(c)) == c);
    if (c.ocll()) {
      ++oclls;
      CHECK(tsleStepsLeft(c) == 1);
    } else {
      ++byLevel[tsleStepsLeft(c)];
    }
    // Every turn of a case has its steps left.
    for (int q = 1; q < 4; ++q) {
      CHECK(tsleStepsLeft(tsleTurned(c, q)) == tsleStepsLeft(c));
      CHECK(tsleCase(tsleTurned(c, q)) == c);
    }
  }
  CHECK(oclls == 7);
  // With every trigger to choose from, no case needs more than three.
  CHECK(byLevel == std::map<int, int>{{1, 3}, {2, 54}, {3, 40}});
  CHECK(tsleStepsLeft(TsleState{}) == 0);
  CHECK(TsleState{}.done());
  for (int i = 0; i < kTsleStates; ++i) {
    CHECK(TsleState::fromIndex(i).index() == i);
    CHECK(tsleStepsLeft(TsleState::fromIndex(i)) <= 3); // the most steps
  }
}

TEST_CASE("TSLE states are read from cubes of every slot", "[tsle]") {
  // The front-right edge taken out with a trigger.
  auto s = tsleStateOf(cubieOf(parseAlgOrEmpty("R U R'")), Slot::FR);
  REQUIRE(s);
  CHECK(s->edge != TsleState::kInSlot);
  CHECK(tsleStepsLeft(*s) == 1);
  CHECK(tsleStateOf(CubieCube{}, Slot::FL) == TsleState{});
  // Its mirror images are the same state for the other slots.
  CHECK(tsleStateOf(cubieOf(parseAlgOrEmpty("L' U' L")), Slot::FL) == s);
  CHECK(tsleStateOf(cubieOf(parseAlgOrEmpty("R' U' R")), Slot::BR) == s);
  CHECK(tsleStateOf(cubieOf(parseAlgOrEmpty("L U L'")), Slot::BL) == s);
  // Another slot open, or EO broken: not a TSLE state.
  CHECK_FALSE(tsleStateOf(cubieOf(parseAlgOrEmpty("R U R'")), Slot::FL));
  CHECK_FALSE(
      tsleStateOf(cubieOf(parseAlgOrEmpty("F R U R' U' F'")), Slot::FR));

  Rng rng(4);
  for (TsleState c : tsleCases()) {
    for (Slot slot : kF2LSlots) {
      TsleState turned = tsleTurned(c, static_cast<int>(rng() % 4));
      CubieCube cube = randomTsleCube(turned, slot, rng);
      REQUIRE(cube.isValid());
      CHECK(tsleStateOf(cube, slot) == turned);
      // The tables agree with turning a real cube.
      TsleSolver solver(slot);
      for (const TsleOption &o : solver.options(turned)) {
        CHECK(tsleStateOf(after(cube, o.alg), slot) == o.next);
      }
    }
  }
}

TEST_CASE("the best way through TSLE finishes it", "[tsle]") {
  Rng rng(9);
  for (Slot slot : kF2LSlots) {
    TsleSolver solver(slot);
    for (int i = 0; i < kTsleStates; ++i) {
      TsleState s = TsleState::fromIndex(i);
      CubieCube cube = randomTsleCube(s, slot, rng);
      std::vector<Alg> path = solver.bestPath(s);
      Alg all;
      for (const Alg &step : path) {
        all.insert(all.end(), step.begin(), step.end());
      }
      INFO(i << " " << toString(all));
      CHECK(static_cast<int>(path.size()) == tsleStepsLeft(s));
      CHECK(htmCount(simplify(all)) == solver.movesLeft(s));
      auto done = tsleStateOf(after(cube, all), slot);
      REQUIRE(done);
      CHECK(done->done());

      std::vector<TsleOption> options = solver.options(s);
      if (s.done() || s.ocll()) {
        CHECK(options.empty());
        CHECK(s.done() != solver.finish(s).has_value());
        continue;
      }
      REQUIRE(options.size() == 12);
      // Good ones first; the first is a best one; best ones are good.
      CHECK(options.front().good);
      CHECK(options.front().best);
      CHECK(options.front().total == solver.movesLeft(s));
      bool pastGood = false;
      for (const TsleOption &o : options) {
        CHECK(o.good == (o.stepsAfter == tsleStepsLeft(s) - 1));
        CHECK(o.moves == (o.auf ? 4 : 3));
        if (!o.good) {
          pastGood = true;
          CHECK_FALSE(o.best);
        } else {
          CHECK_FALSE(pastGood);
          CHECK(o.total >= solver.movesLeft(s));
        }
        CHECK(o.finish.has_value() == o.next.ocll());
      }
    }
  }
}

TEST_CASE("OCLL algs are done as written in every slot", "[tsle]") {
  for (const OcllAlg &a : defaultOcllAlgs()) {
    CHECK(ocllAlgWorks(a.id, parseAlgOrEmpty(a.alg)));
  }
  CHECK(ocllAlgWorks("sune", parseAlgOrEmpty("R U2 R' U' R U' R'")) == false);
  CHECK(ocllAlgWorks("antisune", parseAlgOrEmpty("R U2 R' U' R U' R'")));
  CHECK_FALSE(ocllAlgWorks("sune", {}));
  CHECK_FALSE(ocllAlgWorks("nothing", parseAlgOrEmpty("R U R' U R U2 R'")));

  Rng rng(2);
  for (Slot slot : kF2LSlots) {
    TsleSolver solver(slot);
    std::set<std::string> seen;
    for (TsleState c : tsleCases()) {
      if (!c.ocll()) {
        continue;
      }
      auto f = solver.finish(c);
      REQUIRE(f);
      seen.insert(f->id);
      // The alg as written, after a U turn at most.
      const std::vector<OcllAlg> &algs = defaultOcllAlgs();
      auto alg = std::find_if(algs.begin(), algs.end(),
                              [&](const OcllAlg &a) { return a.id == f->id; });
      REQUIRE(alg != algs.end());
      Alg written = parseAlgOrEmpty(alg->alg);
      Alg tail(f->alg.end() - static_cast<std::ptrdiff_t>(written.size()),
               f->alg.end());
      bool joinedTurn =
          f->alg.size() < written.size() + 1 && f->alg.size() != written.size();
      if (!joinedTurn && f->alg.size() >= written.size()) {
        CHECK(tail == written);
      }
      CubieCube cube = randomTsleCube(c, slot, rng);
      auto done = tsleStateOf(after(cube, f->alg), slot);
      REQUIRE(done);
      CHECK(done->done());
    }
    CHECK(seen.size() == 7);
  }
  // A changed alg is used when it works, and ignored when it doesn't.
  std::vector<OcllAlg> mine = defaultOcllAlgs();
  mine[0].alg = "L' U2 R U R' U2 L U' R U' R'"; // not a Sune
  mine[1].alg = "L' U' L U' L' U2 L";           // an Antisune from the left
  TsleSolver changed(Slot::FR, mine);
  for (TsleState c : tsleCases()) {
    if (!c.ocll()) {
      continue;
    }
    auto f = changed.finish(c);
    REQUIRE(f);
    if (f->id == "sune") {
      CHECK(f->moves <= 8);
    }
    if (f->id == "antisune") {
      CHECK(std::any_of(f->alg.begin(), f->alg.end(),
                        [](Move m) { return m.type == MoveType::L; }));
    }
  }
}
