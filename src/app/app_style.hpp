#pragma once

#include <QProxyStyle>

namespace vcube {

// The app's style: Fusion, with two changes made in one place so that
// every page has them.
// - Buttons, drop-downs, spin boxes and text fields are at least
//   kMinControlHeight high (a comfortable target for the mouse), and
//   icon-only tool buttons as wide.
// - A tool button that opens its menu on a click ("Session: …", "…")
//   shows a drop-down arrow at its right, as a combo box does, rather than
//   Fusion's small mark in the corner.
class AppStyle : public QProxyStyle {
  Q_OBJECT
public:
  static constexpr int kMinControlHeight = 28;

  AppStyle();

  QSize sizeFromContents(ContentsType type, const QStyleOption *option,
                         const QSize &size,
                         const QWidget *widget) const override;
  void drawComplexControl(ComplexControl control,
                          const QStyleOptionComplex *option, QPainter *painter,
                          const QWidget *widget) const override;
};

} // namespace vcube
