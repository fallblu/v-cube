#pragma once

#include "cube/move.hpp"

#include <cstdint>
#include <optional>

namespace vcube {

// A key as the training pages' flows read it. There is one vocabulary for
// all of them, so that a key one page learns is not silently Other on the
// rest: each flow acts on the keys it has a use for and passes over the
// others.
enum class TrainKey : std::uint8_t {
  Space,
  Enter,
  Tab,
  Backspace,
  Delete,
  Escape,
  Left,
  Right,
  Home,
  Digit, // 0-9 (see TrainKeyEvent::digit)
  Undo,  // Ctrl+Z
  // The chords of Untimed and Move Count, for the last attempt and the
  // scramble.
  Dnf,     // Alt+3
  Comment, // Alt+C
  Review,  // Alt+R
  Skip,    // Ctrl+Right
  // A key that turns the page's virtual cube, where the page gives its
  // turns a key of their own (the others read TrainKeyEvent::move).
  Turn,
  Modifier, // Shift, Ctrl, Alt, the lock keys ...
  Other,
};

struct TrainKeyEvent {
  TrainKey key = TrainKey::Other;
  bool press = true;
  bool shift = false;
  std::optional<Move> move; // the turn the key makes (virtual cube)
  bool autoRepeat = false;
  int digit = 0;         // for TrainKey::Digit
  bool modified = false; // Ctrl, Alt or Meta held
};

// A key going down or coming back up.
enum class KeyEdge : std::uint8_t { Down, Up };

// Whether a scramble is shown to solve. While one is still awaited no
// solve starts and nothing is recorded.
enum class Scramble : std::uint8_t { Shown, Awaited };

// Space held to start a solve, as the flows keep it and the time display
// shows it: it must be held for the hold time (Ready) before letting go
// starts the solve.
enum class HoldCue : std::uint8_t { None, Holding, Ready };

} // namespace vcube
