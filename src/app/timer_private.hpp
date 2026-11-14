#pragma once

#include "app/page_parts.hpp"
#include "app/review_dialog.hpp"
#include "app/ui_state.hpp"
#include "io/data_file.hpp"
#include "train/step_solver.hpp"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace vcube {

// What the files of TimerPage share (see timer_page.cpp).
namespace {

[[maybe_unused]] QString statText(StatValue v, int decimals) {
  return QString::fromStdString(formatTime(v, decimals));
}

// The table's columns. Those after ao12 are for virtual sessions.
enum Column {
  kNumber,
  kTime,
  kAo5,
  kAo12,
  kMoves,
  kTps,
  kCross,
  kF2l,
  kLl,
  kColumns
};

} // namespace

} // namespace vcube
