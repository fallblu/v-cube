#include "cube/scramble.hpp"

#include "cube/solver.hpp"

#include <algorithm>

namespace vcube {

Rng makeRng() {
  std::random_device rd;
  std::seed_seq seq{rd(), rd(), rd(), rd()};
  return Rng(seq);
}

PieceMask PieceMask::all() {
  PieceMask m;
  m.cp.fill(true);
  m.co.fill(true);
  m.ep.fill(true);
  m.eo.fill(true);
  return m;
}

PieceMask &PieceMask::corners(std::initializer_list<Corner> cs, bool perm,
                              bool ori) {
  for (Corner c : cs) {
    cp[c] = cp[c] || perm;
    co[c] = co[c] || ori;
  }
  return *this;
}

PieceMask &PieceMask::edges(std::initializer_list<Edge> es, bool perm,
                            bool ori) {
  for (Edge e : es) {
    ep[e] = ep[e] || perm;
    eo[e] = eo[e] || ori;
  }
  return *this;
}

PieceMask &PieceMask::lastLayer(bool perm, bool cornerOri, bool edgeOri) {
  corners({URF, UFL, ULB, UBR}, perm, cornerOri);
  edges({UR, UF, UL, UB}, perm, edgeOri);
  return *this;
}

void PieceMask::normalize() {
  auto fix = [](auto &perm, auto &ori) {
    bool anyFreeOri = false;
    for (std::size_t i = 0; i < perm.size(); ++i) {
      anyFreeOri = anyFreeOri || (perm[i] && ori[i]);
    }
    if (anyFreeOri) {
      for (std::size_t i = 0; i < perm.size(); ++i) {
        ori[i] = ori[i] || perm[i];
      }
    }
  };
  fix(cp, co);
  fix(ep, eo);
}

bool PieceMask::isEmpty() const { return *this == PieceMask{}; }

namespace {

template <std::size_t N>
std::vector<std::size_t> freePositions(const std::array<bool, N> &free) {
  std::vector<std::size_t> out;
  for (std::size_t i = 0; i < N; ++i) {
    if (free[i]) {
      out.push_back(i);
    }
  }
  return out;
}

template <std::size_t N>
void shuffleAmong(std::array<std::uint8_t, N> &perm,
                  const std::vector<std::size_t> &pos, Rng &rng) {
  std::vector<std::uint8_t> pieces;
  for (auto p : pos) {
    pieces.push_back(perm[p]);
  }
  std::shuffle(pieces.begin(), pieces.end(), rng);
  for (std::size_t i = 0; i < pos.size(); ++i) {
    perm[pos[i]] = pieces[i];
  }
}

template <std::size_t N>
void randomOrientation(std::array<std::uint8_t, N> &ori,
                       const std::array<bool, N> &free, int base, Rng &rng) {
  auto pos = freePositions(free);
  if (pos.empty()) {
    return;
  }
  std::uniform_int_distribution<int> dist(0, base - 1);
  int sum = 0;
  for (std::size_t i = 0; i + 1 < pos.size(); ++i) {
    ori[pos[i]] = static_cast<std::uint8_t>(dist(rng));
    sum += ori[pos[i]];
  }
  // The last free position makes the total a multiple of `base`; sums of
  // the fixed positions are zero.
  ori[pos.back()] = static_cast<std::uint8_t>((base - sum % base) % base);
}

} // namespace

CubieCube randomState(const PieceMask &maskIn, Rng &rng) {
  PieceMask mask = maskIn;
  mask.normalize();
  CubieCube c;
  auto cpos = freePositions(mask.cp);
  auto epos = freePositions(mask.ep);
  shuffleAmong(c.cp, cpos, rng);
  shuffleAmong(c.ep, epos, rng);
  if (c.cornerParity() != c.edgeParity()) {
    // Fix parity by swapping two free pieces, preferring edges.
    if (epos.size() >= 2) {
      std::swap(c.ep[epos[0]], c.ep[epos[1]]);
    } else if (cpos.size() >= 2) {
      std::swap(c.cp[cpos[0]], c.cp[cpos[1]]);
    }
  }
  randomOrientation(c.co, mask.co, 3, rng);
  randomOrientation(c.eo, mask.eo, 2, rng);
  return c;
}

bool withinMask(const CubieCube &s, const PieceMask &mask) {
  for (std::size_t i = 0; i < kCorners; ++i) {
    if ((!mask.cp[i] && s.cp[i] != i) || (!mask.co[i] && s.co[i] != 0)) {
      return false;
    }
  }
  for (std::size_t i = 0; i < kEdges; ++i) {
    if ((!mask.ep[i] && s.ep[i] != i) || (!mask.eo[i] && s.eo[i] != 0)) {
      return false;
    }
  }
  return true;
}

Alg scrambleToState(const CubieCube &state) {
  return invert(TwoPhaseSolver::solve(state));
}

const std::vector<ScrambleType> &scrambleTypes() {
  static const std::vector<ScrambleType> types = [] {
    std::vector<ScrambleType> t;
    auto add = [&](std::string id, std::string name, PieceMask m, bool before,
                   bool after) {
      m.normalize();
      t.push_back({std::move(id), std::move(name), m, before, after});
    };
    PieceMask ll = PieceMask{}.lastLayer(true, true, true);
    PieceMask cross = PieceMask::all();
    for (Edge e : {DR, DF, DL, DB}) {
      cross.ep[e] = false;
      cross.eo[e] = false;
    }
    PieceMask xcross = cross;
    xcross.ep[FR] = xcross.eo[FR] = false;
    xcross.cp[DFR] = xcross.co[DFR] = false;

    PieceMask eoline;
    eoline.cp.fill(true);
    eoline.co.fill(true);
    eoline.ep.fill(true);
    eoline.ep[DF] = eoline.ep[DB] = false;
    PieceMask eocross = eoline;
    eocross.ep[DR] = eocross.ep[DL] = false;

    PieceMask cornersOnly;
    cornersOnly.cp.fill(true);
    cornersOnly.co.fill(true);
    PieceMask edgesOnly;
    edgesOnly.ep.fill(true);
    edgesOnly.eo.fill(true);

    add("333", "3x3 random state (WCA)", PieceMask::all(), false, false);
    add("f2l", "F2L (cross solved)", cross, false, false);
    add("xcross", "XCross solved (FR slot done)", xcross, false, false);
    add("eoline", "EOLine solved", eoline, false, false);
    add("eocross", "EOCross solved", eocross, false, false);
    add("lsll", "Last slot + last layer",
        PieceMask{}
            .lastLayer(true, true, true)
            .corners({DFR}, true, true)
            .edges({FR}, true, true),
        false, true);
    add("ll", "Last layer", ll, false, true);
    add("zbll", "ZBLL (LL edges oriented)",
        PieceMask{}.lastLayer(true, true, false), false, true);
    add("cll", "LL corners only",
        PieceMask{}.corners({URF, UFL, ULB, UBR}, true, true), false, false);
    add("ell", "LL edges only", PieceMask{}.edges({UR, UF, UL, UB}, true, true),
        false, false);
    add("pll", "PLL", PieceMask{}.lastLayer(true, false, false), false, true);
    add("epll", "EPLL (LL edges permuted)",
        PieceMask{}.edges({UR, UF, UL, UB}, true, false), false, false);
    add("corners", "Corners only (edges solved)", cornersOnly, false, false);
    add("edges", "Edges only (corners solved)", edgesOnly, false, false);
    return t;
  }();
  return types;
}

const ScrambleType *findScrambleType(const std::string &id) {
  for (const auto &t : scrambleTypes()) {
    if (t.id == id) {
      return &t;
    }
  }
  return nullptr;
}

CubieCube randomStateOf(const ScrambleType &type, Rng &rng) {
  std::uniform_int_distribution<int> quarter(0, 3);
  for (int attempt = 0; attempt < 1000; ++attempt) {
    CubieCube s = randomState(type.mask, rng);
    if (type.aufBefore) {
      s = uTurn(quarter(rng)) * s;
    }
    if (type.aufAfter) {
      s = s * uTurn(quarter(rng));
    }
    if (s.isSolved()) {
      continue;
    }
    // Reject states one face turn from solved.
    bool trivial = false;
    for (int f = 0; f < 6 && !trivial; ++f) {
      CubieCube t = s;
      for (int a = 1; a <= 3; ++a) {
        t = t * faceTurnCubie(static_cast<Face>(f));
        trivial = trivial || t.isSolved();
      }
    }
    if (!trivial) {
      return s;
    }
  }
  return randomState(type.mask, rng);
}

} // namespace vcube
