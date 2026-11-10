#include "cube/hold.hpp"

#include "cube/facelet.hpp"

#include <algorithm>

namespace vcube {

namespace {

// The colours at each face's centre after `rotation` from the WCA hold.
std::array<int, 6> centresAfter(const Alg &rotation) {
  FaceletCube c;
  c.apply(rotation);
  std::array<int, 6> out{};
  for (int f = 0; f < 6; ++f) {
    out[static_cast<std::size_t>(f)] = c[f * 9 + 4];
  }
  return out;
}

Alg rotations(std::initializer_list<Move> moves) { return Alg(moves); }

} // namespace

bool isValidHold(Hold h) {
  return h.top >= 0 && h.top < 6 && h.front >= 0 && h.front < 6 &&
         h.top != h.front && h.top % 3 != h.front % 3;
}

const std::array<Hold, 24> &allHolds() {
  static const std::array<Hold, 24> holds = [] {
    std::array<Hold, 24> out{};
    std::size_t n = 0;
    for (int t = 0; t < 6; ++t) {
      for (int f = 0; f < 6; ++f) {
        if (isValidHold({t, f})) {
          out[n++] = {t, f};
        }
      }
    }
    return out;
  }();
  return holds;
}

Alg holdRotation(Hold h) {
  for (const Alg &r : allRotations()) {
    std::array<int, 6> c = centresAfter(r);
    if (c[0] == h.top && c[2] == h.front) {
      return r;
    }
  }
  return {};
}

Hold holdAfter(const Alg &rotation) {
  std::array<int, 6> c = centresAfter(rotation);
  return {c[0], c[2]};
}

std::array<int, 6> holdColours(Hold h) { return centresAfter(holdRotation(h)); }

CubieCube heldState(const CubieCube &state, Hold h) {
  // Paint the solver's view in the colours it shows held as `h`, then turn
  // the cube back to the WCA hold.
  std::array<int, 6> colours = holdColours(h);
  FaceletCube f = state.toFacelets();
  for (int i = 0; i < kFaceletCount; ++i) {
    f[i] = static_cast<std::uint8_t>(colours[f[i]]);
  }
  f.apply(invert(holdRotation(h)));
  return CubieCube::fromRealFacelets(f);
}

CubieCube solverState(const CubieCube &scrambled, Hold h) {
  std::array<int, 6> colours = holdColours(h);
  std::array<std::uint8_t, 6> faceOf{};
  for (std::size_t face = 0; face < 6; ++face) {
    faceOf[static_cast<std::size_t>(colours[face])] =
        static_cast<std::uint8_t>(face);
  }
  FaceletCube f = scrambled.toFacelets();
  f.apply(holdRotation(h));
  for (int i = 0; i < kFaceletCount; ++i) {
    f[i] = faceOf[f[i]];
  }
  return CubieCube::fromRealFacelets(f);
}

std::optional<Hold> holdOf(const FaceletCube &cube) {
  Hold h{cube[4], cube[22]};
  if (!isValidHold(h)) {
    return std::nullopt;
  }
  return h;
}

std::optional<CubieCube> heldCubie(const FaceletCube &cube, Hold h) {
  if (!isValidHold(h)) {
    return std::nullopt;
  }
  for (const Alg &r : allRotations()) {
    FaceletCube held = cube;
    held.apply(r);
    if (held[4] != h.top || held[22] != h.front) {
      continue;
    }
    // Rename each sticker for the face whose centre has its colour.
    std::array<std::uint8_t, 6> faceOf{};
    std::array<bool, 6> seen{};
    for (int face = 0; face < 6; ++face) {
      std::uint8_t centre = held[face * 9 + 4];
      if (centre >= 6 || seen[centre]) {
        return std::nullopt;
      }
      seen[centre] = true;
      faceOf[centre] = static_cast<std::uint8_t>(face);
    }
    FaceletCube::Stickers s{};
    for (int i = 0; i < kFaceletCount; ++i) {
      if (held[i] >= 6) {
        return std::nullopt;
      }
      s[static_cast<std::size_t>(i)] = faceOf[held[i]];
    }
    return CubieCube::fromFacelets(FaceletCube(s));
  }
  return std::nullopt;
}

std::vector<Hold> neutralHolds(Hold main, Neutrality n) {
  if (!isValidHold(main)) {
    main = kWcaHold;
  }
  if (n == Neutrality::Full) {
    return {allHolds().begin(), allHolds().end()};
  }
  constexpr MoveType X = MoveType::X, Y = MoveType::Y;
  std::vector<Alg> extra = {{}};
  switch (n) {
  case Neutrality::Fixed:
  case Neutrality::Full:
    break;
  case Neutrality::Y2:
    extra.push_back(rotations({{Y, 2}}));
    break;
  case Neutrality::Y:
    extra.push_back(rotations({{Y, 1}}));
    extra.push_back(rotations({{Y, 2}}));
    extra.push_back(rotations({{Y, 3}}));
    break;
  case Neutrality::X2:
    extra.push_back(rotations({{X, 2}}));
    break;
  case Neutrality::X2Y2:
    extra.push_back(rotations({{Y, 2}}));
    extra.push_back(rotations({{X, 2}}));
    extra.push_back(rotations({{X, 2}, {Y, 2}}));
    break;
  case Neutrality::X2Y:
    for (int x = 0; x <= 2; x += 2) {
      for (int y = 0; y < 4; ++y) {
        Alg a;
        if (x) {
          a.push_back({X, 2});
        }
        if (y) {
          a.push_back({Y, static_cast<std::uint8_t>(y)});
        }
        if (!a.empty()) {
          extra.push_back(a);
        }
      }
    }
    break;
  }
  // From the WCA hold, turn to the main hold, then rotate further in the
  // solver's frame.
  Alg toMain = holdRotation(main);
  std::vector<Hold> out;
  for (const Alg &e : extra) {
    Alg a = toMain;
    a.insert(a.end(), e.begin(), e.end());
    out.push_back(holdAfter(a));
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

ColourNeutrality ColourNeutrality::preset(Hold main, Neutrality n) {
  ColourNeutrality c;
  c.main = isValidHold(main) ? main : kWcaHold;
  c.holds = neutralHolds(c.main, n);
  return c;
}

void ColourNeutrality::normalize() {
  if (!isValidHold(main)) {
    main = kWcaHold;
  }
  std::erase_if(holds, [](Hold h) { return !isValidHold(h); });
  std::sort(holds.begin(), holds.end());
  holds.erase(std::unique(holds.begin(), holds.end()), holds.end());
  if (holds.empty()) {
    holds.push_back(main);
  }
}

Hold ColourNeutrality::pick(std::mt19937_64 &rng) const {
  if (holds.empty()) {
    return main;
  }
  std::uniform_int_distribution<std::size_t> d(0, holds.size() - 1);
  return holds[d(rng)];
}

std::optional<Neutrality> ColourNeutrality::matchingPreset() const {
  for (int i = 0; i < kNeutralityCount; ++i) {
    auto n = static_cast<Neutrality>(i);
    if (neutralHolds(main, n) == holds) {
      return n;
    }
  }
  return std::nullopt;
}

} // namespace vcube
