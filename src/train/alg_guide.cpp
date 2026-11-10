#include "train/alg_guide.hpp"

#include <optional>

namespace vcube {

FaceletCube oriented(FaceletCube f) {
  f.normalizeOrientation();
  return f;
}

bool sameCentres(const FaceletCube &a, const FaceletCube &b) {
  for (int face = 0; face < 6; ++face) {
    if (a[face * 9 + 4] != b[face * 9 + 4]) {
      return false;
    }
  }
  return true;
}

AlgGuide::AlgGuide(Alg moves, const FaceletCube &start)
    : moves_(std::move(moves)) {
  FaceletCube f = start;
  states_.push_back(oriented(f));
  held_.push_back(f);
  for (Move m : moves_) {
    f.apply(m);
    states_.push_back(oriented(f));
    held_.push_back(f);
  }
}

Alg AlgGuide::step(int delta) {
  if (delta > 0 && at_ < moves_.size()) {
    return {moves_[at_++]};
  }
  if (delta < 0 && at_ > 0) {
    return invert(Alg{moves_[--at_]});
  }
  return {};
}

void AlgGuide::restart() {
  at_ = 0;
  matched_ = true;
}

void AlgGuide::track(const FaceletCube &cube) {
  FaceletCube now = oriented(cube);
  // Forward from where it was first, so that a state the alg passes twice
  // counts as the later one.
  std::optional<std::size_t> found;
  for (std::size_t k = at_; k < states_.size() && !found; ++k) {
    if (states_[k] == now) {
      found = k;
    }
  }
  for (std::size_t k = 0; k < at_ && !found; ++k) {
    if (states_[k] == now) {
      found = k;
    }
  }
  matched_ = found.has_value();
  if (!found) {
    return;
  }
  // A rotation leaves the state as it was: it is done once the cube isn't
  // held as before it any more (as after it, or some other way when the
  // cube was turned differently from the start).
  std::size_t k = *found;
  while (k < moves_.size() && isRotation(moves_[k].type) &&
         (sameCentres(cube, held_[k + 1]) || !sameCentres(cube, held_[k]))) {
    ++k;
  }
  at_ = k;
}

} // namespace vcube
