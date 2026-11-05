#include "app/case_filter.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QTreeWidget>

namespace vcube {

CaseFilter::CaseFilter(QTreeWidget *tree, QWidget *parent)
    : QWidget(parent), tree_(tree) {
  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  search_ = new QLineEdit(this);
  search_->setPlaceholderText(tr("Search cases"));
  search_->setClearButtonEnabled(true);
  // Only a click gives it the keys, which the cube or timer needs.
  search_->setFocusPolicy(Qt::ClickFocus);
  search_->setToolTip(
      tr("Show the cases whose name or group contains this text. Enter "
         "gives the keys back; Esc clears the search."));
  search_->installEventFilter(this);
  status_ = new QComboBox(this);
  status_->setFocusPolicy(Qt::NoFocus);
  status_->setToolTip(tr("Show only the cases with this status"));
  status_->addItem(tr("All cases"));
  layout->addWidget(search_, 1);
  layout->addWidget(status_);
  connect(search_, &QLineEdit::textChanged, this, [this] {
    apply();
    emit changed();
  });
  connect(status_, &QComboBox::currentIndexChanged, this, [this] {
    apply();
    emit changed();
  });
}

void CaseFilter::setStatuses(std::vector<Status> statuses) {
  statuses_ = std::move(statuses);
  QSignalBlocker block(status_);
  status_->clear();
  status_->addItem(tr("All cases"));
  for (const Status &s : statuses_) {
    status_->addItem(s.name);
  }
}

bool CaseFilter::isActive() const {
  return !search_->text().trimmed().isEmpty() || status_->currentIndex() > 0;
}

void CaseFilter::release() { search_->clearFocus(); }

void CaseFilter::apply() {
  QString text = search_->text().trimmed();
  int choice = status_->currentIndex() - 1;
  const Test *test = choice >= 0 && choice < static_cast<int>(statuses_.size())
                         ? &statuses_[static_cast<std::size_t>(choice)].test
                         : nullptr;
  bool active = isActive();
  for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
    QTreeWidgetItem *group = tree_->topLevelItem(g);
    bool groupFound =
        text.isEmpty() || group->text(0).contains(text, Qt::CaseInsensitive);
    int shown = 0;
    for (int i = 0; i < group->childCount(); ++i) {
      QTreeWidgetItem *item = group->child(i);
      bool show =
          (groupFound || item->text(0).contains(text, Qt::CaseInsensitive)) &&
          (!test || (*test)(item));
      item->setHidden(!show);
      shown += show ? 1 : 0;
    }
    group->setHidden(active && shown == 0);
  }
}

bool CaseFilter::eventFilter(QObject *watched, QEvent *event) {
  if (watched == search_ && event->type() == QEvent::KeyPress) {
    int key = static_cast<QKeyEvent *>(event)->key();
    if (key == Qt::Key_Escape) {
      search_->clear();
      search_->clearFocus();
      return true;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
      search_->clearFocus();
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}

} // namespace vcube
