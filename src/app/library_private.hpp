#pragma once

#include "app/alglab_common.hpp"
#include "app/case_filter.hpp"
#include "app/case_history_dialog.hpp"
#include "app/cube_painter.hpp"
#include "app/drill_history_dialog.hpp"
#include "app/grip_view.hpp"
#include "app/mode_table.hpp"
#include "app/paste_dialog.hpp"
#include "app/set_properties_dialog.hpp"
#include "app/ui_state.hpp"
#include "app/ui_text.hpp"
#include "train/reduction.hpp"
#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDropEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QSplitter>
#include <QTabWidget>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace vcube {

// What the files of LibraryPage share (see library_page.cpp).
namespace {

constexpr int kCaseRole = Qt::UserRole;
constexpr int kGroupRole = Qt::UserRole + 1;

constexpr int kDiagramSize = 160;

} // namespace

} // namespace vcube
