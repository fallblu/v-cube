#include "train/alg_follow.hpp"

#include <algorithm>

namespace vcube {

namespace {

// What it costs to do a move: its quarter turns as written, so R3 may be
// done as three. An alg's wide and slice turns cost two for each quarter
// turn, so that they may be done as the two turns they are made of; a
// rotation costs nothing.
int algCost(Move m) {
  if (isRotation(m.type)) {
    return 0;
  }
  int quarters = quarterTurns(m);
  return isFaceTurn(m.type) ? quarters : 2 * quarters;
}

// A move done on the cube costs its quarter turns, whatever the layer.
int doneCost(Move m) { return isRotation(m.type) ? 0 : quarterTurns(m); }

// The axis a turn is about, by the centres on it whatever way the cube is
// held: 0 white-yellow, 1 red-orange, 2 green-blue.
int heldAxis(const FaceletCube &f, MoveType t) {
  static constexpr int kFaceOnAxis[3] = {1, 0, 2}; // R, U, F
  return f[kFaceOnAxis[moveAxis(t)] * 9 + 4] % 3;
}

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

Move turn(MoveType t, int quarters) {
  return {t, static_cast<std::uint8_t>(quarters)};
}

// The states, orientation removed, that quarter turns of the layers on
// `axis` (as the cube is held) take `cube` to within `limit` of them, each
// with the fewest turns that reach it. There are at most 16.
std::vector<std::pair<FaceletCube, int>> withinTurns(const FaceletCube &cube,
                                                     int axis, int limit) {
  static constexpr MoveType kLayers[] = {MoveType::U, MoveType::R, MoveType::F,
                                         MoveType::D, MoveType::L, MoveType::B,
                                         MoveType::M, MoveType::E, MoveType::S};
  std::vector<MoveType> layers;
  for (MoveType t : kLayers) {
    if (heldAxis(cube, t) == axis) {
      layers.push_back(t);
    }
  }
  std::vector<std::pair<FaceletCube, int>> seen{{oriented(cube), 0}};
  std::vector<FaceletCube> frontier{cube};
  for (int turns = 1; turns <= limit && !frontier.empty(); ++turns) {
    std::vector<FaceletCube> next;
    for (const FaceletCube &c : frontier) {
      for (MoveType t : layers) {
        for (int q : {1, 3}) {
          FaceletCube n = c;
          n.apply(turn(t, q));
          FaceletCube o = oriented(n);
          if (std::none_of(seen.begin(), seen.end(),
                           [&o](const auto &s) { return s.first == o; })) {
            seen.emplace_back(o, turns);
            next.push_back(n);
          }
        }
      }
    }
    frontier = std::move(next);
  }
  return seen;
}

} // namespace

std::optional<UdTurn> udTurn(const FaceletCube &cube, Move m) {
  FaceletCube after = cube;
  after.apply(m);
  FaceletCube target = oriented(after);
  FaceletCube base = oriented(cube);
  for (int u = 0; u < 4; ++u) {
    FaceletCube c = base;
    if (u != 0) {
      c.apply(turn(MoveType::U, u));
    }
    for (int d = 0; d < 4; ++d) {
      FaceletCube e = c;
      if (d != 0) {
        e.apply(turn(MoveType::D, d));
      }
      if (e != target) {
        continue;
      }
      for (const Alg &r : allRotations()) {
        FaceletCube turned = cube;
        turned.apply(r);
        if (sameCentres(turned, after)) {
          return UdTurn{u, d, r};
        }
      }
    }
  }
  return std::nullopt;
}

AlgOpening algOpening(const Alg &alg) {
  AlgOpening o;
  FaceletCube f;
  std::size_t i = 0;
  for (; i < alg.size(); ++i) {
    std::optional<UdTurn> t = udTurn(f, alg[i]);
    if (!t) {
      break;
    }
    f.apply(alg[i]);
    if (t->d != 0) {
      o.d = (o.d + t->d) % 4;
      o.dCost += algCost(alg[i]);
    }
    o.rotation.insert(o.rotation.end(), t->rotation.begin(), t->rotation.end());
  }
  o.rest.assign(alg.begin() + static_cast<std::ptrdiff_t>(i), alg.end());
  return o;
}

AlgFollower::AlgFollower(const FaceletCube &start, std::vector<Alg> algs)
    : start_(start), cube_(start) {
  setAlgs(std::move(algs));
}

void AlgFollower::move(Move m) {
  log_.push_back(m);
  step(log_.size() - 1);
}

void AlgFollower::restart() {
  abandoned_ = abandoned_ || !opening_ || openingDCost_ > 0;
  log_.clear();
  replay();
}

void AlgFollower::setAlgs(std::vector<Alg> algs) {
  openings_.clear();
  for (const Alg &a : algs) {
    openings_.push_back(algOpening(a));
  }
  replay();
}

void AlgFollower::replay() {
  cube_ = start_;
  opening_ = true;
  openingD_ = 0;
  openingDCost_ = 0;
  paths_.clear();
  leftAt_.reset();
  for (std::size_t i = 0; i < log_.size(); ++i) {
    step(i);
  }
}

bool AlgFollower::openingFits(const AlgOpening &o, bool complete) const {
  if (openingDCost_ > o.dCost) {
    return false;
  }
  // With cost to spare, more D turns may still make up the alg's.
  return openingD_ == o.d || (!complete && openingDCost_ < o.dCost);
}

bool AlgFollower::deviated() const { return abandoned_ || leftAt_.has_value(); }

bool AlgFollower::onAlg() const {
  if (abandoned_) {
    return false;
  }
  if (opening_) {
    // Done before any other layer was turned: only an alg with nothing
    // else to it fits, or no move at all.
    return log_.empty() ||
           std::any_of(openings_.begin(), openings_.end(),
                       [this](const AlgOpening &o) {
                         return o.rest.empty() && openingFits(o, true);
                       });
  }
  FaceletCube now = oriented(cube_);
  for (const Path &p : paths_) {
    if (!p.alive) {
      continue;
    }
    if (p.transit == 0) {
      return true;
    }
    // A U turn short of a state ahead: the last layer isn't lined up.
    FaceletCube f = now;
    for (int q = 1; q < 4; ++q) {
      f.apply(turn(MoveType::U, 1));
      auto ahead = p.states.begin() + static_cast<std::ptrdiff_t>(p.at) + 1;
      auto runEnd =
          p.states.begin() + static_cast<std::ptrdiff_t>(p.runEnd[p.at]) + 1;
      if (std::find(ahead, runEnd, f) != runEnd) {
        return true;
      }
    }
  }
  return false;
}

bool AlgFollower::finished() const {
  return !opening_ &&
         std::any_of(paths_.begin(), paths_.end(), [](const Path &p) {
           return p.alive && p.transit == 0 && p.at + 1 == p.states.size();
         });
}

bool AlgFollower::canCatchUp(const Path &p) const {
  int limit = p.cost[p.runEnd[p.at]] - p.cost[p.at] - p.transit;
  if (limit < 0) {
    return false;
  }
  auto reach = withinTurns(cube_, p.transitAxis, limit);
  for (std::size_t j = p.at + 1; j <= p.runEnd[p.at]; ++j) {
    int budget = p.cost[j] - p.cost[p.at] - p.transit;
    for (const auto &[state, turns] : reach) {
      if (turns <= budget && state == p.states[j]) {
        return true;
      }
    }
  }
  return false;
}

void AlgFollower::layOutPaths() {
  // Each alg's rest from where the opening left the cube, from each side,
  // for the algs whose D turns the opening did.
  FaceletCube from = oriented(cube_);
  for (const AlgOpening &o : openings_) {
    if (!openingFits(o, true)) {
      continue;
    }
    for (int side = 0; side < 4; ++side) {
      Path p;
      FaceletCube f = from;
      if (side != 0) {
        f.apply(turn(MoveType::Y, side));
      }
      f.apply(o.rotation);
      p.states.push_back(oriented(f));
      p.cost.push_back(0);
      int cost = 0;
      for (Move a : o.rest) {
        bool layer = !isRotation(a.type);
        if (layer) {
          p.axis.push_back(heldAxis(f, a.type));
        }
        f.apply(a);
        cost += algCost(a);
        if (layer) {
          p.states.push_back(oriented(f));
          p.cost.push_back(cost);
        }
      }
      p.axis.push_back(-1);
      std::size_t n = p.states.size();
      p.runEnd.assign(n, n - 1);
      for (std::size_t i = n - 1; i-- > 0;) {
        p.runEnd[i] = p.axis[i + 1] == p.axis[i] ? p.runEnd[i + 1] : i + 1;
      }
      paths_.push_back(std::move(p));
    }
  }
}

void AlgFollower::step(std::size_t i) {
  Move m = log_[i];
  if (opening_) {
    if (std::optional<UdTurn> t = udTurn(cube_, m)) {
      cube_.apply(m);
      if (t->d != 0) {
        openingD_ = (openingD_ + t->d) % 4;
        openingDCost_ += doneCost(m);
        if (!leftAt_ && std::none_of(openings_.begin(), openings_.end(),
                                     [this](const AlgOpening &o) {
                                       return openingFits(o, false);
                                     })) {
          leftAt_ = i;
        }
      }
      return;
    }
    opening_ = false;
    layOutPaths();
  }
  int cost = doneCost(m);
  int axis = cost == 0 ? -1 : heldAxis(cube_, m.type);
  // A turn of the U layer alone, which after the end of an alg is the AUF
  // that lines up the last layer.
  std::optional<UdTurn> ud = udTurn(cube_, m);
  bool auf = ud && ud->d == 0;
  cube_.apply(m);
  if (cost == 0) {
    return; // a rotation changes nothing that is compared
  }
  FaceletCube now = oriented(cube_);
  for (Path &p : paths_) {
    if (!p.alive) {
      continue;
    }
    if (auf && p.transit == 0 && p.at + 1 == p.states.size()) {
      continue; // the alg is done; U turns only line up the last layer
    }
    // Another way of doing the alg's next turns only turns layers on the
    // same axis as they do until it catches up with the alg.
    if (p.transit == 0) {
      p.transitAxis = p.axis[p.at];
      p.lostAt.reset();
    }
    // The move that left the alg is the first after which its turns on
    // this axis could no longer be caught up with (U' where the alg turns
    // U, L where it turns R2), or else the one that left (R after U where
    // the alg turns U2 R, M' after M where it turns M2).
    if (axis != p.transitAxis) {
      p.alive = false;
      p.leftAt = p.lostAt.value_or(i);
      continue;
    }
    p.transit += cost;
    int runCost = p.cost[p.runEnd[p.at]] - p.cost[p.at];
    auto begin = p.states.begin();
    auto at = begin + static_cast<std::ptrdiff_t>(p.at);
    auto runEnd = begin + static_cast<std::ptrdiff_t>(p.runEnd[p.at]) + 1;
    auto ahead = std::find(at + 1, runEnd, now);
    if (std::find(begin, at + 1, now) != at + 1) {
      p.alive = false; // back where it already was: a turn was undone
    } else if (ahead != runEnd) {
      auto j = static_cast<std::size_t>(ahead - begin);
      p.alive = p.transit <= p.cost[j] - p.cost[p.at];
      p.at = j;
      p.transit = 0;
    } else {
      // Between two states of the alg: left once more has been turned than
      // the alg's turns on this axis need.
      p.alive = p.transit <= runCost;
    }
    if (!p.alive) {
      p.leftAt = p.lostAt.value_or(i);
    } else if (p.transit > 0 && !p.lostAt && !canCatchUp(p)) {
      p.lostAt = i;
    }
  }
  if (!leftAt_ && std::none_of(paths_.begin(), paths_.end(),
                               [](const Path &p) { return p.alive; })) {
    // The moves kept to some alg up to where the one followed furthest
    // was left.
    std::size_t left = i;
    if (!paths_.empty()) {
      left = 0;
      for (const Path &p : paths_) {
        left = std::max(left, p.leftAt);
      }
    }
    leftAt_ = left;
  }
}

} // namespace vcube
