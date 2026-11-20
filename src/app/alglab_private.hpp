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

#include <algorithm>
#include <vector>

namespace vcube {

struct AlgLabPage::Job {
  std::atomic<bool> stop{false};
  int found = 0;
  bool dirty = false; // candidates added since the last save
  // Lab cases deleted or edited since the search began: what it still
  // finds for them belongs to a case that is gone or changed.
  std::vector<LabCaseKey> dropped;
};

// What the files of AlgLabPage share (see alglab_page.cpp).
namespace {

enum Column : int {
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
