#pragma once

#include "cube/cubie.hpp"
#include "cube/facelet.hpp"
#include "cube/hold.hpp"
#include "cube/move.hpp"
#include "cube/scramble.hpp"

#include <functional>
#include <string>

namespace vcube {

struct AlgSet;

// Learn's in-context practice of a set of F2L pairs: a cube with EO Cross
// done and some slots open, solved in any order and then compared with
// the set's algs and its cheapest order. Not graded.
class ContextPractice {
public:
  // A new state with `open` slots open, set up from the cases learned so
  // far (`started` says whether a case is) where it can be.
  ContextPractice(const AlgSet &set, int open, Rng &rng,
                  const std::function<bool(const std::string &)> &started);

  const CubieCube &start() const { return start_; } // in the solver's frame
  // The pairs open at the start.
  int openPairs() const;
  // The hold the cube is shown in.
  Hold hold() const { return hold_; }
  void setHold(Hold h) { hold_ = h; }
  const Alg &moves() const { return moves_; } // done since the start
  bool gaveUp() const { return gaveUp_; }

  // The same pairs from the start again.
  void reset();
  // A turn of the cube (now `cube`): true once every pair is solved.
  bool turned(Move m, const FaceletCube &cube);
  // How the practice ended: every pair solved, or given up.
  enum class End { Solved, GaveUp };
  void finish(End end) { gaveUp_ = end == End::GaveUp; }

private:
  CubieCube start_;
  Hold hold_ = kWcaHold;
  Alg moves_;
  bool gaveUp_ = false;
};

} // namespace vcube
