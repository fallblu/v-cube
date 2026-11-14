#pragma once

#include "app/alglab_page.hpp"

#include "app/alglab_dialogs.hpp"
#include "app/app_settings.hpp"
#include "app/grip_view.hpp"
#include "app/lab_test_panel.hpp"
#include "app/ui_state.hpp"
#include "app/widgets.hpp"
#include "io/algset_json.hpp"
#include "train/ergonomics.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace vcube {

struct AlgLabPage::Job {
  std::atomic<bool> stop{false};
  int cases = 0;
  int found = 0;
  bool dirty = false; // candidates added since the last save
};

// What the files of AlgLabPage share (see alglab_page.cpp).
namespace {

enum Column {
  ColAlg,
  ColHtm,
  ColStm,
  ColQtm,
  ColEtm,
  ColScore,
  ColSource,
  ColCount,
  ColMean,
  ColMedian,
  ColBest,
  ColAo5,
  ColAo12,
  ColSd,
  ColDnf,
  ColumnCount
};

} // namespace

} // namespace vcube
