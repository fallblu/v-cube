#include "app/key_router.hpp"

#include "app/mode_page.hpp"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>

namespace vcube {

KeyRouter::KeyRouter(QWidget *window, std::function<ModePage *()> page,
                     QObject *parent)
    : QObject(parent), window_(window), page_(std::move(page)) {}

bool KeyRouter::eventFilter(QObject *obj, QEvent *event) {
  if (event->type() != QEvent::KeyPress &&
      event->type() != QEvent::KeyRelease) {
    return QObject::eventFilter(obj, event);
  }
  // Only keys aimed at this window, not at dialogs, menus or text fields.
  if (QApplication::activeModalWidget() || QApplication::activePopupWidget()) {
    return false;
  }
  auto *w = qobject_cast<QWidget *>(obj);
  if (!w || w->window() != window_) {
    return false;
  }
  QWidget *focus = QApplication::focusWidget();
  if (qobject_cast<QLineEdit *>(focus) || qobject_cast<QTextEdit *>(focus) ||
      qobject_cast<QPlainTextEdit *>(focus) ||
      qobject_cast<QAbstractSpinBox *>(focus) ||
      qobject_cast<QKeySequenceEdit *>(focus)) {
    return false;
  }
  auto *key = static_cast<QKeyEvent *>(event);
  // An unhandled key event is offered again to each parent; handle it once.
  // Only the parent of the widget it was last offered to gets it that way:
  // another event can look the same (events are made in the same place, and
  // two can have one timestamp), and starts again at the widget with the
  // focus.
  KeySeen seen{event, event->type(), key->key(), key->timestamp()};
  bool offeredAgain = seen == lastKey_ && w == nextOffer_;
  lastKey_ = seen;
  nextOffer_ = w->parentWidget();
  if (offeredAgain) {
    return false;
  }
  return route(key, event->type() == QEvent::KeyPress);
}

bool KeyRouter::route(QKeyEvent *e, bool press) {
  ModePage *page = page_();
  if (!page) {
    return false;
  }
  const int key = e->key();
  if (e->isAutoRepeat()) {
    bool used = used_.contains(key);
    if (used && page->acceptsAutoRepeat(e)) {
      return page->handleKey(e, press);
    }
    return used;
  }
  bool used = page->handleKey(e, press);
  if (press && used) {
    used_.insert(key);
  } else {
    used_.erase(key);
  }
  return used;
}

bool isModifierKey(int key) {
  switch (key) {
  case Qt::Key_Shift:
  case Qt::Key_Control:
  case Qt::Key_Alt:
  case Qt::Key_AltGr:
  case Qt::Key_Meta:
  case Qt::Key_Super_L:
  case Qt::Key_Super_R:
  case Qt::Key_Hyper_L:
  case Qt::Key_Hyper_R:
  case Qt::Key_CapsLock:
  case Qt::Key_NumLock:
    return true;
  default:
    return false;
  }
}

} // namespace vcube
