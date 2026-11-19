#include "train/alg_search.hpp"

#include "cube/facelet.hpp"

#include <algorithm>
#include <bit>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>

namespace vcube {

namespace {

constexpr std::uint8_t kUnseen = 0xff;
// The most entries an edge table may have (one byte each).
constexpr std::uint64_t kEdgeBudget = 48'000'000;
// Tables kept between searches, in bytes.
constexpr std::size_t kCacheBudget = 900'000'000;
constexpr std::array<int, 4> kAufs = {0, 1, 3, 2};

std::string bytesOf(const CubieCube &c) {
  std::string s;
  s.append(c.cp.begin(), c.cp.end());
  s.append(c.co.begin(), c.co.end());
  s.append(c.ep.begin(), c.ep.end());
  s.append(c.eo.begin(), c.eo.end());
  return s;
}

std::string bytesOf(const PieceMask &m) {
  std::string s;
  for (bool b : m.cp) {
    s += b ? '1' : '0';
  }
  for (bool b : m.co) {
    s += b ? '1' : '0';
  }
  for (bool b : m.ep) {
    s += b ? '1' : '0';
  }
  for (bool b : m.eo) {
    s += b ? '1' : '0';
  }
  return s;
}

// ---------------------------------------------------------------------
// The ways the cube can be held, and what each move does in each.

// The rotation a move makes of the whole cube besides turning layers, as
// seen from the centres: r turns the centres as x does, M as x', ...
Alg rotationPart(Move m) {
  using enum MoveType;
  MoveType r = X;
  bool inverse = false;
  switch (m.type) {
  case Rw:
    r = X;
    break;
  case Lw:
    r = X;
    inverse = true;
    break;
  case Uw:
    r = Y;
    break;
  case Dw:
    r = Y;
    inverse = true;
    break;
  case Fw:
    r = Z;
    break;
  case Bw:
    r = Z;
    inverse = true;
    break;
  case M:
    r = X;
    inverse = true;
    break;
  case E:
    r = Y;
    inverse = true;
    break;
  case S:
    r = Z;
    break;
  default:
    return {};
  }
  int amount = m.amount;
  if (inverse) {
    amount = (4 - amount) % 4;
  }
  return {Move{r, static_cast<std::uint8_t>(amount), 0}};
}

struct MoveModel {
  std::vector<Move> moves;           // the move set's moves
  std::vector<CubieCube> effects;    // distinct effects on the pieces
  int frames = 1;                    // ways of holding the cube reached
  std::vector<int> effectOf;         // [frame * moves + move]
  std::vector<int> nextFrame;        // [frame * moves + move]
  std::vector<std::uint8_t> centreU; // [frame * moves + move]: a U turn
  std::string key;                   // the effects, for table caching
};

MoveModel buildModel(const MoveSet &set) {
  MoveModel m;
  m.moves = set.moves();
  const std::size_t nm = m.moves.size();
  std::map<FaceletCube::Stickers, int> frameIds;
  std::vector<Alg> frameRot;
  auto frameOf = [&](const Alg &rot) {
    FaceletCube f;
    f.apply(rot);
    auto [it, added] =
        frameIds.emplace(f.stickers(), static_cast<int>(frameRot.size()));
    if (added) {
      frameRot.push_back(rot);
    }
    return it->second;
  };
  frameOf({});
  std::map<std::string, int> effectIds;
  const CubieCube u1 = uTurn(1), u2 = uTurn(2), u3 = uTurn(3);
  for (std::size_t f = 0; f < frameRot.size(); ++f) {
    Alg rot = frameRot[f];
    Alg back = invert(rot);
    for (std::size_t i = 0; i < nm; ++i) {
      Alg a = rot;
      a.push_back(m.moves[i]);
      a.insert(a.end(), back.begin(), back.end());
      CubieCube e = cubieOf(a);
      auto [it, added] =
          effectIds.emplace(bytesOf(e), static_cast<int>(m.effects.size()));
      if (added) {
        m.effects.push_back(e);
      }
      Alg next = rot;
      Alg part = rotationPart(m.moves[i]);
      next.insert(next.end(), part.begin(), part.end());
      int nf = frameOf(next); // may add a frame, looked at in turn
      m.effectOf.push_back(it->second);
      m.nextFrame.push_back(nf);
      m.centreU.push_back(e == u1 || e == u2 || e == u3);
    }
  }
  m.frames = static_cast<int>(frameRot.size());
  for (const auto &e : m.effects) {
    m.key += bytesOf(e);
  }
  return m;
}

// ---------------------------------------------------------------------
// Pattern databases.

// Runs a breadth-first search from `seeds` over `size` states, `next(i,
// e)` giving state i after effect e, on `threads` threads.
template <typename Next>
std::vector<std::uint8_t> distances(std::size_t size,
                                    const std::vector<std::size_t> &seeds,
                                    int effects, int threads, Next next) {
  std::vector<std::uint8_t> dist(size, kUnseen);
  for (std::size_t s : seeds) {
    dist[s] = 0;
  }
  for (int depth = 0; depth < 254; ++depth) {
    std::atomic<bool> any{false};
    std::atomic<std::size_t> chunk{0};
    constexpr std::size_t kChunk = 1 << 16;
    auto work = [&] {
      bool found = false;
      for (;;) {
        std::size_t begin = chunk.fetch_add(kChunk);
        if (begin >= size) {
          break;
        }
        std::size_t end = std::min(size, begin + kChunk);
        for (std::size_t i = begin; i < end; ++i) {
          if (std::atomic_ref<std::uint8_t>(dist[i]).load(
                  std::memory_order_relaxed) != depth) {
            continue;
          }
          for (int e = 0; e < effects; ++e) {
            std::size_t n = next(i, e);
            std::atomic_ref<std::uint8_t> d(dist[n]);
            if (d.load(std::memory_order_relaxed) == kUnseen) {
              d.store(static_cast<std::uint8_t>(depth + 1),
                      std::memory_order_relaxed);
              found = true;
            }
          }
        }
      }
      if (found) {
        any.store(true, std::memory_order_relaxed);
      }
    };
    int n = size < (1u << 18) ? 1 : threads;
    std::vector<std::jthread> pool;
    for (int t = 1; t < n; ++t) {
      pool.emplace_back(work);
    }
    work();
    pool.clear();
    if (!any.load()) {
      break;
    }
  }
  return dist;
}

std::uint64_t pow3(int n) {
  std::uint64_t r = 1;
  for (int i = 0; i < n; ++i) {
    r *= 3;
  }
  return r;
}

// The corners alone: where each kind of corner is (corners whose place is
// free are one kind), and the twist at each place, over the places the
// moves turn.
struct CornerTable {
  std::vector<int> places; // the positions in play
  std::array<int, kCorners> local{};
  std::array<std::uint8_t, kCorners> label{}; // by piece
  bool derive = false; // the last place's twist follows from the others
  std::uint32_t coSize = 1;
  std::vector<std::array<std::uint8_t, kCorners>> arrangements;
  std::unordered_map<std::uint32_t, std::uint32_t> rankOf;
  int effects = 0;
  std::vector<std::uint32_t> cpMove, coMove;
  std::vector<std::uint8_t> dist;

  std::uint32_t code(const std::array<std::uint8_t, kCorners> &lab) const {
    std::uint32_t c = 0;
    for (std::size_t i = 0; i < places.size(); ++i) {
      c = c * 16 + lab[i];
    }
    return c;
  }
  std::uint32_t coIndex(const std::array<std::uint8_t, kCorners> &co) const {
    std::uint32_t c = 0;
    std::size_t n = places.size() - (derive ? 1 : 0);
    for (std::size_t i = 0; i < n; ++i) {
      c = c * 3 + co[i];
    }
    return c;
  }
  std::array<std::uint8_t, kCorners> coDigits(std::uint32_t c) const {
    std::array<std::uint8_t, kCorners> co{};
    std::size_t n = places.size() - (derive ? 1 : 0);
    int sum = 0;
    for (std::size_t i = n; i-- > 0;) {
      co[i] = static_cast<std::uint8_t>(c % 3);
      sum += co[i];
      c /= 3;
    }
    if (derive) {
      co[places.size() - 1] = static_cast<std::uint8_t>((3 - sum % 3) % 3);
    }
    return co;
  }
  // Labels and twists of a cube's places.
  void read(const CubieCube &c, std::array<std::uint8_t, kCorners> &lab,
            std::array<std::uint8_t, kCorners> &co) const {
    for (std::size_t i = 0; i < places.size(); ++i) {
      auto p = static_cast<std::size_t>(places[i]);
      lab[i] = label[c.cp[p]];
      co[i] = c.co[p];
    }
  }
  void applyTo(const CubieCube &e, std::array<std::uint8_t, kCorners> &lab,
               std::array<std::uint8_t, kCorners> &co) const {
    std::array<std::uint8_t, kCorners> nl{}, nc{};
    for (std::size_t i = 0; i < places.size(); ++i) {
      auto from = static_cast<std::size_t>(
          local[e.cp[static_cast<std::size_t>(places[i])]]);
      nl[i] = lab[from];
      nc[i] = static_cast<std::uint8_t>(
          (co[from] + e.co[static_cast<std::size_t>(places[i])]) % 3);
    }
    lab = nl;
    co = nc;
  }
  std::size_t index(std::uint32_t r, std::uint32_t c) const {
    return static_cast<std::size_t>(r) * coSize + c;
  }
};

// A group of edges: the place and flip of each, over the places the moves
// turn.
struct EdgeTable {
  std::vector<int> places;
  std::array<int, kEdges> local{};
  std::vector<int> pieces;                // the edges followed
  std::array<std::int8_t, kEdges> slot{}; // by piece: index in pieces or -1
  int n = 0, k = 0;
  std::vector<std::uint32_t> perms; // perms[i] = P(n - 1 - i, k - 1 - i)
  std::vector<std::uint8_t> dist;

  std::size_t rank(const std::array<std::uint8_t, kEdges> &pos,
                   std::uint32_t flips) const {
    std::uint32_t used = 0;
    std::size_t r = 0;
    for (int i = 0; i < k; ++i) {
      std::uint32_t p = pos[static_cast<std::size_t>(i)];
      int smaller = std::popcount(used & ((1u << p) - 1));
      r += (p - static_cast<std::uint32_t>(smaller)) *
           perms[static_cast<std::size_t>(i)];
      used |= 1u << p;
    }
    return (r << k) | flips;
  }
  void unrank(std::size_t idx, std::array<std::uint8_t, kEdges> &pos,
              std::uint32_t &flips) const {
    flips = static_cast<std::uint32_t>(idx & ((1u << k) - 1));
    std::size_t r = idx >> k;
    std::uint32_t used = 0;
    for (int i = 0; i < k; ++i) {
      auto d = static_cast<int>(r / perms[static_cast<std::size_t>(i)]);
      r %= perms[static_cast<std::size_t>(i)];
      int p = 0;
      for (;; ++p) {
        if (!(used & (1u << p))) {
          if (d == 0) {
            break;
          }
          --d;
        }
      }
      pos[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(p);
      used |= 1u << p;
    }
  }
  std::size_t indexOf(const CubieCube &c) const {
    std::array<std::uint8_t, kEdges> pos{};
    std::uint32_t flips = 0;
    for (int p : places) {
      int s = slot[c.ep[static_cast<std::size_t>(p)]];
      if (s >= 0) {
        pos[static_cast<std::size_t>(s)] =
            static_cast<std::uint8_t>(local[static_cast<std::size_t>(p)]);
        flips |= static_cast<std::uint32_t>(c.eo[static_cast<std::size_t>(p)])
                 << s;
      }
    }
    return rank(pos, flips);
  }
};

struct PlaceMoves {
  // For each effect, as seen from a place: where the piece there goes.
  std::vector<std::array<std::uint8_t, kEdges>> to;
  std::vector<std::array<std::uint8_t, kEdges>> flip;
};

std::vector<std::uint8_t> goalAufs(bool aufAfter) {
  if (!aufAfter) {
    return {0};
  }
  return {0, 1, 2, 3};
}

std::shared_ptr<CornerTable> buildCorners(const MoveModel &model,
                                          const std::vector<int> &places,
                                          const PieceMask &free, bool aufAfter,
                                          int threads) {
  auto t = std::make_shared<CornerTable>();
  t->places = places;
  t->local.fill(-1);
  for (std::size_t i = 0; i < places.size(); ++i) {
    t->local[static_cast<std::size_t>(places[i])] = static_cast<int>(i);
  }
  for (std::size_t q = 0; q < kCorners; ++q) {
    t->label[q] = free.cp[q] ? 15 : static_cast<std::uint8_t>(q);
  }
  const std::size_t n = places.size();
  t->derive = n == kCorners;
  t->coSize = static_cast<std::uint32_t>(
      pow3(static_cast<int>(n) - (t->derive ? 1 : 0)));
  std::array<std::uint8_t, kCorners> goal{};
  for (std::size_t i = 0; i < n; ++i) {
    goal[i] = t->label[static_cast<std::size_t>(places[i])];
  }
  // Every arrangement of the labels, in order (a vector, so that sorting
  // a part of it doesn't trip GCC's bounds warnings).
  std::vector<std::uint8_t> labels(
      goal.begin(), goal.begin() + static_cast<std::ptrdiff_t>(n));
  std::sort(labels.begin(), labels.end());
  do {
    std::array<std::uint8_t, kCorners> arr{};
    std::copy(labels.begin(), labels.end(), arr.begin());
    t->rankOf.emplace(t->code(arr),
                      static_cast<std::uint32_t>(t->arrangements.size()));
    t->arrangements.push_back(arr);
  } while (std::next_permutation(labels.begin(), labels.end()));
  const int ne = static_cast<int>(model.effects.size());
  t->effects = ne;
  t->cpMove.resize(t->arrangements.size() * static_cast<std::size_t>(ne));
  t->coMove.resize(static_cast<std::size_t>(t->coSize) *
                   static_cast<std::size_t>(ne));
  std::array<std::uint8_t, kCorners> zero{};
  for (std::size_t r = 0; r < t->arrangements.size(); ++r) {
    for (int e = 0; e < ne; ++e) {
      auto lab = t->arrangements[r];
      auto co = zero;
      t->applyTo(model.effects[static_cast<std::size_t>(e)], lab, co);
      t->cpMove[r * static_cast<std::size_t>(ne) +
                static_cast<std::size_t>(e)] = t->rankOf.at(t->code(lab));
    }
  }
  std::array<std::uint8_t, kCorners> any = goal;
  for (std::uint32_t c = 0; c < t->coSize; ++c) {
    for (int e = 0; e < ne; ++e) {
      auto lab = any;
      auto co = t->coDigits(c);
      t->applyTo(model.effects[static_cast<std::size_t>(e)], lab, co);
      t->coMove[c * static_cast<std::size_t>(ne) +
                static_cast<std::size_t>(e)] = t->coIndex(co);
    }
  }
  // Seeds: the goal arrangement with every twist the step allows, turned
  // back by each U turn allowed after the alg.
  std::set<std::size_t> seeds;
  for (std::uint32_t c = 0; c < t->coSize; ++c) {
    auto co = t->coDigits(c);
    bool ok = true;
    for (std::size_t i = 0; i < n && ok; ++i) {
      ok = co[i] == 0 || free.co[static_cast<std::size_t>(places[i])];
    }
    if (!ok) {
      continue;
    }
    for (std::uint8_t v : goalAufs(aufAfter)) {
      auto lab = goal;
      auto cc = co;
      t->applyTo(uTurn((4 - v) % 4), lab, cc);
      auto it = t->rankOf.find(t->code(lab));
      if (it != t->rankOf.end()) {
        seeds.insert(t->index(it->second, t->coIndex(cc)));
      }
    }
  }
  const std::size_t size = t->arrangements.size() * t->coSize;
  const auto *tp = t.get();
  t->dist = distances(size, {seeds.begin(), seeds.end()}, ne, threads,
                      [tp, ne](std::size_t i, int e) {
                        std::size_t r = i / tp->coSize, c = i % tp->coSize;
                        auto ue = static_cast<std::size_t>(e);
                        auto une = static_cast<std::size_t>(ne);
                        return tp->index(tp->cpMove[r * une + ue],
                                         tp->coMove[c * une + ue]);
                      });
  return t;
}

std::shared_ptr<EdgeTable> buildEdges(const MoveModel &model,
                                      const std::vector<int> &places,
                                      const std::vector<int> &pieces,
                                      const PieceMask &free, bool aufAfter,
                                      int threads) {
  auto t = std::make_shared<EdgeTable>();
  t->places = places;
  t->local.fill(-1);
  for (std::size_t i = 0; i < places.size(); ++i) {
    t->local[static_cast<std::size_t>(places[i])] = static_cast<int>(i);
  }
  t->pieces = pieces;
  t->slot.fill(-1);
  for (std::size_t j = 0; j < pieces.size(); ++j) {
    t->slot[static_cast<std::size_t>(pieces[j])] = static_cast<std::int8_t>(j);
  }
  t->n = static_cast<int>(places.size());
  t->k = static_cast<int>(pieces.size());
  for (int i = 0; i < t->k; ++i) {
    std::uint32_t p = 1;
    for (int a = 0; a < t->k - 1 - i; ++a) {
      p *= static_cast<std::uint32_t>(t->n - 1 - i - a);
    }
    t->perms.push_back(p);
  }
  // Where each effect takes the piece at each local place, and whether it
  // flips it.
  const int ne = static_cast<int>(model.effects.size());
  auto movesOf = [&](const CubieCube &e) {
    std::array<std::uint8_t, kEdges> to{}, fl{};
    for (std::size_t q = 0; q < kEdges; ++q) {
      int from = t->local[e.ep[q]];
      int at = t->local[q];
      if (from >= 0 && at >= 0) {
        to[static_cast<std::size_t>(from)] = static_cast<std::uint8_t>(at);
        fl[static_cast<std::size_t>(from)] = e.eo[q];
      }
    }
    return std::pair{to, fl};
  };
  PlaceMoves pm;
  for (const auto &e : model.effects) {
    auto [to, fl] = movesOf(e);
    pm.to.push_back(to);
    pm.flip.push_back(fl);
  }
  std::size_t size = 1;
  for (int i = 0; i < t->k; ++i) {
    size *= static_cast<std::size_t>(t->n - i);
  }
  size <<= t->k;
  // Seeds: the pieces home, or (for pieces whose place is free) at any of
  // the free places, flipped where the step allows.
  std::vector<int> freePlaces;
  for (int p : places) {
    if (free.ep[static_cast<std::size_t>(p)]) {
      freePlaces.push_back(t->local[static_cast<std::size_t>(p)]);
    }
  }
  std::set<std::size_t> seeds;
  std::array<std::uint8_t, kEdges> pos{};
  std::vector<bool> taken(kEdges, false);
  std::vector<std::array<std::uint8_t, kEdges>> placements;
  std::function<void(int)> place = [&](int j) {
    if (j == t->k) {
      placements.push_back(pos);
      return;
    }
    auto piece = static_cast<std::size_t>(pieces[static_cast<std::size_t>(j)]);
    if (!free.ep[piece]) {
      pos[static_cast<std::size_t>(j)] =
          static_cast<std::uint8_t>(t->local[piece]);
      place(j + 1);
      return;
    }
    for (int fp : freePlaces) {
      if (!taken[static_cast<std::size_t>(fp)]) {
        taken[static_cast<std::size_t>(fp)] = true;
        pos[static_cast<std::size_t>(j)] = static_cast<std::uint8_t>(fp);
        place(j + 1);
        taken[static_cast<std::size_t>(fp)] = false;
      }
    }
  };
  place(0);
  std::uint32_t freeFlips = 0;
  for (int j = 0; j < t->k; ++j) {
    if (free.eo[static_cast<std::size_t>(
            pieces[static_cast<std::size_t>(j)])]) {
      freeFlips |= 1u << j;
    }
  }
  std::vector<PlaceMoves> aufMoves;
  for (std::uint8_t v : goalAufs(aufAfter)) {
    PlaceMoves am;
    auto [to, fl] = movesOf(uTurn((4 - v) % 4));
    am.to.push_back(to);
    am.flip.push_back(fl);
    aufMoves.push_back(am);
  }
  for (const auto &pl : placements) {
    // Every subset of the free flips.
    for (std::uint32_t sub = freeFlips;; sub = (sub - 1) & freeFlips) {
      for (const auto &am : aufMoves) {
        std::array<std::uint8_t, kEdges> np{};
        std::uint32_t nf = 0;
        for (int j = 0; j < t->k; ++j) {
          auto uj = static_cast<std::size_t>(j);
          // A local place, below kEdges (clamped so that GCC sees it).
          std::size_t at = std::min<std::size_t>(pl[uj], kEdges - 1);
          np[uj] = am.to[0][at];
          nf |= (((sub >> j) & 1u) ^ am.flip[0][at]) << j;
        }
        seeds.insert(t->rank(np, nf));
      }
      if (sub == 0) {
        break;
      }
    }
  }
  // Move tables on the places alone: the places after each effect, and
  // which of the pieces it flips.
  const std::size_t nperm = size >> t->k;
  const auto une = static_cast<std::size_t>(ne);
  std::vector<std::uint32_t> permMove(nperm * une);
  std::vector<std::uint8_t> flipMove(nperm * une);
  {
    auto fill = [&](std::size_t begin, std::size_t end) {
      for (std::size_t r = begin; r < end; ++r) {
        std::array<std::uint8_t, kEdges> p{};
        std::uint32_t f = 0;
        t->unrank(r << t->k, p, f);
        for (std::size_t e = 0; e < une; ++e) {
          std::array<std::uint8_t, kEdges> np{};
          std::uint32_t nf = 0;
          for (int j = 0; j < t->k; ++j) {
            auto uj = static_cast<std::size_t>(j);
            nf |= static_cast<std::uint32_t>(pm.flip[e][p[uj]]) << j;
            np[uj] = pm.to[e][p[uj]];
          }
          permMove[r * une + e] =
              static_cast<std::uint32_t>(t->rank(np, 0) >> t->k);
          flipMove[r * une + e] = static_cast<std::uint8_t>(nf);
        }
      }
    };
    std::vector<std::jthread> pool;
    std::size_t step = (nperm + static_cast<std::size_t>(threads) - 1) /
                       static_cast<std::size_t>(threads);
    for (std::size_t b = 0; b < nperm; b += step) {
      pool.emplace_back(fill, b, std::min(nperm, b + step));
    }
  }
  const int k = t->k;
  const std::uint32_t mask = (1u << k) - 1;
  t->dist = distances(size, {seeds.begin(), seeds.end()}, ne, threads,
                      [&, k, mask, une](std::size_t i, int e) {
                        std::size_t r = i >> k;
                        auto f = static_cast<std::uint32_t>(i) & mask;
                        std::size_t at = r * une + static_cast<std::size_t>(e);
                        return (static_cast<std::size_t>(permMove[at]) << k) |
                               (f ^ flipMove[at]);
                      });
  return t;
}

// Tables kept between searches.
struct TableCache {
  std::mutex mutex;
  std::map<std::string, std::shared_ptr<void>> tables;
  std::map<std::string, std::size_t> bytes;
  std::map<std::string, std::uint64_t> used;
  std::uint64_t clock = 0;

  template <typename T, typename Build>
  std::shared_ptr<T> get(const std::string &key, Build build) {
    {
      std::lock_guard lock(mutex);
      auto it = tables.find(key);
      if (it != tables.end()) {
        used[key] = ++clock;
        return std::static_pointer_cast<T>(it->second);
      }
    }
    std::shared_ptr<T> t = build();
    std::lock_guard lock(mutex);
    std::size_t total = t->dist.size();
    for (const auto &[k, b] : bytes) {
      total += b;
    }
    while (total > kCacheBudget && !tables.empty()) {
      auto oldest = std::min_element(
          used.begin(), used.end(),
          [](const auto &a, const auto &b) { return a.second < b.second; });
      total -= bytes[oldest->first];
      tables.erase(oldest->first);
      bytes.erase(oldest->first);
      used.erase(oldest);
    }
    tables[key] = t;
    bytes[key] = t->dist.size();
    used[key] = ++clock;
    return t;
  }
};

TableCache &cache() {
  static TableCache c;
  return c;
}

// ---------------------------------------------------------------------
// The search.

struct Node {
  CubieCube cube;
  int frame = 0;
  std::uint32_t cr = 0, cc = 0; // corner arrangement and twist
  int lastType = -1;            // the last move's layer type
  bool runHasU = false;         // a U turn among the last moves of one axis
};

struct Searcher {
  Searcher(const SearchGoal &g, const SearchOptions &o,
           const std::atomic<bool> &s, const SearchCallbacks &c)
      : goal(g), options(o), stop(s), callbacks(c) {}

  const SearchGoal &goal;
  const SearchOptions &options;
  const std::atomic<bool> &stop;
  const SearchCallbacks &callbacks;
  MoveModel model;
  std::shared_ptr<CornerTable> corners;
  std::vector<std::shared_ptr<EdgeTable>> edges;
  std::vector<std::uint8_t> aufs; // allowed after
  std::vector<int> axisOf;        // per move
  std::vector<int> typeOf;        // per move

  std::mutex resultMutex;
  std::set<std::string> seen;
  std::atomic<int> found{0};
  std::atomic<bool> capped{false};
  std::atomic<std::uint64_t> nodes{0};
  int shortestFound = -1;

  bool stopped() const {
    return stop.load(std::memory_order_relaxed) ||
           capped.load(std::memory_order_relaxed);
  }

  int heuristic(const Node &n) const {
    int h = corners->dist[corners->index(n.cr, n.cc)];
    for (const auto &e : edges) {
      h = std::max<int>(h, e->dist[e->indexOf(n.cube)]);
      if (h == kUnseen) {
        break;
      }
    }
    return h;
  }

  bool atGoal(const CubieCube &c) const {
    for (std::uint8_t v : aufs) {
      if (withinMask(v == 0 ? c : c * uTurn(v), goal.free)) {
        return true;
      }
    }
    return false;
  }

  Node step(const Node &n, std::size_t mi) const {
    Node r;
    std::size_t at =
        static_cast<std::size_t>(n.frame) * model.moves.size() + mi;
    auto e = static_cast<std::size_t>(model.effectOf[at]);
    r.cube = n.cube * model.effects[e];
    r.frame = model.nextFrame[at];
    auto ne = static_cast<std::size_t>(corners->effects);
    r.cr = corners->cpMove[n.cr * ne + e];
    r.cc = corners->coMove[n.cc * ne + e];
    r.lastType = typeOf[mi];
    bool sameAxis = n.lastType >= 0 &&
                    axisOf[mi] == moveAxis(static_cast<MoveType>(n.lastType));
    r.runHasU = (sameAxis && n.runHasU) || model.centreU[at] != 0;
    return r;
  }

  // Whether move `mi` may follow `n` at `depth`.
  bool allowed(const Node &n, std::size_t mi, int depth) const {
    int t = typeOf[mi];
    if (n.lastType >= 0) {
      if (t == n.lastType) {
        return false;
      }
      if (axisOf[mi] == moveAxis(static_cast<MoveType>(n.lastType)) &&
          t < n.lastType) {
        return false;
      }
    }
    if (depth == 0 && goal.aufBefore &&
        model.centreU[static_cast<std::size_t>(n.frame) * model.moves.size() +
                      mi] != 0) {
      return false;
    }
    return true;
  }

  void report(const std::vector<std::size_t> &path) {
    Alg alg;
    for (std::size_t mi : path) {
      alg.push_back(model.moves[mi]);
    }
    auto fit = fitAlg(goal.state, alg, goal.free, aufsOf(goal));
    if (!fit) {
      return;
    }
    SearchResult r{alg, toString(alg), *fit, static_cast<int>(alg.size())};
    std::lock_guard lock(resultMutex);
    if (capped.load() || !seen.insert(r.text).second) {
      return;
    }
    int count = found.fetch_add(1) + 1;
    if (shortestFound < 0 || r.length < shortestFound) {
      shortestFound = r.length;
    }
    if (callbacks.result) {
      callbacks.result(r);
    }
    if (count >= options.maxResults) {
      capped.store(true);
    }
  }

  void dfs(const Node &n, int depth, int bound, std::vector<std::size_t> &path,
           std::uint64_t &count) {
    if (stopped()) {
      return;
    }
    ++count;
    int remaining = bound - depth;
    int h = heuristic(n);
    if (h > remaining) {
      return;
    }
    if (remaining == 0) {
      if (atGoal(n.cube) && !(goal.aufAfter && n.runHasU)) {
        report(path);
      }
      return;
    }
    if (depth > 0 && h == 0 && atGoal(n.cube)) {
      return; // already an alg: the rest would only undo and redo
    }
    for (std::size_t mi = 0; mi < model.moves.size(); ++mi) {
      if (!allowed(n, mi, depth)) {
        continue;
      }
      path.push_back(mi);
      dfs(step(n, mi), depth + 1, bound, path, count);
      path.pop_back();
    }
  }

  struct Task {
    Node node;
    std::vector<std::size_t> path;
  };

  // The nodes `split` moves in (fewer at the bound), pruned, for workers.
  void tasks(const Node &n, int depth, int bound, int split,
             std::vector<std::size_t> &path, std::vector<Task> &out) {
    int h = heuristic(n);
    if (h > bound - depth) {
      return;
    }
    if (depth == split || depth == bound) {
      out.push_back({n, path});
      return;
    }
    if (depth > 0 && h == 0 && atGoal(n.cube)) {
      return;
    }
    for (std::size_t mi = 0; mi < model.moves.size(); ++mi) {
      if (!allowed(n, mi, depth)) {
        continue;
      }
      path.push_back(mi);
      tasks(step(n, mi), depth + 1, bound, split, path, out);
      path.pop_back();
    }
  }

  SearchOutcome run() {
    SearchOutcome out;
    int threads =
        options.threads > 0
            ? options.threads
            : std::max(1,
                       static_cast<int>(std::thread::hardware_concurrency()));
    model = buildModel(options.moves);
    for (Move m : model.moves) {
      axisOf.push_back(moveAxis(m.type));
      typeOf.push_back(static_cast<int>(m.type));
    }
    aufs = goalAufs(goal.aufAfter);
    if (model.moves.empty()) {
      out.end = SearchEnd::Unsolvable;
      return out;
    }
    // The places the moves (and the AUFs) turn.
    std::array<bool, kCorners> cMoved{};
    std::array<bool, kEdges> eMoved{};
    auto mark = [&](const CubieCube &e) {
      for (std::size_t i = 0; i < kCorners; ++i) {
        cMoved[i] = cMoved[i] || e.cp[i] != i || e.co[i] != 0;
      }
      for (std::size_t i = 0; i < kEdges; ++i) {
        eMoved[i] = eMoved[i] || e.ep[i] != i || e.eo[i] != 0;
      }
    };
    for (const auto &e : model.effects) {
      mark(e);
    }
    if (goal.aufBefore || goal.aufAfter) {
      mark(uTurn(1));
    }
    std::vector<int> cPlaces, ePlaces;
    for (int i = 0; i < kCorners; ++i) {
      if (cMoved[static_cast<std::size_t>(i)]) {
        cPlaces.push_back(i);
      }
    }
    // Edges in the order groups are made from: the U layer, the middle
    // layer, the D layer.
    for (int i : {UR, UF, UL, UB, FR, FL, BL, BR, DR, DF, DL, DB}) {
      if (eMoved[static_cast<std::size_t>(i)]) {
        ePlaces.push_back(i);
      }
    }
    std::vector<int> tracked;
    for (int p : ePlaces) {
      auto up = static_cast<std::size_t>(p);
      if (!(goal.free.ep[up] && goal.free.eo[up])) {
        tracked.push_back(p);
      }
    }
    std::vector<int> sortedPlaces = ePlaces;
    std::sort(sortedPlaces.begin(), sortedPlaces.end());
    // Groups of edges as large as the budget allows: one group, or the
    // first and the last that many (overlapping if need be).
    const int ne = static_cast<int>(sortedPlaces.size());
    int kmax = 0;
    for (int k = 1; k <= ne; ++k) {
      std::uint64_t s = 1;
      for (int i = 0; i < k; ++i) {
        s *= static_cast<std::uint64_t>(ne - i);
      }
      s <<= k;
      if (s > kEdgeBudget) {
        break;
      }
      kmax = k;
    }
    std::vector<std::vector<int>> groups;
    const int nt = static_cast<int>(tracked.size());
    if (nt > 0 && nt <= kmax) {
      groups.push_back(tracked);
    } else if (nt > 0) {
      groups.emplace_back(tracked.begin(), tracked.begin() + kmax);
      groups.emplace_back(tracked.end() - kmax, tracked.end());
    }

    // Starts: the case after each U turn allowed before it, when the
    // places no move turns are already as the step wants them.
    std::vector<CubieCube> starts;
    for (int b : kAufs) {
      if (b != 0 && !goal.aufBefore) {
        continue;
      }
      CubieCube s = b == 0 ? goal.state : goal.state * uTurn(b);
      bool ok = true;
      for (std::size_t i = 0; i < kCorners && ok; ++i) {
        if (!cMoved[i]) {
          ok = (goal.free.cp[i] || s.cp[i] == i) &&
               (goal.free.co[i] || s.co[i] == 0);
        }
      }
      for (std::size_t i = 0; i < kEdges && ok; ++i) {
        if (!eMoved[i]) {
          ok = (goal.free.ep[i] || s.ep[i] == i) &&
               (goal.free.eo[i] || s.eo[i] == 0);
        }
      }
      for (std::size_t i = 0; i < kEdges && ok; ++i) {
        // Every edge followed must be somewhere the moves reach.
        if (!eMoved[i]) {
          ok = std::find(tracked.begin(), tracked.end(), s.ep[i]) ==
               tracked.end();
        }
      }
      if (ok && std::find(starts.begin(), starts.end(), s) == starts.end()) {
        starts.push_back(s);
      }
    }
    if (starts.empty()) {
      out.end = SearchEnd::Unsolvable;
      return out;
    }

    std::string base = model.key + "|" + bytesOf(goal.free) + "|" +
                       (goal.aufAfter ? "a" : "-") +
                       (goal.aufBefore ? "b" : "-");
    bool announced = false;
    auto announce = [&] {
      if (!announced && callbacks.progress) {
        callbacks.progress("Building tables…");
      }
      announced = true;
    };
    {
      std::string key = base + "|c";
      {
        std::lock_guard lock(cache().mutex);
        if (!cache().tables.contains(key)) {
          announce();
        }
      }
      corners = cache().get<CornerTable>(key, [&] {
        return buildCorners(model, cPlaces, goal.free, goal.aufAfter, threads);
      });
    }
    for (const auto &g : groups) {
      std::string key = base + "|e";
      for (int p : g) {
        key += static_cast<char>('a' + p);
      }
      {
        std::lock_guard lock(cache().mutex);
        if (!cache().tables.contains(key)) {
          announce();
        }
      }
      edges.push_back(cache().get<EdgeTable>(key, [&] {
        return buildEdges(model, sortedPlaces, g, goal.free, goal.aufAfter,
                          threads);
      }));
    }

    std::vector<Node> roots;
    int lowest = kUnseen;
    for (const auto &s : starts) {
      Node n;
      n.cube = s;
      std::array<std::uint8_t, kCorners> lab{}, co{};
      corners->read(s, lab, co);
      auto it = corners->rankOf.find(corners->code(lab));
      if (it == corners->rankOf.end()) {
        continue;
      }
      n.cr = it->second;
      n.cc = corners->coIndex(co);
      if (atGoal(s)) {
        continue; // nothing to solve from this side
      }
      int h = heuristic(n);
      if (h == kUnseen) {
        continue;
      }
      lowest = std::min(lowest, h);
      roots.push_back(n);
    }
    if (roots.empty()) {
      out.end = SearchEnd::Unsolvable;
      return out;
    }

    int last = options.maxLength;
    bool limited = false;
    for (int bound = std::max(1, lowest); bound <= last; ++bound) {
      if (stopped()) {
        break;
      }
      if (callbacks.progress) {
        callbacks.progress("Searching " + std::to_string(bound) + " moves…");
      }
      std::vector<Task> work;
      for (const auto &r : roots) {
        std::vector<std::size_t> path;
        tasks(r, 0, bound, std::min(2, bound), path, work);
      }
      std::atomic<std::size_t> next{0};
      auto worker = [&] {
        std::uint64_t count = 0;
        for (;;) {
          std::size_t i = next.fetch_add(1);
          if (i >= work.size() || stopped()) {
            break;
          }
          auto path = work[i].path;
          dfs(work[i].node, static_cast<int>(path.size()), bound, path, count);
        }
        nodes.fetch_add(count);
      };
      {
        std::vector<std::jthread> pool;
        for (int t = 1; t < threads; ++t) {
          pool.emplace_back(worker);
        }
        worker();
      }
      if (stopped()) {
        break;
      }
      out.deepest = bound;
      if (found.load() > 0 && !limited) {
        limited = true;
        if (options.extra >= 0) {
          last = std::min(last, bound + options.extra);
        }
      }
    }
    out.found = found.load();
    out.shortest = shortestFound;
    out.nodes = nodes.load();
    out.end = capped.load() ? SearchEnd::Capped
              : stop.load() ? SearchEnd::Stopped
                            : SearchEnd::Done;
    return out;
  }
};

} // namespace

SearchOutcome searchAlgs(const SearchGoal &goal, const SearchOptions &options,
                         const std::atomic<bool> &stop,
                         const SearchCallbacks &callbacks) {
  Searcher s(goal, options, stop, callbacks);
  return s.run();
}

} // namespace vcube
