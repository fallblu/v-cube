#include "app/case_list_panel.hpp"

#include "app/case_filter.hpp"
#include "app/ui_state.hpp"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace vcube {

CaseListPanel::CaseListPanel(QString mode, int thumbnailSize, QWidget *parent)
    : QWidget(parent), mode_(std::move(mode)) {
  layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  form = new QFormLayout;
  setBox = new QComboBox(this);
  setBox->setFocusPolicy(Qt::NoFocus);
  form->addRow(tr("Alg set:"), setBox);
  layout->addLayout(form);

  tree = new QTreeWidget(this);
  tree->setIconSize(QSize(thumbnailSize, thumbnailSize));
  tree->setIndentation(14);
  tree->setFocusPolicy(Qt::NoFocus);
  tree->setContextMenuPolicy(Qt::CustomContextMenu);
  tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
  tree->header()->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(tree->header(), &QHeaderView::customContextMenuRequested, this,
          &CaseListPanel::showHeaderMenu);
  connect(tree->header(), &QHeaderView::sectionClicked, this,
          &CaseListPanel::sortClicked);
  filter = new CaseFilter(tree, this);
  layout->addWidget(filter);
  layout->addWidget(tree, 1);

  tickRow = new QHBoxLayout;
  tickAllButton_ = new QPushButton(this);
  tickNoneButton_ = new QPushButton(this);
  for (auto *b : {tickAllButton_, tickNoneButton_}) {
    b->setFocusPolicy(Qt::NoFocus);
    tickRow->addWidget(b);
  }
  filterChanged();
  tickRow->addStretch();
  layout->addLayout(tickRow);
  summary = new QLabel(this);
  summary->setWordWrap(true);
  layout->addWidget(summary);

  connect(tickAllButton_, &QPushButton::clicked, this,
          [this] { tickShown(true); });
  connect(tickNoneButton_, &QPushButton::clicked, this,
          [this] { tickShown(false); });
  connect(filter, &CaseFilter::changed, this, &CaseListPanel::filterChanged);
}

void CaseListPanel::setColumns(const QStringList &headers,
                               const QStringList &defaultHidden) {
  defaultHidden_ = defaultHidden;
  QStringList all = {tr("Case")};
  all << headers;
  tree->setColumnCount(static_cast<int>(all.size()));
  tree->setHeaderLabels(all);
  tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  for (int c = 1; c < all.size(); ++c) {
    tree->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
  }
  applyHiddenColumns();
}

void CaseListPanel::setSortable(bool on) {
  sortable_ = on;
  if (!on) {
    return;
  }
  tree->header()->setSectionsClickable(true);
  sortColumn_ = saved::sortColumn(mode_).get();
  if (sortColumn_ <= 0 || sortColumn_ >= tree->columnCount()) {
    sortColumn_ = -1;
  }
  sortOrder_ = saved::sortAscending(mode_).get() ? Qt::AscendingOrder
                                                 : Qt::DescendingOrder;
}

void CaseListPanel::tickShown(bool on) {
  Qt::CheckState state = on ? Qt::Checked : Qt::Unchecked;
  for (int g = 0; g < tree->topLevelItemCount(); ++g) {
    QTreeWidgetItem *group = tree->topLevelItem(g);
    if (!filter->isActive()) {
      group->setCheckState(0, state); // its cases follow
      continue;
    }
    for (int i = 0; i < group->childCount(); ++i) {
      if (!group->child(i)->isHidden()) {
        group->child(i)->setCheckState(0, state);
      }
    }
  }
}

void CaseListPanel::filterChanged() {
  bool filtered = filter->isActive();
  tickAllButton_->setText(filtered ? tr("Tick shown") : tr("Tick all"));
  tickNoneButton_->setText(filtered ? tr("Untick shown") : tr("Tick none"));
  tickAllButton_->setToolTip(filtered ? tr("Tick the cases shown")
                                      : tr("Tick every case"));
  tickNoneButton_->setToolTip(filtered ? tr("Untick the cases shown")
                                       : tr("Untick every case"));
}

void CaseListPanel::showHeaderMenu(const QPoint &pos) {
  QMenu menu(this);
  for (int c = 1; c < tree->columnCount(); ++c) {
    QAction *a = menu.addAction(tree->headerItem()->text(c));
    a->setCheckable(true);
    a->setChecked(!tree->isColumnHidden(c));
    connect(a, &QAction::toggled, this, [this, c](bool on) {
      tree->setColumnHidden(c, !on);
      QStringList hidden;
      for (int k = 1; k < tree->columnCount(); ++k) {
        if (tree->isColumnHidden(k)) {
          hidden << tree->headerItem()->text(k);
        }
      }
      saved::hiddenColumns(mode_, defaultHidden_).set(hidden);
    });
  }
  menu.exec(tree->header()->viewport()->mapToGlobal(pos));
}

void CaseListPanel::applyHiddenColumns() {
  QStringList hidden = saved::hiddenColumns(mode_, defaultHidden_).get();
  for (int c = 1; c < tree->columnCount(); ++c) {
    tree->setColumnHidden(c, hidden.contains(tree->headerItem()->text(c)));
  }
}

void CaseListPanel::sortClicked(int column) {
  if (!sortable_) {
    return;
  }
  if (column <= 0) {
    sortColumn_ = -1;
  } else if (column != sortColumn_) {
    sortColumn_ = column;
    sortOrder_ = Qt::DescendingOrder;
  } else if (sortOrder_ == Qt::DescendingOrder) {
    sortOrder_ = Qt::AscendingOrder;
  } else {
    sortColumn_ = -1;
  }
  saved::sortColumn(mode_).set(sortColumn_);
  saved::sortAscending(mode_).set(sortOrder_ == Qt::AscendingOrder);
  applySort();
  emit sorted();
}

void CaseListPanel::applySort() {
  if (!sortable_) {
    return;
  }
  QSignalBlocker block(tree);
  if (sortColumn_ < 0) {
    tree->sortItems(0, Qt::AscendingOrder); // list order
    tree->header()->setSortIndicatorShown(false);
  } else {
    tree->sortItems(sortColumn_, sortOrder_);
    tree->header()->setSortIndicatorShown(true);
  }
}

} // namespace vcube
