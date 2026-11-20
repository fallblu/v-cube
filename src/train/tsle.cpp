#include "train/tsle.hpp"

#include "train/f2l.hpp"
#include "train/families.hpp"

#include <algorithm>
#include <climits>

namespace vcube {

namespace {

constexpr std::array<Corner, 5> kPlaces = {URF, UFL, ULB, UBR, DFR};
constexpr std::array<Edge, 5> kEdgePlaces = {UR, UF, UL, UB, FR};
constexpr int kOptions = 12; // 4 AUFs and 3 triggers

// A cube in a state, with every piece but the slot's edge (and the edge
// it trades places with) at home: not a real cube with the edge out, but
// turns move it as they would a real one.
CubieCube representative(TsleState s) {
  CubieCube c;
  for (std::size_t i = 0; i < kPlaces.size(); ++i) {
    c.co[kPlaces[i]] = s.twist[i];
  }
  if (s.edge != TsleState::kInSlot) {
    std::swap(c.ep[FR], c.ep[s.edge]);
  }
  return c;
}

TsleState stateOf(const CubieCube &c) {
  TsleState s;
  auto at = static_cast<std::size_t>(std::find(c.ep.begin(), c.ep.end(), FR) -
                                     c.ep.begin());
  s.edge = at == FR ? TsleState::kInSlot : static_cast<std::uint8_t>(at);
  for (std::size_t i = 0; i < kPlaces.size(); ++i) {
    s.twist[i] = c.co[kPlaces[i]];
  }
  return s;
}

Alg uTurn2(int quarters) {
  quarters = ((quarters % 4) + 4) % 4;
  return quarters
             ? Alg{Move{MoveType::U, static_cast<std::uint8_t>(quarters), 0}}
             : Alg{};
}

// The AUF and the trigger R U^turn R', in the front-right view.
Alg triggerAlg(int auf, int turn) {
  Alg a = uTurn2(auf);
  a.push_back(Move{MoveType::R, 1, 0});
  a.push_back(Move{MoveType::U, static_cast<std::uint8_t>(turn), 0});
  a.push_back(Move{MoveType::R, 3, 0});
  return a;
}

Alg mirrored(Alg alg, Mirror m) {
  if (m.leftRight) {
    alg = mirrorM(alg);
  }
  if (m.frontBack) {
    alg = mirrorS(alg);
  }
  return alg;
}

Alg joined(Alg a, const Alg &b) {
  a.insert(a.end(), b.begin(), b.end());
  return simplify(a);
}

struct Graph {
  std::array<std::array<std::uint16_t, 4>, kTsleStates> turned{};
  std::array<std::array<std::uint16_t, kOptions>, kTsleStates> after{};
  std::array<int, kTsleStates> steps{};
};

const Graph &graph() {
  static const Graph g = [] {
    Graph out;
    for (int i = 0; i < kTsleStates; ++i) {
      auto at = static_cast<std::size_t>(i);
      CubieCube c = representative(TsleState::fromIndex(i));
      for (int auf = 0; auf < 4; ++auf) {
        CubieCube turned = c;
        turned.apply(uTurn2(auf));
        out.turned[at][static_cast<std::size_t>(auf)] =
            static_cast<std::uint16_t>(stateOf(turned).index());
        for (int turn = 1; turn <= 3; ++turn) {
          CubieCube done = c;
          done.apply(triggerAlg(auf, turn));
          out.after[at][static_cast<std::size_t>(auf * 3 + turn - 1)] =
              static_cast<std::uint16_t>(stateOf(done).index());
        }
      }
    }
    for (int i = 0; i < kTsleStates; ++i) {
      TsleState s = TsleState::fromIndex(i);
      out.steps[static_cast<std::size_t>(i)] = s.done()   ? 0
                                               : s.ocll() ? 1
                                                          : INT_MAX / 2;
    }
    for (bool changed = true; changed;) {
      changed = false;
      for (std::size_t i = 0; i < kTsleStates; ++i) {
        TsleState s = TsleState::fromIndex(static_cast<int>(i));
        if (s.done() || s.ocll()) {
          continue;
        }
        for (std::uint16_t next : out.after[i]) {
          if (out.steps[next] + 1 < out.steps[i]) {
            out.steps[i] = out.steps[next] + 1;
            changed = true;
          }
        }
      }
    }
    return out;
  }();
  return g;
}

bool f2lWhole(const CubieCube &c) {
  return crossSolved(c) && edgesOriented(c) && unsolvedSlots(c).empty();
}

// The OCLL an alg of the default ones solves: the cube before it.
CubieCube ocllCube(std::size_t index) {
  return cubieOf(invert(parseAlgOrEmpty(defaultOcllAlgs()[index].alg)));
}

bool cornersOriented(const CubieCube &c) {
  return std::all_of(c.co.begin(), c.co.end(),
                     [](std::uint8_t t) { return t == 0; });
}

} // namespace

// --- states and cases --------------------------------------------------------

int TsleState::index() const {
  return edge * 81 + twist[0] * 27 + twist[1] * 9 + twist[2] * 3 + twist[3];
}

TsleState TsleState::fromIndex(int index) {
  TsleState s;
  s.edge = static_cast<std::uint8_t>(index / 81);
  int t = index % 81;
  int sum = 0;
  for (std::size_t i = 4; i-- > 0;) {
    s.twist[i] = static_cast<std::uint8_t>(t % 3);
    sum += s.twist[i];
    t /= 3;
  }
  s.twist[4] = static_cast<std::uint8_t>((3 - sum % 3) % 3);
  return s;
}

std::optional<TsleState> tsleStateOf(const CubieCube &cube, Slot slot) {
  CubieCube m = mirrorState(cube, slotMirror(slot));
  if (!crossSolved(m) || !edgesOriented(m) || !slotSolved(m, Slot::FL) ||
      !slotSolved(m, Slot::BL) || !slotSolved(m, Slot::BR)) {
    return std::nullopt;
  }
  return stateOf(m);
}

CubieCube randomTsleCube(TsleState state, Slot slot, Rng &rng) {
  CubieCube c;
  std::array<Corner, 5> corners = kPlaces;
  std::shuffle(corners.begin(), corners.end(), rng);
  for (std::size_t i = 0; i < kPlaces.size(); ++i) {
    c.cp[kPlaces[i]] = corners[i];
    c.co[kPlaces[i]] = state.twist[i];
  }
  // The slot's edge where the state has it, the top layer's edges in the
  // other four places.
  std::array<Edge, 4> edges = {UR, UF, UL, UB};
  std::shuffle(edges.begin(), edges.end(), rng);
  std::vector<Edge> others;
  std::size_t next = 0;
  for (Edge place : kEdgePlaces) {
    bool slotEdgeHere = state.edge == TsleState::kInSlot
                            ? place == FR
                            : place == static_cast<Edge>(state.edge);
    if (slotEdgeHere) {
      c.ep[place] = FR;
    } else {
      c.ep[place] = edges[next++];
      others.push_back(place);
    }
  }
  if (c.cornerParity() != c.edgeParity()) {
    std::swap(c.ep[others[0]], c.ep[others[1]]);
  }
  return mirrorState(c, slotMirror(slot));
}

TsleState tsleTurned(TsleState state, int quarters) {
  return TsleState::fromIndex(graph().turned[static_cast<std::size_t>(
      state.index())][static_cast<std::size_t>(((quarters % 4) + 4) % 4)]);
}

TsleState tsleAfter(TsleState state, int auf, int turn) {
  return TsleState::fromIndex(graph().after[static_cast<std::size_t>(
      state.index())][static_cast<std::size_t>((((auf % 4) + 4) % 4) * 3 +
                                               turn - 1)]);
}

TsleState tsleCase(TsleState state) {
  TsleState best = state;
  for (int q = 0; q < 4; ++q) {
    TsleState t = tsleTurned(state, q);
    if (state.edge != TsleState::kInSlot) {
      if (t.edge == UF) {
        return t;
      }
    } else if (t.index() < best.index()) {
      best = t;
    }
  }
  return best;
}

std::string tsleKey(TsleState c) {
  std::string key = c.edge == TsleState::kInSlot ? "FR" : "UF";
  key += c.twist[4] == 0 ? "-N-" : c.twist[4] == 1 ? "-CW-" : "-CCW-";
  for (std::size_t i = 0; i < 4; ++i) {
    key += static_cast<char>('0' + c.twist[i]);
  }
  return key;
}

std::optional<TsleState> tsleFromKey(std::string_view key) {
  for (TsleState c : tsleCases()) {
    if (tsleKey(c) == key) {
      return c;
    }
  }
  return std::nullopt;
}

std::string tsleName(TsleState c) {
  if (c.ocll()) {
    static const TsleSolver solver(Slot::FR);
    if (auto f = solver.finish(c)) {
      return f->name;
    }
  }
  std::string key = tsleKey(c);
  std::replace(key.begin(), key.end(), '-', ' ');
  return key;
}

const std::vector<TsleState> &tsleCases() {
  static const std::vector<TsleState> cases = [] {
    std::vector<TsleState> out;
    for (int i = 0; i < kTsleStates; ++i) {
      TsleState s = TsleState::fromIndex(i);
      if (!s.done() && tsleCase(s) == s) {
        out.push_back(s);
      }
    }
    std::stable_sort(out.begin(), out.end(), [](TsleState a, TsleState b) {
      // OCLLs first, then by steps left.
      int sa = a.ocll() ? 0 : tsleStepsLeft(a);
      int sb = b.ocll() ? 0 : tsleStepsLeft(b);
      return sa != sb ? sa < sb : tsleKey(a) < tsleKey(b);
    });
    return out;
  }();
  return cases;
}

std::vector<std::string> tsleCardPool() {
  std::vector<std::string> pool;
  for (TsleState c : tsleCases()) {
    if (!c.ocll()) {
      pool.push_back(tsleKey(c));
    }
  }
  return pool;
}

int tsleStepsLeft(TsleState state) {
  return graph().steps[static_cast<std::size_t>(state.index())];
}

FaceletCube tsleDiagram(TsleState state) {
  FaceletCube f = representative(state).toFacelets();
  auto topOrBottom = [](std::uint8_t colour) {
    return colour == 0 || colour == 3;
  };
  for (Corner place : kPlaces) {
    for (std::uint8_t facelet : kCornerFacelets[place]) {
      if (!topOrBottom(f[facelet])) {
        f[facelet] = kMaskedColour;
      }
    }
  }
  for (Edge place : kEdgePlaces) {
    const auto &facelets = kEdgeFacelets[place];
    bool top = f[facelets[0]] == 0 || f[facelets[1]] == 0;
    if (top) { // a last-layer edge: only its top colour
      for (std::uint8_t facelet : facelets) {
        if (f[facelet] != 0) {
          f[facelet] = kMaskedColour;
        }
      }
    }
  }
  return f;
}

// --- the OCLLs ---------------------------------------------------------------

const std::vector<OcllAlg> &defaultOcllAlgs() {
  static const std::vector<OcllAlg> algs = {
      {"sune", "Sune", "R U R' U R U2 R'"},
      {"antisune", "Antisune", "R' U' R U' R' U2 R"},
      {"h", "H", "R U R' U R U' R' U R U2 R'"},
      {"pi", "Pi", "R U2 R2 U' R2 U' R2 U2 R"},
      {"headlights", "Headlights", "R2 D R' U2 R D' R' U2 R'"},
      {"chameleon", "Chameleon", "r U R' U' r' F R F'"},
      {"bowtie", "Bowtie", "R2 D R' U R D' R' U' R'"},
  };
  return algs;
}

bool ocllAlgWorks(std::string_view id, const Alg &alg) {
  const std::vector<OcllAlg> &defaults = defaultOcllAlgs();
  for (std::size_t i = 0; i < defaults.size(); ++i) {
    if (defaults[i].id != id || alg.empty()) {
      continue;
    }
    CubieCube start = ocllCube(i);
    for (int auf = 0; auf < 4; ++auf) {
      auto c = applyAlg(start, alg, auf);
      if (c && f2lWhole(*c) && cornersOriented(*c)) {
        return true;
      }
    }
  }
  return false;
}

// --- what to do from a state -------------------------------------------------

TsleSolver::TsleSolver(Slot slot, const std::vector<OcllAlg> &algs)
    : slot_(slot), algs_(defaultOcllAlgs()) {
  for (OcllAlg &mine : algs_) {
    for (const OcllAlg &given : algs) {
      if (given.id == mine.id &&
          ocllAlgWorks(mine.id, parseAlgOrEmpty(given.alg))) {
        mine.alg = given.alg;
      }
    }
  }
  // The OCLL algs as they act in the front-right view of this slot.
  std::vector<Alg> world;
  for (const OcllAlg &a : algs_) {
    world.push_back(mirrored(parseAlgOrEmpty(a.alg), slotMirror(slot_)));
  }
  const Graph &g = graph();
  std::vector<int> order(kTsleStates);
  for (int i = 0; i < kTsleStates; ++i) {
    order[static_cast<std::size_t>(i)] = i;
  }
  std::stable_sort(order.begin(), order.end(), [&g](int a, int b) {
    return g.steps[static_cast<std::size_t>(a)] <
           g.steps[static_cast<std::size_t>(b)];
  });
  for (int i : order) {
    auto at = static_cast<std::size_t>(i);
    TsleState s = TsleState::fromIndex(i);
    if (s.done()) {
      total_[at] = 0;
      continue;
    }
    if (s.ocll()) {
      CubieCube cube = representative(s);
      const WorldFinish *best = nullptr;
      for (std::size_t a = 0; a < world.size(); ++a) {
        for (int auf = 0; auf < 4; ++auf) {
          Alg moves = joined(uTurn2(auf), world[a]);
          auto c = applyAlg(cube, moves);
          if (c && f2lWhole(*c) && cornersOriented(*c)) {
            finishes_[at].push_back({a, std::move(moves)});
          }
        }
      }
      for (const WorldFinish &f : finishes_[at]) {
        if (!best || htmCount(f.moves) < htmCount(best->moves)) {
          best = &f;
        }
      }
      if (best) {
        finish_[at] = finishFrom(*best);
        total_[at] = finish_[at]->moves;
      }
      continue;
    }
    int best = INT_MAX;
    for (int auf = 0; auf < 4; ++auf) {
      for (int turn = 1; turn <= 3; ++turn) {
        TsleOption o = option(s, auf, turn);
        if (o.stepsAfter == g.steps[at] - 1) {
          best = std::min(best, o.total);
        }
      }
    }
    total_[at] = best;
  }
}

TsleFinish TsleSolver::finishFrom(const WorldFinish &f) const {
  TsleFinish out;
  out.id = algs_[f.alg].id;
  out.name = algs_[f.alg].name;
  out.alg = forSlot(f.moves);
  out.moves = htmCount(f.moves);
  return out;
}

Alg TsleSolver::forSlot(const Alg &alg) const {
  return mirrored(alg, slotMirror(slot_));
}

int TsleSolver::movesLeft(TsleState state) const {
  return total_[static_cast<std::size_t>(state.index())];
}

std::optional<TsleFinish> TsleSolver::finish(TsleState state) const {
  return finish_[static_cast<std::size_t>(state.index())];
}

TsleOption TsleSolver::option(TsleState state, int auf, int turn) const {
  TsleOption o;
  o.auf = auf;
  o.turn = turn;
  Alg moves = triggerAlg(auf, turn);
  o.alg = forSlot(moves);
  o.next = tsleAfter(state, auf, turn);
  o.stepsAfter = tsleStepsLeft(o.next);
  o.moves = htmCount(moves);
  auto next = static_cast<std::size_t>(o.next.index());
  o.total = o.moves + total_[next];
  if (o.next.ocll()) {
    // The OCLL alg that joins on best (R' R cancels, say).
    const WorldFinish *best = nullptr;
    int bestMoves = INT_MAX;
    for (const WorldFinish &f : finishes_[next]) {
      int n = htmCount(joined(moves, f.moves));
      if (n < bestMoves) {
        bestMoves = n;
        best = &f;
      }
    }
    if (best) {
      o.finish = finishFrom(*best);
      o.total = bestMoves;
    }
  }
  int steps = tsleStepsLeft(state);
  o.good = o.stepsAfter == steps - 1;
  o.best = o.good && o.total == movesLeft(state);
  return o;
}

std::vector<TsleOption> TsleSolver::options(TsleState state) const {
  std::vector<TsleOption> out;
  if (state.done() || state.ocll()) {
    return out;
  }
  for (int auf = 0; auf < 4; ++auf) {
    for (int turn = 1; turn <= 3; ++turn) {
      out.push_back(option(state, auf, turn));
    }
  }
  std::stable_sort(out.begin(), out.end(),
                   [](const TsleOption &a, const TsleOption &b) {
                     if (a.good != b.good) {
                       return a.good;
                     }
                     if (a.stepsAfter != b.stepsAfter) {
                       return a.stepsAfter < b.stepsAfter;
                     }
                     return a.total < b.total;
                   });
  return out;
}

std::vector<Alg> TsleSolver::bestPath(TsleState state) const {
  std::vector<Alg> path;
  for (int guard = 0; guard < 8 && !state.done(); ++guard) {
    if (state.ocll()) {
      if (auto f = finish(state)) {
        path.push_back(f->alg);
      }
      break;
    }
    TsleOption o = options(state).front();
    path.push_back(o.alg);
    if (o.finish) {
      path.push_back(o.finish->alg);
      break;
    }
    state = o.next;
  }
  return path;
}

} // namespace vcube
