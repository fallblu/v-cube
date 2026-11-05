#include "app/colours.hpp"

#include <QCoreApplication>

namespace vcube {

ColourScheme ColourScheme::standard() {
  ColourScheme s;
  s.faces = {QColor(0xf4, 0xf4, 0xf4), QColor(0xd6, 0x22, 0x2a),
             QColor(0x1f, 0xa8, 0x4a), QColor(0xff, 0xd5, 0x00),
             QColor(0xff, 0x80, 0x1a), QColor(0x1e, 0x5c, 0xd6)};
  return s;
}

QString colourName(int slot) {
  static const std::array<const char *, 6> names = {
      QT_TRANSLATE_NOOP("vcube", "White"),
      QT_TRANSLATE_NOOP("vcube", "Red"),
      QT_TRANSLATE_NOOP("vcube", "Green"),
      QT_TRANSLATE_NOOP("vcube", "Yellow"),
      QT_TRANSLATE_NOOP("vcube", "Orange"),
      QT_TRANSLATE_NOOP("vcube", "Blue")};
  if (slot < 0 || slot >= 6) {
    return {};
  }
  return QCoreApplication::translate("vcube",
                                     names[static_cast<std::size_t>(slot)]);
}

QString holdHtml(Hold h, const ColourScheme &scheme) {
  auto square = [&scheme](int slot) {
    return QString("<span style=\"color:%1\">&#9632;</span>")
        .arg(scheme.colourOfSlot(slot).name());
  };
  return QCoreApplication::translate("vcube", "%1 %2 top, %3 %4 front")
      .arg(square(h.top), colourName(h.top).toLower().toHtmlEscaped(),
           square(h.front), colourName(h.front).toLower().toHtmlEscaped());
}

} // namespace vcube
