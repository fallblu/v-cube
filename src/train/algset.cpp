#include "train/algset.hpp"

#include <array>
#include <sstream>

namespace vcube {

namespace {

constexpr std::array<std::string_view, kCorners> kCornerNames = {
    "URF", "UFL", "ULB", "UBR", "DFR", "DLF", "DBL", "DRB"};
constexpr std::array<std::string_view, kEdges> kEdgeNames = {
    "UR", "UF", "UL", "UB", "DR", "DF", "DL", "DB", "FR", "FL", "BL", "BR"};

struct Group {
  std::string_view name;
  std::vector<std::size_t> corners;
  std::vector<std::size_t> edges;
};

const std::vector<Group> &namedGroups() {
  static const std::vector<Group> groups = {
      {"ALL", {0, 1, 2, 3, 4, 5, 6, 7}, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
      {"LL", {0, 1, 2, 3}, {0, 1, 2, 3}},
      {"F2L", {4, 5, 6, 7}, {4, 5, 6, 7, 8, 9, 10, 11}},
      {"LLC", {0, 1, 2, 3}, {}},
      {"LLE", {}, {0, 1, 2, 3}},
  };
  return groups;
}

std::optional<Group> resolveGroup(std::string_view name) {
  for (const auto &g : namedGroups()) {
    if (g.name == name) {
      return g;
    }
  }
  for (std::size_t i = 0; i < kCorners; ++i) {
    if (kCornerNames[i] == name) {
      return Group{name, {i}, {}};
    }
  }
  for (std::size_t i = 0; i < kEdges; ++i) {
    if (kEdgeNames[i] == name) {
      return Group{name, {}, {i}};
    }
  }
  return std::nullopt;
}

} // namespace

int AlgSet::caseCount() const {
  int n = 0;
  for (const auto &g : groups) {
    n += static_cast<int>(g.cases.size());
  }
  return n;
}

AlgCase *AlgSet::findCase(std::string_view caseId) {
  for (auto &g : groups) {
    for (auto &c : g.cases) {
      if (c.id == caseId) {
        return &c;
      }
    }
  }
  return nullptr;
}

const AlgCase *AlgSet::findCase(std::string_view caseId) const {
  return const_cast<AlgSet *>(this)->findCase(caseId);
}

const CaseGroup *AlgSet::groupOf(std::string_view caseId) const {
  for (const auto &g : groups) {
    for (const auto &c : g.cases) {
      if (c.id == caseId) {
        return &g;
      }
    }
  }
  return nullptr;
}

std::string AlgSet::uniqueCaseId(std::string_view base) const {
  std::string b(base.empty() ? "case" : base);
  if (!findCase(b)) {
    return b;
  }
  for (int i = 2;; ++i) {
    std::string candidate = b + "-" + std::to_string(i);
    if (!findCase(candidate)) {
      return candidate;
    }
  }
}

std::string freeToString(const PieceMask &m) {
  // Greedily describe whole named groups first, then single positions.
  std::array<std::uint8_t, kCorners> cornerLeft{};
  std::array<std::uint8_t, kEdges> edgeLeft{};
  for (std::size_t i = 0; i < kCorners; ++i) {
    cornerLeft[i] = static_cast<std::uint8_t>((m.cp[i] ? 1 : 0) | (m.co[i] ? 2 : 0));
  }
  for (std::size_t i = 0; i < kEdges; ++i) {
    edgeLeft[i] = static_cast<std::uint8_t>((m.ep[i] ? 1 : 0) | (m.eo[i] ? 2 : 0));
  }
  std::vector<std::string> tokens;
  auto flagsText = [](std::uint8_t f) {
    std::string s;
    if (f & 1) {
      s += 'p';
    }
    if (f & 2) {
      s += 'o';
    }
    return s;
  };
  for (const auto &g : namedGroups()) {
    for (std::uint8_t flags : {std::uint8_t{3}, std::uint8_t{1}, std::uint8_t{2}}) {
      bool covers = !(g.corners.empty() && g.edges.empty());
      for (auto c : g.corners) {
        covers = covers && (cornerLeft[c] & flags) == flags;
      }
      for (auto e : g.edges) {
        covers = covers && (edgeLeft[e] & flags) == flags;
      }
      if (covers) {
        tokens.push_back(std::string(g.name) + ":" + flagsText(flags));
        for (auto c : g.corners) {
          cornerLeft[c] = static_cast<std::uint8_t>(cornerLeft[c] & ~flags);
        }
        for (auto e : g.edges) {
          edgeLeft[e] = static_cast<std::uint8_t>(edgeLeft[e] & ~flags);
        }
      }
    }
  }
  for (std::size_t i = 0; i < kCorners; ++i) {
    if (cornerLeft[i]) {
      tokens.push_back(std::string(kCornerNames[i]) + ":" + flagsText(cornerLeft[i]));
    }
  }
  for (std::size_t i = 0; i < kEdges; ++i) {
    if (edgeLeft[i]) {
      tokens.push_back(std::string(kEdgeNames[i]) + ":" + flagsText(edgeLeft[i]));
    }
  }
  std::string out;
  for (const auto &t : tokens) {
    if (!out.empty()) {
      out += ' ';
    }
    out += t;
  }
  return out;
}

std::optional<PieceMask> parseFree(std::string_view text) {
  PieceMask m;
  std::istringstream in{std::string(text)};
  std::string token;
  while (in >> token) {
    auto colon = token.find(':');
    if (colon == std::string::npos) {
      return std::nullopt;
    }
    auto group = resolveGroup(std::string_view(token).substr(0, colon));
    if (!group) {
      return std::nullopt;
    }
    bool perm = false, ori = false;
    for (char f : token.substr(colon + 1)) {
      if (f == 'p') {
        perm = true;
      } else if (f == 'o') {
        ori = true;
      } else {
        return std::nullopt;
      }
    }
    for (auto c : group->corners) {
      m.cp[c] = m.cp[c] || perm;
      m.co[c] = m.co[c] || ori;
    }
    for (auto e : group->edges) {
      m.ep[e] = m.ep[e] || perm;
      m.eo[e] = m.eo[e] || ori;
    }
  }
  return m;
}

std::string_view stickerModeName(StickerMode m) {
  switch (m) {
  case StickerMode::Full: return "full";
  case StickerMode::Orientation: return "orientation";
  case StickerMode::Hidden: return "hidden";
  }
  return "full";
}

std::optional<StickerMode> parseStickerMode(std::string_view s) {
  if (s == "full") {
    return StickerMode::Full;
  }
  if (s == "orientation") {
    return StickerMode::Orientation;
  }
  if (s == "hidden") {
    return StickerMode::Hidden;
  }
  return std::nullopt;
}

FaceletCube maskedFacelets(const CubieCube &state, const DiagramMask &mask) {
  FaceletCube f = state.toFacelets();
  auto applyMode = [&f](StickerMode mode, auto facelets) {
    for (auto idx : facelets) {
      int i = static_cast<int>(idx);
      if (mode == StickerMode::Hidden ||
          (mode == StickerMode::Orientation &&
           f[i] != static_cast<std::uint8_t>(Face::U))) {
        f[i] = kMaskedColour;
      }
    }
  };
  for (std::size_t i = 0; i < kCorners; ++i) {
    applyMode(i < 4 ? mask.llCorners : mask.rest, kCornerFacelets[i]);
  }
  for (std::size_t i = 0; i < kEdges; ++i) {
    applyMode(i < 4 ? mask.llEdges : mask.rest, kEdgeFacelets[i]);
  }
  return f;
}

} // namespace vcube
