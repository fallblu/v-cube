#include "train/f2l.hpp"
#include "train/f2l_stats.hpp"

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
  CHECK(pairCases(true).size() == 83);
  CHECK(pairCases(false).size() == 167);
  std::set<std::string> keys;
  for (PairCase c : pairCases(false)) {
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
    for (PairCase pc : pairCases(config.eo)) {
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
  for (Slot s : kF2LSlots) {
    for (bool solved : {true, false}) {
      PairCase pc = pairCases(true)[40];
      auto setup = casePairSetup(config, pc, s, solved, rng);
      REQUIRE(setup);
      CHECK(pairCaseOf(setup->state, s) == pc);
      if (!solved) {
        CHECK(unsolvedSlots(setup->state).size() == 4);
      }
    }
  }
  // A late stage leaves fewer slots open.
  for (int i = 0; i < 10; ++i) {
    PairCase top = pairCases(true)[0];
    auto last = randomPairSetup(config, 3, top, rng);
    REQUIRE(last);
    CHECK(unsolvedSlots(last->state).size() == 2);
    auto first = randomPairSetup(config, 1, top, rng);
    REQUIRE(first);
    CHECK(unsolvedSlots(first->state).size() == 4);
  }
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
      CubieCube state = randomChoiceState(config, rng);
      REQUIRE(pairsLeft(state, config) >= 1);
      REQUIRE(unsolvedSlots(state).size() >= 2);
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

TEST_CASE("weak cases weigh more", "[f2l]") {
  CHECK(summarizeCase({}, 0).weight == kUnseenWeight);
  PairAttempt good;
  good.ms = 2000;
  PairAttempt bad = good;
  bad.excess = 3.0;
  PairAttempt hinted = good;
  hinted.hint = true;
  PairAttempt slow = good;
  slow.ms = 6000;
  CaseSummary g = summarizeCase({good, good}, 2000);
  CHECK_FALSE(g.weak);
  CHECK(g.attempts == 2);
  CHECK(summarizeCase({bad}, 2000).weight > g.weight);
  CHECK(summarizeCase({bad}, 2000).weak);
  CHECK(summarizeCase({hinted}, 2000).weight > g.weight);
  CHECK(summarizeCase({slow}, 2000).weight > g.weight);
  // Old attempts stop counting.
  std::vector<PairAttempt> improved{bad, bad};
  improved.insert(improved.end(), kRecentAttempts, good);
  CHECK(summarizeCase(improved, 2000).weight == g.weight);

  F2LStats stats;
  for (std::size_t i = 0; i < F2LStats::kPerCase + 5; ++i) {
    good.timestamp = static_cast<std::int64_t>(i);
    stats.add("k", good);
  }
  CHECK(stats.cases["k"].size() == F2LStats::kPerCase);
  CHECK(stats.cases["k"].front().timestamp == 5);
  CHECK(meanPairMs(stats) == 2000.0);

  Rng rng(1);
  std::array<int, 3> hits{};
  for (int i = 0; i < 3000; ++i) {
    ++hits[pickWeighted({1.0, 0.0, 9.0}, rng)];
  }
  CHECK(hits[1] == 0);
  CHECK(hits[2] > hits[0] * 5);
  CHECK(pickWeighted({}, rng) == 0);
}

TEST_CASE("pair searches are quick enough", "[.f2lperf]") {
  Rng rng(5);
  for (int m = 0; m < kF2LMethods; ++m) {
    F2LConfig config = configOf(static_cast<F2LMethod>(m));
    AnalysisOptions options = optionsFor(config);
    for (int stage = 1; stage <= stageCount(config); ++stage) {
      double worst = 0, sum = 0;
      std::size_t most = 0;
      int longest = 0;
      for (int i = 0; i < 12; ++i) {
        auto setup = randomPairSetup(config, stage, std::nullopt, rng);
        auto t0 = std::chrono::steady_clock::now();
        PairSearchOptions o;
        o.moves = config.moves();
        PairSearchResult r = searchPair(setup->state, setup->target, o);
        double s1 =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
                .count();
        PairAnalysis a = analysePair(setup->state, setup->target, options);
        double s =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
                .count();
        worst = std::max(worst, s);
        sum += s;
        most = std::max(most, r.algs.size());
        longest = std::max(longest, r.shortest);
        if (i == 0) {
          std::printf("   e.g. search %.3fs, %zu algs, complete %d, best %s "
                      "(%.2f)\n",
                      s1, r.algs.size(), r.complete,
                      a.best() ? a.best()->text.c_str() : "-",
                      a.best() ? a.best()->cost : 0.0);
        }
      }
      std::printf("%s stage %d: mean %.3fs worst %.3fs, most algs %zu, "
                  "longest %d\n",
                  std::string(methodName(methodOf(config))).c_str(), stage,
                  sum / 12, worst, most, longest);
    }
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 3; ++i) {
      CubieCube state = randomFullState(config, rng);
      rankPairs(state, options);
    }
    std::printf(
        "  full plan: %.3fs each\n",
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
                .count() /
            3);
  }
}
