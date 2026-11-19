#include "train/families.hpp"

#include "cube/facelet.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <numeric>

namespace vcube {

namespace {

// The mirrors a family is made of: left-right, front-back and both.
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

} // namespace

PieceMask mirrorMask(const PieceMask &mask, Mirror m) {
  if (m.none()) {
    return mask;
  }
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

std::string mirrorAlgText(std::string_view text, Mirror m) {
  if (m.none()) {
    return std::string(text);
  }
  static constexpr std::string_view kLetters = "URFDLBurfdlbMESxyzXYZ";
  auto isPrime = [](char c) { return c == '\''; };
  std::string out;
  std::size_t i = 0;
  while (i < text.size()) {
    if (text.substr(i).starts_with("//")) {
      break; // a comment
    }
    // An x straight after a bracket and before a count repeats the group
    // ("(R U R' U')x3"): it isn't a rotation.
    bool repeat = (text[i] == 'x' || text[i] == 'X') && i > 0 &&
                  (text[i - 1] == ')' || text[i - 1] == ']') &&
                  i + 1 < text.size() &&
                  std::isdigit(static_cast<unsigned char>(text[i + 1]));
    if (repeat || kLetters.find(text[i]) == std::string_view::npos) {
      out += text[i++];
      continue;
    }
    std::size_t end = i + 1;
    bool wideW = end < text.size() && text[end] == 'w';
    if (wideW) {
      ++end;
    }
    std::size_t amountAt = end;
    while (end < text.size() &&
           (std::isdigit(static_cast<unsigned char>(text[end])) ||
            isPrime(text[end]))) {
      ++end;
    }
    std::string_view token = text.substr(i, end - i);
    ParseResult parsed = parseAlg(token);
    if (parsed && parsed.alg.size() == 1) {
      Alg one = parsed.alg;
      if (m.leftRight) {
        one = mirrorM(one);
      }
      if (m.frontBack) {
        one = mirrorS(one);
      }
      // The mirrored layer, written as the token was (Rw stays Lw, X
      // stays upper case), with the count as written (R3 is L3', three
      // quarter turns) and the direction turned round where the mirror
      // turns it.
      std::string name(moveTypeName(one.front().type));
      if (wideW) {
        name = std::string(1, static_cast<char>(std::toupper(
                                  static_cast<unsigned char>(name.front())))) +
               "w";
      } else if (std::isupper(static_cast<unsigned char>(text[i]))) {
        name.front() = static_cast<char>(
            std::toupper(static_cast<unsigned char>(name.front())));
      }
      std::string amount(text.substr(amountAt, end - amountAt));
      if (one.front().amount != parsed.alg.front().amount) {
        if (auto p = amount.find('\''); p != std::string::npos) {
          amount.erase(p, 1);
        } else {
          amount += '\'';
        }
      }
      out += name + amount;
    } else {
      out += token;
    }
    i = end;
  }
  return out;
}

namespace {

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

CubieCube mirrorState(const CubieCube &c, Mirror m) {
  if (!m.leftRight && !m.frontBack) {
    return c;
  }
  const auto &to = faceletMirrors()[mirrorIndex(m)];
  FaceletCube f = c.toFacelets();
  FaceletCube out;
  for (int i = 0; i < kFaceletCount; ++i) {
    out[to[static_cast<std::size_t>(i)]] = mirroredColour(f[i], m);
  }
  return CubieCube::fromRealFacelets(out);
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
CaseFamilies::familyFor(const std::string &id) const {
  if (auto it = familyOf.find(id); it != familyOf.end()) {
    return &families[it->second];
  }
  auto it = familyOfId.find(id);
  return it == familyOfId.end() ? nullptr : &families[it->second];
}

std::string CaseFamilies::representative(const std::string &caseId) const {
  if (auto it = familyOf.find(caseId);
      it != familyOf.end() && it->second < ids.size()) {
    return ids[it->second];
  }
  return caseId;
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
      CubieCube mirroredState = mirrorState(a.c->state, m);
      PieceMask free = mirrorMask(a.free, m);
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
  std::vector<std::vector<int>> keys;
  for (std::size_t i = 0; i < cases.size(); ++i) {
    std::size_t root = uf.find(i);
    auto [it, added] = familyOfRoot.emplace(root, out.families.size());
    if (added) {
      out.families.emplace_back();
      keys.emplace_back();
    }
    out.families[it->second].push_back(cases[i].c->id);
    out.familyOf[cases[i].c->id] = it->second;
    // The family's key: the least of its cases' mirror images with any U
    // turns around them (whatever U turns the set allows, so that the key
    // doesn't change with them), which is the same for every case of it.
    const Entry &e = cases[i];
    std::vector<int> &best = keys[it->second];
    for (std::size_t k = 0; k <= kMirrors.size(); ++k) {
      Mirror m = k == 0 ? Mirror{} : kMirrors[k - 1];
      CubieCube mirroredState = mirrorState(e.c->state, m);
      PieceMask free = mirrorMask(e.free, m);
      for (int u = 0; u < 4; ++u) {
        for (int v = 0; v < 4; ++v) {
          CubieCube x = uTurn(v) * mirroredState * uTurn(u);
          std::vector<int> key = pieceKey(casePieces(x, free), x);
          if (best.empty() || key < best) {
            best = std::move(key);
          }
        }
      }
    }
  }
  std::map<std::string, int> used;
  for (std::size_t f = 0; f < keys.size(); ++f) {
    std::string id = "family:";
    for (std::size_t j = 0; j < keys[f].size(); ++j) {
      id += (j == 0 ? "" : ".") + std::to_string(keys[f][j]);
    }
    // Families that the set's U turns keep apart but that differ only by
    // U turns.
    if (int n = ++used[id]; n > 1) {
      id += "~" + std::to_string(n);
    }
    out.familyOfId[id] = f;
    out.ids.push_back(std::move(id));
  }
  return out;
}

bool adoptFamilyCards(const CaseFamilies &families, SetProgress &progress) {
  bool changed = false;
  for (std::size_t f = 0; f < families.families.size(); ++f) {
    if (f >= families.ids.size() || progress.contains(families.ids[f])) {
      continue;
    }
    CaseProgress *best = nullptr;
    for (const std::string &id : families.families[f]) {
      auto it = progress.find(id);
      if (it == progress.end() || it->second.card.state == CardState::New) {
        continue;
      }
      if (!best || it->second.card.reps > best->card.reps) {
        best = &it->second;
      }
    }
    if (!best) {
      continue;
    }
    CaseProgress card;
    card.card = best->card;
    for (const Attempt &a : best->attempts) {
      if (a.mode == AttemptMode::Learn) {
        card.attempts.push_back(a);
      }
    }
    resetLearning(*best);
    progress[families.ids[f]] = std::move(card);
    changed = true;
  }
  return changed;
}

} // namespace vcube
