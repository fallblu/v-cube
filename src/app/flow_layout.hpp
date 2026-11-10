#pragma once

#include <QLayout>

#include <vector>

namespace vcube {

// A row of controls that wraps onto further lines when it is too narrow,
// for the option rows above the pages: used like a QHBoxLayout (widgets,
// spacing, stretch), but its minimum width is that of its widest item, not
// of all of them together. A label is kept on the line of the item after
// it, and the room left on a line goes to its stretches and stretched
// widgets, so a row that fits on one line looks as a QHBoxLayout would.
class FlowLayout : public QLayout {
  Q_OBJECT
public:
  explicit FlowLayout(QWidget *parent = nullptr);
  ~FlowLayout() override;

  void addWidget(QWidget *w, int stretch = 0);
  void addSpacing(int size);
  void addStretch(int stretch = 1);

  void addItem(QLayoutItem *item) override;
  int count() const override;
  QLayoutItem *itemAt(int index) const override;
  QLayoutItem *takeAt(int index) override;
  Qt::Orientations expandingDirections() const override;
  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int width) const override;
  QSize minimumSize() const override;
  QSize sizeHint() const override;
  void setGeometry(const QRect &rect) override;

private:
  struct Entry {
    QLayoutItem *item;
    int stretch;
  };
  std::vector<Entry> entries_;

  // Lays the items out in `rect`, moving them only when `apply` is set;
  // returns the height used.
  int doLayout(const QRect &rect, bool apply) const;
  int spacingOr(int fallback) const;
};

} // namespace vcube
