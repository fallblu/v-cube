#include "app/text_style.hpp"

#include "train/enum_ids.hpp"

#include <QApplication>
#include <QEvent>
#include <QFontInfo>
#include <QWidget>

#include <array>
#include <cmath>
#include <optional>

namespace vcube {

namespace {

const char *const kRoleProperty = "vcubeTextRole";
const char *const kBoldProperty = "vcubeTextBold";

// Caption, Body, Heading, Title, Display.
double roleScale(TextRole role) {
  return idOf(std::array{0.9, 1.0, 1.25, 1.5, 1.8}, role, 1.0);
}

// Only the size and the weight: the rest comes from the widget's parent.
QFont roleOnly(TextRole role, bool bold) {
  QFont f;
  scaleSize(f, QApplication::font(), roleScale(role));
  if (bold) {
    f.setBold(true);
  }
  return f;
}

std::optional<QFont> &systemFont() {
  static std::optional<QFont> font;
  return font;
}

} // namespace

void scaleSize(QFont &f, const QFont &base, double scale) {
  if (base.pointSizeF() > 0) {
    f.setPointSizeF(base.pointSizeF() * scale);
  } else if (base.pixelSize() > 0) {
    f.setPixelSize(static_cast<int>(std::lround(base.pixelSize() * scale)));
  }
}

void setTextRole(QWidget *w, TextRole role, bool bold) {
  w->setProperty(kRoleProperty, static_cast<int>(role) + 1);
  w->setProperty(kBoldProperty, bold);
  w->setFont(roleOnly(role, bold));
}

void setInterfaceTextPercent(int percent) {
  if (!systemFont()) {
    systemFont() = QApplication::font();
  }
  QFont f = *systemFont();
  scaleSize(f, *systemFont(), percent / 100.0);
  if (f == QApplication::font()) {
    return;
  }
  QApplication::setFont(f);
  // QApplication passes the change on only once exec() runs; the windows
  // are told here too, so that it happens before as well.
  for (QWidget *w : QApplication::topLevelWidgets()) {
    if (!w->testAttribute(Qt::WA_SetFont)) {
      QEvent change(QEvent::ApplicationFontChange);
      QApplication::sendEvent(w, &change);
    }
  }
  for (QWidget *w : QApplication::allWidgets()) {
    if (int role = w->property(kRoleProperty).toInt(); role > 0) {
      w->setFont(roleOnly(static_cast<TextRole>(role - 1),
                          w->property(kBoldProperty).toBool()));
    }
  }
}

QFont withTabularFigures(QFont f) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
  f.setFeature(QFont::Tag("tnum"), 1);
#endif
  return f;
}

void useTabularFigures(QWidget *w) {
  // Only the feature is set: the size and family come from the parent.
  w->setFont(withTabularFigures(QFont()));
}

int scaledToText(int pixels) {
  static const double scale = [] {
    const QFont now = QApplication::font();
    const QFont system = systemFont().value_or(now);
    if (system.pointSizeF() > 0 && now.pointSizeF() > 0) {
      return now.pointSizeF() / system.pointSizeF();
    }
    if (system.pixelSize() > 0 && now.pixelSize() > 0) {
      return static_cast<double>(now.pixelSize()) / system.pixelSize();
    }
    return 1.0;
  }();
  return static_cast<int>(std::lround(pixels * scale));
}

int interfaceTextPixels(int percent) {
  QFont f = systemFont().value_or(QApplication::font());
  scaleSize(f, f, percent / 100.0);
  return QFontInfo(f).pixelSize();
}

} // namespace vcube
