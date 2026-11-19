#include "train/f2l.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
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

F2LConfig configOf(F2LMethod m) {
  F2LConfig c;
  applyMethod(c, m);
  return c;
}

AnalysisOptions optionsFor(const F2LConfig &config) {
  AnalysisOptions o;
  o.config = config;
  o.profile = builtinErgoProfiles().front();
  return o;
}

} // namespace

TEST_CASE("pair cases are counted and keyed", "[f2l]") {
  CHECK(pairCases(Edges::Oriented).size() == 83);
  CHECK(pairCases(Edges::Any).size() == 167);
  std::set<std::string> keys;
  for (PairCase c : pairCases(Edges::Any)) {
    CHECK(keys.insert(caseKey(c)).second);
    CHECK(caseFromKey(caseKey(c)) == c);
    CHECK_FALSE(caseName(c).empty());
  }
  CHECK_FALSE(caseFromKey("nonsense"));
  // The basic insert in each slot is one case.
  PairCase fr = pairCaseOf(cubieOf(parseAlgOrEmpty("R U' R'")), Slot::FR);
  CHECK(groupOf(fr) == PairGroup::BothTop);
  CHECK(pairCaseOf(cubieOf(parseAlgOrEmpty("L' U L")), Slot::FL) == fr);
  CHECK(pairCaseOf(cubieOf(parseAlgOrEmpty("R' U R")), Slot::BR) == fr);
  CHECK(pairCaseOf(cubieOf(parseAlgOrEmpty("L U' L'")), Slot::BL) == fr);
  CHECK(pairCaseOf(cubieOf(parseAlgOrEmpty("U2 R U' R' U")), Slot::FR) == fr);
  CHECK(slotSeenFrom(Slot::FL, Slot::BL) == Slot::BR);
  CHECK(slotSeenFrom(Slot::FR, Slot::BR) == Slot::BR);
}

TEST_CASE("the methods' F2L is done as they define it", "[f2l]") {
  F2LConfig ct = configOf(F2LMethod::ZZMinus1);
  CHECK(ct.eo);
  CHECK(methodOf(ct) == F2LMethod::ZZMinus1);
  CHECK(f2lDone(CubieCube{}, ct));
  CHECK(f2lDone(cubieOf(parseAlgOrEmpty("R U R' U'")), ct));     // FR left
  CHECK(f2lDone(cubieOf(parseAlgOrEmpty("L' U' L")), ct));       // FL left
  CHECK_FALSE(f2lDone(cubieOf(parseAlgOrEmpty("R' U' R")), ct)); // BR open
  CHECK(pairsLeft(cubieOf(parseAlgOrEmpty("R' U' R")), ct) == 1);
  CHECK(pairsLeft(cubieOf(parseAlgOrEmpty("R U R' L' U L")), ct) == 1);
  F2LConfig op = configOf(F2LMethod::ZZ);
  CHECK_FALSE(f2lDone(cubieOf(parseAlgOrEmpty("R U R' U'")), op));
  // EO must hold with EO on, and needn't without.
  CubieCube flipped = cubieOf(parseAlgOrEmpty("F R U R' U' F'"));
  CHECK_FALSE(f2lDone(flipped, op));
  CHECK(f2lDone(flipped, configOf(F2LMethod::CFOP)));
  CHECK(stageCount(ct) == 3);
  CHECK(stageCount(op) == 4);
  for (int m = 0; m < kF2LMethods; ++m) {
    auto method = static_cast<F2LMethod>(m);
    CHECK(methodFromId(methodId(method)) == method);
    CHECK(methodOf(configOf(method)) == method);
  }
}

TEST_CASE("pair solutions solve the pair and keep the rest", "[f2l]") {
  Rng rng(7);
  for (int m = 0; m < kF2LMethods; ++m) {
    F2LConfig config = configOf(static_cast<F2LMethod>(m));
    for (int i = 0; i < 6; ++i) {
      auto setup = randomPairSetup(config, 0, std::nullopt, rng);
      REQUIRE(setup);
      REQUIRE(f2lBase(setup->state, config));
      PairSearchOptions o;
      o.moves = config.moves();
      o.maxResults = 300;
      PairSearchResult r = searchPair(setup->state, setup->target, o);
      REQUIRE(r.shortest > 0);
      REQUIRE_FALSE(r.algs.empty());
      std::set<std::string> seen;
      for (const Alg &alg : r.algs) {
        INFO(toString(alg));
        CHECK(static_cast<int>(alg.size()) >= r.shortest);
        CHECK(static_cast<int>(alg.size()) <= r.shortest + o.extra);
        CHECK(seen.insert(toString(alg)).second);
        CubieCube done = after(setup->state, alg);
        CHECK(f2lBase(done, config));
        CHECK(slotSolved(done, setup->target));
        for (Slot s : kF2LSlots) {
          if (slotSolved(setup->state, s)) {
            CHECK(slotSolved(done, s));
          }
        }
        // No slices or rotations, and wide turns only without EO.
        for (Move mv : alg) {
          auto t = static_cast<int>(mv.type);
          CHECK(t < static_cast<int>(MoveType::M));
          if (config.eo) {
            CHECK(isFaceTurn(mv.type));
          }
        }
      }
      CHECK(static_cast<int>(r.algs.front().size()) == r.shortest);
    }
  }
}

TEST_CASE("a pair's shortest solution is found", "[f2l]") {
  CubieCube c = cubieOf(parseAlgOrEmpty("R U' R'"));
  PairSearchResult r = searchPair(c, Slot::FR);
  CHECK(r.shortest == 3);
  auto has = [&r](const char *text) {
    return std::find(r.algs.begin(), r.algs.end(), parseAlgOrEmpty(text)) !=
           r.algs.end();
  };
  CHECK(has("R U R'"));
  // Nothing to do for a solved slot, and no cross, no search.
  CHECK(searchPair(CubieCube{}, Slot::FR).algs.empty());
  CHECK(searchPair(cubieOf(parseAlgOrEmpty("F")), Slot::FR).algs.empty());
  // Without D and F2/B2 only R, U and L turn.
  PairSearchOptions rul;
  rul.moves.d = false;
  rul.moves.halfFB = false;
  for (const Alg &alg : searchPair(c, Slot::FR, rul).algs) {
    for (Move m : alg) {
      CHECK((m.type == MoveType::R || m.type == MoveType::U ||
             m.type == MoveType::L));
    }
  }
}

TEST_CASE("every case can be set up in every method", "[f2l]") {
  Rng rng(11);
  for (F2LMethod m : {F2LMethod::ZZMinus1, F2LMethod::CFOP}) {
    F2LConfig config = configOf(m);
    for (PairCase pc : pairCases(config.edges())) {
      INFO(caseKey(pc));
      auto setup = randomPairSetup(config, 0, pc, rng);
      REQUIRE(setup);
      CHECK(setup->pcase == pc);
      CHECK(pairCaseOf(setup->state, setup->target) == pc);
      CHECK(f2lBase(setup->state, config));
      CHECK_FALSE(slotSolved(setup->state, setup->target));
    }
  }
  F2LConfig config;
  // A late stage leaves fewer slots open.
  for (int i = 0; i < 10; ++i) {
    PairCase top = pairCases(Edges::Oriented)[0];
    auto last = randomPairSetup(config, 3, top, rng);
    REQUIRE(last);
    CHECK(unsolvedSlots(last->state).size() == 2);
    auto first = randomPairSetup(config, 1, top, rng);
    REQUIRE(first);
    CHECK(unsolvedSlots(first->state).size() == 4);
  }
}

TEST_CASE("cards are set up for their side's slots", "[f2l]") {
  Rng rng(5);
  F2LConfig config;
  for (PairCase pc : pairCases(Edges::Oriented)) {
    for (PairSide side : {PairSide::Front, PairSide::Back}) {
      const PairCard card{side, pc};
      INFO(cardKey(card));
      CHECK(cardFromKey(cardKey(card)) == card);
      // The state solutions are chosen for: the side's own slot.
      std::optional<CubieCube> own = cardState(card, Edges::Oriented, rng);
      REQUIRE(own);
      CHECK(pairCardOf(*own, sideSlot(side)) == card);
      CHECK(f2lBase(*own, config));
      // Practised in either of the side's slots, the others solved but
      // for the pieces' own and (perhaps) the last slot.
      for (bool leaveOpen : {false, true}) {
        std::optional<PairSetup> setup =
            cardSetup(card, config, leaveOpen, rng);
        REQUIRE(setup);
        CHECK(sideOf(setup->target) == side);
        CHECK(pairCardOf(setup->state, setup->target) == card);
        const std::size_t open = unsolvedSlots(setup->state).size();
        const std::size_t needed =
            1 + (pc.cornerSlot() && *pc.cornerSlot() != Slot::FR ? 1 : 0) +
            (pc.edgeSlot() && *pc.edgeSlot() != Slot::FR &&
                     pc.edgeSlot() != pc.cornerSlot()
                 ? 1
                 : 0);
        CHECK(open >= needed);
        CHECK(open <= needed + (leaveOpen ? 1 : 0));
      }
    }
  }
}

TEST_CASE("a chosen solution is known done in either slot", "[f2l]") {
  Rng rng(9);
  F2LConfig config;
  AnalysisOptions options = optionsFor(config);
  options.keep = 3;
  int checked = 0;
  for (std::size_t i = 0; i < pairCases(Edges::Oriented).size(); i += 9) {
    for (PairSide side : {PairSide::Front, PairSide::Back}) {
      const PairCard card{side, pairCases(Edges::Oriented)[i]};
      INFO(cardKey(card));
      std::optional<CubieCube> own = cardState(card, Edges::Oriented, rng);
      REQUIRE(own);
      PairAnalysis a = analysePair(*own, sideSlot(side), options);
      REQUIRE(a.found());
      const std::vector<Alg> chosen{a.solutions.front().alg};
      for (int n = 0; n < 4; ++n) {
        std::optional<PairSetup> setup = cardSetup(card, config, false, rng);
        REQUIRE(setup);
        // Done as it solves the state there: mirrored for the side's other
        // slot, from the U turn the state needs.
        std::optional<Alg> here =
            chosenFor(setup->state, setup->target, chosen.front(), config);
        REQUIRE(here);
        CubieCube done = after(setup->state, *here);
        CHECK(slotSolved(done, setup->target));
        CHECK(matchChosen(*here, side, setup->target, chosen) == 0u);
        // A U turn more at the end, or the cube turned first, is the same.
        Alg more = *here;
        more.push_back({MoveType::U, 2});
        CHECK(matchChosen(more, side, setup->target, chosen) == 0u);
        // y2 then the moves as seen after it: the same turns of the cube.
        Alg seen{Move{MoveType::Y, 2}};
        for (Move m : *here) {
          Move t = m;
          switch (m.type) {
          case MoveType::R:
            t.type = MoveType::L;
            break;
          case MoveType::L:
            t.type = MoveType::R;
            break;
          case MoveType::F:
            t.type = MoveType::B;
            break;
          case MoveType::B:
            t.type = MoveType::F;
            break;
          default:
            break;
          }
          seen.push_back(t);
        }
        CHECK(matchChosen(seen, side, setup->target, chosen) == 0u);
        ++checked;
      }
      // Something else isn't.
      const Alg other = parseAlgOrEmpty("D2 R U2 R'");
      if (comparableSolution(other) != comparableSolution(chosen.front())) {
        CHECK_FALSE(matchChosen(other, side, sideSlot(side), chosen));
      }
    }
  }
  CHECK(checked > 0);
}

TEST_CASE("solutions compare without their AUF", "[f2l]") {
  auto same = [](const char *a, const char *b) {
    return comparableSolution(parseAlgOrEmpty(a)) ==
           comparableSolution(parseAlgOrEmpty(b));
  };
  CHECK(same("U R U' R'", "R U' R'"));
  CHECK(same("R U' R' U2", "R U' R'"));
  // A U turn beside a D turn, on either side of it, is still the AUF.
  CHECK(same("D U R U' R' D'", "D R U' R' D'"));
  CHECK(same("U D R U' R' D'", "D R U' R' D'"));
  CHECK(same("D R U' R' D' U", "D R U' R' D'"));
  CHECK(same("D R U' R' U D'", "D R U' R' D'"));
  CHECK(same("U' D U2 R U R'", "D R U R'"));
  CHECK_FALSE(same("D R U' R' D'", "R U' R'"));
  CHECK_FALSE(same("D2 R U' R'", "R U' R'"));
}

TEST_CASE("the flat view's blind spots", "[f2l]") {
  CHECK(flatStickers(true, URF) == 3);
  CHECK(flatStickers(true, ULB) == 2);
  CHECK(flatStickers(true, DFR) == 2);
  CHECK(flatStickers(true, DBL) == 0);
  CHECK(flatStickers(false, UB) == 1);
  CHECK(flatStickers(false, UR) == 2);
  CHECK(flatStickers(false, FR) == 2);
  CHECK(flatStickers(false, BL) == 0);
  // The back-left pair in its slot can't be seen; the front-right one can.
  CHECK(hiddenCost(CubieCube{}, Slot::BL) == 2 * kHidden);
  CHECK(hiddenCost(CubieCube{}, Slot::FR) == 0.0);
}

TEST_CASE("pairs are ranked by the cheapest way on", "[f2l]") {
  Rng rng(3);
  for (F2LMethod m : {F2LMethod::ZZMinus1, F2LMethod::ZZ}) {
    F2LConfig config = configOf(m);
    AnalysisOptions options = optionsFor(config);
    for (int i = 0; i < 3; ++i) {
      CubieCube state = randomFullState(config, rng);
      REQUIRE(pairsLeft(state, config) >= 3);
      REQUIRE(unsolvedSlots(state).size() == 4);
      std::vector<PairRank> ranks = rankPairs(state, options);
      REQUIRE(ranks.size() == unsolvedSlots(state).size());
      for (std::size_t k = 0; k < ranks.size(); ++k) {
        const PairRank &r = ranks[k];
        if (k > 0) {
          CHECK(ranks[k - 1].total <= r.total);
        }
        REQUIRE(r.pair.best());
        CHECK(r.order.front() == r.pair.slot);
        CHECK(r.total ==
              r.pair.best()->cost + r.rest + hiddenCost(state, r.pair.slot));
        // Solutions are cheapest first and solve the pair.
        for (std::size_t s = 0; s < r.pair.solutions.size(); ++s) {
          const RankedSolution &sol = r.pair.solutions[s];
          CHECK(slotSolved(after(state, sol.alg), r.pair.slot));
          if (s > 0) {
            CHECK(r.pair.solutions[s - 1].cost <= sol.cost);
          }
          CHECK(sol.htm <= r.pair.shortest + options.extra);
        }
      }
    }
  }
}

TEST_CASE("a solve is split into its pairs", "[f2l]") {
  F2LConfig config = configOf(F2LMethod::ZZ);
  // The rotation is followed: after y the front-right slot is solved as
  // the front-left one would be.
  Alg moves = parseAlgOrEmpty("L' U' L U y F U F'");
  CubieCube base = cubieOf(parseAlgOrEmpty("R U' R' U' L' U L"));
  std::vector<F2LSegment> segs = f2lSegments(base, moves, config);
  REQUIRE(segs.size() == 2);
  CHECK(segs[0].begin == 0);
  CHECK(segs[0].end == 3);
  CHECK(segs[0].solved == std::vector<Slot>{Slot::FL});
  CHECK(segs[0].before == base);
  CHECK(segs[1].begin == 3);
  CHECK(segs[1].end == moves.size());
  CHECK(segs[1].solved == std::vector<Slot>{Slot::FR});
}
