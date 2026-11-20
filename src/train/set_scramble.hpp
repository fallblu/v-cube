#pragma once

#include "cube/cubie.hpp"
#include "cube/scramble.hpp"
#include "train/algset.hpp"

#include <string>
#include <vector>

namespace vcube {

// A state an alg set scramble sets up, and its case.
struct SetScramble {
  CubieCube state; // in the solver's frame
  // The case set up; empty for a random full cube, given when the case
  // came out solved.
  std::string caseId;
};

// The cases a "set:<id>" scramble type picks from: the set's cases not in
// `unticked`, or all of them when none is ticked, each as Drill sets it
// up (oriented for its main alg).
class SetScramblePool {
public:
  SetScramblePool(const AlgSet &set, const std::vector<std::string> &unticked);

  bool empty() const { return entries_.empty(); }
  std::size_t size() const { return entries_.size(); }

  // One of the cases in one of the set's mirror images, with its free
  // pieces scrambled and the U turns the set allows before and after it.
  SetScramble draw(Rng &rng) const;

private:
  struct Entry {
    CubieCube state;
    PieceMask free;
    std::string caseId;
  };
  std::vector<Entry> entries_;
  std::vector<Mirror> worlds_;
  bool aufBefore_ = true;
  bool aufAfter_ = true;
};

} // namespace vcube
