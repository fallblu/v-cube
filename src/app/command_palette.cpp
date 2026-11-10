#include "app/command_palette.hpp"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace vcube {

namespace {

constexpr int kCommandRole = Qt::UserRole;

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
  for (std::size_t i = 0; i < commands_.size(); ++i) {
    const Command &c = commands_[i];
    QString hay = c.text.toLower();
    bool match = std::all_of(terms.begin(), terms.end(),
                             [&](const QString &t) { return hay.contains(t); });
    if (!match) {
      continue;
    }
    QString text = c.text;
    if (!c.shortcut.isEmpty()) {
      text += "    " + c.shortcut;
    }
    auto *item = new QListWidgetItem(text, list_);
    item->setData(kCommandRole, static_cast<int>(i));
  }
  list_->setCurrentRow(0);
}

void CommandPalette::runSelected() {
  QListWidgetItem *item = list_->currentItem();
  if (!item) {
    return;
  }
  auto run =
      commands_[static_cast<std::size_t>(item->data(kCommandRole).toInt())].run;
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
      list_->setCurrentRow(std::min(row + 1, list_->count() - 1));
      return true;
    case Qt::Key_Up:
      list_->setCurrentRow(std::max(row - 1, 0));
      return true;
    case Qt::Key_PageDown:
      list_->setCurrentRow(std::min(row + 8, list_->count() - 1));
      return true;
    case Qt::Key_PageUp:
      list_->setCurrentRow(std::max(row - 8, 0));
      return true;
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
                         }});
        }
      };
  for (QAction *a : bar->actions()) {
    if (QMenu *menu = a->menu()) {
      walk(menu, plain(a->text()) + ": ");
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
