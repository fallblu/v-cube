#include "app/keymap.hpp"

#include <QCoreApplication>
#include <QKeySequence>

#include <string>

namespace vcube {

Keymap Keymap::defaultLayout() {
  // Each face key turns the face; Shift + the same key turns the wide layer.
  const std::vector<std::pair<int, const char *>> faces = {
      {Qt::Key_I, "R"}, {Qt::Key_K, "R'"}, {Qt::Key_D, "L"}, {Qt::Key_E, "L'"},
      {Qt::Key_J, "U"}, {Qt::Key_F, "U'"}, {Qt::Key_S, "D"}, {Qt::Key_L, "D'"},
      {Qt::Key_H, "F"}, {Qt::Key_G, "F'"}, {Qt::Key_W, "B"}, {Qt::Key_O, "B'"},
  };
  const std::vector<std::pair<int, const char *>> others = {
      {Qt::Key_U, "M'"}, {Qt::Key_M, "M"},         {Qt::Key_R, "x"},
      {Qt::Key_V, "x'"}, {Qt::Key_Semicolon, "y"}, {Qt::Key_A, "y'"},
      {Qt::Key_P, "z"},  {Qt::Key_Q, "z'"},
  };
  Keymap k;
  for (const auto &[key, move] : faces) {
    k.bindings.push_back({key, false, parseAlgOrEmpty(move).front()});
    std::string wide = move;
    wide[0] = static_cast<char>(wide[0] - 'A' + 'a');
    k.bindings.push_back({key, true, parseAlgOrEmpty(wide).front()});
  }
  for (const auto &[key, move] : others) {
    k.bindings.push_back({key, false, parseAlgOrEmpty(move).front()});
  }
  return k;
}

std::optional<Move> Keymap::lookup(int key, bool shift) const {
  std::optional<Move> fallback;
  for (const auto &b : bindings) {
    if (b.key != key) {
      continue;
    }
    if (b.shift == shift) {
      return b.move;
    }
    if (!b.shift) {
      fallback = b.move;
    }
  }
  return fallback;
}

std::optional<Move> Keymap::lookup(const QKeyEvent *e) const {
  Qt::KeyboardModifiers mods =
      e->modifiers() & ~(Qt::KeypadModifier | Qt::ShiftModifier);
  if (mods != Qt::NoModifier) {
    return std::nullopt; // leave Ctrl/Alt shortcuts alone
  }
  return lookup(e->key(), e->modifiers().testFlag(Qt::ShiftModifier));
}

QString Keymap::keyText(int key, bool shift) {
  QKeyCombination combo(shift ? Qt::ShiftModifier : Qt::NoModifier,
                        static_cast<Qt::Key>(key));
  return QKeySequence(combo).toString(QKeySequence::PortableText);
}

std::optional<std::pair<int, bool>> Keymap::parseKeyText(const QString &text) {
  QKeySequence seq(text, QKeySequence::PortableText);
  // One key, with Shift or nothing: a binding only ever has those, and
  // lookup() leaves keys with Ctrl, Alt or Meta alone.
  if (seq.count() != 1) {
    return std::nullopt;
  }
  QKeyCombination c = seq[0];
  if ((c.keyboardModifiers() & ~Qt::ShiftModifier) != Qt::NoModifier) {
    return std::nullopt;
  }
  return std::pair<int, bool>{
      c.key(), c.keyboardModifiers().testFlag(Qt::ShiftModifier)};
}

QStringList Keymap::toStrings() const {
  QStringList out;
  for (const auto &b : bindings) {
    out << keyText(b.key, b.shift) + "=" +
               QString::fromStdString(toString(b.move));
  }
  return out;
}

Keymap Keymap::fromStrings(const QStringList &lines, int *dropped) {
  Keymap k;
  int skipped = 0;
  for (const QString &line : lines) {
    // The key itself may be "=", so split at the last '='.
    auto eq = line.lastIndexOf('=');
    if (eq <= 0) {
      ++skipped;
      continue;
    }
    auto key = parseKeyText(line.left(eq));
    Alg a = parseAlgOrEmpty(line.mid(eq + 1).toStdString());
    if (key && a.size() == 1 && reservedReason(key->first).isEmpty()) {
      // A key turns its layer once, so R3 is bound as R'.
      k.bindings.push_back(
          {key->first, key->second, {a.front().type, a.front().amount}});
    } else {
      ++skipped;
    }
  }
  if (dropped) {
    *dropped = skipped;
  }
  return k;
}

QString Keymap::reservedReason(int key) {
  switch (key) {
  case Qt::Key_Space:
  case Qt::Key_Escape:
  case Qt::Key_Backspace:
  case Qt::Key_Delete:
  case Qt::Key_Tab:
  case Qt::Key_Backtab:
  case Qt::Key_Return:
  case Qt::Key_Enter:
    return QCoreApplication::translate(
        "vcube::Keymap",
        "Space, Esc, Backspace, Delete, Tab and Enter are the modes' own "
        "controls.");
  case Qt::Key_Left:
  case Qt::Key_Right:
  case Qt::Key_Up:
  case Qt::Key_Down:
  case Qt::Key_Home:
  case Qt::Key_End:
    return QCoreApplication::translate(
        "vcube::Keymap", "The arrow keys, Home and End step through "
                         "walkthroughs, replays and scrambles.");
  default:
    break;
  }
  if (key >= Qt::Key_0 && key <= Qt::Key_9) {
    return QCoreApplication::translate(
        "vcube::Keymap", "The digits grade cases in Learn and type counts in "
                         "Move Count and EO Cross.");
  }
  if (key >= Qt::Key_F1 && key <= Qt::Key_F35) {
    return QCoreApplication::translate(
        "vcube::Keymap", "The function keys are shortcuts (F1 help, F9 "
                         "Learn's case list, F10 focus mode, F11 full "
                         "screen).");
  }
  return {};
}

} // namespace vcube
