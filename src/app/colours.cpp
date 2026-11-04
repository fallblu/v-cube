#include "app/colours.hpp"

namespace vcube {

ColourScheme ColourScheme::standard() {
  ColourScheme s;
  s.faces = {QColor(0xf4, 0xf4, 0xf4), QColor(0xd6, 0x22, 0x2a),
             QColor(0x1f, 0xa8, 0x4a), QColor(0xff, 0xd5, 0x00),
             QColor(0xff, 0x80, 0x1a), QColor(0x1e, 0x5c, 0xd6)};
  return s;
}

namespace {

// The colour slots at each face's centre after `rotation` from WCA.
ColourMap mapAfter(const Alg &rotation) {
  FaceletCube c;
  c.apply(rotation);
  ColourMap m{};
  for (int f = 0; f < 6; ++f) {
    m[static_cast<std::size_t>(f)] = c[f * 9 + 4];
  }
  return m;
}

} // namespace

std::optional<ColourMap> colourMapFor(int topSlot, int frontSlot) {
  for (const Alg &r : allRotations()) {
    ColourMap m = mapAfter(r);
    if (m[0] == topSlot && m[2] == frontSlot) {
      return m;
    }
  }
  return std::nullopt;
}

Alg rotationFromWca(const ColourMap &map) {
  for (const Alg &r : allRotations()) {
    if (mapAfter(r) == map) {
      return r;
    }
  }
  return {};
}

QString orientationHint(const ColourMap &map, const ColourScheme &scheme) {
  if (map == kIdentityColourMap) {
    return {};
  }
  auto square = [&scheme](int slot) {
    return QString("<span style=\"color:%1\">&#9632;</span>")
        .arg(scheme.colourOfSlot(slot).name());
  };
  QString rotation = QString::fromStdString(toString(rotationFromWca(map)));
  return QString("Hold %1 top, %2 front (%3 from WCA)")
      .arg(square(map[0]), square(map[2]), rotation.toHtmlEscaped());
}

} // namespace vcube
