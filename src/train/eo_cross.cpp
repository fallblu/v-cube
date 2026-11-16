#include "train/eo_cross.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <numeric>

namespace vcube {

namespace {

constexpr std::array<std::string_view, kEoDrills> kDrillIds = {
    "edges", "axis", "plan", "solve", "full"};

bool hasStep(EoDrill d) {
  return d == EoDrill::Axis || d == EoDrill::Plan || d == EoDrill::Solve;
}
bool hasKinds(EoDrill d) { return d != EoDrill::Axis; }

std::optional<int> toInt(std::string_view s) {
  int value = 0;
  auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
  if (ec != std::errc() || end != s.data() + s.size()) {
    return std::nullopt;
  }
  return value;
}

} // namespace

std::string_view eoDrillId(EoDrill d) {
  return kDrillIds[static_cast<std::size_t>(d)];
}

const std::vector<SolveStep> &eoCrossSteps() {
  static const std::vector<SolveStep> steps = {
      SolveStep::EO, SolveStep::EOLine, SolveStep::EOPlus1, SolveStep::EOPlus2,
      SolveStep::EOCross};
  return steps;
}

double badEdgeShare(int bad) {
  if (bad < 0 || bad > kEdges || bad % 2 != 0) {
    return 0.0;
  }
  double choose = 1.0;
  for (int i = 0; i < bad; ++i) {
    choose = choose * (kEdges - i) / (i + 1);
  }
  return choose / 2048.0;
}

EoKind normalized(EoKind k) {
  if (!hasStep(k.drill)) {
    k.step = SolveStep::EOCross;
  }
  if (!hasKinds(k.drill)) {
    k.bad = -1;
  }
  return k;
}

std::string eoKindKey(const EoKind &kind) {
  EoKind k = normalized(kind);
  std::string key(eoDrillId(k.drill));
  if (hasStep(k.drill)) {
    key += "/";
    key += stepId(k.step);
  }
  if (hasKinds(k.drill)) {
    key += "/" + std::to_string(k.bad);
  }
  return key;
}

std::optional<EoKind> eoKindFromKey(std::string_view key) {
  std::vector<std::string_view> parts;
  while (true) {
    std::size_t slash = key.find('/');
    parts.push_back(key.substr(0, slash));
    if (slash == std::string_view::npos) {
      break;
    }
    key.remove_prefix(slash + 1);
  }
  auto drill = std::find(kDrillIds.begin(), kDrillIds.end(), parts.front());
  if (drill == kDrillIds.end()) {
    return std::nullopt;
  }
  EoKind k;
  k.drill = static_cast<EoDrill>(drill - kDrillIds.begin());
  std::size_t at = 1;
  if (hasStep(k.drill)) {
    if (at >= parts.size()) {
      return std::nullopt;
    }
    std::optional<SolveStep> step = stepFromId(parts[at++]);
    const std::vector<SolveStep> &steps = eoCrossSteps();
    if (!step || std::find(steps.begin(), steps.end(), *step) == steps.end()) {
      return std::nullopt;
    }
    k.step = *step;
  }
  if (hasKinds(k.drill)) {
    if (at >= parts.size()) {
      return std::nullopt;
    }
    std::optional<int> bad = toInt(parts[at++]);
    if (!bad || std::find(kBadEdgeKinds.begin(), kBadEdgeKinds.end(), *bad) ==
                    kBadEdgeKinds.end()) {
      return std::nullopt;
    }
    k.bad = *bad;
  }
  if (at != parts.size()) {
    return std::nullopt;
  }
  return k;
}

void EoCrossStats::add(const EoKind &kind, const PairAttempt &a) {
  std::vector<PairAttempt> &to = kinds[eoKindKey(kind)];
  to.push_back(a);
  if (to.size() > kPerKind) {
    to.erase(to.begin(),
             to.begin() + static_cast<std::ptrdiff_t>(to.size() - kPerKind));
  }
}

const std::vector<PairAttempt> &EoCrossStats::tries(const EoKind &kind) const {
  static const std::vector<PairAttempt> none;
  auto it = kinds.find(eoKindKey(kind));
  return it == kinds.end() ? none : it->second;
}

double eoMeanMs(const EoCrossStats &stats, EoDrill drill, SolveStep step) {
  double sum = 0.0;
  int n = 0;
  auto count = [&](const EoKind &kind) {
    for (const PairAttempt &a : stats.tries(kind)) {
      if (!a.gaveUp) {
        sum += static_cast<double>(a.ms);
        ++n;
      }
    }
  };
  if (hasKinds(drill)) {
    for (int bad : kBadEdgeKinds) {
      count({drill, step, bad});
    }
  } else {
    count({drill, step, -1});
  }
  return n ? sum / n : 0.0;
}

CaseSummary eoSummary(const EoCrossStats &stats, const EoKind &kind) {
  return summarizeCase(stats.tries(kind),
                       eoMeanMs(stats, kind.drill, normalized(kind).step));
}

std::array<double, 7> badEdgeWeights(const EoCrossStats &stats, EoDrill drill,
                                     SolveStep step) {
  // The mean time of the drill is taken once for all its kinds.
  const double overall = eoMeanMs(stats, drill, step);
  std::array<double, 7> weights{};
  for (std::size_t i = 0; i < kBadEdgeKinds.size(); ++i) {
    // The square root flattens the shares: 0 and 12 bad edges, one state
    // in 2048 each, come up about once in fifty.
    weights[i] =
        std::sqrt(badEdgeShare(kBadEdgeKinds[i])) *
        summarizeCase(stats.tries({drill, step, kBadEdgeKinds[i]}), overall)
            .weight;
  }
  return weights;
}

int pickBadEdges(const EoCrossStats &stats, EoDrill drill, SolveStep step,
                 Rng &rng) {
  std::array<double, 7> weights = badEdgeWeights(stats, drill, step);
  return kBadEdgeKinds[pickWeighted(
      std::vector<double>(weights.begin(), weights.end()), rng)];
}

CubieCube randomEoState(int bad, Rng &rng) {
  CubieCube c = randomState(PieceMask::all(), rng);
  if (bad < 0 || bad > kEdges || bad % 2 != 0) {
    return c;
  }
  // An edge is bad for F/B exactly when it is flipped, so the flips can be
  // dealt out afresh: any even number of them is a real cube.
  std::array<int, kEdges> places{};
  std::iota(places.begin(), places.end(), 0);
  std::shuffle(places.begin(), places.end(), rng);
  for (int i = 0; i < kEdges; ++i) {
    c.eo[static_cast<std::size_t>(places[static_cast<std::size_t>(i)])] =
        i < bad ? 1 : 0;
  }
  return c;
}

std::vector<Hold> eoHolds(const ColourNeutrality &neutrality) {
  return withBothEoAxes(neutrality.holds);
}

std::optional<Hold> doneHold(const FaceletCube &cube, SolveStep step,
                             const std::vector<Hold> &allowed) {
  std::vector<Hold> holds;
  for (Hold h : stepReachedHolds(cube, step)) {
    if (std::any_of(allowed.begin(), allowed.end(),
                    [&](Hold a) { return sameStepProblem(h, a, step); })) {
      holds.push_back(h);
    }
  }
  if (holds.empty()) {
    return std::nullopt;
  }
  if (std::optional<Hold> now = holdOf(cube)) {
    if (std::find(holds.begin(), holds.end(), *now) != holds.end()) {
      return *now;
    }
    for (Hold h : holds) {
      if (h.top == now->top) {
        return h;
      }
    }
  }
  return holds.front();
}

std::optional<std::size_t> rankOfHold(const std::vector<HeldOptimum> &ranked,
                                      Hold hold, SolveStep step) {
  for (std::size_t i = 0; i < ranked.size(); ++i) {
    if (sameStepProblem(ranked[i].hold, hold, step)) {
      return i;
    }
  }
  return std::nullopt;
}

EoBreakdown eoBreakdown(const FaceletCube &start, const Alg &solution,
                        Hold hold) {
  static constexpr std::array<SolveStep, 4> kMilestones = {
      SolveStep::EO, SolveStep::EOPlus1, SolveStep::EOPlus2,
      SolveStep::EOCross};
  EoBreakdown out;
  auto held = [hold](const FaceletCube &f) {
    return heldCubie(f, hold).value_or(CubieCube{});
  };
  auto toGo = [](const CubieCube &c) {
    return stepDistance(c, SolveStep::EOCross).value_or(0);
  };
  FaceletCube cube = start;
  out.optimal = toGo(held(cube));
  int before = out.optimal;
  std::size_t next = 0, begin = 0;
  for (std::size_t done = 0; done <= solution.size(); ++done) {
    if (done > 0) {
      cube.apply(solution[done - 1]);
    }
    CubieCube c = held(cube);
    std::optional<SolveStep> reached;
    while (next < kMilestones.size() && stepDoneAsHeld(c, kMilestones[next])) {
      reached = kMilestones[next++];
    }
    if (!reached) {
      continue;
    }
    if (done == 0) {
      // Steps the start already had are no part.
      if (next == kMilestones.size()) {
        break;
      }
      continue;
    }
    EoPart part;
    part.step = *reached;
    part.begin = begin;
    part.end = done;
    part.moves = htmCount(
        simplify(Alg(solution.begin() + static_cast<std::ptrdiff_t>(begin),
                     solution.begin() + static_cast<std::ptrdiff_t>(done))));
    part.toGo = toGo(c);
    part.lost = part.moves + part.toGo - before;
    before = part.toGo;
    begin = done;
    out.parts.push_back(part);
    if (next == kMilestones.size()) {
      break;
    }
  }
  out.done = next == kMilestones.size();
  return out;
}

} // namespace vcube
