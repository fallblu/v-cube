#include "train/set_scramble.hpp"

#include "train/case_logic.hpp"
#include "train/reduction.hpp"

#include <algorithm>
#include <random>

namespace vcube {

SetScramblePool::SetScramblePool(const AlgSet &set,
                                 const std::vector<std::string> &unticked)
    : worlds_(set.worlds()), aufBefore_(set.aufBefore),
      aufAfter_(set.aufAfter) {
  auto fill = [&](bool tickedOnly) {
    for (const auto &g : set.groups) {
      for (const auto &c : g.cases) {
        if (!tickedOnly || std::find(unticked.begin(), unticked.end(), c.id) ==
                               unticked.end()) {
          // The case as Drill sets it up: oriented for its main alg.
          entries_.push_back(
              {analyzeCase(set, g, c).display, set.specFor(g).free, c.id});
        }
      }
    }
  };
  fill(true);
  if (entries_.empty()) {
    fill(false);
  }
}

SetScramble SetScramblePool::draw(Rng &rng) const {
  std::uniform_int_distribution<std::size_t> pick(0, entries_.size() - 1);
  std::uniform_int_distribution<std::size_t> world(0, worlds_.size() - 1);
  std::uniform_int_distribution<int> quarter(0, 3);
  const Entry &e = entries_[pick(rng)];
  // As Drill sets a case up: in one of the set's mirror images.
  CubieCube s = setUpCase(e.state, e.free, worlds_[world(rng)],
                          FreePieces::Scrambled, rng);
  if (aufBefore_) {
    s = s * uTurn(quarter(rng));
  }
  if (aufAfter_) {
    s = uTurn(quarter(rng)) * s;
  }
  if (s.isSolved()) {
    return {randomState(PieceMask::all(), rng), {}};
  }
  return {s, e.caseId};
}

} // namespace vcube
