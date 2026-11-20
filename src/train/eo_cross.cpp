#include "train/eo_cross.hpp"

#include <algorithm>

namespace vcube {

const std::vector<SolveStep> &eoCrossSteps() {
  static const std::vector<SolveStep> steps = {
      SolveStep::EO, SolveStep::EOLine, SolveStep::EOPlus1, SolveStep::EOPlus2,
      SolveStep::EOCross};
  return steps;
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
    part.moves = htmCount(simplify(slice(solution, begin, done)));
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
