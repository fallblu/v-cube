#include "cube/solver.hpp"

#include "cube/coord.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

namespace vcube {

namespace {

using coord::flipOf;
using coord::kFlip;
using coord::kMoves;
using coord::kTwist;
using coord::setTwist;
using coord::twistOf;

constexpr int kSlice = 495;       // C(12, 4)
constexpr int kSolvedSlice = 494; // slice edges in positions FR..BR
constexpr int kPerm8 = 40320;     // 8!
constexpr int kPerm4 = 24;        // 4!

// The ten moves of phase 2: U, U2, U', R2, F2, D, D2, D', L2, B2.
constexpr std::array<int, 10> kPhase2Moves = {0, 1, 2, 4, 7, 9, 10, 11, 13, 16};

constexpr bool isPhase2Move(int m) {
  return std::find(kPhase2Moves.begin(), kPhase2Moves.end(), m) !=
         kPhase2Moves.end();
}

int binomial(int n, int k) {
  if (k < 0 || k > n) {
    return 0;
  }
  int r = 1;
  for (int i = 0; i < k; ++i) {
    r = r * (n - i) / (i + 1);
  }
  return r;
}

template <std::size_t N> int rankPermutation(const std::array<int, N> &p) {
  int rank = 0;
  for (std::size_t i = 0; i < N; ++i) {
    int smaller = 0;
    for (std::size_t j = i + 1; j < N; ++j) {
      if (p[j] < p[i]) {
        ++smaller;
      }
    }
    rank = rank * static_cast<int>(N - i) + smaller;
  }
  return rank;
}

template <std::size_t N> std::array<int, N> unrankPermutation(int rank) {
  std::array<int, N> digits{};
  for (std::size_t i = N; i-- > 0;) {
    int base = static_cast<int>(N - i);
    digits[i] = rank % base;
    rank /= base;
  }
  std::vector<int> pool;
  for (std::size_t i = 0; i < N; ++i) {
    pool.push_back(static_cast<int>(i));
  }
  std::array<int, N> p{};
  for (std::size_t i = 0; i < N; ++i) {
    auto d = static_cast<std::size_t>(digits[i]);
    p[i] = pool[d];
    pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(d));
  }
  return p;
}

// --- coordinates ----------------------------------------------------------

// Which 4 positions hold the slice edges (FR, FL, BL, BR), as a
// combination index.
int sliceOf(const CubieCube &c) {
  int idx = 0, k = 0;
  for (std::size_t p = 0; p < kEdges; ++p) {
    if (c.ep[p] >= 8) {
      ++k;
      idx += binomial(static_cast<int>(p), k);
    }
  }
  return idx;
}

void setSlice(CubieCube &c, int idx) {
  std::array<bool, kEdges> slice{};
  for (int k = 4; k >= 1; --k) {
    int p = k - 1;
    while (binomial(p + 1, k) <= idx) {
      ++p;
    }
    slice[static_cast<std::size_t>(p)] = true;
    idx -= binomial(p, k);
  }
  std::uint8_t nextSlice = 8, nextOther = 0;
  for (std::size_t p = 0; p < kEdges; ++p) {
    c.ep[p] = slice[p] ? nextSlice++ : nextOther++;
  }
}

int cornerPermOf(const CubieCube &c) {
  std::array<int, 8> p{};
  for (std::size_t i = 0; i < 8; ++i) {
    p[i] = c.cp[i];
  }
  return rankPermutation(p);
}

void setCornerPerm(CubieCube &c, int r) {
  auto p = unrankPermutation<8>(r);
  for (std::size_t i = 0; i < 8; ++i) {
    c.cp[i] = static_cast<std::uint8_t>(p[i]);
  }
}

// Permutation of the eight U/D edges (valid in phase 2).
int udEdgePermOf(const CubieCube &c) {
  std::array<int, 8> p{};
  for (std::size_t i = 0; i < 8; ++i) {
    p[i] = c.ep[i];
  }
  return rankPermutation(p);
}

void setUdEdgePerm(CubieCube &c, int r) {
  auto p = unrankPermutation<8>(r);
  for (std::size_t i = 0; i < 8; ++i) {
    c.ep[i] = static_cast<std::uint8_t>(p[i]);
  }
  for (std::size_t i = 8; i < 12; ++i) {
    c.ep[i] = static_cast<std::uint8_t>(i);
  }
}

// Permutation of the slice edges within the slice (valid in phase 2).
int slicePermOf(const CubieCube &c) {
  std::array<int, 4> p{};
  for (std::size_t i = 0; i < 4; ++i) {
    p[i] = c.ep[8 + i] - 8;
  }
  return rankPermutation(p);
}

void setSlicePerm(CubieCube &c, int r) {
  auto p = unrankPermutation<4>(r);
  for (std::size_t i = 0; i < 8; ++i) {
    c.ep[i] = static_cast<std::uint8_t>(i);
  }
  for (std::size_t i = 0; i < 4; ++i) {
    c.ep[8 + i] = static_cast<std::uint8_t>(8 + p[i]);
  }
}

// --- tables ---------------------------------------------------------------

struct Tables {
  std::array<CubieCube, kMoves> moveCubes;
  std::vector<std::uint16_t> twistMove, flipMove, sliceMove;
  std::vector<std::uint16_t> cornerPermMove, udEdgePermMove, slicePermMove;
  std::vector<std::int8_t> twistSlicePrune, flipSlicePrune;
  std::vector<std::int8_t> cornerSlicePrune, edgeSlicePrune;

  Tables() : moveCubes(coord::moveCubies()) {
    using coord::buildMoveTable;
    twistMove = buildMoveTable(kTwist, setTwist, twistOf, allMoves());
    flipMove = coord::flipMoveTable();
    sliceMove = buildMoveTable(kSlice, setSlice, sliceOf, allMoves());
    cornerPermMove =
        buildMoveTable(kPerm8, setCornerPerm, cornerPermOf, phase2());
    udEdgePermMove =
        buildMoveTable(kPerm8, setUdEdgePerm, udEdgePermOf, phase2());
    slicePermMove = buildMoveTable(kPerm4, setSlicePerm, slicePermOf, phase2());

    twistSlicePrune = buildPrune(kTwist, kSlice, twistMove, sliceMove,
                                 0 * kSlice + kSolvedSlice, allMoves());
    flipSlicePrune = buildPrune(kFlip, kSlice, flipMove, sliceMove,
                                0 * kSlice + kSolvedSlice, allMoves());
    cornerSlicePrune =
        buildPrune(kPerm8, kPerm4, cornerPermMove, slicePermMove, 0, phase2());
    edgeSlicePrune =
        buildPrune(kPerm8, kPerm4, udEdgePermMove, slicePermMove, 0, phase2());
  }

  static std::vector<int> allMoves() {
    std::vector<int> v(kMoves);
    for (int i = 0; i < kMoves; ++i) {
      v[static_cast<std::size_t>(i)] = i;
    }
    return v;
  }
  static std::vector<int> phase2() {
    return {kPhase2Moves.begin(), kPhase2Moves.end()};
  }

  // Breadth-first distances over the product of two coordinates.
  static std::vector<std::int8_t>
  buildPrune(int sizeA, int sizeB, const std::vector<std::uint16_t> &moveA,
             const std::vector<std::uint16_t> &moveB, int start,
             const std::vector<int> &moves) {
    std::vector<std::int8_t> dist(static_cast<std::size_t>(sizeA * sizeB), -1);
    dist[static_cast<std::size_t>(start)] = 0;
    std::vector<int> frontier{start};
    std::int8_t depth = 0;
    while (!frontier.empty()) {
      std::vector<int> next;
      for (int idx : frontier) {
        int a = idx / sizeB, b = idx % sizeB;
        for (int m : moves) {
          int na = moveA[static_cast<std::size_t>(a * kMoves + m)];
          int nb = moveB[static_cast<std::size_t>(b * kMoves + m)];
          auto n = static_cast<std::size_t>(na * sizeB + nb);
          if (dist[n] < 0) {
            dist[n] = static_cast<std::int8_t>(depth + 1);
            next.push_back(static_cast<int>(n));
          }
        }
      }
      frontier = std::move(next);
      ++depth;
    }
    return dist;
  }
};

const Tables &tables() {
  static const Tables t;
  return t;
}

// --- search ---------------------------------------------------------------

constexpr int kMaxDepth = 32;

// Solutions of at most this many moves are good enough to stop for once
// the search stops finding shorter ones quickly.
constexpr int kGoodLength = 21;
// After each shorter solution the search goes on for at least this long,
// and at least as long again as it has taken so far.
constexpr auto kMinPatience = std::chrono::milliseconds(10);

class Search {
public:
  using Clock = TwoPhaseSolver::Clock;

  // The first solution of at most `maxLength` moves found before `deadline`.
  Search(const CubieCube &cube, int maxLength, Clock::time_point deadline)
      : t_(tables()), cube_(cube), maxLength_(maxLength), deadline_(deadline) {}

  // Anytime: a solution, made shorter while that pays (see improved()).
  Search(const CubieCube &cube, std::chrono::milliseconds budget)
      : t_(tables()), cube_(cube), maxLength_(kMaxDepth - 2),
        deadline_(Clock::time_point::max()), anytime_(true),
        start_(Clock::now()), budget_(budget) {}

  std::optional<Alg> run() {
    int twist = twistOf(cube_), flip = flipOf(cube_), slice = sliceOf(cube_);
    for (int depth1 = 0; depth1 <= maxLength_; ++depth1) {
      if (phase1(twist, flip, slice, 0, depth1, -1)) {
        return result_;
      }
      if (timedOut_) {
        break;
      }
    }
    return best_;
  }

private:
  const Tables &t_;
  CubieCube cube_;
  int maxLength_;
  Clock::time_point deadline_;
  std::array<int, kMaxDepth> moves_{};
  Alg result_;
  long nodes_ = 0;
  bool timedOut_ = false;

  bool anytime_ = false;
  Clock::time_point start_;
  std::chrono::milliseconds budget_{0};
  std::optional<Alg> best_;
  std::optional<Clock::time_point> polishEnd_;

  // Anytime: result_ is the shortest solution so far. Only shorter ones
  // are looked for from now on. Until one of at most kGoodLength moves is
  // found the search goes on for the whole budget (and past it until there
  // is any solution); after that it stops when no shorter one has turned
  // up for a while (kMinPatience), or a fifth of the budget after the
  // first good one.
  void improved() {
    best_ = result_;
    maxLength_ = static_cast<int>(result_.size()) - 1;
    auto now = Clock::now();
    if (static_cast<int>(result_.size()) > kGoodLength) {
      deadline_ = start_ + budget_;
      return;
    }
    if (!polishEnd_) {
      polishEnd_ = now + budget_ / 5;
    }
    auto patience =
        std::max<Clock::duration>(now - start_, Clock::duration(kMinPatience));
    deadline_ = std::min({start_ + budget_, *polishEnd_, now + patience});
  }

  bool checkTime() {
    if ((++nodes_ & 0xFFF) == 0 && TwoPhaseSolver::Clock::now() > deadline_) {
      timedOut_ = true;
    }
    return timedOut_;
  }

  int prune1(int twist, int flip, int slice) const {
    return std::max(
        t_.twistSlicePrune[static_cast<std::size_t>(twist * kSlice + slice)],
        t_.flipSlicePrune[static_cast<std::size_t>(flip * kSlice + slice)]);
  }

  int prune2(int corner, int edge, int slicePerm) const {
    return std::max(
        t_.cornerSlicePrune[static_cast<std::size_t>(corner * kPerm4 +
                                                     slicePerm)],
        t_.edgeSlicePrune[static_cast<std::size_t>(edge * kPerm4 + slicePerm)]);
  }

  bool phase1(int twist, int flip, int slice, int depth, int togo,
              int prevFace) {
    if (depth + togo > maxLength_) {
      return false; // a shorter solution was found meanwhile
    }
    if (togo == 0) {
      if (twist != 0 || flip != 0 || slice != kSolvedSlice) {
        return false;
      }
      // If the last move kept us in the subgroup we were already there one
      // move earlier, and that shorter phase 1 has been tried.
      if (depth > 0 &&
          isPhase2Move(moves_[static_cast<std::size_t>(depth - 1)])) {
        return false;
      }
      return startPhase2(depth);
    }
    if (checkTime()) {
      return false;
    }
    for (int face = 0; face < 6; ++face) {
      if (coord::skipFace(face, prevFace)) {
        continue;
      }
      for (int a = 0; a < 3; ++a) {
        int m = face * 3 + a;
        auto mi = static_cast<std::size_t>(m);
        int nt = t_.twistMove[static_cast<std::size_t>(twist * kMoves) + mi];
        int nf = t_.flipMove[static_cast<std::size_t>(flip * kMoves) + mi];
        int ns = t_.sliceMove[static_cast<std::size_t>(slice * kMoves) + mi];
        if (prune1(nt, nf, ns) > togo - 1) {
          continue;
        }
        moves_[static_cast<std::size_t>(depth)] = m;
        if (phase1(nt, nf, ns, depth + 1, togo - 1, face)) {
          return true;
        }
        if (timedOut_) {
          return false;
        }
      }
    }
    return false;
  }

  bool startPhase2(int depth1) {
    CubieCube c = cube_;
    for (int i = 0; i < depth1; ++i) {
      c = c * t_.moveCubes[static_cast<std::size_t>(
                  moves_[static_cast<std::size_t>(i)])];
    }
    int corner = cornerPermOf(c), edge = udEdgePermOf(c),
        slicePerm = slicePermOf(c);
    int maxDepth2 = std::min(maxLength_ - depth1, 18);
    int prevFace =
        depth1 > 0 ? moves_[static_cast<std::size_t>(depth1 - 1)] / 3 : -1;
    for (int d2 = prune2(corner, edge, slicePerm); d2 <= maxDepth2; ++d2) {
      if (phase2(corner, edge, slicePerm, depth1, d2, prevFace)) {
        result_.clear();
        for (int i = 0; i < depth1 + d2; ++i) {
          int m = moves_[static_cast<std::size_t>(i)];
          result_.push_back(coord::moveOf(m));
        }
        if (!anytime_) {
          return true;
        }
        improved();
        return false; // a longer phase 2 from here can't be shorter
      }
      if (timedOut_) {
        return false;
      }
    }
    return false;
  }

  bool phase2(int corner, int edge, int slicePerm, int depth, int togo,
              int prevFace) {
    if (togo == 0) {
      return corner == 0 && edge == 0 && slicePerm == 0;
    }
    if (checkTime()) {
      return false;
    }
    for (int m : kPhase2Moves) {
      int face = m / 3;
      if (coord::skipFace(face, prevFace)) {
        continue;
      }
      auto mi = static_cast<std::size_t>(m);
      int nc =
          t_.cornerPermMove[static_cast<std::size_t>(corner * kMoves) + mi];
      int ne = t_.udEdgePermMove[static_cast<std::size_t>(edge * kMoves) + mi];
      int ns =
          t_.slicePermMove[static_cast<std::size_t>(slicePerm * kMoves) + mi];
      if (prune2(nc, ne, ns) > togo - 1) {
        continue;
      }
      moves_[static_cast<std::size_t>(depth)] = m;
      if (phase2(nc, ne, ns, depth + 1, togo - 1, face)) {
        return true;
      }
      if (timedOut_) {
        return false;
      }
    }
    return false;
  }
};

} // namespace

void TwoPhaseSolver::warmUp() { (void)tables(); }

std::optional<Alg> TwoPhaseSolver::solve(const CubieCube &cube, int maxLength,
                                         Clock::time_point deadline) {
  if (!cube.isValid()) {
    return std::nullopt;
  }
  if (cube.isSolved()) {
    return Alg{};
  }
  return Search(cube, std::min(maxLength, kMaxDepth - 1), deadline).run();
}

Alg TwoPhaseSolver::solve(const CubieCube &cube,
                          std::chrono::milliseconds budget) {
  warmUp();
  if (!cube.isValid() || cube.isSolved()) {
    return {};
  }
  // One search that keeps the shortest solution so far and stops once
  // shorter ones stop coming quickly (or exhausts the space, which is
  // quick near solved).
  return Search(cube, budget).run().value_or(Alg{});
}

} // namespace vcube
