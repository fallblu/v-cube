#include "app/app_style.hpp"

#include <QAbstractSpinBox>
#include <QStyleFactory>
#include <QStyleOption>

#include <algorithm>

namespace vcube {

namespace {

// The room at a menu button's right for its arrow.
constexpr int kArrowSpace = 18;

// A tool button whose click opens its menu (not one split in two, whose
// menu half Fusion already draws).
bool opensMenu(const QStyleOptionToolButton &o) {
  return (o.features & QStyleOptionToolButton::HasMenu) &&
         !(o.features & QStyleOptionToolButton::MenuButtonPopup);
}

} // namespace

AppStyle::AppStyle() : QProxyStyle(QStyleFactory::create("Fusion")) {}

QSize AppStyle::sizeFromContents(ContentsType type, const QStyleOption *option,
                                 const QSize &size,
                                 const QWidget *widget) const {
  QSize s = QProxyStyle::sizeFromContents(type, option, size, widget);
  switch (type) {
  case CT_LineEdit:
    // The field inside a spin box: the spin box has the height.
    if (widget && qobject_cast<const QAbstractSpinBox *>(widget->parent())) {
      return s;
    }
    break;
  case CT_ToolButton:
    if (const auto *tb =
            qstyleoption_cast<const QStyleOptionToolButton *>(option)) {
      if (opensMenu(*tb)) {
        s.rwidth() += kArrowSpace;
      }
      if (tb->toolButtonStyle == Qt::ToolButtonIconOnly || tb->text.isEmpty()) {
        s.setWidth(std::max(s.width(), kMinControlHeight));
      }
    }
    break;
  case CT_PushButton:
  case CT_ComboBox:
  case CT_SpinBox:
    break;
  default:
    return s;
  }
  s.setHeight(std::max(s.height(), kMinControlHeight));
  return s;
}

void AppStyle::drawComplexControl(ComplexControl control,
                                  const QStyleOptionComplex *option,
                                  QPainter *painter,
                                  const QWidget *widget) const {
  const auto *tb = qstyleoption_cast<const QStyleOptionToolButton *>(option);
  if (control != CC_ToolButton || !tb || !opensMenu(*tb)) {
    QProxyStyle::drawComplexControl(control, option, painter, widget);
    return;
  }
  // The button without its label or corner mark, then the label clear of
  // the arrow, then the arrow.
  QStyleOptionToolButton panel = *tb;
  panel.features &= ~QStyleOptionToolButton::HasMenu;
  panel.text.clear();
  panel.icon = QIcon();
  panel.arrowType = Qt::NoArrow;
  QProxyStyle::drawComplexControl(control, &panel, painter, widget);

  const int frame = proxy()->pixelMetric(PM_DefaultFrameWidth, tb, widget);
  QStyleOptionToolButton label = *tb;
  label.features &= ~QStyleOptionToolButton::HasMenu;
  label.rect =
      tb->rect.adjusted(frame, frame, -frame - kArrowSpace + 4, -frame);
  proxy()->drawControl(CE_ToolButtonLabel, &label, painter, widget);

  QStyleOption arrow = *tb;
  arrow.rect = QRect(tb->rect.right() - kArrowSpace, tb->rect.top(),
                     kArrowSpace - 4, tb->rect.height());
  proxy()->drawPrimitive(PE_IndicatorArrowDown, &arrow, painter, widget);
}

} // namespace vcube
