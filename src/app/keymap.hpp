#pragma once

#include "cube/move.hpp"

#include <QKeyEvent>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>
#include <vector>

namespace vcube {

// Keyboard controls for the virtual cube: each binding maps a key (plus
// optional Shift) to one move.
struct KeyBinding {
  int key = 0; // Qt::Key
  bool shift = false;
  Move move;
};

class Keymap {
public:
  // Face keys from cstimer's layout: I/K R R', D/E L L', J/F U U', S/L D D',
  // H/G F F', W/O B B'; Shift + a face key turns the wide layer (Shift+I r).
  // U/M M' M, R/V x x', ;/A y y', P/Q z z'.
  static Keymap defaultLayout();

  std::optional<Move> lookup(const QKeyEvent *e) const;
  std::optional<Move> lookup(int key, bool shift) const;

  std::vector<KeyBinding> bindings;

  // "I=R", "Shift+H=S", ... for settings storage and the editor.
  QStringList toStrings() const;
  static Keymap fromStrings(const QStringList &lines);

  static QString keyText(int key, bool shift);
  // Why a key can't turn the cube (the modes or the window use it), or an
  // empty string if it can.
  static QString reservedReason(int key);
  static std::optional<std::pair<int, bool>> parseKeyText(const QString &text);
};

} // namespace vcube
