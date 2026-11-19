#include "train/f2l.hpp"

#include "train/families.hpp"

#include <algorithm>
#include <limits>
#include <set>

namespace vcube {

namespace {

constexpr std::array<Edge, 4> kCross = {DR, DF, DL, DB};
constexpr std::array<const char *, kCorners> kCornerNames = {
    "URF", "UFL", "ULB", "UBR", "DFR", "DLF", "DBL", "DRB"};
constexpr std::array<const char *, kEdges> kEdgeNames = {
    "UR", "UF", "UL", "UB", "DR", "DF", "DL", "DB", "FR", "FL", "BL", "BR"};
constexpr std::array<const char *, 6> kFaceNames = {"U", "R", "F",
                                                    "D", "L", "B"};

Mirror mirrorOf(Slot s) { return slotMirror(s); }

std::optional<Slot> slotOfCorner(int place) {
  for (Slot s : kF2LSlots) {
    if (slotCorner(s) == place) {
      return s;
    }
  }
  return std::nullopt;
}

std::optional<Slot> slotOfEdge(int place) {
  for (Slot s : kF2LSlots) {
    if (slotEdge(s) == place) {
      return s;
    }
  }
  return std::nullopt;
}

// Where each piece value goes under a U turn.
struct UTurns {
  std::array<std::uint8_t, 24> corner{};
  std::array<std::uint8_t, 24> edge{};
};

const UTurns &uTurns() {
  static const UTurns turns = [] {
    UTurns t;
    const CubieCube &u = faceTurnCubie(Face::U);
    for (std::size_t i = 0; i < kCorners; ++i) {
      for (int twist = 0; twist < 3; ++twist) {
        t.corner[static_cast<std::size_t>(u.cp[i] * 3 + twist)] =
            static_cast<std::uint8_t>(static_cast<int>(i) * 3 +
                                      (twist + u.co[i]) % 3);
      }
    }
    for (std::size_t i = 0; i < kEdges; ++i) {
      for (int flip = 0; flip < 2; ++flip) {
        t.edge[static_cast<std::size_t>(u.ep[i] * 2 + flip)] =
            static_cast<std::uint8_t>(static_cast<int>(i) * 2 +
                                      (flip + u.eo[i]) % 2);
      }
    }
    return t;
  }();
  return turns;
}

PairCase normalized(int corner, int edge) {
  const UTurns &u = uTurns();
  PairCase c = {static_cast<std::uint8_t>(corner),
                static_cast<std::uint8_t>(edge)};
  for (int i = 0; i < 4; ++i) {
    bool there = c.cornerOnTop() ? c.corner / 3 == URF
                                 : !c.edgeOnTop() || c.edge / 2 == UF;
    if (there) {
      break;
    }
    if (c.cornerOnTop()) {
      c.corner = u.corner[c.corner];
    }
    if (c.edgeOnTop()) {
      c.edge = u.edge[c.edge];
    }
  }
  return c;
}

int placeOfCorner(const CubieCube &c, Corner piece) {
  return static_cast<int>(std::find(c.cp.begin(), c.cp.end(), piece) -
                          c.cp.begin());
}

int placeOfEdge(const CubieCube &c, Edge piece) {
  return static_cast<int>(std::find(c.ep.begin(), c.ep.end(), piece) -
                          c.ep.begin());
}

template <typename T> const T &pickOne(const std::vector<T> &from, Rng &rng) {
  std::uniform_int_distribution<std::size_t> d(0, from.size() - 1);
  return from[d(rng)];
}

// The slots (besides its own) a case's pieces sit in, for `target`.
std::vector<Slot> slotsNeeded(PairCase pc, Slot target) {
  std::vector<Slot> out;
  for (std::optional<Slot> s : {pc.cornerSlot(), pc.edgeSlot()}) {
    if (s && *s != Slot::FR) {
      Slot real = slotSeenFrom(*s, target);
      if (std::find(out.begin(), out.end(), real) == out.end()) {
        out.push_back(real);
      }
    }
  }
  return out;
}

constexpr int kSetupTries = 40000;

// A state with `open` open whose `target` pair is `wanted`.
std::optional<CubieCube> stateWithCase(const std::vector<Slot> &open, bool eo,
                                       Slot target, PairCase wanted, Rng &rng) {
  for (int i = 0; i < kSetupTries; ++i) {
    CubieCube c = randomF2LState(open, eo, rng);
    // A quick look before the mirrored one: the pieces' layers.
    bool cornerTop = placeOfCorner(c, slotCorner(target)) < 4;
    bool edgeTop = placeOfEdge(c, slotEdge(target)) < 4;
    if (cornerTop != wanted.cornerOnTop() || edgeTop != wanted.edgeOnTop()) {
      continue;
    }
    if (pairCaseOf(c, target) == wanted) {
      return c;
    }
  }
  return std::nullopt;
}

std::optional<CubieCube> afterAlg(const CubieCube &state, const Alg &alg) {
  FaceletCube f = state.toFacelets();
  f.apply(alg);
  return centredCubie(f);
}

int wideTurns(const Alg &alg) {
  return static_cast<int>(std::count_if(alg.begin(), alg.end(), [](Move m) {
    auto t = static_cast<int>(m.type);
    return t >= static_cast<int>(MoveType::Uw) &&
           t <= static_cast<int>(MoveType::Bw);
  }));
}

// Turns of opposite faces in a row in one order (U before D, R before L,
// F before B), as the search writes them, so that the same solution
// written the other way round compares equal.
Alg ordered(Alg alg) {
  for (bool swapped = true; swapped;) {
    swapped = false;
    for (std::size_t i = 0; i + 1 < alg.size(); ++i) {
      auto a = static_cast<int>(alg[i].type),
           b = static_cast<int>(alg[i + 1].type);
      if (a < 6 && b < 6 && a % 3 == b % 3 && b < a) {
        std::swap(alg[i], alg[i + 1]);
        swapped = true;
      }
    }
  }
  return alg;
}

// Whether the moves solve a slot's pair from a state, keeping the base and
// the slots that were solved.
bool solvesPair(const CubieCube &state, Slot slot, const Alg &alg,
                const F2LConfig &config) {
  auto c = afterAlg(state, alg);
  return c && f2lBase(*c, config) && slotSolved(*c, slot) &&
         std::all_of(kF2LSlots.begin(), kF2LSlots.end(), [&](Slot s) {
           return !slotSolved(state, s) || slotSolved(*c, s);
         });
}

// A solution with a turn it doesn't need: without it the pair is solved
// all the same (a U turn slipped in beside a D turn, say).
bool hasSpareTurn(const CubieCube &state, Slot slot, const Alg &alg,
                  const F2LConfig &config) {
  for (std::size_t i = 0; i < alg.size(); ++i) {
    Alg without = alg;
    without.erase(without.begin() + static_cast<std::ptrdiff_t>(i));
    if (solvesPair(state, slot, without, config)) {
      return true;
    }
  }
  return false;
}

struct Plan {
  double cost = 0.0;
  int moves = 0;
  std::vector<Slot> order;
  bool ok = true;
};

Plan cheapestPlan(const CubieCube &state, const AnalysisOptions &options,
                  const std::atomic<bool> *stop) {
  if (f2lDone(state, options.config)) {
    return {};
  }
  Plan best;
  best.ok = false;
  best.cost = std::numeric_limits<double>::infinity();
  for (Slot slot : unsolvedSlots(state)) {
    if (stop && stop->load()) {
      break;
    }
    PairAnalysis a = analysePair(state, slot, options, stop);
    if (!a.found()) {
      continue;
    }
    auto next = afterAlg(state, a.solutions[0].alg);
    if (!next) {
      continue;
    }
    Plan rest = cheapestPlan(*next, options, stop);
    if (!rest.ok) {
      continue;
    }
    double cost = a.solutions[0].cost + rest.cost;
    if (cost < best.cost) {
      best.ok = true;
      best.cost = cost;
      best.moves = a.solutions[0].htm + rest.moves;
      best.order = {slot};
      best.order.insert(best.order.end(), rest.order.begin(), rest.order.end());
    }
  }
  return best;
}

} // namespace

// --- the F2L of a method -----------------------------------------------------

PairMoves F2LConfig::moves() const {
  PairMoves m;
  m.d = dMoves;
  m.halfFB = eo ? halfFB : true;
  m.quarterFB = !eo;
  m.wide = !eo && wide;
  return m;
}

std::string_view methodId(F2LMethod m) {
  switch (m) {
  case F2LMethod::ZZMinus1:
    return "zz-1";
  case F2LMethod::ZZ:
    return "zz";
  case F2LMethod::CFOP:
    return "cfop";
  case F2LMethod::CFOPMinus1:
    return "cfop-1";
  }
  return "zz-1";
}

std::optional<F2LMethod> methodFromId(std::string_view id) {
  for (int i = 0; i < kF2LMethods; ++i) {
    if (methodId(static_cast<F2LMethod>(i)) == id) {
      return static_cast<F2LMethod>(i);
    }
  }
  return std::nullopt;
}

std::string_view methodName(F2LMethod m) {
  switch (m) {
  case F2LMethod::ZZMinus1:
    return "ZZ F2L - 1 (ZZ-CT)";
  case F2LMethod::ZZ:
    return "ZZ F2L (ZZ-OP, ZZ-A)";
  case F2LMethod::CFOP:
    return "CFOP F2L";
  case F2LMethod::CFOPMinus1:
    return "CFOP F2L - 1";
  }
  return "";
}

void applyMethod(F2LConfig &config, F2LMethod m) {
  config.eo = m == F2LMethod::ZZMinus1 || m == F2LMethod::ZZ;
  config.lastSlot = m == F2LMethod::ZZMinus1 || m == F2LMethod::CFOPMinus1;
}

F2LMethod methodOf(const F2LConfig &config) {
  if (config.eo) {
    return config.lastSlot ? F2LMethod::ZZMinus1 : F2LMethod::ZZ;
  }
  return config.lastSlot ? F2LMethod::CFOPMinus1 : F2LMethod::CFOP;
}

bool crossSolved(const CubieCube &c) {
  return std::all_of(kCross.begin(), kCross.end(),
                     [&c](Edge e) { return edgeSolved(c, e); });
}

bool edgesOriented(const CubieCube &c) {
  return std::all_of(c.eo.begin(), c.eo.end(),
                     [](std::uint8_t flip) { return flip == 0; });
}

bool f2lBase(const CubieCube &c, const F2LConfig &config) {
  return crossSolved(c) && (!config.eo || edgesOriented(c));
}

std::vector<Slot> unsolvedSlots(const CubieCube &c) {
  std::vector<Slot> out;
  for (Slot s : kF2LSlots) {
    if (!slotSolved(c, s)) {
      out.push_back(s);
    }
  }
  return out;
}

int solvedSlotCount(const CubieCube &c) {
  return kSlots - static_cast<int>(unsolvedSlots(c).size());
}

bool f2lDone(const CubieCube &c, const F2LConfig &config) {
  return f2lBase(c, config) && pairsLeft(c, config) == 0;
}

int pairsLeft(const CubieCube &c, const F2LConfig &config) {
  std::vector<Slot> open = unsolvedSlots(c);
  bool canLeave = std::any_of(open.begin(), open.end(),
                              [&](Slot s) { return config.leaves(s); });
  return static_cast<int>(open.size()) - (canLeave ? 1 : 0);
}

std::optional<CubieCube> centredCubie(const FaceletCube &cube) {
  FaceletCube f = cube;
  f.normalizeOrientation();
  return CubieCube::fromFacelets(f);
}

// --- pair cases --------------------------------------------------------------

std::optional<Slot> PairCase::cornerSlot() const {
  return slotOfCorner(corner / 3);
}

std::optional<Slot> PairCase::edgeSlot() const { return slotOfEdge(edge / 2); }

PairCase pairCaseOf(const CubieCube &state, Slot target) {
  CubieCube m = mirrorState(state, mirrorOf(target));
  int cp = placeOfCorner(m, DFR);
  int ep = placeOfEdge(m, FR);
  return normalized(cp * 3 + m.co[static_cast<std::size_t>(cp)],
                    ep * 2 + m.eo[static_cast<std::size_t>(ep)]);
}

std::string caseKey(PairCase c) {
  return std::string(kCornerNames[c.corner / 3]) +
         std::to_string(c.corner % 3) + "-" + kEdgeNames[c.edge / 2] +
         std::to_string(c.edge % 2);
}

std::optional<PairCase> caseFromKey(std::string_view key) {
  for (bool eo : {true, false}) {
    for (PairCase c : pairCases(eo)) {
      if (caseKey(c) == key) {
        return c;
      }
    }
  }
  return std::nullopt;
}

std::string caseName(PairCase c) {
  std::string out;
  int twist = c.corner % 3;
  // The face the corner's cross-colour sticker is on.
  int face = kCornerFacelets[c.corner / 3][static_cast<std::size_t>(twist)] / 9;
  if (c.cornerOnTop()) {
    out = face == 0 ? "Corner up" : face == 1 ? "Corner right" : "Corner front";
  } else if (*c.cornerSlot() == Slot::FR) {
    out = twist == 0 ? "Corner in slot"
                     : std::string("Corner in slot, twisted to ") +
                           kFaceNames[static_cast<std::size_t>(face)];
  } else {
    out = "Corner in " + std::string(slotName(*c.cornerSlot())) + " slot";
    if (twist != 0) {
      out += std::string(", twisted to ") +
             kFaceNames[static_cast<std::size_t>(face)];
    }
  }
  bool flipped = c.edge % 2 != 0;
  if (c.edgeOnTop()) {
    out += c.cornerOnTop() ? std::string("; edge ") + kEdgeNames[c.edge / 2]
                           : std::string("; edge on top");
  } else if (*c.edgeSlot() == Slot::FR) {
    out += "; edge in slot";
  } else {
    out += "; edge in " + std::string(slotName(*c.edgeSlot())) + " slot";
  }
  if (flipped) {
    out += " (flipped)";
  }
  return out;
}

PairGroup groupOf(PairCase c) {
  bool cornerOwn = !c.cornerOnTop() && *c.cornerSlot() == Slot::FR;
  bool edgeOwn = !c.edgeOnTop() && *c.edgeSlot() == Slot::FR;
  bool cornerOther = !c.cornerOnTop() && !cornerOwn;
  bool edgeOther = !c.edgeOnTop() && !edgeOwn;
  if (cornerOther && edgeOther) {
    return PairGroup::BothElsewhere;
  }
  if (cornerOther) {
    return PairGroup::CornerElsewhere;
  }
  if (edgeOther) {
    return PairGroup::EdgeElsewhere;
  }
  if (cornerOwn && edgeOwn) {
    return PairGroup::BothSlot;
  }
  if (cornerOwn) {
    return PairGroup::EdgeTopCornerSlot;
  }
  if (edgeOwn) {
    return PairGroup::CornerTopEdgeSlot;
  }
  return PairGroup::BothTop;
}

std::string_view groupName(PairGroup g) {
  switch (g) {
  case PairGroup::BothTop:
    return "Both on top";
  case PairGroup::CornerTopEdgeSlot:
    return "Corner on top, edge in slot";
  case PairGroup::EdgeTopCornerSlot:
    return "Edge on top, corner in slot";
  case PairGroup::BothSlot:
    return "Both in slot";
  case PairGroup::CornerElsewhere:
    return "Corner in another slot";
  case PairGroup::EdgeElsewhere:
    return "Edge in another slot";
  case PairGroup::BothElsewhere:
    return "Both in other slots";
  }
  return "";
}

const std::vector<PairCase> &pairCases(bool eo) {
  static const std::array<std::vector<PairCase>, 2> cases = [] {
    std::array<std::vector<PairCase>, 2> out;
    for (std::size_t withEo = 0; withEo < 2; ++withEo) {
      std::set<PairCase> seen;
      for (int corner = 0; corner < 24; ++corner) {
        for (int edge = 0; edge < 24; ++edge) {
          int place = edge / 2;
          if ((place >= DR && place <= DB) || (withEo == 1 && edge % 2 != 0)) {
            continue;
          }
          PairCase c = normalized(corner, edge);
          if (!c.solved()) {
            seen.insert(c);
          }
        }
      }
      out[withEo].assign(seen.begin(), seen.end());
      std::stable_sort(
          out[withEo].begin(), out[withEo].end(),
          [](PairCase a, PairCase b) { return groupOf(a) < groupOf(b); });
    }
    return out;
  }();
  return cases[eo ? 1 : 0];
}

// --- cards -------------------------------------------------------------------

std::string_view sideName(PairSide side) {
  return side == PairSide::Front ? "front" : "back";
}

std::string cardKey(const PairCard &card) {
  return std::string(sideName(card.side)) + "/" + caseKey(card.pcase);
}

std::optional<PairCard> cardFromKey(std::string_view key) {
  for (PairSide side : {PairSide::Front, PairSide::Back}) {
    std::string prefix = std::string(sideName(side)) + "/";
    if (key.starts_with(prefix)) {
      if (auto c = caseFromKey(key.substr(prefix.size()))) {
        return PairCard{side, *c};
      }
    }
  }
  return std::nullopt;
}

Slot sideSlot(PairSide side) {
  return side == PairSide::Front ? Slot::FR : Slot::BR;
}

PairSide sideOf(Slot slot) {
  return slot == Slot::FR || slot == Slot::FL ? PairSide::Front
                                              : PairSide::Back;
}

PairCard pairCardOf(const CubieCube &state, Slot target) {
  return {sideOf(target), pairCaseOf(state, target)};
}

std::vector<std::string> pairCardPool(bool eo) {
  std::vector<std::string> pool;
  for (PairCase c : pairCases(eo)) {
    pool.push_back(cardKey({PairSide::Front, c}));
    pool.push_back(cardKey({PairSide::Back, c}));
  }
  return pool;
}

std::optional<CubieCube> cardState(const PairCard &card, bool eo, Rng &rng) {
  const Slot target = sideSlot(card.side);
  std::vector<Slot> open{target};
  for (Slot s : slotsNeeded(card.pcase, target)) {
    open.push_back(s);
  }
  std::sort(open.begin(), open.end());
  std::optional<CubieCube> state =
      stateWithCase(open, eo, target, card.pcase, rng);
  if (!state) {
    return state;
  }
  // The top layer turned so that the pair is where the case has it, not
  // a U turn away: the solutions found don't depend on where it fell.
  for (int turn = 0; turn < 4; ++turn) {
    CubieCube m = mirrorState(*state, mirrorOf(target));
    const int cp = placeOfCorner(m, DFR);
    const int ep = placeOfEdge(m, FR);
    if (cp * 3 + m.co[static_cast<std::size_t>(cp)] == card.pcase.corner &&
        ep * 2 + m.eo[static_cast<std::size_t>(ep)] == card.pcase.edge) {
      break;
    }
    state = afterAlg(*state, {Move{MoveType::U, 1}});
  }
  return state;
}

std::optional<PairSetup> cardSetup(const PairCard &card,
                                   const F2LConfig &config, bool leaveOpen,
                                   Rng &rng) {
  const Slot own = sideSlot(card.side);
  const Slot other = own == Slot::FR ? Slot::FL : Slot::BL;
  const Slot target =
      std::uniform_int_distribution<int>(0, 1)(rng) == 0 ? own : other;
  std::vector<Slot> open{target};
  for (Slot s : slotsNeeded(card.pcase, target)) {
    open.push_back(s);
  }
  if (leaveOpen) {
    std::vector<Slot> leavable;
    for (Slot s : kF2LSlots) {
      if (config.leaves(s) && s != target) {
        leavable.push_back(s);
      }
    }
    if (!leavable.empty()) {
      Slot left = pickOne(leavable, rng);
      if (std::find(open.begin(), open.end(), left) == open.end()) {
        open.push_back(left);
      }
    }
  }
  std::sort(open.begin(), open.end());
  auto state = stateWithCase(open, config.eo, target, card.pcase, rng);
  if (!state) {
    return std::nullopt;
  }
  PairSetup out;
  out.state = *state;
  out.target = target;
  out.pcase = card.pcase;
  return out;
}

Alg sideToSlot(const Alg &alg, PairSide side, Slot slot) {
  return sideOf(slot) == side && slot != sideSlot(side) ? mirrorM(alg) : alg;
}

Alg comparableSolution(const Alg &moves) {
  Alg a = simplify(fixedFrame(moves));
  auto isU = [](Move m) { return m.type == MoveType::U; };
  auto isUD = [&isU](Move m) { return isU(m) || m.type == MoveType::D; };
  // The U turns among the U and D turns it starts and ends with (which
  // turn in either order) are the AUF; without them, what is left may
  // simplify, and start or end with more.
  for (bool changed = true; changed;) {
    const std::size_t before = a.size();
    auto front = std::find_if_not(a.begin(), a.end(), isUD);
    a.erase(std::remove_if(a.begin(), front, isU), front);
    auto back = std::find_if_not(a.rbegin(), a.rend(), isUD).base();
    a.erase(std::remove_if(back, a.end(), isU), a.end());
    changed = a.size() != before;
    if (changed) {
      a = simplify(a);
    }
  }
  return ordered(a);
}

std::optional<std::size_t> matchChosen(const Alg &moves, PairSide side,
                                       Slot slot,
                                       const std::vector<Alg> &chosen) {
  const Alg mine =
      comparableSolution(slotToSide(fixedFrame(moves), side, slot));
  for (std::size_t i = 0; i < chosen.size(); ++i) {
    if (comparableSolution(chosen[i]) == mine) {
      return i;
    }
  }
  return std::nullopt;
}

std::optional<int> solvesPairWithAuf(const CubieCube &state, Slot slot,
                                     const Alg &alg, const F2LConfig &config) {
  // Without the U turns it starts with, which the one put in replaces.
  Alg body = alg;
  while (!body.empty() && body.front().type == MoveType::U) {
    body.erase(body.begin());
  }
  for (int auf = 0; auf < 4; ++auf) {
    Alg moves;
    if (auf != 0) {
      moves.push_back({MoveType::U, static_cast<std::uint8_t>(auf)});
    }
    moves.insert(moves.end(), body.begin(), body.end());
    if (solvesPair(state, slot, moves, config)) {
      return auf;
    }
  }
  return std::nullopt;
}

std::optional<Alg> chosenFor(const CubieCube &state, Slot slot,
                             const Alg &chosen, const F2LConfig &config) {
  Alg body = sideToSlot(chosen, sideOf(slot), slot);
  while (!body.empty() && body.front().type == MoveType::U) {
    body.erase(body.begin());
  }
  std::optional<int> auf = solvesPairWithAuf(state, slot, body, config);
  if (!auf) {
    return std::nullopt;
  }
  Alg moves;
  if (*auf != 0) {
    moves.push_back({MoveType::U, static_cast<std::uint8_t>(*auf)});
  }
  moves.insert(moves.end(), body.begin(), body.end());
  return moves;
}

Mirror slotMirror(Slot s) {
  return {s == Slot::FL || s == Slot::BL, s == Slot::BL || s == Slot::BR};
}

Slot slotSeenFrom(Slot seen, Slot target) {
  Mirror m = mirrorOf(target);
  bool left = seen == Slot::FL || seen == Slot::BL;
  bool back = seen == Slot::BL || seen == Slot::BR;
  left = left != m.leftRight;
  back = back != m.frontBack;
  return back ? (left ? Slot::BL : Slot::BR) : (left ? Slot::FL : Slot::FR);
}

FaceletCube caseDiagram(PairCase c) {
  FaceletCube::Stickers s;
  s.fill(kMaskedColour);
  for (int face = 0; face < 6; ++face) {
    s[static_cast<std::size_t>(face * 9 + 4)] = static_cast<std::uint8_t>(face);
  }
  for (Edge e : kCross) {
    for (std::uint8_t facelet : kEdgeFacelets[e]) {
      s[facelet] = static_cast<std::uint8_t>(facelet / 9);
    }
  }
  for (std::size_t k = 0; k < 3; ++k) {
    s[kCornerFacelets[c.corner / 3][(k + c.corner % 3) % 3]] =
        static_cast<std::uint8_t>(kCornerFacelets[DFR][k] / 9);
  }
  for (std::size_t k = 0; k < 2; ++k) {
    s[kEdgeFacelets[c.edge / 2][(k + c.edge % 2) % 2]] =
        static_cast<std::uint8_t>(kEdgeFacelets[FR][k] / 9);
  }
  return FaceletCube(s);
}

FaceletCube pairDiagram(const CubieCube &state, Slot slot) {
  const FaceletCube full = state.toFacelets();
  FaceletCube::Stickers s;
  s.fill(kMaskedColour);
  for (int face = 0; face < 6; ++face) {
    s[static_cast<std::size_t>(face * 9 + 4)] = static_cast<std::uint8_t>(face);
  }
  for (std::size_t place = 0; place < kCorners; ++place) {
    if (state.cp[place] == slotCorner(slot)) {
      for (std::uint8_t f : kCornerFacelets[place]) {
        s[f] = full[f];
      }
    }
  }
  for (std::size_t place = 0; place < kEdges; ++place) {
    const Edge piece = static_cast<Edge>(state.ep[place]);
    if (piece == slotEdge(slot) ||
        std::find(kCross.begin(), kCross.end(), piece) != kCross.end()) {
      for (std::uint8_t f : kEdgeFacelets[place]) {
        s[f] = full[f];
      }
    }
  }
  return FaceletCube(s);
}

// --- starting states ---------------------------------------------------------

CubieCube randomF2LState(const std::vector<Slot> &open, bool eo, Rng &rng) {
  const Free edgeFree = eo ? Free::Perm : Free::PermAndOri;
  PieceMask mask = PieceMask{}.lastLayer(Free::PermAndOri, edgeFree);
  for (Slot s : open) {
    mask.corners({slotCorner(s)}, Free::PermAndOri);
    mask.edges({slotEdge(s)}, edgeFree);
  }
  mask.normalize();
  CubieCube c;
  for (int tries = 0; tries < 200; ++tries) {
    c = randomState(mask, rng);
    if (std::none_of(open.begin(), open.end(),
                     [&](Slot s) { return slotSolved(c, s); })) {
      break;
    }
  }
  return c;
}

int stageCount(const F2LConfig &config) {
  bool any = std::any_of(kF2LSlots.begin(), kF2LSlots.end(),
                         [&](Slot s) { return config.leaves(s); });
  return any ? kSlots - 1 : kSlots;
}

std::optional<PairSetup> randomPairSetup(const F2LConfig &config, int stage,
                                         std::optional<PairCase> wanted,
                                         Rng &rng) {
  for (int attempt = 0; attempt < 8; ++attempt) {
    // The slot left to the end, and the pair to solve among the others.
    std::vector<Slot> leavable, targets;
    for (Slot s : kF2LSlots) {
      if (config.leaves(s)) {
        leavable.push_back(s);
      }
    }
    std::optional<Slot> left;
    if (!leavable.empty()) {
      left = pickOne(leavable, rng);
    }
    for (Slot s : kF2LSlots) {
      if (s != left) {
        targets.push_back(s);
      }
    }
    Slot target = pickOne(targets, rng);
    std::vector<Slot> needed{target};
    if (left) {
      needed.push_back(*left);
    }
    if (wanted) {
      for (Slot s : slotsNeeded(*wanted, target)) {
        if (std::find(needed.begin(), needed.end(), s) == needed.end()) {
          needed.push_back(s);
        }
      }
    }
    int stages = stageCount(config);
    int before = stage > 0
                     ? std::min(stage, stages) - 1
                     : std::uniform_int_distribution<int>(0, stages - 1)(rng);
    before = std::min(before, kSlots - static_cast<int>(needed.size()));
    std::vector<Slot> others;
    for (Slot s : kF2LSlots) {
      if (std::find(needed.begin(), needed.end(), s) == needed.end()) {
        others.push_back(s);
      }
    }
    std::shuffle(others.begin(), others.end(), rng);
    std::vector<Slot> open = needed;
    open.insert(open.end(), others.begin() + before, others.end());
    std::sort(open.begin(), open.end());
    PairSetup out;
    out.target = target;
    if (wanted) {
      auto state = stateWithCase(open, config.eo, target, *wanted, rng);
      if (!state) {
        continue;
      }
      out.state = *state;
    } else {
      out.state = randomF2LState(open, config.eo, rng);
    }
    out.pcase = pairCaseOf(out.state, target);
    return out;
  }
  return std::nullopt;
}

CubieCube randomFullState(const F2LConfig &config, Rng &rng) {
  return randomF2LState({kF2LSlots.begin(), kF2LSlots.end()}, config.eo, rng);
}

// --- blind spots -------------------------------------------------------------

int flatStickers(bool corner, int place) {
  static const std::array<bool, kFaceletCount> shown = [] {
    std::array<bool, kFaceletCount> out{};
    for (const auto &row : flatLayout()) {
      for (int facelet : row) {
        out[static_cast<std::size_t>(facelet)] = true;
      }
    }
    return out;
  }();
  int n = 0;
  if (corner) {
    for (std::uint8_t f : kCornerFacelets[static_cast<std::size_t>(place)]) {
      n += shown[f] ? 1 : 0;
    }
  } else {
    for (std::uint8_t f : kEdgeFacelets[static_cast<std::size_t>(place)]) {
      n += shown[f] ? 1 : 0;
    }
  }
  return n;
}

double hiddenCost(const CubieCube &state, Slot slot) {
  auto cost = [](int shown, int needed) {
    return shown >= needed ? 0.0 : shown == 0 ? kHidden : kPartlyHidden;
  };
  return cost(flatStickers(true, placeOfCorner(state, slotCorner(slot))), 2) +
         cost(flatStickers(false, placeOfEdge(state, slotEdge(slot))), 2);
}

// --- ranking -----------------------------------------------------------------

double solutionCost(const Alg &alg, const AnalysisOptions &options) {
  Alg simple = simplify(alg);
  return ergoTotal(simple, options.profile) +
         options.widePenalty * wideTurns(simple);
}

PairAnalysis analysePair(const CubieCube &state, Slot slot,
                         const AnalysisOptions &options,
                         const std::atomic<bool> *stop) {
  PairAnalysis out;
  out.slot = slot;
  out.pcase = pairCaseOf(state, slot);
  if (!f2lBase(state, options.config)) {
    return out;
  }
  PairSearchOptions search;
  search.moves = options.config.moves();
  search.extra = options.extra;
  search.maxResults = options.maxResults;
  PairSearchResult found = searchPair(state, slot, search, stop);
  out.shortest = std::max(found.shortest, 0);
  std::vector<RankedSolution> ranked;
  ranked.reserve(found.algs.size());
  for (Alg &alg : found.algs) {
    if (simplify(alg).size() != alg.size() ||
        (static_cast<int>(alg.size()) > found.shortest &&
         hasSpareTurn(state, slot, alg, options.config))) {
      continue; // a shorter solution written long
    }
    RankedSolution r;
    r.htm = htmCount(alg);
    r.cost = solutionCost(alg, options);
    r.alg = std::move(alg);
    ranked.push_back(std::move(r));
  }
  std::stable_sort(ranked.begin(), ranked.end(),
                   [](const RankedSolution &a, const RankedSolution &b) {
                     return a.cost != b.cost ? a.cost < b.cost : a.htm < b.htm;
                   });
  if (ranked.size() > options.keep) {
    ranked.resize(options.keep);
  }
  for (RankedSolution &r : ranked) {
    r.text = toString(r.alg);
  }
  out.solutions = std::move(ranked);
  // The reference set's alg for the pair.
  if (options.reference && edgesOriented(state)) {
    for (const PairOption &o : pairOptions(state, options.reference.get())) {
      if (o.slot != slot || o.caseId.empty()) {
        continue;
      }
      Alg alg = ordered(simplify(parseAlgOrEmpty(o.alg)));
      std::string name = o.caseName.empty() ? o.caseId : o.caseName;
      auto same = std::find_if(
          out.solutions.begin(), out.solutions.end(),
          [&alg](const RankedSolution &r) { return r.alg == alg; });
      if (same != out.solutions.end()) {
        same->setCase = name;
      } else if (!alg.empty()) {
        RankedSolution r;
        r.text = toString(alg);
        r.htm = htmCount(alg);
        r.cost = solutionCost(alg, options);
        r.alg = std::move(alg);
        r.setCase = name;
        auto at = std::find_if(
            out.solutions.begin(), out.solutions.end(),
            [&r](const RankedSolution &s) { return r.cost < s.cost; });
        out.solutions.insert(at, std::move(r));
      }
    }
  }
  return out;
}

std::vector<PairRank> rankPairs(const CubieCube &state,
                                const AnalysisOptions &options,
                                const std::atomic<bool> *stop) {
  std::vector<PairRank> out;
  if (!f2lBase(state, options.config)) {
    return out;
  }
  // The pairs after the first are looked at more quickly: one solution
  // each, a move over the shortest at most, no reference set.
  AnalysisOptions ahead = options;
  ahead.keep = 1;
  ahead.extra = std::min(options.extra, 1);
  ahead.maxResults = 150;
  ahead.reference = nullptr;
  for (Slot slot : unsolvedSlots(state)) {
    if (stop && stop->load()) {
      break;
    }
    PairRank r;
    r.pair = analysePair(state, slot, options, stop);
    if (!r.pair.found()) {
      continue;
    }
    const RankedSolution &best = r.pair.solutions[0];
    auto next = afterAlg(state, best.alg);
    if (!next) {
      continue;
    }
    Plan rest = cheapestPlan(*next, ahead, stop);
    if (!rest.ok) {
      continue;
    }
    r.rest = rest.cost;
    r.hidden = hiddenCost(state, slot);
    r.total = best.cost + r.rest + r.hidden;
    r.order = {slot};
    r.order.insert(r.order.end(), rest.order.begin(), rest.order.end());
    r.moves = best.htm + rest.moves;
    out.push_back(std::move(r));
  }
  std::stable_sort(
      out.begin(), out.end(),
      [](const PairRank &a, const PairRank &b) { return a.total < b.total; });
  return out;
}

// --- a solve's pairs ---------------------------------------------------------

std::vector<F2LSegment> f2lSegments(const CubieCube &start, const Alg &moves,
                                    const F2LConfig &config) {
  std::vector<F2LSegment> out;
  FaceletCube f = start.toFacelets();
  CubieCube before = start;
  int best = solvedSlotCount(start);
  std::size_t begin = 0;
  for (std::size_t i = 0; i < moves.size(); ++i) {
    f.apply(moves[i]);
    auto c = centredCubie(f);
    if (!c || !f2lBase(*c, config) || solvedSlotCount(*c) <= best) {
      continue;
    }
    F2LSegment seg;
    seg.begin = begin;
    seg.end = i + 1;
    seg.before = before;
    for (Slot s : kF2LSlots) {
      if (slotSolved(*c, s) && !slotSolved(before, s)) {
        seg.solved.push_back(s);
      }
    }
    out.push_back(std::move(seg));
    best = solvedSlotCount(*c);
    before = *c;
    begin = i + 1;
  }
  return out;
}

} // namespace vcube
