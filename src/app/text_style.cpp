#include "app/text_style.hpp"

#include <QApplication>
#include <QEvent>
#include <QFontInfo>
#include <QWidget>

#include <cmath>
#include <optional>

namespace vcube {

namespace {

const char *const kRoleProperty = "vcubeTextRole";
const char *const kBoldProperty = "vcubeTextBold";

double roleScale(TextRole role) {
  switch (role) {
  case TextRole::Caption:
    return 0.9;
  case TextRole::Body:
    return 1.0;
  case TextRole::Heading:
    return 1.25;
  case TextRole::Title:
    return 1.5;
  case TextRole::Display:
    return 1.8;
  }
  return 1.0;
}

// `base` at `scale` times its size, in points or pixels as it is given.
void scaleSize(QFont &f, const QFont &base, double scale) {
  if (base.pointSizeF() > 0) {
    f.setPointSizeF(base.pointSizeF() * scale);
  } else if (base.pixelSize() > 0) {
    f.setPixelSize(static_cast<int>(std::lround(base.pixelSize() * scale)));
  }
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

int interfaceTextPixels(int percent) {
  QFont f = systemFont().value_or(QApplication::font());
  scaleSize(f, f, percent / 100.0);
  return QFontInfo(f).pixelSize();
}

} // namespace vcube
