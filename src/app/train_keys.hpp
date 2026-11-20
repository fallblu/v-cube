#pragma once

#include "train/keys.hpp"

#include <cstdint>

class QKeyEvent;

namespace vcube {

// How a page reads the keys held with Ctrl or Alt.
enum class KeyChords : std::uint8_t {
  // Every key is itself whatever is held with it: Ctrl+Z is not a key of
  // the page's, Alt+3 is the digit 3.
  None,
  // Ctrl+Z is Undo, with nothing else held (Ctrl+Shift+Z is Library's
  // redo); the rest as None.
  Undo,
  // Untimed and Move Count: Alt+3, Alt+C, Alt+R, Ctrl+Z and Ctrl+Right are
  // Dnf, Comment, Review, Undo and Skip with exactly that held, and Delete
  // is Delete only with nothing held.
  Attempt,
};

// The one translation of a Qt key event into the flows' vocabulary. The
// turn a key makes is the page's to add (TrainKeyEvent::move, or
// TrainKey::Turn), since only the page knows whether its cube is the
// virtual one.
TrainKeyEvent trainKeyEvent(const QKeyEvent *e, bool press, KeyChords chords);

} // namespace vcube
