#include "train/algset.hpp"

#include <algorithm>
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

std::vector<Mirror> AlgSet::worlds() const {
  std::vector<Mirror> out{Mirror{}};
  for (Mirror m : mirrors) {
    if (!m.none() && std::find(out.begin(), out.end(), m) == out.end()) {
      out.push_back(m);
    }
  }
  return out;
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

namespace {

struct Place {
  std::size_t group = 0;
  std::size_t index = 0;
};

std::optional<Place> placeOf(const AlgSet &set, std::string_view id) {
  for (std::size_t g = 0; g < set.groups.size(); ++g) {
    const auto &cases = set.groups[g].cases;
    for (std::size_t i = 0; i < cases.size(); ++i) {
      if (cases[i].id == id) {
        return Place{g, i};
      }
    }
  }
  return std::nullopt;
}

// The order of the cases, to tell whether anything moved.
std::vector<std::vector<std::string>> caseOrder(const AlgSet &set) {
  std::vector<std::vector<std::string>> out;
  for (const auto &g : set.groups) {
    auto &ids = out.emplace_back();
    for (const auto &c : g.cases) {
      ids.push_back(c.id);
    }
  }
  return out;
}

} // namespace

bool moveCases(AlgSet &set, const std::vector<std::string> &ids,
               std::size_t group, std::string_view before) {
  if (group >= set.groups.size()) {
    return false;
  }
  auto order = caseOrder(set);
  auto moving = [&ids](const AlgCase &c) {
    return std::find(ids.begin(), ids.end(), c.id) != ids.end();
  };
  // Insert before the first case from `before` on that stays.
  std::string anchor;
  if (!before.empty()) {
    const auto &cases = set.groups[group].cases;
    auto it = std::find_if(cases.begin(), cases.end(),
                           [&](const AlgCase &c) { return c.id == before; });
    for (; it != cases.end(); ++it) {
      if (!moving(*it)) {
        anchor = it->id;
        break;
      }
    }
  }
  std::vector<AlgCase> moved;
  for (auto &g : set.groups) {
    std::vector<AlgCase> kept;
    for (auto &c : g.cases) {
      (moving(c) ? moved : kept).push_back(std::move(c));
    }
    g.cases = std::move(kept);
  }
  auto &cases = set.groups[group].cases;
  auto at = std::find_if(cases.begin(), cases.end(),
                         [&](const AlgCase &c) { return c.id == anchor; });
  cases.insert(at, std::make_move_iterator(moved.begin()),
               std::make_move_iterator(moved.end()));
  return caseOrder(set) != order;
}

bool shiftCases(AlgSet &set, const std::vector<std::string> &ids,
                int direction) {
  auto order = caseOrder(set);
  // In set order, the first to move first.
  std::vector<std::string> todo;
  for (const auto &g : order) {
    for (const auto &id : g) {
      if (std::find(ids.begin(), ids.end(), id) != ids.end()) {
        todo.push_back(id);
      }
    }
  }
  if (direction > 0) {
    std::reverse(todo.begin(), todo.end());
  }
  std::vector<std::string> blocked;
  auto isBlocked = [&blocked](const std::string &id) {
    return std::find(blocked.begin(), blocked.end(), id) != blocked.end();
  };
  for (const std::string &id : todo) {
    Place p = *placeOf(set, id);
    auto &cases = set.groups[p.group].cases;
    if (direction < 0) {
      if (p.index > 0) {
        if (isBlocked(cases[p.index - 1].id)) {
          blocked.push_back(id);
        } else {
          std::swap(cases[p.index], cases[p.index - 1]);
        }
      } else if (p.group > 0) {
        AlgCase c = std::move(cases[p.index]);
        cases.erase(cases.begin());
        set.groups[p.group - 1].cases.push_back(std::move(c));
      } else {
        blocked.push_back(id);
      }
    } else {
      if (p.index + 1 < cases.size()) {
        if (isBlocked(cases[p.index + 1].id)) {
          blocked.push_back(id);
        } else {
          std::swap(cases[p.index], cases[p.index + 1]);
        }
      } else if (p.group + 1 < set.groups.size()) {
        AlgCase c = std::move(cases[p.index]);
        cases.pop_back();
        auto &next = set.groups[p.group + 1].cases;
        next.insert(next.begin(), std::move(c));
      } else {
        blocked.push_back(id);
      }
    }
  }
  return caseOrder(set) != order;
}

bool moveGroup(AlgSet &set, std::size_t from, std::size_t to) {
  if (from >= set.groups.size() || to >= set.groups.size() || from == to) {
    return false;
  }
  CaseGroup g = std::move(set.groups[from]);
  set.groups.erase(set.groups.begin() + static_cast<std::ptrdiff_t>(from));
  set.groups.insert(set.groups.begin() + static_cast<std::ptrdiff_t>(to),
                    std::move(g));
  return true;
}

std::string freeToString(const PieceMask &m) {
  // Greedily describe whole named groups first, then single positions.
  std::array<std::uint8_t, kCorners> cornerLeft{};
  std::array<std::uint8_t, kEdges> edgeLeft{};
  for (std::size_t i = 0; i < kCorners; ++i) {
    cornerLeft[i] =
        static_cast<std::uint8_t>((m.cp[i] ? 1 : 0) | (m.co[i] ? 2 : 0));
  }
  for (std::size_t i = 0; i < kEdges; ++i) {
    edgeLeft[i] =
        static_cast<std::uint8_t>((m.ep[i] ? 1 : 0) | (m.eo[i] ? 2 : 0));
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
    for (std::uint8_t flags :
         {std::uint8_t{3}, std::uint8_t{1}, std::uint8_t{2}}) {
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
      tokens.push_back(std::string(kCornerNames[i]) + ":" +
                       flagsText(cornerLeft[i]));
    }
  }
  for (std::size_t i = 0; i < kEdges; ++i) {
    if (edgeLeft[i]) {
      tokens.push_back(std::string(kEdgeNames[i]) + ":" +
                       flagsText(edgeLeft[i]));
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

std::optional<PieceMask> parseFree(std::string_view text, bool *widened) {
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
  PieceMask written = m;
  m.normalize();
  if (widened) {
    *widened = !(m == written);
  }
  return m;
}

std::string_view stickerModeName(StickerMode m) {
  switch (m) {
  case StickerMode::Full:
    return "full";
  case StickerMode::Orientation:
    return "orientation";
  case StickerMode::Hidden:
    return "hidden";
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

FaceletCube maskedFacelets(const CubieCube &state, const DiagramMask &mask,
                           const PieceMask &free) {
  FaceletCube f = state.toFacelets();
  // `shown`: the colour that tells a piece's orientation.
  auto applyMode = [&f](StickerMode mode, auto facelets, Face shown) {
    for (auto idx : facelets) {
      int i = static_cast<int>(idx);
      if (mode == StickerMode::Hidden ||
          (mode == StickerMode::Orientation &&
           f[i] != static_cast<std::uint8_t>(shown))) {
        f[i] = kMaskedColour;
      }
    }
  };
  // By position, a slot is a last-layer one; by piece, the piece in it
  // is a last-layer piece or one the step leaves free.
  for (std::size_t i = 0; i < kCorners; ++i) {
    std::size_t piece = state.cp[i];
    bool ll = mask.byPiece ? piece < 4 || free.cp[piece] : i < 4;
    // A D-layer corner the step leaves free is oriented by its D sticker,
    // as it is when it sits in the last layer.
    Face shown = mask.byPiece && piece >= 4 ? Face::D : Face::U;
    applyMode(ll ? mask.llCorners : mask.rest, kCornerFacelets[i], shown);
  }
  for (std::size_t i = 0; i < kEdges; ++i) {
    std::size_t piece = state.ep[i];
    bool ll = mask.byPiece ? piece < 4 || free.ep[piece] : i < 4;
    applyMode(ll ? mask.llEdges : mask.rest, kEdgeFacelets[i], Face::U);
  }
  return f;
}

} // namespace vcube
