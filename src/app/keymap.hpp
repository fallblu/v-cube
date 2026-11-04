#pragma once

#include "cube/move.hpp"

#include <QKeyEvent>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace vcube {

// Keyboard controls for the virtual cube: each binding maps a key (plus
// optional Shift) to one move.
struct KeyBinding {
  int key = 0;        // Qt::Key
  bool shift = false;
  Move move;
};

class Keymap {
public:
  // cstimer's layout (Ryan Heise's): I/K R R', J/F U U', H/G F F', D/E L L',
  // S/L D D', W/O B B', U/M r r', V/R l l', T/Y x, B/N x', ;/A y y', P/Q z z',
  // ./X M', 5/6 M, 2/9 E E', 0/1 S S', ,/C u u', Z// d d'.
  static Keymap cstimerDefault();

  std::optional<Move> lookup(const QKeyEvent *e) const;
  std::optional<Move> lookup(int key, bool shift) const;

  std::vector<KeyBinding> bindings;

  // "I=R", "Shift+H=S", ... for settings storage and the editor.
  QStringList toStrings() const;
  static Keymap fromStrings(const QStringList &lines);

  static QString keyText(int key, bool shift);
  static std::optional<std::pair<int, bool>> parseKeyText(const QString &text);
};

} // namespace vcube
