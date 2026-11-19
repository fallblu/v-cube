#include "app/colours.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QPalette>

#include <algorithm>

namespace vcube {

ColourScheme ColourScheme::standard() {
  ColourScheme s;
  s.faces = {QColor(0xf4, 0xf4, 0xf4), QColor(0xd6, 0x22, 0x2a),
             QColor(0x1f, 0xa8, 0x4a), QColor(0xff, 0xd5, 0x00),
             QColor(0xff, 0x80, 0x1a), QColor(0x1e, 0x5c, 0xd6)};
  return s;
}

bool darkTheme() {
  return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
}

QColor statusColour(Status s) {
  bool dark = darkTheme();
  switch (s) {
  case Status::Good:
    return dark ? QColor(0x3d, 0xd6, 0x6b) : QColor(0x17, 0x7a, 0x33);
  case Status::Warning:
    return dark ? QColor(0xff, 0xb0, 0x30) : QColor(0x9a, 0x5b, 0x00);
  case Status::Bad:
    return dark ? QColor(0xff, 0x55, 0x55) : QColor(0xc6, 0x28, 0x28);
  }
  return {};
}

QColor roleColour(Role r) {
  bool dark = darkTheme();
  switch (r) {
  case Role::Fail:
    return dark ? QColor(0xff, 0x6b, 0x6b) : QColor(0xc6, 0x28, 0x28);
  case Role::Info:
  case Role::Series1:
    return dark ? QColor(0x5a, 0xa0, 0xff) : QColor(0x1f, 0x5f, 0xbf);
  case Role::Muted:
    return QGuiApplication::palette().color(QPalette::PlaceholderText);
  case Role::Series2:
    return dark ? QColor(0xff, 0x9a, 0x52) : QColor(0xb0, 0x4a, 0x00);
  case Role::Series3:
    return dark ? QColor(0x3d, 0xd6, 0x6b) : QColor(0x17, 0x7a, 0x33);
  // The badges carry white text, so they are dark enough for it whatever
  // the theme.
  case Role::GradeAgain:
    return QColor(0xc0, 0x39, 0x2b);
  case Role::GradeHard:
    return QColor(0xa8, 0x5d, 0x00);
  case Role::GradeGood:
    return QColor(0x1d, 0x7d, 0x3e);
  case Role::GradeEasy:
    return QColor(0x2b, 0x6c, 0xc4);
  case Role::GradeNone:
    return QColor(0x6b, 0x6d, 0x75);
  case Role::GradeWalked:
    return QColor(0x56, 0x6a, 0x85);
  case Role::OnBadge:
    return QColor(Qt::white);
  case Role::Banner:
    return QColor(0x8e, 0x1f, 0x1f);
  case Role::FieldEdge:
    return dark ? QColor(0x77, 0x78, 0x7f) : QColor(0x86, 0x87, 0x8d);
  }
  return {};
}

QColor labSeriesColour(std::size_t i) {
  static constexpr std::array<QRgb, 8> light = {0x2a6fd0, 0xc4501c, 0x127a55,
                                                0x9a6a00, 0xc2457a, 0x2f7d00,
                                                0x6250d6, 0xc23b3a};
  static constexpr std::array<QRgb, 8> dark = {0x5b9cf0, 0xec7a45, 0x2fbf8a,
                                               0xd9a400, 0xe77aa6, 0x5fb83a,
                                               0x9d93ef, 0xf07272};
  return QColor::fromRgb(darkTheme() ? dark[i % 8] : light[i % 8]);
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

QString colourHtml(int slot, const ColourScheme &scheme) {
  return QString("<span style=\"color:%1\">&#9632;</span> %2")
      .arg(scheme.colourOfSlot(slot).name(),
           colourName(slot).toLower().toHtmlEscaped());
}

QString stepHoldHtml(Hold h, bool eoOnly, const ColourScheme &scheme) {
  int bottom = (h.top + 3) % 6; // opposite colours are 3 apart
  int back = (h.front + 3) % 6;
  if (eoOnly) {
    return QCoreApplication::translate("vcube", "EO on %1 / %2")
        .arg(colourHtml(std::min(h.front, back), scheme),
             colourHtml(std::max(h.front, back), scheme));
  }
  return QCoreApplication::translate("vcube", "%1 cross, %2 front")
      .arg(colourHtml(bottom, scheme), colourHtml(h.front, scheme));
}

} // namespace vcube
