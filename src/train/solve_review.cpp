#include "train/solve_review.hpp"

#include "train/case_logic.hpp"
#include "train/families.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace vcube {

namespace {

constexpr std::array<Slot, kSlots> kAllSlots = {Slot::FR, Slot::FL, Slot::BL,
                                                Slot::BR};

bool edgeSolved(const CubieCube &c, Edge e) {
  return c.ep[e] == e && c.eo[e] == 0;
}

bool cornerSolved(const CubieCube &c, Corner k) {
  return c.cp[k] == k && c.co[k] == 0;
}

// Where a piece is: its position and orientation there.
struct Place {
  int position = -1;
  int orientation = 0;
};

Place cornerPlace(const CubieCube &c, Corner piece) {
  for (std::size_t i = 0; i < kCorners; ++i) {
    if (c.cp[i] == piece) {
      return {static_cast<int>(i), c.co[i]};
    }
  }
  return {};
}

Place edgePlace(const CubieCube &c, Edge piece) {
  for (std::size_t i = 0; i < kEdges; ++i) {
    if (c.ep[i] == piece) {
      return {static_cast<int>(i), c.eo[i]};
    }
  }
  return {};
}

// The slot whose pair these pieces are: its corner, its edge or both (a
// case may have one of them solved already).
std::optional<Slot> pairSlot(const CasePieces &pieces) {
  if (pieces.corners.size() > 1 || pieces.edges.size() > 1 ||
      (pieces.corners.empty() && pieces.edges.empty())) {
    return std::nullopt;
  }
  for (Slot s : kAllSlots) {
    bool corner = pieces.corners.empty() || pieces.corners[0] == slotCorner(s);
    bool edge = pieces.edges.empty() || pieces.edges[0] == slotEdge(s);
    if (corner && edge) {
      return s;
    }
  }
  return std::nullopt;
}

// --- the cases of a reference set, by where their pair is ---------------

struct PairCase {
  std::string id;
  std::string name;
  struct Passing {
    std::string text; // as written in the set
    Alg alg;          // parsed
    int before = 0;   // U turns the case needs before it
  };
  std::vector<Passing> algs;
};

// Slot, then the pair's corner position and twist and edge position and
// flip.
using PairKey = std::array<int, 5>;

PairKey pairKey(Slot s, const CubieCube &c) {
  Place corner = cornerPlace(c, slotCorner(s));
  Place edge = edgePlace(c, slotEdge(s));
  return {static_cast<int>(s), corner.position, corner.orientation,
          edge.position, edge.orientation};
}

struct PairIndex {
  std::vector<PairCase> cases;
  std::map<PairKey, std::vector<std::size_t>> byKey;
};

// Everything that changes what the index holds.
std::size_t fingerprint(const AlgSet &set) {
  std::string blob = set.id;
  blob += set.aufBefore ? '1' : '0';
  blob += set.aufAfter ? '1' : '0';
  for (const auto &g : set.groups) {
    blob += '\x1d';
    blob += freeToString(set.specFor(g).free);
    for (const auto &c : g.cases) {
      blob += '\x1e';
      blob += c.id;
      blob += '\x1f';
      blob += c.name;
      blob += '\x1f';
      blob.append(c.state.cp.begin(), c.state.cp.end());
      blob.append(c.state.co.begin(), c.state.co.end());
      blob.append(c.state.ep.begin(), c.state.ep.end());
      blob.append(c.state.eo.begin(), c.state.eo.end());
      for (const auto &a : c.algs) {
        blob += '\x1f';
        blob += a;
      }
    }
  }
  return std::hash<std::string>{}(blob);
}

std::shared_ptr<const PairIndex> buildIndex(const AlgSet &set) {
  auto index = std::make_shared<PairIndex>();
  for (const auto &g : set.groups) {
    const PieceMask &free = set.specFor(g).free;
    for (const auto &c : g.cases) {
      auto slot = pairSlot(casePieces(c.state, free));
      if (!slot) {
        continue;
      }
      PairCase pc{c.id, c.name, {}};
      for (const auto &text : c.algs) {
        AlgCheck check =
            checkAlg(c.state, text, free, set.aufBefore, set.aufAfter);
        if (check.passes()) {
          pc.algs.push_back({text, parseAlgOrEmpty(text), check.fit->before});
        }
      }
      if (pc.algs.empty()) {
        continue;
      }
      index->byKey[pairKey(*slot, c.state)].push_back(index->cases.size());
      index->cases.push_back(std::move(pc));
    }
  }
  return index;
}

// The index of a set, kept until the set changes.
std::shared_ptr<const PairIndex> indexOf(const AlgSet &set) {
  static std::mutex mutex;
  static std::map<std::string,
                  std::pair<std::size_t, std::shared_ptr<const PairIndex>>>
      cache;
  std::size_t print = fingerprint(set);
  {
    std::lock_guard lock(mutex);
    auto it = cache.find(set.id);
    if (it != cache.end() && it->second.first == print) {
      return it->second.second;
    }
  }
  auto index = buildIndex(set);
  std::lock_guard lock(mutex);
  cache[set.id] = {print, index};
  return index;
}

// The state after an alg, done after `quarters` U turns; nullopt if it
// can't be worked out (a malformed alg).
std::optional<CubieCube> applied(const CubieCube &state, int quarters,
                                 const Alg &alg) {
  FaceletCube f = state.toFacelets();
  if (quarters % 4 != 0) {
    f.apply(Move{MoveType::U, static_cast<std::uint8_t>(quarters % 4)});
  }
  f.apply(alg);
  f.normalizeOrientation();
  return CubieCube::fromFacelets(f);
}

std::array<bool, kSlots> solvedSlots(const CubieCube &c) {
  std::array<bool, kSlots> out{};
  for (Slot s : kAllSlots) {
    out[static_cast<std::size_t>(s)] = slotSolved(c, s);
  }
  return out;
}

int count(const std::array<bool, kSlots> &slots) {
  return static_cast<int>(std::count(slots.begin(), slots.end(), true));
}

PairOption bestOption(const CubieCube &state, Slot slot,
                      const PairIndex &index) {
  PairOption best;
  best.slot = slot;
  std::array<bool, kSlots> solvedBefore = solvedSlots(state);
  int bestMoves = INT_MAX;
  for (int a = 0; a < 4; ++a) {
    auto it = index.byKey.find(pairKey(slot, state * uTurn(a)));
    if (it == index.byKey.end()) {
      continue;
    }
    for (std::size_t i : it->second) {
      const PairCase &pc = index.cases[i];
      for (const auto &p : pc.algs) {
        int total = (a + p.before) % 4;
        auto after = applied(state, total, p.alg);
        if (!after || !slotSolved(*after, slot) || !eoCrossSolved(*after)) {
          continue;
        }
        bool kept = true;
        for (Slot s : kAllSlots) {
          kept = kept && (!solvedBefore[static_cast<std::size_t>(s)] ||
                          slotSolved(*after, s));
        }
        if (!kept) {
          continue;
        }
        std::string text = withAufs(p.text, {total, 0});
        int moves = htmCount(parseAlgOrEmpty(text));
        if (moves < bestMoves) {
          bestMoves = moves;
          best.caseId = pc.id;
          best.caseName = pc.name;
          best.alg = text;
          best.moves = moves;
        }
      }
    }
  }
  return best;
}

} // namespace

std::vector<StepMark> stepMarks(const FaceletCube &start, const Alg &solution) {
  const auto &steps = solveSteps();
  std::vector<std::optional<StepMark>> found(steps.size());
  FaceletCube f = start;
  int htm = 0;
  for (std::size_t k = 0; k <= solution.size(); ++k) {
    bool all = true;
    for (std::size_t i = 0; i < steps.size(); ++i) {
      if (found[i]) {
        continue;
      }
      std::vector<Hold> holds = stepReachedHolds(f, steps[i]);
      if (holds.empty()) {
        all = false;
        continue;
      }
      found[i] = StepMark{steps[i], k, htm, holds.front()};
    }
    if (all || k == solution.size()) {
      break;
    }
    f.apply(solution[k]);
    htm += htmCount(Alg{solution[k]});
  }
  std::vector<StepMark> out;
  for (const auto &m : found) {
    if (m) {
      out.push_back(*m);
    }
  }
  return out;
}

std::string_view slotName(Slot s) {
  switch (s) {
  case Slot::FR:
    return "FR";
  case Slot::FL:
    return "FL";
  case Slot::BL:
    return "BL";
  case Slot::BR:
    return "BR";
  }
  return "";
}

Corner slotCorner(Slot s) {
  switch (s) {
  case Slot::FR:
    return DFR;
  case Slot::FL:
    return DLF;
  case Slot::BL:
    return DBL;
  case Slot::BR:
    return DRB;
  }
  return DFR;
}

Edge slotEdge(Slot s) {
  switch (s) {
  case Slot::FR:
    return FR;
  case Slot::FL:
    return FL;
  case Slot::BL:
    return BL;
  case Slot::BR:
    return BR;
  }
  return FR;
}

bool slotSolved(const CubieCube &c, Slot s) {
  return cornerSolved(c, slotCorner(s)) && edgeSolved(c, slotEdge(s));
}

bool eoCrossSolved(const CubieCube &c) {
  if (std::any_of(c.eo.begin(), c.eo.end(), [](auto o) { return o != 0; })) {
    return false;
  }
  return edgeSolved(c, DF) && edgeSolved(c, DR) && edgeSolved(c, DB) &&
         edgeSolved(c, DL);
}

std::vector<PairOption> pairOptions(const CubieCube &state,
                                    const AlgSet *reference) {
  std::shared_ptr<const PairIndex> index =
      reference ? indexOf(*reference) : nullptr;
  std::vector<PairOption> out;
  for (Slot s : kAllSlots) {
    if (slotSolved(state, s)) {
      continue;
    }
    if (index) {
      out.push_back(bestOption(state, s, *index));
    } else {
      PairOption none;
      none.slot = s;
      out.push_back(none);
    }
  }
  return out;
}

bool isPairSet(const AlgSet &set) {
  if (set.caseCount() == 0) {
    return false;
  }
  for (const auto &g : set.groups) {
    const PieceMask &free = set.specFor(g).free;
    for (const auto &c : g.cases) {
      if (!pairSlot(casePieces(c.state, free)) || !eoCrossSolved(c.state)) {
        return false;
      }
    }
  }
  return true;
}

F2LBreakdown f2lBreakdown(const FaceletCube &start, const Alg &solution,
                          const AlgSet *reference) {
  F2LBreakdown out;
  FaceletCube f = start;
  std::size_t k = 0;
  for (;; ++k) {
    std::vector<Hold> holds = stepReachedHolds(f, SolveStep::EOCross);
    if (!holds.empty()) {
      out.found = true;
      out.eoCross = k;
      // Name the slots as the cube was held for the F2L: after any
      // rotations that follow, or as it was held then, when EO Cross is
      // done in that hold.
      FaceletCube turned = f;
      for (std::size_t j = k;
           j < solution.size() && isRotation(solution[j].type); ++j) {
        turned.apply(solution[j]);
      }
      auto has = [&](std::optional<Hold> h) {
        return h && std::find(holds.begin(), holds.end(), *h) != holds.end();
      };
      out.hold = has(holdOf(turned)) ? *holdOf(turned)
                 : has(holdOf(f))    ? *holdOf(f)
                                     : holds.front();
      break;
    }
    if (k == solution.size()) {
      return out;
    }
    f.apply(solution[k]);
  }
  auto stateAt = [&](const FaceletCube &cube) {
    return heldCubie(cube, out.hold);
  };
  std::optional<CubieCube> begin = stateAt(f);
  if (!begin) {
    return out;
  }
  int best = count(solvedSlots(*begin));
  std::array<bool, kSlots> before = solvedSlots(*begin);
  std::size_t segmentBegin = k;
  CubieCube beginState = *begin;
  bool allSolved = best == kSlots;
  for (; k < solution.size(); ++k) {
    f.apply(solution[k]);
    auto c = stateAt(f);
    bool cross = c && eoCrossSolved(*c);
    std::array<bool, kSlots> now =
        cross ? solvedSlots(*c) : std::array<bool, kSlots>{};
    allSolved = cross && count(now) == kSlots;
    if (!cross || count(now) <= best) {
      continue;
    }
    PairSegment seg;
    seg.begin = segmentBegin;
    seg.end = k + 1;
    seg.htm = htmCount(
        Alg(solution.begin() + static_cast<std::ptrdiff_t>(segmentBegin),
            solution.begin() + static_cast<std::ptrdiff_t>(k + 1)));
    for (Slot s : kAllSlots) {
      auto i = static_cast<std::size_t>(s);
      if (now[i] && !before[i]) {
        seg.solved.push_back(s);
      }
    }
    seg.options = pairOptions(beginState, reference);
    out.segments.push_back(std::move(seg));
    best = count(now);
    before = now;
    segmentBegin = k + 1;
    beginState = *c;
  }
  out.complete = allSolved;
  return out;
}

PairPlan cheapestPairOrder(const CubieCube &state, const AlgSet &reference) {
  PairPlan best;
  bool haveBest = false;
  auto better = [](const PairPlan &a, const PairPlan &b) {
    // Complete plans first, then those that solve more pairs, then fewer
    // moves.
    if (a.complete != b.complete) {
      return a.complete;
    }
    if (a.steps.size() != b.steps.size()) {
      return a.steps.size() > b.steps.size();
    }
    return a.moves < b.moves;
  };
  PairPlan current;
  current.complete = true;
  std::function<void(const CubieCube &)> search = [&](const CubieCube &s) {
    std::vector<PairOption> options = pairOptions(s, &reference);
    bool went = false;
    for (const PairOption &o : options) {
      if (o.caseId.empty()) {
        continue;
      }
      auto next = applied(s, 0, parseAlgOrEmpty(o.alg));
      if (!next) {
        continue;
      }
      went = true;
      current.steps.push_back(o);
      current.moves += o.moves;
      search(*next);
      current.moves -= o.moves;
      current.steps.pop_back();
    }
    if (!went) {
      PairPlan leaf = current;
      leaf.complete = options.empty();
      if (!haveBest || better(leaf, best)) {
        best = leaf;
        haveBest = true;
      }
    }
  };
  search(state);
  return best;
}

CubieCube randomPairState(const std::vector<Slot> &open, Rng &rng) {
  PieceMask mask = PieceMask{}.lastLayer(true, true, false);
  for (Slot s : open) {
    mask.corners({slotCorner(s)}, true, true);
    mask.edges({slotEdge(s)}, true, false);
  }
  mask.normalize();
  CubieCube c;
  for (int tries = 0; tries < 100; ++tries) {
    c = randomState(mask, rng);
    bool allOpen = std::none_of(open.begin(), open.end(),
                                [&](Slot s) { return slotSolved(c, s); });
    if (allOpen) {
      break;
    }
  }
  return c;
}

CubieCube randomPairState(int count, Rng &rng) {
  std::array<Slot, kSlots> slots = kAllSlots;
  std::shuffle(slots.begin(), slots.end(), rng);
  auto n = static_cast<std::size_t>(std::clamp(count, 1, kSlots));
  std::vector<Slot> open(slots.begin(),
                         slots.begin() + static_cast<std::ptrdiff_t>(n));
  std::sort(open.begin(), open.end());
  return randomPairState(open, rng);
}

} // namespace vcube
