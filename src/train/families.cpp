#include "train/families.hpp"

#include "cube/facelet.hpp"

#include <algorithm>
#include <array>
#include <numeric>

namespace vcube {

namespace {

// The mirrors a family is made of: left-right, front-back and both.
struct Mirror {
  bool leftRight;
  bool frontBack;
};
constexpr std::array<Mirror, 3> kMirrors = {{
    {true, false},
    {false, true},
    {true, true},
}};

Vec3i mirrored(Vec3i v, Mirror m) {
  return {m.leftRight ? -v.x : v.x, v.y, m.frontBack ? -v.z : v.z};
}

std::size_t mirrorIndex(Mirror m) {
  return static_cast<std::size_t>((m.leftRight ? 1 : 0) +
                                  (m.frontBack ? 2 : 0));
}

// Where each facelet goes in each mirror (indexed by mirrorIndex).
const std::array<std::array<std::uint8_t, kFaceletCount>, 4> &faceletMirrors() {
  static const auto table = [] {
    std::array<std::array<std::uint8_t, kFaceletCount>, 4> out{};
    for (std::size_t k = 0; k < 4; ++k) {
      Mirror m{(k & 1) != 0, (k & 2) != 0};
      for (int i = 0; i < kFaceletCount; ++i) {
        Vec3i p = mirrored(faceletPosition(i), m);
        Vec3i n = mirrored(faceletNormal(i), m);
        for (int j = 0; j < kFaceletCount; ++j) {
          if (faceletPosition(j) == p && faceletNormal(j) == n) {
            out[k][static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(j);
            break;
          }
        }
      }
    }
    return out;
  }();
  return table;
}

std::uint8_t mirroredColour(std::uint8_t colour, Mirror m) {
  auto face = static_cast<Face>(colour);
  if (m.leftRight && (face == Face::R || face == Face::L)) {
    return static_cast<std::uint8_t>(face == Face::R ? Face::L : Face::R);
  }
  if (m.frontBack && (face == Face::F || face == Face::B)) {
    return static_cast<std::uint8_t>(face == Face::F ? Face::B : Face::F);
  }
  return colour;
}

// The corner and edge positions each mirror maps each position to.
struct PieceMirror {
  std::array<std::uint8_t, kCorners> corners{};
  std::array<std::uint8_t, kEdges> edges{};
};

const PieceMirror &pieceMirror(Mirror m) {
  static const auto table = [] {
    std::array<PieceMirror, 4> out{};
    for (std::size_t k = 0; k < 4; ++k) {
      Mirror mm{(k & 1) != 0, (k & 2) != 0};
      for (std::size_t c = 0; c < kCorners; ++c) {
        Vec3i p = mirrored(faceletPosition(kCornerFacelets[c][0]), mm);
        for (std::size_t d = 0; d < kCorners; ++d) {
          if (faceletPosition(kCornerFacelets[d][0]) == p) {
            out[k].corners[c] = static_cast<std::uint8_t>(d);
          }
        }
      }
      for (std::size_t e = 0; e < kEdges; ++e) {
        Vec3i p = mirrored(faceletPosition(kEdgeFacelets[e][0]), mm);
        for (std::size_t d = 0; d < kEdges; ++d) {
          if (faceletPosition(kEdgeFacelets[d][0]) == p) {
            out[k].edges[e] = static_cast<std::uint8_t>(d);
          }
        }
      }
    }
    return out;
  }();
  return table[mirrorIndex(m)];
}

PieceMask mirroredMask(const PieceMask &mask, Mirror m) {
  const PieceMirror &pm = pieceMirror(m);
  PieceMask out;
  for (std::size_t c = 0; c < kCorners; ++c) {
    out.cp[pm.corners[c]] = mask.cp[c];
    out.co[pm.corners[c]] = mask.co[c];
  }
  for (std::size_t e = 0; e < kEdges; ++e) {
    out.ep[pm.edges[e]] = mask.ep[e];
    out.eo[pm.edges[e]] = mask.eo[e];
  }
  return out;
}

// Where a case's pieces to solve are: piece, position and orientation of
// each, corners then edges, for looking cases up.
std::vector<int> pieceKey(const CasePieces &pieces, const CubieCube &state) {
  std::vector<int> key;
  for (Corner p : pieces.corners) {
    for (std::size_t i = 0; i < kCorners; ++i) {
      if (state.cp[i] == p) {
        key.insert(key.end(), {static_cast<int>(p), static_cast<int>(i),
                               static_cast<int>(state.co[i])});
      }
    }
  }
  key.push_back(-1);
  for (Edge p : pieces.edges) {
    for (std::size_t i = 0; i < kEdges; ++i) {
      if (state.ep[i] == p) {
        key.insert(key.end(), {static_cast<int>(p), static_cast<int>(i),
                               static_cast<int>(state.eo[i])});
      }
    }
  }
  return key;
}

struct UnionFind {
  std::vector<std::size_t> parent;
  explicit UnionFind(std::size_t n) : parent(n) {
    std::iota(parent.begin(), parent.end(), std::size_t{0});
  }
  std::size_t find(std::size_t i) {
    while (parent[i] != i) {
      parent[i] = parent[parent[i]];
      i = parent[i];
    }
    return i;
  }
  void unite(std::size_t a, std::size_t b) {
    a = find(a);
    b = find(b);
    if (a != b) {
      // The earlier case stays the root.
      parent[std::max(a, b)] = std::min(a, b);
    }
  }
};

} // namespace

CubieCube mirrorState(const CubieCube &c, bool leftRight, bool frontBack) {
  Mirror m{leftRight, frontBack};
  if (!leftRight && !frontBack) {
    return c;
  }
  const auto &to = faceletMirrors()[mirrorIndex(m)];
  FaceletCube f = c.toFacelets();
  FaceletCube out;
  for (int i = 0; i < kFaceletCount; ++i) {
    out[to[static_cast<std::size_t>(i)]] = mirroredColour(f[i], m);
  }
  return CubieCube::fromFacelets(out).value_or(CubieCube{});
}

CasePieces casePieces(const CubieCube &state, const PieceMask &free) {
  CasePieces out;
  for (std::size_t p = 0; p < kCorners; ++p) {
    if (free.cp[p]) {
      continue;
    }
    // Solved: the piece is at its home position, untwisted.
    if (state.cp[p] != p || state.co[p] != 0) {
      out.corners.push_back(static_cast<Corner>(p));
    }
  }
  for (std::size_t p = 0; p < kEdges; ++p) {
    if (free.ep[p]) {
      continue;
    }
    if (state.ep[p] != p || state.eo[p] != 0) {
      out.edges.push_back(static_cast<Edge>(p));
    }
  }
  return out;
}

const std::vector<std::string> *
CaseFamilies::familyFor(const std::string &caseId) const {
  auto it = familyOf.find(caseId);
  return it == familyOf.end() ? nullptr : &families[it->second];
}

std::string CaseFamilies::representative(const std::string &caseId) const {
  const auto *family = familyFor(caseId);
  return family && !family->empty() ? family->front() : caseId;
}

CaseFamilies findFamilies(const AlgSet &set) {
  struct Entry {
    const AlgCase *c;
    PieceMask free;
    CasePieces pieces;
  };
  std::vector<Entry> cases;
  for (const auto &g : set.groups) {
    for (const auto &c : g.cases) {
      const PieceMask &free = set.specFor(g).free;
      cases.push_back({&c, free, casePieces(c.state, free)});
    }
  }
  // Cases by where their pieces to solve are.
  std::map<std::vector<int>, std::vector<std::size_t>> byKey;
  for (std::size_t i = 0; i < cases.size(); ++i) {
    byKey[pieceKey(cases[i].pieces, cases[i].c->state)].push_back(i);
  }
  UnionFind uf(cases.size());
  const int before = set.aufBefore ? 4 : 1;
  const int after = set.aufAfter ? 4 : 1;
  for (std::size_t i = 0; i < cases.size(); ++i) {
    const Entry &a = cases[i];
    for (Mirror m : kMirrors) {
      CubieCube mirroredState =
          mirrorState(a.c->state, m.leftRight, m.frontBack);
      PieceMask free = mirroredMask(a.free, m);
      for (int u = 0; u < before; ++u) {
        for (int v = 0; v < after; ++v) {
          // The mirrored case with a U turn after it (to undo before the
          // alg) and one before it (left by the alg).
          CubieCube x = uTurn(v) * mirroredState * uTurn(u);
          auto it = byKey.find(pieceKey(casePieces(x, free), x));
          if (it == byKey.end()) {
            continue;
          }
          for (std::size_t j : it->second) {
            if (j == i) {
              continue;
            }
            // The same case: they differ only by pieces the step leaves
            // free, as Drill's states of a case do.
            const Entry &b = cases[j];
            if (withinMask(x * b.c->state.inverse(), b.free)) {
              uf.unite(i, j);
            }
          }
        }
      }
    }
  }
  CaseFamilies out;
  std::map<std::size_t, std::size_t> familyOfRoot;
  for (std::size_t i = 0; i < cases.size(); ++i) {
    std::size_t root = uf.find(i);
    auto [it, added] = familyOfRoot.emplace(root, out.families.size());
    if (added) {
      out.families.emplace_back();
    }
    out.families[it->second].push_back(cases[i].c->id);
    out.familyOf[cases[i].c->id] = it->second;
  }
  return out;
}

} // namespace vcube
