#include "train/reduction.hpp"

#include "train/families.hpp"

#include <algorithm>

namespace vcube {

namespace {

// More steps than any chain of cases is followed for.
constexpr int kMaxSteps = 32;

// The U turn (in quarters) before `cube` that makes it the state `inverse`
// undoes, up to `free` pieces and a U turn after it.
std::optional<int> lineUp(const AlgSet &set, const CubieCube &cube,
                          const CubieCube &inverse, const PieceMask &free) {
  for (int u : kAufOrder) {
    if (u != 0 && !set.aufBefore) {
      continue;
    }
    CubieCube turned = cube * uTurn(u) * inverse;
    for (int v : kAufOrder) {
      if (v != 0 && !set.aufAfter) {
        continue;
      }
      if (withinMask(uTurn(v) * turned, free)) {
        return u;
      }
    }
  }
  return std::nullopt;
}

std::optional<CaseView> directView(const AlgSet &set, const CaseGroup &group,
                                   const AlgCase &c, const CubieCube &cube,
                                   Mirror world) {
  const PieceMask &free = set.specFor(group).free;
  if (!world.none() && !slotIndependent(c.state, free, world)) {
    return std::nullopt;
  }
  if (auto u = lineUp(set, cube, c.state.inverse(), mirrorMask(free, world))) {
    return CaseView{c.id, false, *u};
  }
  return std::nullopt;
}

// `mirrored` is the cube mirrored back into the set's own frame.
std::optional<CaseView> mirroredView(const AlgSet &set, const CaseGroup &group,
                                     const AlgCase &c,
                                     const CubieCube &mirrored) {
  if (auto u =
          lineUp(set, mirrored, c.state.inverse(), set.specFor(group).free)) {
    return CaseView{c.id, true, *u};
  }
  return std::nullopt;
}

} // namespace

bool slotIndependent(const CubieCube &state, const PieceMask &free,
                     Mirror world) {
  if (world.none()) {
    return true;
  }
  CasePieces pieces = casePieces(state, free);
  if (!pieces.corners.empty() || !pieces.edges.empty()) {
    return false;
  }
  PieceMask other = mirrorMask(free, world);
  for (std::size_t i = 0; i < kCorners; ++i) {
    bool differs = free.cp[i] != other.cp[i] || free.co[i] != other.co[i];
    if (differs && (state.cp[i] != i || state.co[i] != 0)) {
      return false;
    }
  }
  for (std::size_t i = 0; i < kEdges; ++i) {
    bool differs = free.ep[i] != other.ep[i] || free.eo[i] != other.eo[i];
    if (differs && (state.ep[i] != i || state.eo[i] != 0)) {
      return false;
    }
  }
  return true;
}

std::optional<CaseView> viewOfCase(const AlgSet &set, const CaseGroup &group,
                                   const AlgCase &c, const CubieCube &cube,
                                   Mirror world) {
  if (auto view = directView(set, group, c, cube, world)) {
    return view;
  }
  if (world.none()) {
    return std::nullopt;
  }
  return mirroredView(set, group, c, mirrorState(cube, world));
}

std::optional<CaseView> seenCase(const AlgSet &set, const CubieCube &cube,
                                 Mirror world) {
  for (const auto &g : set.groups) {
    for (const auto &c : g.cases) {
      if (auto view = directView(set, g, c, cube, world)) {
        return view;
      }
    }
  }
  if (world.none()) {
    return std::nullopt;
  }
  CubieCube mirrored = mirrorState(cube, world);
  for (const auto &g : set.groups) {
    for (const auto &c : g.cases) {
      if (auto view = mirroredView(set, g, c, mirrored)) {
        return view;
      }
    }
  }
  return std::nullopt;
}

CubieCube setUpCase(const CubieCube &state, const PieceMask &free, Mirror world,
                    FreePieces pieces, Rng &rng) {
  const bool scramble = pieces == FreePieces::Scrambled;
  if (slotIndependent(state, free, world)) {
    return scramble ? randomState(mirrorMask(free, world), rng) * state : state;
  }
  return mirrorState(scramble ? randomState(free, rng) * state : state, world);
}

std::vector<ViewAlg> algsFor(const AlgSet &set, const CaseGroup &group,
                             const AlgCase &c, const CaseAnalysis &analysis,
                             const CaseView &view, const CubieCube &cube,
                             Mirror world) {
  // The algs are worked out in the frame they are written in.
  Mirror flip = view.mirrored ? world : Mirror{};
  CubieCube canonical = mirrorState(cube, flip);
  PieceMask free = set.specFor(group).free;
  if (!view.mirrored) {
    free = mirrorMask(free, world);
  }
  std::vector<ViewAlg> out;
  for (std::size_t i : analysis.passing) {
    if (i >= c.algs.size() || i >= analysis.checks.size()) {
      continue;
    }
    ViewAlg alg;
    alg.reducesTo = analysis.checks[i].reducesTo;
    if (!alg.reducesTo.empty()) {
      // A step is written from the case as it is stored.
      alg.text = withAufBefore(c.algs[i], view.before);
    } else {
      // The main alg is kept as it is done from the case as drawn; the
      // others as they are written.
      const std::string &text =
          out.empty() && analysis.main == i ? analysis.mainAlg : c.algs[i];
      auto fit = fitAlg(canonical, parseAlgOrEmpty(text), free, aufsOf(set));
      // A reduction set's last steps read as its other steps do.
      alg.text = !fit ? text
                 : set.reduction && fit->after == 0
                     ? withAufBefore(text, fit->before)
                     : withAufs(text, *fit);
    }
    alg.text = mirrorAlgText(alg.text, flip);
    if (!alg.reducesTo.empty() && view.mirrored) {
      // The case a mirrored step stops at may be one that is seen as it
      // is written: its mirror image, by name.
      std::optional<CubieCube> next = applyAlg(cube, parseAlgOrEmpty(alg.text));
      if (auto seen = next ? seenCase(set, *next, world) : std::nullopt) {
        alg.reducesTo = seen->caseId;
      }
    }
    out.push_back(std::move(alg));
  }
  return out;
}

std::map<std::string, int>
stepsLeft(const AlgSet &set,
          const std::function<CaseAnalysis(const CaseGroup &, const AlgCase &)>
              &analysisOf) {
  struct Entry {
    std::string id;
    std::vector<std::string> next; // per passing alg; empty = finishes
  };
  std::vector<Entry> entries;
  for (const auto &g : set.groups) {
    for (const auto &c : g.cases) {
      CaseAnalysis a = analysisOf ? analysisOf(g, c) : analyzeCase(set, g, c);
      Entry e{c.id, {}};
      for (std::size_t i : a.passing) {
        e.next.push_back(a.checks[i].reducesTo);
      }
      entries.push_back(std::move(e));
    }
  }
  std::map<std::string, int> out;
  for (bool changed = true; changed;) {
    changed = false;
    for (const Entry &e : entries) {
      std::optional<int> best;
      for (const std::string &next : e.next) {
        std::optional<int> steps;
        if (next.empty()) {
          steps = 1;
        } else if (auto it = out.find(next); it != out.end()) {
          steps = it->second + 1;
        }
        if (steps && (!best || *steps < *best)) {
          best = steps;
        }
      }
      auto it = out.find(e.id);
      if (best && (it == out.end() || *best < it->second)) {
        out[e.id] = *best;
        changed = true;
      }
    }
  }
  return out;
}

Chain solveChain(const AlgSet &set, const CubieCube &cube, Mirror world) {
  Chain chain;
  CubieCube at = cube;
  for (int n = 0; n < kMaxSteps; ++n) {
    std::optional<CaseView> view = seenCase(set, at, world);
    const AlgCase *c = view ? set.findCase(view->caseId) : nullptr;
    const CaseGroup *g = view ? set.groupOf(view->caseId) : nullptr;
    if (!c || !g) {
      break;
    }
    std::vector<ViewAlg> algs =
        algsFor(set, *g, *c, analyzeCase(set, *g, *c), *view, at, world);
    if (algs.empty()) {
      break;
    }
    chain.steps.push_back({*view, algs.front().text});
    if (algs.front().reducesTo.empty()) {
      chain.complete = true;
      break;
    }
    std::optional<CubieCube> next =
        applyAlg(at, parseAlgOrEmpty(algs.front().text));
    if (!next) {
      break;
    }
    at = *next;
  }
  return chain;
}

std::optional<WorldView> worldOf(const AlgSet &set, const CubieCube &cube) {
  for (Mirror world : set.worlds()) {
    if (auto view = seenCase(set, cube, world)) {
      return WorldView{world, *view};
    }
  }
  return std::nullopt;
}

} // namespace vcube
