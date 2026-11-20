#include "train/step_solver.hpp"

#include "cube/coord.hpp"
#include "cube/facelet.hpp"
#include "train/f2l_search.hpp"

#include <algorithm>
#include <climits>
#include <map>
#include <mutex>
#include <numeric>
#include <span>
#include <string>
#include <tuple>

namespace vcube {

namespace {

using coord::kFlip;
using coord::kMoves;
constexpr int kPositions = kEdges;
constexpr std::uint8_t kUnseen = 0xff;

// The cross edges, by piece (and home position).
constexpr std::array<int, 4> kCross = {DF, DR, DB, DL};
// The six pairs of cross edges, as indices into kCross.
constexpr std::array<std::array<int, 2>, 6> kPairs = {
    {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}}};

struct Tables {
  // flipMove[flip * kMoves + move], posMove[position * kMoves + move]: the
  // coordinate after the move.
  std::vector<std::uint16_t> flipMove;
  std::vector<std::uint8_t> posMove;
  // Moves to the goal, ignoring everything else: EO alone, EO with one
  // cross edge solved (by kCross index), EO with a pair (by kPairs index).
  std::vector<std::uint8_t> eo;                    // [flip]
  std::array<std::vector<std::uint8_t>, 4> single; // [flip * 12 + pos]
  std::array<std::vector<std::uint8_t>, 6> pair;   // [flip * 144 + a * 12 + b]
  // oriMove[position * kMoves + move]: how the move flips the edge there.
  std::vector<std::uint8_t> oriMove;
  // Moves to the cross alone (edges in any orientation elsewhere): by each
  // cross edge's position and flip, crossIndex().
  std::vector<std::uint8_t> cross;
};

// Each cross edge as position * 2 + flip, the four in kCross order.
std::size_t crossIndex(const std::array<int, 4> &pos,
                       const std::array<int, 4> &ori) {
  std::size_t idx = 0;
  for (std::size_t e = 0; e < 4; ++e) {
    idx = idx * 24 + static_cast<std::size_t>(pos[e] * 2 + ori[e]);
  }
  return idx;
}

// Distances from the solved state for EO plus the given cross edges (by
// kCross index), by breadth-first search over (flip, positions).
std::vector<std::uint8_t> distances(const Tables &t,
                                    std::span<const int> edges) {
  std::size_t posCount = 1;
  for (std::size_t i = 0; i < edges.size(); ++i) {
    posCount *= kPositions;
  }
  std::vector<std::uint8_t> dist(kFlip * posCount, kUnseen);
  auto encode = [&](int flip, std::span<const int> pos) {
    std::size_t idx = static_cast<std::size_t>(flip);
    for (int p : pos) {
      idx = idx * kPositions + static_cast<std::size_t>(p);
    }
    return idx;
  };
  std::array<int, 2> home{};
  for (std::size_t i = 0; i < edges.size(); ++i) {
    home[i] = kCross[static_cast<std::size_t>(edges[i])];
  }
  std::vector<std::size_t> frontier{
      encode(0, std::span(home).first(edges.size()))};
  dist[frontier[0]] = 0;
  for (std::uint8_t depth = 0; !frontier.empty(); ++depth) {
    std::vector<std::size_t> next;
    for (std::size_t idx : frontier) {
      std::array<int, 2> pos{};
      std::size_t rest = idx;
      for (std::size_t i = edges.size(); i-- > 0;) {
        pos[i] = static_cast<int>(rest % kPositions);
        rest /= kPositions;
      }
      int flip = static_cast<int>(rest);
      for (int m = 0; m < kMoves; ++m) {
        int nf = t.flipMove[static_cast<std::size_t>(flip * kMoves + m)];
        std::array<int, 2> np{};
        for (std::size_t i = 0; i < edges.size(); ++i) {
          np[i] = t.posMove[static_cast<std::size_t>(pos[i] * kMoves + m)];
        }
        std::size_t n = encode(nf, std::span(np).first(edges.size()));
        if (dist[n] == kUnseen) {
          dist[n] = static_cast<std::uint8_t>(depth + 1);
          next.push_back(n);
        }
      }
    }
    frontier = std::move(next);
  }
  return dist;
}

Tables buildTables() {
  Tables t;
  const std::array<CubieCube, kMoves> moves = coord::moveCubies();
  t.flipMove = coord::flipMoveTable();
  // After a move, the piece at position j is at the position i that the
  // move fills from j.
  t.posMove.resize(kPositions * kMoves);
  t.oriMove.resize(kPositions * kMoves);
  for (int m = 0; m < kMoves; ++m) {
    const CubieCube &mc = moves[static_cast<std::size_t>(m)];
    for (std::size_t i = 0; i < kEdges; ++i) {
      t.posMove[static_cast<std::size_t>(mc.ep[i] * kMoves + m)] =
          static_cast<std::uint8_t>(i);
      t.oriMove[static_cast<std::size_t>(mc.ep[i] * kMoves + m)] = mc.eo[i];
    }
  }
  std::vector<int> all(kMoves);
  std::iota(all.begin(), all.end(), 0);
  t.cross = pieceDistances(
      {kCross[0] * 2, kCross[1] * 2, kCross[2] * 2, kCross[3] * 2},
      {false, false, false, false}, all);
  t.eo = distances(t, {});
  for (int e = 0; e < 4; ++e) {
    std::array<int, 1> edges{e};
    t.single[static_cast<std::size_t>(e)] = distances(t, edges);
  }
  for (std::size_t p = 0; p < kPairs.size(); ++p) {
    t.pair[p] = distances(t, kPairs[p]);
  }
  return t;
}

const Tables &tables() {
  static const Tables t = buildTables();
  return t;
}

// A step's search state: the flip coordinate and where the cross edges are
// (and how each is flipped).
struct State {
  int flip = 0;
  std::array<int, 4> pos{};
  std::array<int, 4> ori{};
};

State stateOf(const CubieCube &c) {
  State s;
  s.flip = coord::flipOf(c);
  for (std::size_t e = 0; e < kCross.size(); ++e) {
    for (std::size_t i = 0; i < kEdges; ++i) {
      if (c.ep[i] == kCross[e]) {
        s.pos[e] = static_cast<int>(i);
        s.ori[e] = c.eo[i];
      }
    }
  }
  return s;
}

// A lower bound on the moves left, exact at the goal (0 only there).
int heuristic(const Tables &t, SolveStep step, const State &s) {
  auto flipBase = static_cast<std::size_t>(s.flip);
  auto pairDist = [&](std::size_t p) {
    auto a =
        static_cast<std::size_t>(s.pos[static_cast<std::size_t>(kPairs[p][0])]);
    auto b =
        static_cast<std::size_t>(s.pos[static_cast<std::size_t>(kPairs[p][1])]);
    return static_cast<int>(
        t.pair[p][(flipBase * kPositions + a) * kPositions + b]);
  };
  switch (step) {
  case SolveStep::EO:
    return t.eo[flipBase];
  case SolveStep::EOPlus1: {
    int best = INT_MAX;
    for (std::size_t e = 0; e < 4; ++e) {
      best = std::min(
          best,
          static_cast<int>(t.single[e][flipBase * kPositions +
                                       static_cast<std::size_t>(s.pos[e])]));
    }
    return best;
  }
  case SolveStep::EOPlus2: {
    int best = INT_MAX;
    for (std::size_t p = 0; p < kPairs.size(); ++p) {
      best = std::min(best, pairDist(p));
    }
    return best;
  }
  case SolveStep::EOLine:
    return pairDist(1); // DF and DB
  case SolveStep::EOCross: {
    int worst = t.cross[crossIndex(s.pos, s.ori)];
    for (std::size_t p = 0; p < kPairs.size(); ++p) {
      worst = std::max(worst, pairDist(p));
    }
    return worst;
  }
  case SolveStep::Cross:
    return t.cross[crossIndex(s.pos, s.ori)];
  case SolveStep::ZZF2L:
  case SolveStep::XEOCross:
  case SolveStep::CFOPF2L:
    break;
  }
  return 0;
}

// IDA* at one depth: every canonical sequence of exactly that length that
// reaches the goal, in move order, until `limit` are found (and one more
// to know whether there are more).
class Search {
public:
  Search(SolveStep step, std::size_t limit)
      : t_(tables()), step_(step), limit_(limit) {}

  // Searches each depth from the heuristic up to `maxDepth`; the depth of
  // the first solutions, or nullopt if there are none that short.
  std::optional<int> run(const State &start, int maxDepth) {
    int h = heuristic(t_, step_, start);
    for (int depth = h; depth <= maxDepth; ++depth) {
      path_.clear();
      found_.clear();
      more_ = false;
      dfs(start, depth, -1);
      if (!found_.empty()) {
        return depth;
      }
    }
    return std::nullopt;
  }

  const std::vector<Alg> &found() const { return found_; }
  bool more() const { return more_; }

private:
  const Tables &t_;
  SolveStep step_;
  std::size_t limit_;
  std::vector<int> path_;
  std::vector<Alg> found_;
  bool more_ = false;

  // True to stop searching.
  bool dfs(const State &s, int remaining, int lastFace) {
    if (remaining == 0) {
      if (heuristic(t_, step_, s) != 0) {
        return false;
      }
      if (found_.size() >= limit_) {
        more_ = true;
        return true;
      }
      Alg alg;
      for (int m : path_) {
        alg.push_back(coord::moveOf(m));
      }
      found_.push_back(std::move(alg));
      return false;
    }
    for (int face = 0; face < 6; ++face) {
      // Never the same face twice, and opposite faces in one order only.
      if (coord::skipFace(face, lastFace)) {
        continue;
      }
      for (int a = 0; a < 3; ++a) {
        int m = face * 3 + a;
        State n;
        n.flip = t_.flipMove[static_cast<std::size_t>(s.flip * kMoves + m)];
        for (std::size_t e = 0; e < 4; ++e) {
          auto at = static_cast<std::size_t>(s.pos[e] * kMoves + m);
          n.pos[e] = t_.posMove[at];
          n.ori[e] = s.ori[e] ^ t_.oriMove[at];
        }
        if (heuristic(t_, step_, n) > remaining - 1) {
          continue;
        }
        path_.push_back(m);
        bool stop = dfs(n, remaining - 1, face);
        path_.pop_back();
        if (stop) {
          return true;
        }
      }
    }
    return false;
  }
};

// No step here takes more than this (EO Cross is at most 10 or so).
constexpr int kMaxDepth = 20;

// Holds that pose the same problem for the step share a key: the EO axis
// for EO, the top colour for the cross alone, the top colour and the EO
// axis for the rest.
int problemKey(Hold h, SolveStep step) {
  int axis = h.front % 3;
  return step == SolveStep::EO      ? axis
         : step == SolveStep::Cross ? h.top * 3
                                    : h.top * 3 + axis;
}

// Holds a step is solved in: the valid ones, each problem once.
std::vector<Hold> distinctHolds(const std::vector<Hold> &holds,
                                SolveStep step) {
  std::vector<Hold> out;
  std::vector<int> keys;
  for (Hold h : holds) {
    if (!isValidHold(h)) {
      continue;
    }
    int key = problemKey(h, step);
    if (std::find(keys.begin(), keys.end(), key) != keys.end()) {
      continue;
    }
    keys.push_back(key);
    out.push_back(h);
  }
  return out;
}

// The cube seen after a whole-cube rotation, each sticker renamed for the
// face whose centre has its colour then.
CubieCube rotated(const CubieCube &c, Move rotation) {
  FaceletCube f = c.toFacelets();
  f.apply(rotation);
  std::array<std::uint8_t, 6> faceOf{};
  for (int face = 0; face < 6; ++face) {
    faceOf[f[face * 9 + 4]] = static_cast<std::uint8_t>(face);
  }
  for (int i = 0; i < kFaceletCount; ++i) {
    f[i] = faceOf[f[i]];
  }
  return CubieCube::fromRealFacelets(f);
}

// Where each edge position goes under a whole-cube rotation.
std::array<int, kEdges> rotatedPositions(Move rotation) {
  FaceletCube labels;
  for (int i = 0; i < kFaceletCount; ++i) {
    labels[i] = static_cast<std::uint8_t>(i);
  }
  labels.apply(rotation);
  std::array<int, kEdges> out{};
  for (std::size_t p = 0; p < kEdges; ++p) {
    int facelet = kEdgeFacelets[p][0];
    for (int j = 0; j < kFaceletCount; ++j) {
      if (labels[j] != facelet) {
        continue;
      }
      for (std::size_t i = 0; i < kEdges; ++i) {
        if (kEdgeFacelets[i][0] == j || kEdgeFacelets[i][1] == j) {
          out[p] = static_cast<int>(i);
        }
      }
    }
  }
  return out;
}

} // namespace

bool hasOptimalSolver(SolveStep step) {
  return step != SolveStep::ZZF2L && step != SolveStep::XEOCross &&
         step != SolveStep::CFOPF2L;
}

std::optional<StepOptimum> solveStep(const CubieCube &cube, SolveStep step,
                                     int maxSolutions) {
  if (!hasOptimalSolver(step)) {
    return std::nullopt;
  }
  Search search(step, static_cast<std::size_t>(std::max(1, maxSolutions)));
  std::optional<int> depth = search.run(stateOf(cube), kMaxDepth);
  if (!depth) {
    return std::nullopt; // not a real cube
  }
  StepOptimum out;
  out.length = *depth;
  out.solutions = search.found();
  out.more = search.more();
  return out;
}

std::optional<int> stepDistance(const CubieCube &cube, SolveStep step) {
  if (!hasOptimalSolver(step)) {
    return std::nullopt;
  }
  Search search(step, 1);
  return search.run(stateOf(cube), kMaxDepth);
}

std::vector<HeldOptimum> solveStepHeld(const Alg &scramble, SolveStep step,
                                       const std::vector<Hold> &holds,
                                       int maxSolutions) {
  return solveStepHeld(cubieOf(scramble), step, holds, maxSolutions);
}

bool sameStepProblem(Hold a, Hold b, SolveStep step) {
  return isValidHold(a) && isValidHold(b) &&
         problemKey(a, step) == problemKey(b, step);
}

std::vector<HeldOptimum> solveStepHeld(const CubieCube &scrambled,
                                       SolveStep step,
                                       const std::vector<Hold> &holds,
                                       int maxSolutions) {
  std::vector<HeldOptimum> out;
  if (!hasOptimalSolver(step)) {
    return out;
  }
  for (Hold h : distinctHolds(holds, step)) {
    if (auto opt = solveStep(solverState(scrambled, h), step, maxSolutions)) {
      out.push_back({h, std::move(*opt)});
    }
  }
  std::stable_sort(out.begin(), out.end(),
                   [](const HeldOptimum &a, const HeldOptimum &b) {
                     return a.optimum.length < b.optimum.length;
                   });
  return out;
}

std::optional<int> heldDistance(const Alg &scramble, SolveStep step,
                                const std::vector<Hold> &holds) {
  if (!hasOptimalSolver(step) || holds.empty()) {
    return std::nullopt;
  }
  using Key = std::tuple<std::string, int, std::vector<Hold>>;
  static std::mutex mutex;
  static std::map<Key, std::optional<int>> cache;
  Key key{toString(scramble), static_cast<int>(step), holds};
  {
    std::scoped_lock lock(mutex);
    if (auto it = cache.find(key); it != cache.end()) {
      return it->second;
    }
  }
  CubieCube scrambled = cubieOf(scramble);
  std::optional<int> best;
  for (Hold h : distinctHolds(holds, step)) {
    // Only a shorter solution than the best so far matters.
    int maxDepth = best ? *best - 1 : kMaxDepth;
    if (maxDepth < 0) {
      break;
    }
    Search search(step, 1);
    if (auto d = search.run(stateOf(solverState(scrambled, h)), maxDepth)) {
      best = d;
    }
  }
  std::scoped_lock lock(mutex);
  // About 150 bytes an entry: a cap keeps a long session's cache bounded,
  // and starting again costs only the searches of the scrambles in use.
  constexpr std::size_t kMaxCached = 4096;
  if (cache.size() >= kMaxCached) {
    cache.clear();
  }
  cache[key] = best;
  return best;
}

std::vector<std::size_t> distinctSolutions(const std::vector<Alg> &solutions,
                                           std::size_t max) {
  // The moves sorted (the same moves in any order), and the faces sorted
  // (the same faces, whatever the amounts).
  auto movesKey = [](const Alg &a) {
    std::vector<std::pair<int, int>> key;
    for (Move m : a) {
      key.emplace_back(static_cast<int>(m.type), m.amount);
    }
    std::sort(key.begin(), key.end());
    return key;
  };
  auto facesKey = [](const Alg &a) {
    std::vector<int> key;
    for (Move m : a) {
      key.push_back(static_cast<int>(m.type));
    }
    std::sort(key.begin(), key.end());
    return key;
  };
  std::vector<std::size_t> out;
  std::vector<std::vector<std::pair<int, int>>> movesSeen;
  std::vector<std::vector<int>> facesSeen;
  std::vector<bool> taken(solutions.size(), false);
  for (int pass = 0; pass < 2; ++pass) {
    for (std::size_t i = 0; i < solutions.size() && out.size() < max; ++i) {
      if (taken[i]) {
        continue;
      }
      auto moves = movesKey(solutions[i]);
      if (std::find(movesSeen.begin(), movesSeen.end(), moves) !=
          movesSeen.end()) {
        continue; // a reordering of one already picked
      }
      auto faces = facesKey(solutions[i]);
      bool newFaces = std::find(facesSeen.begin(), facesSeen.end(), faces) ==
                      facesSeen.end();
      if (pass == 0 && !newFaces) {
        continue; // only if there is room after the others
      }
      taken[i] = true;
      out.push_back(i);
      movesSeen.push_back(std::move(moves));
      facesSeen.push_back(std::move(faces));
    }
  }
  return out;
}

std::vector<Hold> withBothEoAxes(const std::vector<Hold> &holds) {
  std::vector<Hold> out;
  for (Hold h : holds) {
    if (!isValidHold(h)) {
      continue;
    }
    out.push_back(h);
    Alg turned = holdRotation(h);
    turned.push_back(Move{MoveType::Y, 1, 0});
    out.push_back(holdAfter(turned));
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

std::array<bool, kEdges> badEdges(const CubieCube &cube, int axis) {
  std::array<bool, kEdges> out{};
  if (axis == 0) {
    for (std::size_t i = 0; i < kEdges; ++i) {
      out[i] = cube.eo[i] != 0;
    }
    return out;
  }
  // Turned so that the axis faces front and back (a y brings R/L there,
  // an x brings U/D), the axis's orientation is the F/B one.
  Move rotation{axis == 1 ? MoveType::Y : MoveType::X, 1, 0};
  CubieCube turned = rotated(cube, rotation);
  std::array<int, kEdges> to = rotatedPositions(rotation);
  for (std::size_t p = 0; p < kEdges; ++p) {
    out[p] = turned.eo[static_cast<std::size_t>(to[p])] != 0;
  }
  return out;
}

int badEdgeCount(const CubieCube &cube, int axis) {
  auto bad = badEdges(cube, axis);
  return static_cast<int>(std::count(bad.begin(), bad.end(), true));
}

} // namespace vcube
