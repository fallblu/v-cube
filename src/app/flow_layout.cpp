#include "app/flow_layout.hpp"

#include <QLabel>
#include <QStyle>
#include <QWidget>

#include <algorithm>

namespace vcube {

FlowLayout::FlowLayout(QWidget *parent) : QLayout(parent) {}

FlowLayout::~FlowLayout() {
  while (QLayoutItem *item = takeAt(0)) {
    delete item;
  }
}

void FlowLayout::addWidget(QWidget *w, int stretch) {
  addChildWidget(w);
  entries_.push_back({new QWidgetItem(w), stretch});
  invalidate();
}

void FlowLayout::addSpacing(int size) {
  entries_.push_back(
      {new QSpacerItem(size, 0, QSizePolicy::Fixed, QSizePolicy::Minimum), 0});
  invalidate();
}

void FlowLayout::addStretch(int stretch) {
  entries_.push_back(
      {new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum),
       std::max(1, stretch)});
  invalidate();
}

void FlowLayout::addItem(QLayoutItem *item) {
  entries_.push_back({item, 0});
  invalidate();
}

int FlowLayout::count() const { return static_cast<int>(entries_.size()); }

QLayoutItem *FlowLayout::itemAt(int index) const {
  return index >= 0 && index < count()
             ? entries_[static_cast<std::size_t>(index)].item
             : nullptr;
}

QLayoutItem *FlowLayout::takeAt(int index) {
  if (index < 0 || index >= count()) {
    return nullptr;
  }
  QLayoutItem *item = entries_[static_cast<std::size_t>(index)].item;
  entries_.erase(entries_.begin() + index);
  invalidate();
  return item;
}

Qt::Orientations FlowLayout::expandingDirections() const {
  return Qt::Horizontal;
}

int FlowLayout::spacingOr(int fallback) const {
  if (spacing() >= 0) {
    return spacing();
  }
  if (QWidget *w = parentWidget()) {
    int s =
        w->style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing, nullptr, w);
    if (s >= 0) {
      return s;
    }
  }
  return fallback;
}

namespace {

bool isLabel(const QLayoutItem *item) {
  auto *label = qobject_cast<QLabel *>(item->widget());
  return label && !label->wordWrap();
}

} // namespace

int FlowLayout::doLayout(const QRect &rect, Pass pass) const {
  QMargins m = contentsMargins();
  QRect area = rect.marginsRemoved(m);
  int gap = spacingOr(6);

  // Units that stay on one line: a label and the item after it, or one
  // item on its own. Hidden widgets take no room.
  struct Unit {
    std::vector<std::size_t> items;
    int width = 0;
    bool spacer = false;
  };
  std::vector<Unit> units;
  bool joinNext = false;
  for (std::size_t i = 0; i < entries_.size(); ++i) {
    QLayoutItem *item = entries_[i].item;
    if (item->isEmpty() && !item->spacerItem()) {
      continue;
    }
    int w = item->sizeHint().width();
    if (joinNext && !units.empty() && !item->spacerItem()) {
      Unit &u = units.back();
      u.items.push_back(i);
      u.width += gap + w;
    } else {
      units.push_back({{i}, w, item->spacerItem() != nullptr});
    }
    joinNext = isLabel(item);
  }

  // Break the units into lines.
  std::vector<std::vector<const Unit *>> lines(1);
  int x = 0;
  for (const Unit &u : units) {
    auto &line = lines.back();
    if (u.spacer && line.empty()) {
      continue; // no spacing or stretch at the start of a line
    }
    int needed = line.empty() ? u.width : x + gap + u.width;
    if (!line.empty() && needed > area.width() && !u.spacer) {
      // Spacers at the end of the line it leaves go too.
      while (!line.empty() && line.back()->spacer) {
        line.pop_back();
      }
      lines.emplace_back();
      lines.back().push_back(&u);
      x = u.width;
      continue;
    }
    line.push_back(&u);
    x = needed;
  }

  int y = area.top();
  int vgap = gap;
  bool first = true;
  for (const auto &line : lines) {
    if (line.empty()) {
      continue;
    }
    // Widths, then the room left over for stretches.
    std::vector<std::pair<std::size_t, int>> placed; // entry, width
    int used = 0;
    int height = 0;
    int stretchTotal = 0;
    for (const Unit *u : line) {
      for (std::size_t i : u->items) {
        QLayoutItem *item = entries_[i].item;
        int w = item->sizeHint().width();
        placed.emplace_back(i, w);
        used += w;
        if (!item->spacerItem()) {
          height = std::max(height, item->sizeHint().height());
        }
        stretchTotal += entries_[i].stretch;
      }
    }
    used += gap * static_cast<int>(placed.size() - 1);
    int left = area.width() - used;
    if (left > 0 && stretchTotal > 0) {
      for (auto &[i, w] : placed) {
        int s = entries_[i].stretch;
        if (s == 0) {
          continue;
        }
        int extra = left * s / stretchTotal;
        int grown =
            std::min(w + extra, entries_[i].item->maximumSize().width());
        w = grown;
      }
    } else if (left < 0) {
      // Too narrow for even this line: shrink what can shrink.
      for (auto &[i, w] : placed) {
        int min = entries_[i].item->minimumSize().width();
        int cut = std::min(-left, w - min);
        if (cut > 0) {
          w -= cut;
          left += cut;
        }
      }
    }
    if (!first) {
      y += vgap;
    }
    first = false;
    if (pass == Pass::Apply) {
      int px = area.left();
      for (auto &[i, w] : placed) {
        QLayoutItem *item = entries_[i].item;
        if (!item->spacerItem()) {
          int h = std::min(height, std::max(item->sizeHint().height(),
                                            item->minimumSize().height()));
          item->setGeometry(QRect(px, y + (height - h) / 2, w, h));
        }
        px += w + gap;
      }
    }
    y += height;
  }
  return y - area.top() + m.top() + m.bottom();
}

int FlowLayout::heightForWidth(int width) const {
  return doLayout(QRect(0, 0, width, 0), Pass::Measure);
}

QSize FlowLayout::minimumSize() const {
  // The widest label-and-control pair, or item, at its minimum.
  QSize size;
  int gap = spacingOr(6);
  int pending = 0; // a label's width waiting for its control
  for (const Entry &e : entries_) {
    if (e.item->isEmpty()) {
      continue;
    }
    int w = e.item->minimumSize().width();
    if (pending > 0) {
      w += pending + gap;
      pending = 0;
    }
    if (isLabel(e.item)) {
      pending = w;
      continue;
    }
    size = size.expandedTo(QSize(w, e.item->minimumSize().height()));
  }
  size = size.expandedTo(QSize(pending, 0));
  QMargins m = contentsMargins();
  return size + QSize(m.left() + m.right(), m.top() + m.bottom());
}

QSize FlowLayout::sizeHint() const {
  int gap = spacingOr(6);
  int width = 0;
  int height = 0;
  int n = 0;
  for (const Entry &e : entries_) {
    if (e.item->isEmpty() && !e.item->spacerItem()) {
      continue;
    }
    width += e.item->sizeHint().width();
    height = std::max(height, e.item->sizeHint().height());
    ++n;
  }
  width += gap * std::max(0, n - 1);
  QMargins m = contentsMargins();
  return {width + m.left() + m.right(), height + m.top() + m.bottom()};
}

void FlowLayout::setGeometry(const QRect &rect) {
  QLayout::setGeometry(rect);
  doLayout(rect, Pass::Apply);
}

} // namespace vcube
