#include "cube/solver.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <vector>

namespace vcube {

namespace {

constexpr int kTwist = 2187;      // 3^7
constexpr int kFlip = 2048;       // 2^11
constexpr int kSlice = 495;       // C(12, 4)
constexpr int kSolvedSlice = 494; // slice edges in positions FR..BR
constexpr int kPerm8 = 40320;     // 8!
constexpr int kPerm4 = 24;        // 4!
constexpr int kMoves = 18;        // face * 3 + (amount - 1)

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

int twistOf(const CubieCube &c) {
  int t = 0;
  for (std::size_t i = 0; i < 7; ++i) {
    t = t * 3 + c.co[i];
  }
  return t;
}

void setTwist(CubieCube &c, int t) {
  int sum = 0;
  for (std::size_t i = 7; i-- > 0;) {
    c.co[i] = static_cast<std::uint8_t>(t % 3);
    sum += c.co[i];
    t /= 3;
  }
  c.co[7] = static_cast<std::uint8_t>((3 - sum % 3) % 3);
}

int flipOf(const CubieCube &c) {
  int f = 0;
  for (std::size_t i = 0; i < 11; ++i) {
    f = f * 2 + c.eo[i];
  }
  return f;
}

void setFlip(CubieCube &c, int f) {
  int sum = 0;
  for (std::size_t i = 11; i-- > 0;) {
    c.eo[i] = static_cast<std::uint8_t>(f % 2);
    sum += c.eo[i];
    f /= 2;
  }
  c.eo[11] = static_cast<std::uint8_t>(sum % 2);
}

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

  Tables() {
    for (int f = 0; f < 6; ++f) {
      CubieCube c;
      for (int a = 0; a < 3; ++a) {
        c = c * faceTurnCubie(static_cast<Face>(f));
        moveCubes[static_cast<std::size_t>(f * 3 + a)] = c;
      }
    }
    twistMove = buildMove(kTwist, setTwist, twistOf, allMoves());
    flipMove = buildMove(kFlip, setFlip, flipOf, allMoves());
    sliceMove = buildMove(kSlice, setSlice, sliceOf, allMoves());
    cornerPermMove = buildMove(kPerm8, setCornerPerm, cornerPermOf, phase2());
    udEdgePermMove = buildMove(kPerm8, setUdEdgePerm, udEdgePermOf, phase2());
    slicePermMove = buildMove(kPerm4, setSlicePerm, slicePermOf, phase2());

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

  template <typename Set, typename Get>
  std::vector<std::uint16_t> buildMove(int size, Set set, Get get,
                                       const std::vector<int> &moves) const {
    std::vector<std::uint16_t> table(static_cast<std::size_t>(size * kMoves),
                                     0);
    for (int i = 0; i < size; ++i) {
      CubieCube c;
      set(c, i);
      for (int m : moves) {
        CubieCube n = c * moveCubes[static_cast<std::size_t>(m)];
        table[static_cast<std::size_t>(i * kMoves + m)] =
            static_cast<std::uint16_t>(get(n));
      }
    }
    return table;
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

class Search {
public:
  Search(const CubieCube &cube, int maxLength,
         TwoPhaseSolver::Clock::time_point deadline)
      : t_(tables()), cube_(cube), maxLength_(maxLength), deadline_(deadline) {}

  std::optional<Alg> run() {
    int twist = twistOf(cube_), flip = flipOf(cube_), slice = sliceOf(cube_);
    for (int depth1 = 0; depth1 <= maxLength_; ++depth1) {
      if (phase1(twist, flip, slice, 0, depth1, -1)) {
        return result_;
      }
      if (timedOut_) {
        return std::nullopt;
      }
    }
    return std::nullopt;
  }

private:
  const Tables &t_;
  CubieCube cube_;
  int maxLength_;
  TwoPhaseSolver::Clock::time_point deadline_;
  std::array<int, kMaxDepth> moves_{};
  Alg result_;
  long nodes_ = 0;
  bool timedOut_ = false;

  bool checkTime() {
    if ((++nodes_ & 0xFFF) == 0 && TwoPhaseSolver::Clock::now() > deadline_) {
      timedOut_ = true;
    }
    return timedOut_;
  }

  static bool skipFace(int face, int prevFace) {
    if (prevFace < 0) {
      return false;
    }
    // Same face, or the opposite face in the non-canonical order.
    return face == prevFace || (face % 3 == prevFace % 3 && face < prevFace);
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
      if (skipFace(face, prevFace)) {
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
          result_.push_back({static_cast<MoveType>(m / 3),
                             static_cast<std::uint8_t>(m % 3 + 1)});
        }
        return true;
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
      if (skipFace(face, prevFace)) {
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
  auto start = Clock::now();
  std::optional<Alg> best = solve(cube, 21, start + budget);
  if (!best) {
    best = solve(cube, 24, Clock::now() + budget);
  }
  if (!best) {
    return solve(cube, 30, Clock::time_point::max()).value_or(Alg{});
  }
  // Spend a little more time looking for shorter solutions; this mostly
  // pays off for states near solved (subset and case scrambles).
  auto improveUntil = Clock::now() + budget / 5;
  while (!best->empty() && Clock::now() < improveUntil) {
    auto shorter =
        solve(cube, static_cast<int>(best->size()) - 1, improveUntil);
    if (!shorter) {
      break;
    }
    best = shorter;
  }
  return *best;
}

} // namespace vcube
