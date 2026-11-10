#include "cube/hold.hpp"
#include "train/case_logic.hpp"
#include "train/solve_review.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>

using namespace vcube;

namespace {

// Pair cases for each slot: the FR algs and their mirrors.
const std::vector<std::string> kFrAlgs = {"R U' R'", "R U' R' U R U R'",
                                          "R U2 R' U R U' R'"};

Alg slotAlg(const std::string &frAlg, Slot slot) {
  Alg alg = parseAlgOrEmpty(frAlg);
  if (slot == Slot::FL || slot == Slot::BL) {
    alg = mirrorM(alg);
  }
  if (slot == Slot::BR || slot == Slot::BL) {
    alg = mirrorS(alg);
  }
  return alg;
}

AlgSet pairSet() {
  AlgSet set;
  set.id = "review-pairs";
  set.spec.free = *parseFree("LL:p LLC:o");
  for (Slot s : {Slot::FR, Slot::FL, Slot::BR, Slot::BL}) {
    std::string name(slotName(s));
    CaseGroup g{name, std::nullopt, {}};
    for (std::size_t i = 0; i < kFrAlgs.size(); ++i) {
      Alg alg = slotAlg(kFrAlgs[i], s);
      AlgCase c;
      c.id = name + "-" + std::to_string(i + 1);
      c.name = name + " " + std::to_string(i + 1);
      c.state = stateSolvedBy(alg);
      c.algs.push_back(toString(alg));
      g.cases.push_back(c);
    }
    set.groups.push_back(g);
  }
  return set;
}

FaceletCube after(const std::string &alg) {
  FaceletCube f;
  f.apply(parseAlgOrEmpty(alg));
  return f;
}

CubieCube cubieAfter(const Alg &alg) {
  CubieCube c;
  c.apply(alg);
  return c;
}

} // namespace

TEST_CASE("a cube is seen in any hold, however it was turned", "[review]") {
  FaceletCube f;
  f.apply(parseAlgOrEmpty("x2 y"));
  REQUIRE(holdOf(f));
  CHECK(*holdOf(f) == holdAfter(parseAlgOrEmpty("x2 y")));
  CHECK(holdOf(FaceletCube()) == kWcaHold);

  Alg scramble = parseAlgOrEmpty("R U F' L2 D B");
  CubieCube scrambled = cubieAfter(scramble);
  for (Hold h : allHolds()) {
    FaceletCube g;
    g.apply(scramble);
    CHECK(heldCubie(g, h) == solverState(scrambled, h));
    g.apply(parseAlgOrEmpty("z y'")); // turning first changes nothing
    CHECK(heldCubie(g, h) == solverState(scrambled, h));
  }
  CHECK(heldCubie(FaceletCube(), Hold{0, 3}) == std::nullopt);
}

TEST_CASE("steps are reached in the holds that show them", "[review]") {
  FaceletCube solved;
  CHECK(stepReachedHolds(solved, SolveStep::EOCross).size() == 24);
  CHECK(stepReachedHolds(solved, SolveStep::EO).size() == 24);
  // R U R': yellow cross and EO for F/B only.
  FaceletCube f = after("R U R'");
  std::vector<Hold> holds = stepReachedHolds(f, SolveStep::EOCross);
  CHECK(holds == std::vector<Hold>{{0, 2}, {0, 5}});
  CHECK(stepReachedHolds(f, SolveStep::ZZF2L).empty());
}

TEST_CASE("step marks are where a solve first did each step", "[review]") {
  FaceletCube start = after("R U R' F");
  Alg solution = parseAlgOrEmpty("F' R U' R'");
  std::vector<StepMark> marks = stepMarks(start, solution);
  auto markOf = [&](SolveStep step) -> const StepMark * {
    for (const StepMark &m : marks) {
      if (m.step == step) {
        return &m;
      }
    }
    return nullptr;
  };
  REQUIRE(marks.size() == solveSteps().size());
  // In the order of solveSteps().
  for (std::size_t i = 0; i < marks.size(); ++i) {
    CHECK(marks[i].step == solveSteps()[i]);
  }
  for (SolveStep s : {SolveStep::EO, SolveStep::EOPlus1, SolveStep::EOPlus2,
                      SolveStep::EOLine, SolveStep::EOCross}) {
    REQUIRE(markOf(s));
    CHECK(markOf(s)->moves == 1);
    CHECK(markOf(s)->htm == 1);
  }
  CHECK(markOf(SolveStep::EOCross)->hold == Hold{0, 2});
  // After F' R the cube is one R turn from solved: ZZ F2L on the orange
  // side, the L layer and M slice, with EO for any other axis.
  const StepMark *zz = markOf(SolveStep::ZZF2L);
  REQUIRE(zz);
  CHECK(zz->moves == 3);
  CHECK(zz->htm == 3);
  CHECK(zz->hold.top == 1);

  // Rotations are free.
  marks = stepMarks(after("R U R' F"), parseAlgOrEmpty("y2 y2 F'"));
  REQUIRE(markOf(SolveStep::EOCross));
  CHECK(markOf(SolveStep::EOCross)->moves == 3);
  CHECK(markOf(SolveStep::EOCross)->htm == 1);

  // Counted simplified, or as done.
  marks = stepMarks(after("R U R' F R L' R'"), parseAlgOrEmpty("R L R' F'"));
  REQUIRE(markOf(SolveStep::EOCross));
  CHECK(markOf(SolveStep::EOCross)->moves == 4);
  CHECK(markOf(SolveStep::EOCross)->htm == 2); // L F'
  marks =
      stepMarks(after("R U R' F R L' R'"), parseAlgOrEmpty("R L R' F'"), false);
  CHECK(markOf(SolveStep::EOCross)->htm == 4);

  marks = stepMarks(FaceletCube(), {});
  CHECK(marks.size() == solveSteps().size());
  CHECK(std::all_of(marks.begin(), marks.end(),
                    [](const StepMark &m) { return m.moves == 0; }));
}

TEST_CASE("slot and EO Cross checks", "[review]") {
  CubieCube c = cubieAfter(parseAlgOrEmpty("R U R'"));
  CHECK(eoCrossSolved(c));
  CHECK_FALSE(slotSolved(c, Slot::FR));
  CHECK(slotSolved(c, Slot::FL));
  CHECK(slotSolved(c, Slot::BL));
  CHECK(slotSolved(c, Slot::BR));
  CHECK_FALSE(eoCrossSolved(cubieAfter(parseAlgOrEmpty("F"))));
  CHECK(slotName(Slot::BL) == "BL");
  CHECK(slotCorner(Slot::BR) == DRB);
  CHECK(slotEdge(Slot::FL) == FL);
}

TEST_CASE("pair options find the set's case and alg", "[review]") {
  AlgSet set = pairSet();
  REQUIRE(isPairSet(set));
  Alg alg = slotAlg(kFrAlgs[1], Slot::FR);
  CubieCube state = cubieAfter(invert(alg));
  std::vector<PairOption> options = pairOptions(state, &set);
  REQUIRE(options.size() == 1);
  CHECK(options[0].slot == Slot::FR);
  CHECK(options[0].caseId == "FR-2");
  CHECK(options[0].moves == 7);

  // Seen from another side, it needs a U turn first, which counts.
  CubieCube turned = state * uTurn(1);
  options = pairOptions(turned, &set);
  REQUIRE(options.size() == 1);
  CHECK(options[0].caseId == "FR-2");
  CHECK(options[0].moves == 8);
  CHECK(options[0].alg.starts_with("(U')"));
  // The alg it gives does solve the pair.
  FaceletCube f = turned.toFacelets();
  f.apply(parseAlgOrEmpty(options[0].alg));
  auto solved = CubieCube::fromFacelets(f);
  REQUIRE(solved);
  CHECK(slotSolved(*solved, Slot::FR));

  // Without a set, the open slots are listed without cases.
  options = pairOptions(state, nullptr);
  REQUIRE(options.size() == 1);
  CHECK(options[0].caseId.empty());
  CHECK(options[0].moves == 0);
}

TEST_CASE("a set written for one slot covers the others with mirrors",
          "[review]") {
  // Only the front-right cases, mirrored to the other three slots.
  AlgSet set = pairSet();
  set.id = "review-fr-only";
  set.groups.resize(1);
  REQUIRE(set.groups[0].name == "FR");
  Alg fl = slotAlg(kFrAlgs[1], Slot::FL);
  CubieCube state = cubieAfter(invert(fl));
  std::vector<PairOption> options = pairOptions(state, &set);
  REQUIRE(options.size() == 1);
  CHECK(options[0].caseId.empty()); // not without mirrors
  set.mirrors = {Mirror{true, false}, Mirror{false, true}, Mirror{true, true}};
  options = pairOptions(state, &set);
  REQUIRE(options.size() == 1);
  CHECK(options[0].slot == Slot::FL);
  CHECK(options[0].caseId == "FR-2");
  CHECK(options[0].moves == 7);
  FaceletCube f = state.toFacelets();
  f.apply(parseAlgOrEmpty(options[0].alg));
  auto solved = CubieCube::fromFacelets(f);
  REQUIRE(solved);
  CHECK(slotSolved(*solved, Slot::FL));
  // And the back slots.
  Alg bl = slotAlg(kFrAlgs[0], Slot::BL);
  options = pairOptions(cubieAfter(invert(bl)), &set);
  REQUIRE(options.size() == 1);
  CHECK(options[0].caseId == "FR-1");
}

TEST_CASE("the optimal pair keeps the solved slots", "[review]") {
  Alg fr = slotAlg(kFrAlgs[1], Slot::FR);
  CubieCube state = cubieAfter(invert(fr));
  auto alg = optimalPair(state, Slot::FR);
  REQUIRE(alg);
  CHECK(htmCount(*alg) <= 7);
  CubieCube done = state;
  done.apply(*alg);
  for (Slot s : {Slot::FR, Slot::FL, Slot::BL, Slot::BR}) {
    CHECK(slotSolved(done, s));
  }
  CHECK(eoCrossSolved(done));
}

TEST_CASE("a whole solve is split into cross, pairs and last layer",
          "[review]") {
  // EO Cross done by the scramble's inverse in two moves, then two pairs
  // and a last layer alg.
  Alg fr = slotAlg(kFrAlgs[1], Slot::FR);
  Alg fl = slotAlg(kFrAlgs[0], Slot::FL);
  Alg ll = parseAlgOrEmpty("R U R' U R U2 R'"); // keeps the F2L
  Alg solution = parseAlgOrEmpty("D R2");
  solution.insert(solution.end(), fl.begin(), fl.end());
  solution.insert(solution.end(), fr.begin(), fr.end());
  solution.insert(solution.end(), ll.begin(), ll.end());
  FaceletCube start;
  start.apply(invert(solution));
  std::vector<std::int64_t> times;
  for (std::size_t i = 0; i < solution.size(); ++i) {
    times.push_back(static_cast<std::int64_t>(i + 1) * 100);
  }
  SolveSplits s = solveSplits(start, solution, times, 2000);
  CHECK(s.zz);
  REQUIRE(s.phases.size() == 4);
  CHECK(s.phases[0].name == "EO Cross");
  CHECK(s.phases[0].end == 2);
  CHECK(s.phases[0].htm == 2);
  CHECK(s.phases[0].ms == 200);
  CHECK(s.phases[1].name == "FL");
  CHECK(s.phases[1].htm == 3);
  CHECK(s.phases[1].ms == 300);
  CHECK(s.phases[2].name == "FR");
  CHECK(s.phases[3].kind == SolvePhase::Kind::LastLayer);
  CHECK(s.phases[3].htm == 7);
  CHECK(s.phases[3].ms == 2000 - 1200);
  CHECK(s.crossMs == 200);
  CHECK(s.f2lMs == 1000);
  CHECK(s.llMs == 800);
  CHECK(s.f2lHtm == 10);
  CHECK(tps(10, 1000) == 10.0);
  CHECK(tps(10, 0) == 0.0);

  // Without times: the phases, but no times.
  s = solveSplits(start, solution, {}, 2000);
  REQUIRE(s.phases.size() == 4);
  CHECK_FALSE(s.crossMs);
  CHECK(s.phases[1].ms == 0);

  std::string text = reconstruction(invert(solution), solution, s);
  CHECK(text.find("// scramble") != std::string::npos);
  CHECK(text.find("D R2 // EO Cross (2 moves)") != std::string::npos);
  CHECK(text.find("// LL (7 moves)") != std::string::npos);
  std::string url = reconstructionUrl(invert(solution), solution, s, "#1");
  CHECK(url.starts_with("https://alg.cubing.net/?setup="));
  CHECK(url.find("R2_D-") != std::string::npos);

  // A solve that never gets a cross has no phases.
  CHECK(solveSplits(after("R U F D L B R2 F'"), parseAlgOrEmpty("U"), {}, 0)
            .phases.empty());
}

TEST_CASE("a pair set is one slot per case with EO Cross done", "[review]") {
  AlgSet set = pairSet();
  CHECK(isPairSet(set));
  set.groups[0].cases[0].state =
      stateSolvedBy(parseAlgOrEmpty("R U R' U' R' F R F'"));
  CHECK_FALSE(isPairSet(set));
  CHECK_FALSE(isPairSet(AlgSet{}));
}

TEST_CASE("the cheapest order solves every pair", "[review]") {
  AlgSet set = pairSet();
  Alg fr = slotAlg(kFrAlgs[1], Slot::FR); // 7 moves
  Alg fl = slotAlg(kFrAlgs[0], Slot::FL); // 3 moves
  // FR taken out first, then FL: FL comes back with its alg, then FR.
  Alg setup = invert(fr);
  Alg flOut = invert(fl);
  setup.insert(setup.end(), flOut.begin(), flOut.end());
  CubieCube state = cubieAfter(setup);
  PairPlan plan = cheapestPairOrder(state, set);
  CHECK(plan.complete);
  REQUIRE(plan.steps.size() == 2);
  CHECK(plan.moves <= 10);
  std::vector<Slot> slots = {plan.steps[0].slot, plan.steps[1].slot};
  std::sort(slots.begin(), slots.end());
  CHECK(slots == std::vector<Slot>{Slot::FR, Slot::FL});

  // Nothing open: an empty, complete plan.
  plan = cheapestPairOrder(CubieCube{}, set);
  CHECK(plan.complete);
  CHECK(plan.steps.empty());
  CHECK(plan.moves == 0);
}

TEST_CASE("a solve's F2L is split into its pairs", "[review]") {
  AlgSet set = pairSet();
  Alg fr = slotAlg(kFrAlgs[1], Slot::FR);
  Alg fl = slotAlg(kFrAlgs[0], Slot::FL);
  FaceletCube start;
  start.apply(invert(fr));
  start.apply(invert(fl));
  Alg solution = fl;
  solution.insert(solution.end(), fr.begin(), fr.end());

  F2LBreakdown b = f2lBreakdown(start, solution, &set);
  REQUIRE(b.found);
  CHECK(b.eoCross == 0);
  CHECK(b.hold == kWcaHold);
  CHECK(b.complete);
  REQUIRE(b.segments.size() == 2);
  CHECK(b.segments[0].begin == 0);
  CHECK(b.segments[0].end == 3);
  CHECK(b.segments[0].htm == 3);
  CHECK(b.segments[0].solved == std::vector<Slot>{Slot::FL});
  REQUIRE(b.segments[0].options.size() == 2);
  CHECK(b.segments[0].options[1].slot == Slot::FL);
  CHECK(b.segments[0].options[1].caseId == "FL-1");
  CHECK(b.segments[1].begin == 3);
  CHECK(b.segments[1].end == 10);
  CHECK(b.segments[1].htm == 7);
  CHECK(b.segments[1].solved == std::vector<Slot>{Slot::FR});
  REQUIRE(b.segments[1].options.size() == 1);
  CHECK(b.segments[1].options[0].caseId == "FR-2");

  // Held another way (white on the bottom): the slots are named as held.
  FaceletCube s2;
  s2.apply(parseAlgOrEmpty("x2"));
  s2.apply(invert(fr));
  s2.apply(invert(fl));
  b = f2lBreakdown(s2, solution, &set);
  REQUIRE(b.found);
  CHECK(b.hold == holdAfter(parseAlgOrEmpty("x2")));
  REQUIRE(b.segments.size() == 2);
  CHECK(b.segments[0].solved == std::vector<Slot>{Slot::FL});
  CHECK(b.complete);

  // Turned to the hold first: the slots are named as held after turning.
  FaceletCube s3;
  s3.apply(parseAlgOrEmpty("x2"));
  s3.apply(invert(fr));
  s3.apply(invert(fl));
  s3.apply(parseAlgOrEmpty("x2"));
  Alg turnedFirst = parseAlgOrEmpty("x2");
  turnedFirst.insert(turnedFirst.end(), solution.begin(), solution.end());
  b = f2lBreakdown(s3, turnedFirst, &set);
  REQUIRE(b.found);
  CHECK(b.eoCross == 0);
  CHECK(b.hold == holdAfter(parseAlgOrEmpty("x2")));
  REQUIRE(b.segments.size() == 2);
  CHECK(b.segments[0].solved == std::vector<Slot>{Slot::FL});
  CHECK(b.segments[0].htm == 3);
  CHECK(b.segments[1].solved == std::vector<Slot>{Slot::FR});

  // A solve that never gets EO Cross.
  b = f2lBreakdown(after("R U F D L B R2 F'"), parseAlgOrEmpty("U"), &set);
  CHECK_FALSE(b.found);
  CHECK(b.segments.empty());
}

TEST_CASE("random pair states have EO Cross and the open slots open",
          "[review]") {
  Rng rng(11);
  for (int count = 1; count <= 4; ++count) {
    for (int i = 0; i < 20; ++i) {
      CubieCube c = randomPairState(count, rng);
      CHECK(c.isValid());
      CHECK(eoCrossSolved(c));
      int open = 0;
      for (Slot s : {Slot::FR, Slot::FL, Slot::BL, Slot::BR}) {
        open += slotSolved(c, s) ? 0 : 1;
      }
      CHECK(open == count);
      CHECK(pairOptions(c, nullptr).size() == static_cast<std::size_t>(count));
    }
  }
  CubieCube c = randomPairState({Slot::BL}, rng);
  CHECK_FALSE(slotSolved(c, Slot::BL));
  CHECK(slotSolved(c, Slot::FR));
}

TEST_CASE("pair options are quick on a full slot set", "[review]") {
  AlgSet set = pairSet();
  Rng rng(3);
  auto begin = std::chrono::steady_clock::now();
  for (int i = 0; i < 20; ++i) {
    (void)cheapestPairOrder(randomPairState(4, rng), set);
  }
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - begin)
                .count();
  CHECK(ms < 2000);
}
