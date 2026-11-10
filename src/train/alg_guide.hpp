#pragma once

#include "cube/facelet.hpp"
#include "cube/move.hpp"

#include <cstddef>
#include <vector>

namespace vcube {

// Guided learning of a case: its main alg as done from the case as set up
// (its U turns written in), and how far the cube has got along it. A new
// case is first walked through (step() moves along the alg), then tried
// with every next move shown (follow) or, in its learning steps, with
// only the next move as a hint (next).
class AlgGuide {
public:
  AlgGuide() = default;
  // The alg from `start` (the cube as held when the case came up).
  AlgGuide(Alg moves, const FaceletCube &start);

  bool empty() const { return moves_.empty(); }
  const Alg &moves() const { return moves_; }
  // The walkthrough: moves shown. Else: the prefix of the alg the cube
  // reached last.
  std::size_t at() const { return at_; }
  bool done() const { return at_ >= moves_.size(); }
  // The cube is at the state after at() moves (not between two moves of
  // the alg, nor off it).
  bool matched() const { return matched_; }

  // Every next move is shown (a new case's first try).
  bool follow = false;
  // The next move is shown (a learning-step hint).
  bool next = false;

  // The walkthrough one move forward (delta > 0) or back: the moves to
  // apply to the cube, none at either end.
  Alg step(int delta);
  // Back to the start of the alg.
  void restart();
  // Where the cube (as held) is along the alg after a turn.
  void track(const FaceletCube &cube);

private:
  Alg moves_;
  std::vector<FaceletCube> states_; // after each prefix, orientation removed
  std::vector<FaceletCube> held_;   // the same as the cube is held
  std::size_t at_ = 0;
  bool matched_ = true;
};

// The cube with its orientation removed, so that states reached holding
// the cube differently compare equal.
FaceletCube oriented(FaceletCube f);
// The cubes are held the same way (their centres match).
bool sameCentres(const FaceletCube &a, const FaceletCube &b);

} // namespace vcube
