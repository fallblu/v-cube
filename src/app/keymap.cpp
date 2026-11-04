#include "app/keymap.hpp"

#include <QKeySequence>

namespace vcube {

Keymap Keymap::cstimerDefault() {
  const std::vector<std::pair<int, const char *>> table = {
      {Qt::Key_I, "R"},      {Qt::Key_K, "R'"},    {Qt::Key_W, "B"},
      {Qt::Key_O, "B'"},     {Qt::Key_S, "D"},     {Qt::Key_L, "D'"},
      {Qt::Key_D, "L"},      {Qt::Key_E, "L'"},    {Qt::Key_J, "U"},
      {Qt::Key_F, "U'"},     {Qt::Key_H, "F"},     {Qt::Key_G, "F'"},
      {Qt::Key_Semicolon, "y"}, {Qt::Key_A, "y'"}, {Qt::Key_U, "r"},
      {Qt::Key_R, "l'"},     {Qt::Key_M, "r'"},    {Qt::Key_V, "l"},
      {Qt::Key_T, "x"},      {Qt::Key_Y, "x"},     {Qt::Key_N, "x'"},
      {Qt::Key_B, "x'"},     {Qt::Key_Period, "M'"}, {Qt::Key_X, "M'"},
      {Qt::Key_5, "M"},      {Qt::Key_6, "M"},     {Qt::Key_1, "S'"},
      {Qt::Key_2, "E"},      {Qt::Key_9, "E'"},    {Qt::Key_0, "S"},
      {Qt::Key_P, "z"},      {Qt::Key_Q, "z'"},    {Qt::Key_Z, "d"},
      {Qt::Key_C, "u'"},     {Qt::Key_Comma, "u"}, {Qt::Key_Slash, "d'"},
  };
  Keymap k;
  for (const auto &[key, move] : table) {
    Alg a = parseAlgOrEmpty(move);
    k.bindings.push_back({key, false, a.front()});
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
  Qt::KeyboardModifiers mods = e->modifiers() & ~(Qt::KeypadModifier | Qt::ShiftModifier);
  if (mods != Qt::NoModifier) {
    return std::nullopt; // leave Ctrl/Alt shortcuts alone
  }
  return lookup(e->key(), e->modifiers().testFlag(Qt::ShiftModifier));
}

QString Keymap::keyText(int key, bool shift) {
  QKeyCombination combo(shift ? Qt::ShiftModifier : Qt::NoModifier, static_cast<Qt::Key>(key));
  return QKeySequence(combo).toString(QKeySequence::PortableText);
}

std::optional<std::pair<int, bool>> Keymap::parseKeyText(const QString &text) {
  QKeySequence seq(text, QKeySequence::PortableText);
  if (seq.isEmpty()) {
    return std::nullopt;
  }
  QKeyCombination c = seq[0];
  return std::pair<int, bool>{c.key(), c.keyboardModifiers().testFlag(Qt::ShiftModifier)};
}

QStringList Keymap::toStrings() const {
  QStringList out;
  for (const auto &b : bindings) {
    out << keyText(b.key, b.shift) + "=" + QString::fromStdString(toString(b.move));
  }
  return out;
}

Keymap Keymap::fromStrings(const QStringList &lines) {
  Keymap k;
  for (const QString &line : lines) {
    // The key itself may be "=", so split at the last '='.
    auto eq = line.lastIndexOf('=');
    if (eq <= 0) {
      continue;
    }
    auto key = parseKeyText(line.left(eq));
    Alg a = parseAlgOrEmpty(line.mid(eq + 1).toStdString());
    if (key && a.size() == 1) {
      k.bindings.push_back({key->first, key->second, a.front()});
    }
  }
  return k;
}

} // namespace vcube
