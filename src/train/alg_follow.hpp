#pragma once

#include "cube/facelet.hpp"
#include "cube/move.hpp"

#include <optional>
#include <vector>

namespace vcube {

// What a move turns when it only turns the whole cube or layers on the
// axis through the U centre, however the cube is held: quarter turns of
// the U and D layers once the cube's orientation is set aside (a U turn
// is u = 1, D' is d = 3, E is u = 1 and d = 3, the wide d is u = 1), and
// the rotation it turns the cube by.
struct UdTurn {
  int u = 0;
  int d = 0;
  Alg rotation;
};
// nullopt for a move that turns a layer on another axis.
std::optional<UdTurn> udTurn(const FaceletCube &cube, Move m);

// An alg split at its first turn of a layer off the U-D axis. Before that
// point its U turns are the AUF, which whoever does the alg chooses, and
// its D turns can be done before or after the AUF.
struct AlgOpening {
  int d = 0;     // quarter turns of the D layer before that point (0..3)
  int dCost = 0; // what turning them costs
  Alg rotation;  // how the cube is turned by then
  Alg rest;      // the alg from that point
};
AlgOpening algOpening(const Alg &alg);

// Tells whether the moves done on the virtual cube keep to one of a case's
// algs. U turns and rotations are free until the first turn of a layer off
// the U-D axis (for recognition and the AUF), and D turns may come before
// or after them. From there on the moves must take the cube through the
// same states as the rest of an alg does, from any side (any y), so the
// same turns done another way still count:
//   - rotations anywhere, as long as the same layers are turned;
//   - a half turn as two quarter turns either way;
//   - a move written with a 3 (R3) as those three quarter turns or as the
//     one turn the other way (R');
//   - a wide or slice turn as the turns it is made of (r as R M');
//   - turns of opposite faces in either order (R L' as L' R);
//   - leaving out a U turn at the end, which only lines up the last layer.
// Anything else, such as a wrong turn, even one that is undone, leaves the
// algs. So does starting over after the alg was begun.
class AlgFollower {
public:
  AlgFollower() = default;
  // `start` is the case as it is set up, `algs` the algs that count.
  AlgFollower(const FaceletCube &start, std::vector<Alg> algs);

  void move(Move m);
  // The cube was set back to the start.
  void restart();
  // The algs changed: checks the moves done since the last restart again.
  void setAlgs(std::vector<Alg> algs);

  // The moves done so far left every alg.
  bool deviated() const;
  // The algs were never left and the moves done since the last restart
  // are all or part of an alg, up to a whole move of it (not halfway
  // through a half turn, say) or a U turn short of one.
  bool onAlg() const;
  // Restarted after the alg was begun.
  bool startedOver() const { return abandoned_; }
  // Where the moves done since the last restart left every alg, as an
  // index into moves(): the first move that no alg's turns account for.
  // A turn the wrong way counts from there, even though it is only clear
  // a move or two later that it was wrong.
  std::optional<std::size_t> leftAt() const { return leftAt_; }
  // The moves done since the last restart.
  const Alg &moves() const { return log_; }

private:
  // The rest of an alg, done from where the opening left the cube.
  struct Path {
    std::vector<FaceletCube> states; // after each turn, orientation removed
    std::vector<int> cost;           // turn cost to reach states[i]
    std::vector<int> axis; // axis of the turn after states[i], -1 at the end
    // The last state that turns on that axis alone lead to from states[i]:
    // another way of doing those turns has to stay within them.
    std::vector<std::size_t> runEnd;
    std::size_t at = 0;   // the last state reached
    int transit = 0;      // cost of the moves done since then
    int transitAxis = -1; // and their axis
    // The first of them (in log_) after which the alg's turns on that axis
    // could no longer be caught up with.
    std::optional<std::size_t> lostAt;
    bool alive = true;
    std::size_t leftAt = 0; // once not alive: the move that left the alg
  };

  FaceletCube start_;
  FaceletCube cube_;
  std::vector<AlgOpening> openings_; // one per alg
  std::vector<Path> paths_;
  Alg log_;
  bool opening_ = true;
  int openingD_ = 0;       // D layer quarter turns done in the opening
  int openingDCost_ = 0;   // and their cost
  bool abandoned_ = false; // restarted after the alg was begun
  std::optional<std::size_t> leftAt_;

  // Whether the D turns done in the opening may still be an alg's.
  bool openingFits(const AlgOpening &o, bool complete) const;
  // Whether turns on the transit's axis could still take the cube to a
  // state of the path within the alg's cost.
  bool canCatchUp(const Path &p) const;
  // Follows log_[i].
  void step(std::size_t i);
  void layOutPaths();
  void replay();
};

} // namespace vcube
