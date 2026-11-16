#include "train/f2l_search.hpp"

#include "cube/coord.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>

namespace vcube {

namespace {

using coord::kMoves;

constexpr std::uint8_t kUnseen = 0xff;
constexpr std::size_t kTableSize = 24 * 24 * 24 * 24;
constexpr std::array<Edge, 4> kCrossEdges = {DR, DF, DL, DB};
// The two cross edges beside each slot (in Slot order: FR, FL, BL, BR).
constexpr std::array<std::array<Edge, 2>, kSlots> kSlotCross = {
    {{DF, DR}, {DF, DL}, {DB, DL}, {DB, DR}}};

// Where each piece value (place * 3 + twist, place * 2 + flip) goes under
// each face turn.
struct MoveTables {
  std::array<std::uint8_t, 24 * kMoves> corner{};
  std::array<std::uint8_t, 24 * kMoves> edge{};
};

const MoveTables &moveTables() {
  static const MoveTables tables = [] {
    MoveTables t;
    const std::array<CubieCube, kMoves> cubes = coord::moveCubies();
    for (int m = 0; m < kMoves; ++m) {
      const CubieCube &mc = cubes[static_cast<std::size_t>(m)];
      for (std::size_t i = 0; i < kCorners; ++i) {
        for (int twist = 0; twist < 3; ++twist) {
          int from = mc.cp[i] * 3 + twist;
          t.corner[static_cast<std::size_t>(from * kMoves + m)] =
              static_cast<std::uint8_t>(static_cast<int>(i) * 3 +
                                        (twist + mc.co[i]) % 3);
        }
      }
      for (std::size_t i = 0; i < kEdges; ++i) {
        for (int flip = 0; flip < 2; ++flip) {
          int from = mc.ep[i] * 2 + flip;
          t.edge[static_cast<std::size_t>(from * kMoves + m)] =
              static_cast<std::uint8_t>(static_cast<int>(i) * 2 +
                                        (flip + mc.eo[i]) % 2);
        }
      }
    }
    return t;
  }();
  return tables;
}

std::size_t indexOf(int a, int b, int c, int d) {
  return static_cast<std::size_t>(((a * 24 + b) * 24 + c) * 24 + d);
}

// The distance to `start` of every arrangement of four pieces (each a
// corner or an edge) under `moves`.
std::vector<std::uint8_t> distances(std::array<int, 4> start,
                                    std::array<bool, 4> isCorner,
                                    const std::vector<int> &moves) {
  const MoveTables &mt = moveTables();
  std::vector<std::uint8_t> dist(kTableSize, kUnseen);
  std::vector<std::uint32_t> frontier{static_cast<std::uint32_t>(
      indexOf(start[0], start[1], start[2], start[3]))};
  dist[frontier[0]] = 0;
  for (std::uint8_t depth = 0; !frontier.empty(); ++depth) {
    std::vector<std::uint32_t> next;
    for (std::uint32_t idx : frontier) {
      std::array<int, 4> v = {
          static_cast<int>(idx / 13824), static_cast<int>(idx / 576 % 24),
          static_cast<int>(idx / 24 % 24), static_cast<int>(idx % 24)};
      for (int m : moves) {
        std::array<int, 4> n{};
        for (std::size_t i = 0; i < 4; ++i) {
          auto at = static_cast<std::size_t>(v[i] * kMoves + m);
          n[i] = isCorner[i] ? mt.corner[at] : mt.edge[at];
        }
        std::size_t to = indexOf(n[0], n[1], n[2], n[3]);
        if (dist[to] == kUnseen) {
          dist[to] = static_cast<std::uint8_t>(depth + 1);
          next.push_back(static_cast<std::uint32_t>(to));
        }
      }
    }
    frontier = std::move(next);
  }
  return dist;
}

int cornerHome(Slot s) { return slotCorner(s) * 3; }
int edgeHome(Slot s) { return slotEdge(s) * 2; }

// Lower bounds for one set of face turns: each slot's pair with the two
// cross edges beside it, the four cross edges, and each two slots' pairs.
struct Tables {
  std::array<std::vector<std::uint8_t>, kSlots> slot;
  std::vector<std::uint8_t> cross;
  std::array<std::array<std::vector<std::uint8_t>, kSlots>, kSlots> two;
};

const Tables &tablesFor(std::uint32_t faceMask) {
  static std::mutex mutex;
  static std::map<std::uint32_t, std::unique_ptr<Tables>> cache;
  std::scoped_lock lock(mutex);
  std::unique_ptr<Tables> &entry = cache[faceMask];
  if (entry) {
    return *entry;
  }
  std::vector<int> moves;
  for (int m = 0; m < kMoves; ++m) {
    if (faceMask >> m & 1U) {
      moves.push_back(m);
    }
  }
  auto t = std::make_unique<Tables>();
  for (int s = 0; s < kSlots; ++s) {
    auto slot = static_cast<Slot>(s);
    auto at = static_cast<std::size_t>(s);
    t->slot[at] = distances({cornerHome(slot), edgeHome(slot),
                             kSlotCross[at][0] * 2, kSlotCross[at][1] * 2},
                            {true, false, false, false}, moves);
    for (int o = s + 1; o < kSlots; ++o) {
      auto other = static_cast<Slot>(o);
      t->two[at][static_cast<std::size_t>(o)] =
          distances({cornerHome(slot), edgeHome(slot), cornerHome(other),
                     edgeHome(other)},
                    {true, false, true, false}, moves);
    }
  }
  t->cross = distances({kCrossEdges[0] * 2, kCrossEdges[1] * 2,
                        kCrossEdges[2] * 2, kCrossEdges[3] * 2},
                       {false, false, false, false}, moves);
  entry = std::move(t);
  return *entry;
}

// The ways the cube is held after wide turns: which face of the cube (by
// its centre) each face as held is.
using Frame = std::array<std::uint8_t, 6>;
constexpr int kU = 0, kR = 1, kF = 2, kD = 3, kL = 4, kB = 5;

Frame afterX(const Frame &f) {
  // x turns the cube as R does: the front comes to the top.
  Frame n = f;
  n[kU] = f[kF];
  n[kF] = f[kD];
  n[kD] = f[kB];
  n[kB] = f[kU];
  return n;
}

Frame afterY(const Frame &f) {
  // y turns the cube as U does: the right comes to the front.
  Frame n = f;
  n[kF] = f[kR];
  n[kR] = f[kB];
  n[kB] = f[kL];
  n[kL] = f[kF];
  return n;
}

struct Frames {
  std::vector<Frame> frames;            // [0] is the cube as it starts
  std::vector<std::array<int, 2>> next; // [frame][0 x, 1 y]

  Frames() {
    frames.push_back({0, 1, 2, 3, 4, 5});
    for (std::size_t i = 0; i < frames.size(); ++i) {
      std::array<int, 2> n{};
      for (int axis = 0; axis < 2; ++axis) {
        Frame to = axis == 0 ? afterX(frames[i]) : afterY(frames[i]);
        auto it = std::find(frames.begin(), frames.end(), to);
        n[static_cast<std::size_t>(axis)] =
            static_cast<int>(it - frames.begin());
        if (it == frames.end()) {
          frames.push_back(to);
        }
      }
      next.push_back(n);
    }
  }
  int turned(int frame, int axis, int quarters) const {
    for (int i = 0; i < quarters; ++i) {
      frame =
          next[static_cast<std::size_t>(frame)][static_cast<std::size_t>(axis)];
    }
    return frame;
  }
};

const Frames &frames() {
  static const Frames f;
  return f;
}

// A move as written, and what it does in each frame.
struct Written {
  Move move;
  int face = 0; // as held: the face turned (for a wide turn, the far one)
  bool wide = false;
  int axis = 0;     // a wide turn's rotation: 0 x, 1 y
  int quarters = 0; // ...and how many quarter turns of it
};

std::vector<Written> writtenMoves(const PairMoves &moves) {
  std::vector<Written> out;
  auto face = [&out](MoveType t, int f, bool quarter, bool half) {
    for (int a = 1; a <= 3; ++a) {
      if (a == 2 ? half : quarter) {
        out.push_back(
            {Move{t, static_cast<std::uint8_t>(a), 0}, f, false, 0, 0});
      }
    }
  };
  face(MoveType::U, kU, true, true);
  face(MoveType::R, kR, true, true);
  face(MoveType::L, kL, true, true);
  if (moves.d) {
    face(MoveType::D, kD, true, true);
  }
  if (moves.quarterFB || moves.halfFB) {
    face(MoveType::F, kF, moves.quarterFB, true);
    face(MoveType::B, kB, moves.quarterFB, true);
  }
  if (moves.wide) {
    // r = L x, l = R x', u = D y, d = U y'.
    auto wide = [&out](MoveType t, int far, int axis, bool inverse) {
      for (int a = 1; a <= 3; ++a) {
        out.push_back({Move{t, static_cast<std::uint8_t>(a), 0}, far, true,
                       axis, inverse ? (4 - a) % 4 : a});
      }
    };
    wide(MoveType::Rw, kL, 0, false);
    wide(MoveType::Lw, kR, 0, true);
    wide(MoveType::Uw, kD, 1, false);
    wide(MoveType::Dw, kU, 1, true);
  }
  return out;
}

int valueOfCorner(const CubieCube &c, Corner piece) {
  for (std::size_t i = 0; i < kCorners; ++i) {
    if (c.cp[i] == piece) {
      return static_cast<int>(i) * 3 + c.co[i];
    }
  }
  return 0;
}

int valueOfEdge(const CubieCube &c, Edge piece) {
  for (std::size_t i = 0; i < kEdges; ++i) {
    if (c.ep[i] == piece) {
      return static_cast<int>(i) * 2 + c.eo[i];
    }
  }
  return 0;
}

class Search {
public:
  Search(const CubieCube &state, std::vector<Slot> slots,
         const PairSearchOptions &options, const std::atomic<bool> *stop)
      : mt_(moveTables()), fr_(frames()), slots_(std::move(slots)),
        written_(writtenMoves(options.moves)), options_(options), stop_(stop) {
    std::uint32_t mask = 0;
    if (options.moves.wide) {
      mask = (1U << kMoves) - 1;
    } else {
      for (const Written &w : written_) {
        mask |= 1U << (w.face * 3 + w.move.amount - 1);
      }
    }
    tables_ = &tablesFor(mask);
    for (std::size_t i = 0; i < slots_.size(); ++i) {
      start_.c[i] = static_cast<std::uint8_t>(
          valueOfCorner(state, slotCorner(slots_[i])));
      start_.e[i] =
          static_cast<std::uint8_t>(valueOfEdge(state, slotEdge(slots_[i])));
    }
    for (std::size_t i = 0; i < 4; ++i) {
      start_.x[i] =
          static_cast<std::uint8_t>(valueOfEdge(state, kCrossEdges[i]));
    }
    // What each written move is in each frame.
    const std::size_t nf = fr_.frames.size();
    centre_.resize(nf * written_.size());
    next_.resize(nf * written_.size());
    for (std::size_t f = 0; f < nf; ++f) {
      for (std::size_t i = 0; i < written_.size(); ++i) {
        const Written &w = written_[i];
        int face = fr_.frames[f][static_cast<std::size_t>(w.face)];
        centre_[f * written_.size() + i] = face * 3 + w.move.amount - 1;
        next_[f * written_.size() + i] =
            w.wide ? fr_.turned(static_cast<int>(f), w.axis, w.quarters)
                   : static_cast<int>(f);
      }
    }
  }

  PairSearchResult run() {
    PairSearchResult out;
    int h = heuristic(start_);
    if (h >= kUnseen) {
      return out;
    }
    int last = options_.maxLength;
    for (int depth = std::max(h, 1); depth <= last && !halted_; ++depth) {
      path_.clear();
      dfs(start_, 0, depth, -1, -1);
      if (!found_.empty() && out.shortest < 0) {
        out.shortest = depth;
        last = std::min(last, depth + std::max(0, options_.extra));
      }
    }
    out.algs = std::move(found_);
    out.complete = !halted_;
    return out;
  }

private:
  struct Node {
    std::array<std::uint8_t, 4> c{}; // per slot of slots_
    std::array<std::uint8_t, 4> e{};
    std::array<std::uint8_t, 4> x{}; // the cross edges
  };

  const MoveTables &mt_;
  const Frames &fr_;
  const Tables *tables_ = nullptr;
  std::vector<Slot> slots_;
  std::vector<Written> written_;
  PairSearchOptions options_;
  const std::atomic<bool> *stop_;
  Node start_;
  std::vector<int> centre_;
  std::vector<int> next_;
  std::vector<int> path_;
  std::vector<Alg> found_;
  std::uint64_t nodes_ = 0;
  bool halted_ = false;

  int crossAt(const Node &n, Edge e) const {
    for (std::size_t i = 0; i < 4; ++i) {
      if (kCrossEdges[i] == e) {
        return n.x[i];
      }
    }
    return 0;
  }

  int heuristic(const Node &n) const {
    int h = tables_->cross[indexOf(n.x[0], n.x[1], n.x[2], n.x[3])];
    for (std::size_t i = 0; i < slots_.size(); ++i) {
      auto s = static_cast<std::size_t>(slots_[i]);
      h = std::max<int>(
          h,
          tables_->slot[s][indexOf(n.c[i], n.e[i], crossAt(n, kSlotCross[s][0]),
                                   crossAt(n, kSlotCross[s][1]))]);
      for (std::size_t j = i + 1; j < slots_.size(); ++j) {
        auto o = static_cast<std::size_t>(slots_[j]);
        const auto &two = s < o ? tables_->two[s][o] : tables_->two[o][s];
        h = std::max<int>(h,
                          s < o ? two[indexOf(n.c[i], n.e[i], n.c[j], n.e[j])]
                                : two[indexOf(n.c[j], n.e[j], n.c[i], n.e[i])]);
      }
    }
    return h;
  }

  bool held(int frame) const {
    return fr_.frames[static_cast<std::size_t>(frame)][kD] == kD;
  }

  void keep() {
    Alg alg;
    for (int i : path_) {
      alg.push_back(written_[static_cast<std::size_t>(i)].move);
    }
    found_.push_back(std::move(alg));
    if (static_cast<int>(found_.size()) >= options_.maxResults) {
      halted_ = true;
    }
  }

  // `lastWritten` is the move before (-1: none) and `lastFace` the face it
  // turned of the cube, or -1 after a wide turn.
  void dfs(const Node &n, int frame, int remaining, int lastWritten,
           int lastFace) {
    if (halted_) {
      return;
    }
    if ((++nodes_ & 0xfff) == 0 &&
        (nodes_ > options_.maxNodes ||
         (stop_ && stop_->load(std::memory_order_relaxed)))) {
      halted_ = true;
      return;
    }
    const std::size_t nw = written_.size();
    for (std::size_t i = 0; i < nw; ++i) {
      const Written &w = written_[i];
      if (lastWritten >= 0) {
        const Written &before = written_[static_cast<std::size_t>(lastWritten)];
        // Never the same layer twice, nor a wide turn beside a turn about
        // its axis: together those are a slice turn, or cancel.
        if (before.move.type == w.move.type ||
            ((before.wide || w.wide) && before.face % 3 == w.face % 3)) {
          continue;
        }
      }
      int m = centre_[static_cast<std::size_t>(frame) * nw + i];
      int face = m / 3;
      if (!w.wide && coord::skipFace(face, lastFace)) {
        continue;
      }
      Node to;
      for (std::size_t s = 0; s < slots_.size(); ++s) {
        to.c[s] = mt_.corner[static_cast<std::size_t>(n.c[s] * kMoves + m)];
        to.e[s] = mt_.edge[static_cast<std::size_t>(n.e[s] * kMoves + m)];
      }
      for (std::size_t s = 0; s < 4; ++s) {
        to.x[s] = mt_.edge[static_cast<std::size_t>(n.x[s] * kMoves + m)];
      }
      int h = heuristic(to);
      if (h > remaining - 1) {
        continue;
      }
      int toFrame = next_[static_cast<std::size_t>(frame) * nw + i];
      if (h == 0 && held(toFrame)) {
        // The pair is solved: a solution if this is the last move, and
        // nothing to go on from otherwise.
        if (remaining == 1) {
          path_.push_back(static_cast<int>(i));
          keep();
          path_.pop_back();
          if (halted_) {
            return;
          }
        }
        continue;
      }
      if (remaining == 1) {
        continue;
      }
      path_.push_back(static_cast<int>(i));
      dfs(to, toFrame, remaining - 1, static_cast<int>(i), w.wide ? -1 : face);
      path_.pop_back();
      if (halted_) {
        return;
      }
    }
  }
};

} // namespace

PairSearchResult searchPair(const CubieCube &state, Slot target,
                            const PairSearchOptions &options,
                            const std::atomic<bool> *stop) {
  for (Edge e : kCrossEdges) {
    if (!edgeSolved(state, e)) {
      return {};
    }
  }
  if (slotSolved(state, target)) {
    return {};
  }
  std::vector<Slot> slots{target};
  for (int s = 0; s < kSlots; ++s) {
    auto slot = static_cast<Slot>(s);
    if (slot != target && slotSolved(state, slot)) {
      slots.push_back(slot);
    }
  }
  return Search(state, std::move(slots), options, stop).run();
}

} // namespace vcube
