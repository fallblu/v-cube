#include "train/context_practice.hpp"

#include "train/solve_review.hpp"

#include <algorithm>
#include <vector>

namespace vcube {

namespace {

// Tries for an in-context state whose open pairs are all cases already
// started in Learn, before taking any.
constexpr int kContextTries = 40;

} // namespace

ContextPractice::ContextPractice(
    const AlgSet &set, int open, Rng &rng,
    const std::function<bool(const std::string &)> &started) {
  CubieCube state = randomPairState(open, rng);
  for (int i = 0; i < kContextTries; ++i) {
    CubieCube candidate = i == 0 ? state : randomPairState(open, rng);
    std::vector<PairOption> options = pairOptions(candidate, &set);
    bool allStarted = std::all_of(
        options.begin(), options.end(), [&started](const PairOption &o) {
          return !o.caseId.empty() && started(o.caseId);
        });
    if (allStarted) {
      state = candidate;
      break;
    }
  }
  start_ = state;
}

int ContextPractice::openPairs() const {
  return static_cast<int>(
      std::count_if(kF2LSlots.begin(), kF2LSlots.end(),
                    [this](Slot slot) { return !slotSolved(start_, slot); }));
}

void ContextPractice::reset() {
  moves_.clear();
  gaveUp_ = false;
}

bool ContextPractice::turned(Move m, const FaceletCube &cube) {
  moves_.push_back(m);
  FaceletCube f = cube;
  f.normalizeOrientation();
  auto c = CubieCube::fromFacelets(f);
  return c && eoCrossSolved(*c) &&
         std::all_of(kF2LSlots.begin(), kF2LSlots.end(),
                     [&](Slot slot) { return slotSolved(*c, slot); });
}

} // namespace vcube
