#pragma once

#include "app/keymap.hpp"

#include <QString>

namespace vcube {

// The keyboard controls as rich text for the help panel (F1): the section
// for `mode` (an AppMode) first, then the window's keys, the other
// modes and the turning keys in `keymap`.
QString controlsHtml(int mode, const Keymap &keymap);

} // namespace vcube
