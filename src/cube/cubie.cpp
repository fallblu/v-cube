#include "cube/cubie.hpp"

namespace vcube {

// U=0..8 R=9..17 F=18..26 D=27..35 L=36..44 B=45..53
const std::array<std::array<std::uint8_t, 3>, kCorners> kCornerFacelets = {{
    {8, 9, 20},   // URF: U9 R1 F3
    {6, 18, 38},  // UFL: U7 F1 L3
    {0, 36, 47},  // ULB: U1 L1 B3
    {2, 45, 11},  // UBR: U3 B1 R3
    {29, 26, 15}, // DFR: D3 F9 R7
    {27, 44, 24}, // DLF: D1 L9 F7
    {33, 53, 42}, // DBL: D7 B9 L7
    {35, 17, 51}, // DRB: D9 R9 B7
}};

const std::array<std::array<std::uint8_t, 2>, kEdges> kEdgeFacelets = {{
    {5, 10},  // UR
    {7, 19},  // UF
    {3, 37},  // UL
    {1, 46},  // UB
    {32, 16}, // DR
    {28, 25}, // DF
    {30, 43}, // DL
    {34, 52}, // DB
    {23, 12}, // FR
    {21, 41}, // FL
    {50, 39}, // BL
    {48, 14}, // BR
}};

namespace {

std::uint8_t faceOf(std::uint8_t facelet) {
  return static_cast<std::uint8_t>(facelet / 9);
}

template <std::size_t N> int permutationParity(const std::array<std::uint8_t, N> &p) {
  int inversions = 0;
  for (std::size_t i = 0; i < N; ++i) {
    for (std::size_t j = i + 1; j < N; ++j) {
      if (p[i] > p[j]) {
        ++inversions;
      }
    }
  }
  return inversions % 2;
}

} // namespace

CubieCube CubieCube::multiply(const CubieCube &a, const CubieCube &b) {
  CubieCube r;
  for (std::size_t i = 0; i < kCorners; ++i) {
    r.cp[i] = a.cp[b.cp[i]];
    r.co[i] = static_cast<std::uint8_t>((a.co[b.cp[i]] + b.co[i]) % 3);
  }
  for (std::size_t i = 0; i < kEdges; ++i) {
    r.ep[i] = a.ep[b.ep[i]];
    r.eo[i] = static_cast<std::uint8_t>((a.eo[b.ep[i]] + b.eo[i]) % 2);
  }
  return r;
}

CubieCube CubieCube::inverse() const {
  CubieCube r;
  for (std::size_t i = 0; i < kCorners; ++i) {
    r.cp[cp[i]] = static_cast<std::uint8_t>(i);
  }
  for (std::size_t i = 0; i < kCorners; ++i) {
    r.co[i] = static_cast<std::uint8_t>((3 - co[r.cp[i]]) % 3);
  }
  for (std::size_t i = 0; i < kEdges; ++i) {
    r.ep[ep[i]] = static_cast<std::uint8_t>(i);
  }
  for (std::size_t i = 0; i < kEdges; ++i) {
    r.eo[i] = eo[r.ep[i]];
  }
  return r;
}

void CubieCube::apply(Move m) {
  const CubieCube &t = faceTurnCubie(static_cast<Face>(m.type));
  for (int i = 0; i < m.amount; ++i) {
    *this = *this * t;
  }
}

void CubieCube::apply(const Alg &alg) {
  for (Move m : alg) {
    apply(m);
  }
}

int CubieCube::cornerParity() const { return permutationParity(cp); }
int CubieCube::edgeParity() const { return permutationParity(ep); }

bool CubieCube::isValid() const {
  std::array<bool, kEdges> seenE{};
  std::array<bool, kCorners> seenC{};
  int twist = 0, flip = 0;
  for (std::size_t i = 0; i < kCorners; ++i) {
    if (cp[i] >= kCorners || seenC[cp[i]] || co[i] > 2) {
      return false;
    }
    seenC[cp[i]] = true;
    twist += co[i];
  }
  for (std::size_t i = 0; i < kEdges; ++i) {
    if (ep[i] >= kEdges || seenE[ep[i]] || eo[i] > 1) {
      return false;
    }
    seenE[ep[i]] = true;
    flip += eo[i];
  }
  return twist % 3 == 0 && flip % 2 == 0 && cornerParity() == edgeParity();
}

FaceletCube CubieCube::toFacelets() const {
  FaceletCube f;
  for (std::size_t i = 0; i < kCorners; ++i) {
    for (std::size_t k = 0; k < 3; ++k) {
      f[kCornerFacelets[i][(k + co[i]) % 3]] =
          faceOf(kCornerFacelets[cp[i]][k]);
    }
  }
  for (std::size_t i = 0; i < kEdges; ++i) {
    for (std::size_t k = 0; k < 2; ++k) {
      f[kEdgeFacelets[i][(k + eo[i]) % 2]] = faceOf(kEdgeFacelets[ep[i]][k]);
    }
  }
  return f;
}

std::optional<CubieCube> CubieCube::fromFacelets(const FaceletCube &f) {
  if (!f.isStandardOrientation()) {
    return std::nullopt;
  }
  CubieCube c;
  for (std::size_t i = 0; i < kCorners; ++i) {
    std::size_t ori = 0;
    for (; ori < 3; ++ori) {
      auto col = f[kCornerFacelets[i][ori]];
      if (col == static_cast<std::uint8_t>(Face::U) ||
          col == static_cast<std::uint8_t>(Face::D)) {
        break;
      }
    }
    if (ori == 3) {
      return std::nullopt;
    }
    auto col1 = f[kCornerFacelets[i][(ori + 1) % 3]];
    auto col2 = f[kCornerFacelets[i][(ori + 2) % 3]];
    bool found = false;
    for (std::size_t j = 0; j < kCorners; ++j) {
      if (col1 == faceOf(kCornerFacelets[j][1]) &&
          col2 == faceOf(kCornerFacelets[j][2])) {
        c.cp[i] = static_cast<std::uint8_t>(j);
        c.co[i] = static_cast<std::uint8_t>(ori);
        found = true;
        break;
      }
    }
    if (!found) {
      return std::nullopt;
    }
  }
  for (std::size_t i = 0; i < kEdges; ++i) {
    auto a = f[kEdgeFacelets[i][0]];
    auto b = f[kEdgeFacelets[i][1]];
    bool found = false;
    for (std::size_t j = 0; j < kEdges; ++j) {
      auto ja = faceOf(kEdgeFacelets[j][0]);
      auto jb = faceOf(kEdgeFacelets[j][1]);
      if (a == ja && b == jb) {
        c.ep[i] = static_cast<std::uint8_t>(j);
        c.eo[i] = 0;
        found = true;
        break;
      }
      if (a == jb && b == ja) {
        c.ep[i] = static_cast<std::uint8_t>(j);
        c.eo[i] = 1;
        found = true;
        break;
      }
    }
    if (!found) {
      return std::nullopt;
    }
  }
  if (!c.isValid()) {
    return std::nullopt;
  }
  return c;
}

const CubieCube &faceTurnCubie(Face f) {
  static const std::array<CubieCube, 6> turns = [] {
    std::array<CubieCube, 6> out{};
    for (std::size_t i = 0; i < 6; ++i) {
      FaceletCube fc;
      fc.apply(Move{static_cast<MoveType>(i), 1});
      out[i] = *CubieCube::fromFacelets(fc);
    }
    return out;
  }();
  return turns[static_cast<std::size_t>(f)];
}

CubieCube cubieOf(const Alg &alg) {
  FaceletCube f;
  f.apply(alg);
  f.normalizeOrientation();
  return CubieCube::fromFacelets(f).value_or(CubieCube{});
}

CubieCube uTurn(int quarters) {
  CubieCube c;
  int q = ((quarters % 4) + 4) % 4;
  if (q != 0) {
    c.apply(Move{MoveType::U, static_cast<std::uint8_t>(q)});
  }
  return c;
}

} // namespace vcube
