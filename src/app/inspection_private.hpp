#pragma once

#include "app/flow_layout.hpp"
#include "app/page_parts.hpp"
#include "app/session_panel.hpp"
#include "app/ui_state.hpp"
#include "io/session_store.hpp"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <array>
#include <string_view>

namespace vcube {

// What the files of InspectionPage share (see inspection_page.cpp).
namespace {

[[maybe_unused]] QString secondsText(std::int64_t ms) {
  return QString::number(static_cast<double>(ms) / 1000.0, 'f', 1) + " s";
}

// The two colours of an axis of a hold: 0 front-back, 1 right-left.
[[maybe_unused]] QString axisColours(Hold h, int axis) {
  std::array<int, 6> colours = holdColours(h);
  int a = colours[axis == 0 ? 2 : 1];
  int b = colours[axis == 0 ? 5 : 4];
  return QString("%1–%2").arg(colourName(a).toLower(), colourName(b).toLower());
}

[[maybe_unused]] const char *axisName(int axis) {
  return axis == 0 ? "F/B" : "R/L";
}

constexpr std::array<Slot, 4> kSlots4 = {Slot::FL, Slot::BL, Slot::FR,
                                         Slot::BR};

// "BL" -> "back-left".
[[maybe_unused]] QString slotWords(Slot s) {
  switch (s) {
  case Slot::FR:
    return QObject::tr("front-right");
  case Slot::FL:
    return QObject::tr("front-left");
  case Slot::BL:
    return QObject::tr("back-left");
  case Slot::BR:
    return QObject::tr("back-right");
  }
  return {};
}

const StatusColour kGood{Status::Good};
const StatusColour kBad{Status::Bad};
const StatusColour kCall{Status::Warning};
// Ring colours for the bad edges of the two axes.
const QColor kRing[2] = {QColor(0xff, 0x2d, 0xd4), QColor(0x2d, 0xd4, 0xff)};

} // namespace

} // namespace vcube
