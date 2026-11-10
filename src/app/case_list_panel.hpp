#pragma once

#include <QString>
#include <QStringList>
#include <QWidget>

class QComboBox;
class QFormLayout;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QTreeWidget;
class QVBoxLayout;

namespace vcube {

class CaseFilter;

// The left side of Learn and Drill: the alg set selector (and the page's
// own choices below it), the search and status filter, the case list,
// "Tick all" and "Tick none" (which tick only the cases shown while the
// list is filtered) and a summary. The columns after "Case" can be hidden
// from the header's menu and, when the page allows it, sorted by a click
// on their header; both are remembered per mode.
class CaseListPanel : public QWidget {
  Q_OBJECT
public:
  // `mode` names the page in settings keys ("learn", "drill").
  CaseListPanel(QString mode, int thumbnailSize, QWidget *parent = nullptr);

  QVBoxLayout *layout;
  QFormLayout *form; // the set selector; pages add rows
  QComboBox *setBox;
  CaseFilter *filter;
  QTreeWidget *tree;
  QHBoxLayout *tickRow; // tick all/none; pages add buttons
  QLabel *summary;

  // The list's columns: "Case" and `headers` after it, of which
  // `defaultHidden` are hidden until the header's menu shows them.
  void setColumns(const QStringList &headers, const QStringList &defaultHidden);

  // A click on a column's header sorts by it (largest first, then
  // smallest first, then in list order again, as a click on "Case" does).
  void setSortable(bool on);
  bool sortable() const { return sortable_; }
  // Sorts the list again as chosen, e.g. after its values changed.
  void applySort();

signals:
  // The list was sorted another way from its header.
  void sorted();

private:
  QString mode_;
  QStringList defaultHidden_;
  QPushButton *tickAllButton_;
  QPushButton *tickNoneButton_;
  bool sortable_ = false;
  int sortColumn_ = -1; // -1: list order
  Qt::SortOrder sortOrder_ = Qt::DescendingOrder;

  // Ticks or unticks every case shown.
  void tickShown(bool on);
  // "Tick all" becomes "Tick shown" while the list is filtered.
  void filterChanged();
  void showHeaderMenu(const QPoint &pos);
  void applyHiddenColumns();
  void sortClicked(int column);
};

} // namespace vcube
