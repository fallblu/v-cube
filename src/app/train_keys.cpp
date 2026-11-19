#include "app/train_keys.hpp"

#include "app/key_router.hpp"

#include <QKeyEvent>

namespace vcube {

namespace {

TrainKey plainKey(int key) {
  using K = TrainKey;
  switch (key) {
  case Qt::Key_Space:
    return K::Space;
  case Qt::Key_Return:
  case Qt::Key_Enter:
    return K::Enter;
  case Qt::Key_Tab:
    return K::Tab;
  case Qt::Key_Backspace:
    return K::Backspace;
  case Qt::Key_Delete:
    return K::Delete;
  case Qt::Key_Escape:
    return K::Escape;
  case Qt::Key_Left:
    return K::Left;
  case Qt::Key_Right:
    return K::Right;
  case Qt::Key_Home:
    return K::Home;
  default:
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
      return K::Digit;
    }
    return isModifierKey(key) ? K::Modifier : K::Other;
  }
}

} // namespace

TrainKeyEvent trainKeyEvent(const QKeyEvent *e, bool press, KeyChords chords) {
  using K = TrainKey;
  // The number pad's keys are the keys they show.
  const Qt::KeyboardModifiers mods = e->modifiers() & ~Qt::KeypadModifier;
  const int key = e->key();
  TrainKeyEvent k;
  k.press = press;
  k.autoRepeat = e->isAutoRepeat();
  k.shift = mods.testFlag(Qt::ShiftModifier);
  k.modified =
      (mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) != 0;
  if (chords == KeyChords::Attempt) {
    if (mods == Qt::AltModifier &&
        (key == Qt::Key_3 || key == Qt::Key_C || key == Qt::Key_R)) {
      k.key = key == Qt::Key_3   ? K::Dnf
              : key == Qt::Key_C ? K::Comment
                                 : K::Review;
      return k;
    }
    if (mods == Qt::ControlModifier &&
        (key == Qt::Key_Z || key == Qt::Key_Right)) {
      k.key = key == Qt::Key_Z ? K::Undo : K::Skip;
      return k;
    }
  } else if (chords == KeyChords::Undo && key == Qt::Key_Z &&
             mods.testFlag(Qt::ControlModifier)) {
    k.key = K::Undo;
    return k;
  }
  k.key = plainKey(key);
  if (k.key == K::Digit) {
    k.digit = key - Qt::Key_0;
  } else if (k.key == K::Delete && chords == KeyChords::Attempt &&
             mods != Qt::NoModifier) {
    k.key = K::Other;
  }
  return k;
}

} // namespace vcube
