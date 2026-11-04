#include "cube/facelet.hpp"

#include <map>
#include <stdexcept>

namespace vcube {

namespace {

constexpr std::string_view kFaceLetters = "URFDLB";

constexpr Vec3i kNormals[6] = {{0, 1, 0},  {1, 0, 0},  {0, 0, 1},
                               {0, -1, 0}, {-1, 0, 0}, {0, 0, -1}};

Vec3i rotate(Vec3i v, int axis, int sign) {
  // Right-handed rotation by sign * 90 degrees about +axis.
  switch (axis) {
  case 0:
    return sign > 0 ? Vec3i{v.x, -v.z, v.y} : Vec3i{v.x, v.z, -v.y};
  case 1:
    return sign > 0 ? Vec3i{v.z, v.y, -v.x} : Vec3i{-v.z, v.y, v.x};
  default:
    return sign > 0 ? Vec3i{-v.y, v.x, v.z} : Vec3i{v.y, -v.x, v.z};
  }
}

int coord(Vec3i v, int axis) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }

int findFacelet(Vec3i pos, Vec3i normal) {
  for (int i = 0; i < kFaceletCount; ++i) {
    if (faceletPosition(i) == pos && faceletNormal(i) == normal) {
      return i;
    }
  }
  throw std::logic_error("no facelet at rotated position");
}

using Perm = std::array<std::uint8_t, kFaceletCount>;

// perm[i] = where the sticker at i goes after one clockwise quarter turn.
const std::array<Perm, kMoveTypeCount> &quarterTurnPerms() {
  static const std::array<Perm, kMoveTypeCount> perms = [] {
    std::array<Perm, kMoveTypeCount> out{};
    for (int t = 0; t < kMoveTypeCount; ++t) {
      auto type = static_cast<MoveType>(t);
      int axis = moveAxis(type);
      int sign = moveRotationSign(type);
      for (int i = 0; i < kFaceletCount; ++i) {
        Vec3i p = faceletPosition(i);
        int dest = i;
        if (moveTurnsCubie(type, p)) {
          dest = findFacelet(rotate(p, axis, sign),
                             rotate(faceletNormal(i), axis, sign));
        }
        out[static_cast<std::size_t>(t)][static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>(dest);
      }
    }
    return out;
  }();
  return perms;
}

} // namespace

Vec3i faceletPosition(int index) {
  int face = index / 9;
  int r = (index % 9) / 3;
  int c = index % 3;
  switch (face) {
  case 0:
    return {c - 1, 1, r - 1}; // U
  case 1:
    return {1, 1 - r, 1 - c}; // R
  case 2:
    return {c - 1, 1 - r, 1}; // F
  case 3:
    return {c - 1, -1, 1 - r}; // D
  case 4:
    return {-1, 1 - r, c - 1}; // L
  default:
    return {1 - c, 1 - r, -1}; // B
  }
}

Vec3i faceletNormal(int index) { return kNormals[index / 9]; }

bool moveTurnsCubie(MoveType t, Vec3i pos) {
  using T = MoveType;
  int v = coord(pos, moveAxis(t));
  switch (t) {
  case T::U:
  case T::R:
  case T::F:
    return v == 1;
  case T::D:
  case T::L:
  case T::B:
    return v == -1;
  case T::Uw:
  case T::Rw:
  case T::Fw:
    return v >= 0;
  case T::Dw:
  case T::Lw:
  case T::Bw:
    return v <= 0;
  case T::M:
  case T::E:
  case T::S:
    return v == 0;
  case T::X:
  case T::Y:
  case T::Z:
    return true;
  }
  return false;
}

int moveRotationSign(MoveType t) {
  using T = MoveType;
  switch (t) {
  // Clockwise seen from the positive side is a negative rotation.
  case T::U:
  case T::R:
  case T::F:
  case T::Uw:
  case T::Rw:
  case T::Fw:
  case T::S:
  case T::X:
  case T::Y:
  case T::Z:
    return -1;
  // L, D, B (and the slices that follow them) turn as seen from the
  // negative side.
  default:
    return 1;
  }
}

FaceletCube::FaceletCube() {
  for (int i = 0; i < kFaceletCount; ++i) {
    s_[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i / 9);
  }
}

std::optional<FaceletCube> FaceletCube::fromString(std::string_view text) {
  if (text.size() != kFaceletCount) {
    return std::nullopt;
  }
  Stickers s{};
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '.') {
      s[i] = kMaskedColour;
      continue;
    }
    auto f = kFaceLetters.find(text[i]);
    if (f == std::string_view::npos) {
      return std::nullopt;
    }
    s[i] = static_cast<std::uint8_t>(f);
  }
  return FaceletCube(s);
}

std::string FaceletCube::toString() const {
  std::string out;
  out.reserve(kFaceletCount);
  for (auto c : s_) {
    out += c < 6 ? kFaceLetters[c] : '.';
  }
  return out;
}

void FaceletCube::apply(Move m) {
  const Perm &p = quarterTurnPerms()[static_cast<std::size_t>(m.type)];
  for (int n = 0; n < m.amount; ++n) {
    Stickers next{};
    for (std::size_t i = 0; i < kFaceletCount; ++i) {
      next[p[i]] = s_[i];
    }
    s_ = next;
  }
}

void FaceletCube::apply(const Alg &alg) {
  for (Move m : alg) {
    apply(m);
  }
}

bool FaceletCube::isSolved() const {
  for (std::size_t f = 0; f < 6; ++f) {
    for (std::size_t i = 0; i < 9; ++i) {
      if (s_[f * 9 + i] != s_[f * 9 + 4]) {
        return false;
      }
    }
  }
  return true;
}

bool FaceletCube::isStandardOrientation() const {
  for (std::size_t f = 0; f < 6; ++f) {
    if (s_[f * 9 + 4] != f) {
      return false;
    }
  }
  return true;
}

const std::array<Alg, 24> &allRotations() {
  static const std::array<Alg, 24> rotations = [] {
    // Breadth-first over x, y, z from the standard orientation, keyed by
    // where the centres end up.
    std::array<Alg, 24> out{};
    std::map<std::string, Alg> seen;
    std::vector<Alg> queue{{}};
    std::size_t n = 0;
    for (std::size_t head = 0; head < queue.size() && n < 24; ++head) {
      Alg a = queue[head];
      FaceletCube c;
      c.apply(a);
      std::string centres;
      for (int f = 0; f < 6; ++f) {
        centres += static_cast<char>('0' + c[f * 9 + 4]);
      }
      if (seen.contains(centres)) {
        continue;
      }
      seen.emplace(centres, a);
      out[n++] = a;
      for (MoveType t : {MoveType::X, MoveType::Y, MoveType::Z}) {
        Alg next = a;
        next.push_back({t, 1});
        queue.push_back(simplify(next));
      }
    }
    return out;
  }();
  return rotations;
}

Alg FaceletCube::normalizeOrientation() {
  if (isStandardOrientation()) {
    return {};
  }
  for (const Alg &r : allRotations()) {
    FaceletCube c = *this;
    c.apply(r);
    if (c.isStandardOrientation()) {
      *this = c;
      return r;
    }
  }
  return {};
}

} // namespace vcube
