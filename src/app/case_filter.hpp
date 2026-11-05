#pragma once

#include <QWidget>

#include <functional>
#include <vector>

class QComboBox;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace vcube {

// A search box and a status choice over a case list whose top-level items
// are groups and whose children are cases. Cases that don't match are
// hidden, and so are groups left without any. A group's name matches all
// its cases, so "BL" finds every case in a "BL" group. Enter in the search
// box gives the keys back, Esc clears it too.
class CaseFilter : public QWidget {
  Q_OBJECT
public:
  // Whether a case (a group's child item) has a status.
  using Test = std::function<bool(const QTreeWidgetItem *item)>;
  struct Status {
    QString name;
    Test test;
  };

  CaseFilter(QTreeWidget *tree, QWidget *parent);
  // The choices after "All cases".
  void setStatuses(std::vector<Status> statuses);
  // Hides what doesn't match; call again when the list or its rows change.
  void apply();
  // A search or a status is chosen.
  bool isActive() const;
  // Gives the keys back, e.g. when the page is left.
  void release();

signals:
  // The search or status changed (and was applied).
  void changed();

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  QTreeWidget *tree_;
  QLineEdit *search_;
  QComboBox *status_;
  std::vector<Status> statuses_;
};

} // namespace vcube
