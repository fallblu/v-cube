#pragma once

#include "cube/facelet.hpp"
#include "cube/move.hpp"

#include <QColor>
#include <QString>

#include <array>
#include <optional>

namespace vcube {

// The six sticker colours, by the face they belong to in the WCA
// orientation (white top, green front): U R F D L B.
struct ColourScheme {
  std::array<QColor, 6> faces;
  QColor masked{0x55, 0x55, 0x5a};
  QColor body{0x12, 0x12, 0x14};

  static ColourScheme standard();
  QColor colourOfSlot(int slot) const {
    return slot >= 0 && slot < 6 ? faces[static_cast<std::size_t>(slot)]
                                 : masked;
  }
};

// Which scheme colour each logical face shows. The logic always works in a
// fixed frame (U on top); holding the cube another way (yellow top, say) is
// just a different colour for each face. map[face] = colour slot.
using ColourMap = std::array<int, 6>;

inline constexpr ColourMap kIdentityColourMap{0, 1, 2, 3, 4, 5};

// Colour map for holding the cube with `topSlot` on top and `frontSlot` in
// front, or nullopt if those are the same or opposite colours.
std::optional<ColourMap> colourMapFor(int topSlot, int frontSlot);

// The rotation from the WCA orientation to the one with `map`, e.g. "z2".
Alg rotationFromWca(const ColourMap &map);

// Rich-text hint such as "Hold ■ top, ■ front (z2 from WCA)" with coloured
// squares; empty for the WCA orientation.
QString orientationHint(const ColourMap &map, const ColourScheme &scheme);

} // namespace vcube
