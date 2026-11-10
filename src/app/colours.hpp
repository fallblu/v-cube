#pragma once

#include "cube/facelet.hpp"
#include "cube/hold.hpp"
#include "cube/move.hpp"

#include <QBrush>
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

  friend bool operator==(const ColourScheme &, const ColourScheme &) = default;
};

// Which scheme colour each logical face shows. The logic always works in a
// fixed frame (U on top); holding the cube another way (yellow top, say) is
// just a different colour for each face. map[face] = colour slot.
using ColourMap = std::array<int, 6>;

inline constexpr ColourMap kIdentityColourMap{0, 1, 2, 3, 4, 5};

// The colours shown with the cube held as `h`.
inline ColourMap colourMapOf(Hold h) { return holdColours(h); }

// The colours of good, warning and bad results and timer states, in shades
// that read well on the current theme's background (lighter on the dark
// theme, darker on the light one).
enum class Status { Good, Warning, Bad };
QColor statusColour(Status s);

// A status colour that follows the theme wherever it is used as a colour,
// a brush or a name for rich text: `const StatusColour kGood{Status::Good}`.
struct StatusColour {
  Status status;
  operator QColor() const { return statusColour(status); }
  operator QBrush() const { return statusColour(status); }
  QString name() const { return statusColour(status).name(); }
};

// "White", "Red", ... for a colour slot.
QString colourName(int slot);

// Rich text such as "■ yellow top, ■ blue front", with the squares in the
// scheme's colours.
QString holdHtml(Hold h, const ColourScheme &scheme);
// A colour's square and name as rich text, e.g. "■ yellow".
QString colourHtml(int slot, const ColourScheme &scheme);
// A hold as an inspection step sees it: "■ yellow cross, ■ green front"
// (the cross on the bottom), or for EO alone, which only has an axis, "EO
// on ■ green / ■ blue".
QString stepHoldHtml(Hold h, bool eoOnly, const ColourScheme &scheme);

} // namespace vcube
