#include "app/command_palette.hpp"

#include "app/ui_state.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace vcube {

namespace {

constexpr int kCommandRole = Qt::UserRole;
constexpr int kShortcutRole = Qt::UserRole + 1;

// A command with its keys on the right of the row; a heading in bold
// muted text.
class CommandDelegate : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter *p, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override {
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    if (!(index.flags() & Qt::ItemIsSelectable)) {
      opt.font.setBold(true);
      opt.palette.setColor(QPalette::Text,
                           opt.palette.color(QPalette::PlaceholderText));
    }
    const QString keys = index.data(kShortcutRole).toString();
    const QWidget *w = opt.widget;
    QStyle *style = w ? w->style() : QApplication::style();
    if (keys.isEmpty()) {
      style->drawControl(QStyle::CE_ItemViewItem, &opt, p, w);
      return;
    }
    // The row without its text, then the name and the keys.
    const QString text = opt.text;
    opt.text.clear();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, p, w);
    QRect r = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, w)
                  .adjusted(2, 0, -6, 0);
    const bool selected = opt.state & QStyle::State_Selected;
    const int keysWidth = opt.fontMetrics.horizontalAdvance(keys) + 16;
    p->save();
    p->setFont(opt.font);
    p->setPen(opt.palette.color(selected ? QPalette::HighlightedText
                                         : QPalette::Text));
    QRect name = r.adjusted(0, 0, -keysWidth, 0);
    p->drawText(name, Qt::AlignLeft | Qt::AlignVCenter,
                opt.fontMetrics.elidedText(text, Qt::ElideRight, name.width()));
    if (!selected) {
      p->setPen(opt.palette.color(QPalette::PlaceholderText));
    }
    p->drawText(r, Qt::AlignRight | Qt::AlignVCenter, keys);
    p->restore();
  }
};

// A command's text without the state a checkable one or a choice shows,
// so that it is remembered as the same command whatever its state.
QString baseText(QString text) {
  for (const QString &suffix :
       {CommandPalette::tr(" (on)"), CommandPalette::tr(" (off)"),
        CommandPalette::tr(" (current)")}) {
    if (text.endsWith(suffix)) {
      text.chop(suffix.size());
      break;
    }
  }
  return text;
}

QString plain(QString text) {
  text.remove('&');
  text.replace(QStringLiteral("…"), QString());
  return text.trimmed();
}

// The label just before `w` in its parent's layout, e.g. "Scramble:" for
// the scramble type's drop-down, without the colon.
QString labelFor(QWidget *w) {
  QWidget *parent = w->parentWidget();
  if (!parent) {
    return {};
  }
  for (QLabel *label :
       parent->findChildren<QLabel *>(QString(), Qt::FindDirectChildrenOnly)) {
    if (label->buddy() == w) {
      return plain(label->text()).remove(':');
    }
  }
  QLayout *layout = parent->layout();
  if (!layout) {
    return {};
  }
  // The layout may be nested: search it and its sub-layouts.
  std::function<QString(QLayout *)> search = [&](QLayout *l) -> QString {
    for (int i = 0; i < l->count(); ++i) {
      QLayoutItem *item = l->itemAt(i);
      if (item->widget() == w) {
        for (int j = i - 1; j >= 0; --j) {
          QWidget *before = l->itemAt(j)->widget();
          if (!before) {
            continue; // spacing
          }
          if (auto *label = qobject_cast<QLabel *>(before)) {
            return plain(label->text()).remove(':');
          }
          return {};
        }
        return {};
      }
      if (QLayout *sub = item->layout()) {
        if (QString found = search(sub); !found.isEmpty()) {
          return found;
        }
      }
    }
    return {};
  };
  return search(layout);
}

QString firstSentence(const QString &text) {
  QString t = plain(text);
  auto stop = t.indexOf(QRegularExpression("[.:(]"));
  return (stop > 0 ? t.left(stop) : t).trimmed();
}

bool usable(QWidget *w, QWidget *page) {
  return w->isVisibleTo(page) && w->isEnabled() &&
         w->window() == page->window();
}

} // namespace

CommandPalette::CommandPalette(std::vector<Command> commands, QWidget *parent)
    : QDialog(parent, Qt::Popup | Qt::FramelessWindowHint),
      commands_(std::move(commands)) {
  setObjectName("commandPalette");
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(8, 8, 8, 8);
  search_ = new QLineEdit(this);
  search_->setPlaceholderText(tr("Type a command, a mode or a choice"));
  search_->installEventFilter(this);
  list_ = new QListWidget(this);
  list_->setFocusPolicy(Qt::NoFocus);
  list_->setUniformItemSizes(true);
  list_->setItemDelegate(new CommandDelegate(list_));
  list_->setAccessibleName(tr("Commands"));
  search_->setAccessibleName(tr("Search commands"));
  layout->addWidget(search_);
  layout->addWidget(list_);
  connect(search_, &QLineEdit::textChanged, this, &CommandPalette::filter);
  connect(list_, &QListWidget::itemActivated, this,
          &CommandPalette::runSelected);
  connect(list_, &QListWidget::itemClicked, this, &CommandPalette::runSelected);
  filter();
  if (parent) {
    int w = std::min(560, parent->width() - 40);
    int h = std::min(420, parent->height() - 80);
    resize(w, h);
    QPoint top = parent->mapToGlobal(QPoint((parent->width() - w) / 2, 40));
    move(top);
  }
  search_->setFocus();
}

void CommandPalette::filter() {
  QStringList terms = search_->text().toLower().split(' ', Qt::SkipEmptyParts);
  list_->clear();
  // The groups in the order they first come, each with its commands.
  QStringList groups;
  std::vector<std::vector<std::size_t>> members;
  auto addTo = [&](const QString &group, std::size_t i) {
    qsizetype g = groups.indexOf(group);
    if (g < 0) {
      groups << group;
      members.emplace_back();
      g = groups.size() - 1;
    }
    members[static_cast<std::size_t>(g)].push_back(i);
  };
  if (terms.isEmpty()) {
    // The commands last run that can be run now, newest first.
    for (const QString &recent : saved::recentCommands().get()) {
      for (std::size_t i = 0; i < commands_.size(); ++i) {
        if (baseText(commands_[i].text) == recent) {
          addTo(tr("Recent"), i);
          break;
        }
      }
    }
  }
  for (std::size_t i = 0; i < commands_.size(); ++i) {
    const Command &c = commands_[i];
    QString hay = c.text.toLower();
    bool match = std::all_of(terms.begin(), terms.end(),
                             [&](const QString &t) { return hay.contains(t); });
    if (match) {
      addTo(c.group, i);
    }
  }
  for (qsizetype g = 0; g < groups.size(); ++g) {
    if (!groups[g].isEmpty()) {
      auto *heading = new QListWidgetItem(groups[g], list_);
      heading->setFlags(Qt::NoItemFlags);
    }
    for (std::size_t i : members[static_cast<std::size_t>(g)]) {
      const Command &c = commands_[i];
      // "Go: Today" under the heading "Go" reads as "Today".
      QString text = c.text;
      if (text.startsWith(groups[g] + ": ")) {
        text = text.mid(groups[g].size() + 2);
      }
      auto *item = new QListWidgetItem(text, list_);
      item->setData(kCommandRole, static_cast<int>(i));
      item->setData(kShortcutRole, c.shortcut);
      if (!c.shortcut.isEmpty()) {
        item->setData(Qt::AccessibleTextRole,
                      QString("%1, %2").arg(text, c.shortcut));
      }
    }
  }
  list_->setCurrentRow(nextRow(-1, 1));
}

int CommandPalette::nextRow(int row, int step) const {
  for (int r = row + step; r >= 0 && r < list_->count(); r += step) {
    if (list_->item(r)->flags() & Qt::ItemIsSelectable) {
      return r;
    }
  }
  return row;
}

void CommandPalette::runSelected() {
  QListWidgetItem *item = list_->currentItem();
  if (!item || !(item->flags() & Qt::ItemIsSelectable)) {
    return;
  }
  const Command &c =
      commands_[static_cast<std::size_t>(item->data(kCommandRole).toInt())];
  QStringList recent = saved::recentCommands().get();
  const QString text = baseText(c.text);
  recent.removeAll(text);
  recent.prepend(text);
  while (recent.size() > kRecent) {
    recent.removeLast();
  }
  saved::recentCommands().set(recent);
  auto run = c.run;
  accept();
  // After the palette has closed and the keyboard is back with the window.
  QTimer::singleShot(0, parentWidget(), run);
}

bool CommandPalette::eventFilter(QObject *watched, QEvent *event) {
  if (watched == search_ && event->type() == QEvent::KeyPress) {
    auto *key = static_cast<QKeyEvent *>(event);
    int row = list_->currentRow();
    switch (key->key()) {
    case Qt::Key_Down:
    case Qt::Key_Up:
    case Qt::Key_PageDown:
    case Qt::Key_PageUp: {
      const int step =
          key->key() == Qt::Key_Down || key->key() == Qt::Key_PageDown ? 1 : -1;
      const int times =
          key->key() == Qt::Key_Down || key->key() == Qt::Key_Up ? 1 : 8;
      for (int i = 0; i < times; ++i) {
        row = nextRow(row, step);
      }
      list_->setCurrentRow(row);
      return true;
    }
    case Qt::Key_Return:
    case Qt::Key_Enter:
      runSelected();
      return true;
    default:
      break;
    }
  }
  return QDialog::eventFilter(watched, event);
}

std::vector<CommandPalette::Command> CommandPalette::fromMenus(QMenuBar *bar) {
  std::vector<Command> out;
  QSet<QAction *> seen; // an action in two menus is listed once
  QString group;        // the menu bar's menu being walked
  std::function<void(QMenu *, const QString &)> walk =
      [&](QMenu *menu, const QString &path) {
        for (QAction *a : menu->actions()) {
          if (a->isSeparator() || !a->isEnabled() || !a->isVisible() ||
              seen.contains(a)) {
            continue;
          }
          seen.insert(a);
          if (QMenu *sub = a->menu()) {
            walk(sub, path + plain(a->text()) + ": ");
            continue;
          }
          QString text = path + plain(a->text());
          if (a->isCheckable()) {
            text += a->isChecked() ? tr(" (on)") : tr(" (off)");
          }
          QPointer<QAction> action = a;
          out.push_back({text, a->shortcut().toString(QKeySequence::NativeText),
                         [action] {
                           if (action) {
                             action->trigger();
                           }
                         },
                         group});
        }
      };
  for (QAction *a : bar->actions()) {
    if (QMenu *menu = a->menu(); menu && a->isVisible()) {
      group = plain(a->text());
      walk(menu, group + ": ");
    }
  }
  return out;
}

std::vector<CommandPalette::Command>
CommandPalette::fromControls(QWidget *page) {
  std::vector<Command> out;
  for (QComboBox *box : page->findChildren<QComboBox *>()) {
    if (!usable(box, page) || box->isEditable()) {
      continue;
    }
    QString label = labelFor(box);
    if (label.isEmpty()) {
      label = firstSentence(box->toolTip());
    }
    for (int i = 0; i < box->count(); ++i) {
      QString text = box->itemText(i);
      if (text.isEmpty()) {
        continue;
      }
      QPointer<QComboBox> b = box;
      QString entry = label.isEmpty() ? text : label + ": " + text;
      if (i == box->currentIndex()) {
        entry += tr(" (current)");
      }
      out.push_back({entry, {}, [b, i] {
                       if (b && b->isEnabled() && i < b->count()) {
                         b->setCurrentIndex(i);
                         emit b->activated(i);
                         emit b->textActivated(b->itemText(i));
                       }
                     }});
    }
  }
  for (QAbstractButton *button : page->findChildren<QAbstractButton *>()) {
    if (!usable(button, page) || button->text().isEmpty()) {
      continue;
    }
    QString text = plain(button->text());
    QPointer<QAbstractButton> b = button;
    QMenu *menu = nullptr;
    if (auto *push = qobject_cast<QPushButton *>(button)) {
      menu = push->menu();
    } else if (auto *tool = qobject_cast<QToolButton *>(button)) {
      menu = tool->menu();
    }
    if (menu) {
      for (QAction *a : menu->actions()) {
        if (a->isSeparator() || !a->isEnabled() || a->menu()) {
          continue;
        }
        QPointer<QAction> action = a;
        out.push_back({text + " › " + plain(a->text()), {}, [action] {
                         if (action) {
                           action->trigger();
                         }
                       }});
      }
      continue;
    }
    if (qobject_cast<QCheckBox *>(button)) {
      text += button->isChecked() ? tr(" (on)") : tr(" (off)");
    }
    out.push_back(
        {text, button->shortcut().toString(QKeySequence::NativeText), [b] {
           if (b && b->isEnabled()) {
             b->click();
           }
         }});
  }
  return out;
}

} // namespace vcube
