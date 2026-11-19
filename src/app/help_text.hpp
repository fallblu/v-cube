#pragma once

#include "app/keymap.hpp"
#include "app/mode_table.hpp"

#include <QString>

#include <optional>

namespace vcube {

// Every keyboard control, kept in one table: the help panel (F1) and
// docs/keys.md are both written from it, so they can't drift apart.

// The controls as rich text for the help panel: the section for `mode`
// (an AppMode) first, then the window's keys, the other modes and the
// turning keys in `keymap`.
QString controlsHtml(std::optional<AppMode::Id> mode, const Keymap &keymap);

// docs/keys.md, with the default turning keys.
QString controlsMarkdown();

} // namespace vcube
