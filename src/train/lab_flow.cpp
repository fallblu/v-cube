#include "train/lab_flow.hpp"

namespace vcube {

LabFlow::Step LabFlow::key(const KeyEvent &k, std::int64_t now) const {
  // Nothing of the flow's is held with Ctrl, Alt or Meta.
  const bool plain = !k.modified;
  // The flow's own keys and the modifiers never turn the cube.
  const bool turn = k.move && k.key != Key::Space && k.key != Key::Escape &&
                    k.key != Key::Backspace && k.key != Key::Delete &&
                    k.key != Key::Modifier;
  switch (phase_) {
  case Phase::Idle:
  case Phase::Finished:
    if (k.press && k.key == Key::Space && plain) {
      return {Action::Next, true};
    }
    return {};
  case Phase::Done:
    if (!k.press) {
      return {Action::None, k.key == Key::Space};
    }
    if (now - stoppedAt_ < kStopGraceMs && k.key != Key::Modifier) {
      return {Action::None, true};
    }
    if (!plain) {
      return {};
    }
    switch (k.key) {
    case Key::Space:
      return {Action::Next, true};
    case Key::Backspace:
      return {Action::ToggleDnf, true};
    case Key::Delete:
      return {Action::DeleteLast, true};
    case Key::Escape:
      return {Action::SameAgain, true};
    default:
      return {};
    }
  case Phase::Ready:
    if (k.key == Key::Escape && k.press) {
      return {Action::ResetReady, true};
    }
    if (virtual_) {
      if (k.press && plain && turn) {
        return {Action::Turn, true};
      }
      return {};
    }
    if (k.key == Key::Space && plain) {
      return {k.press ? Action::HoldPress : Action::HoldRelease, true};
    }
    return {};
  case Phase::Running:
    if (!k.press) {
      return {Action::None, k.key == Key::Space};
    }
    if (k.key == Key::Escape) {
      return {Action::Cancel, true};
    }
    if (virtual_) {
      if (k.key == Key::Space && plain) {
        return {Action::StopVirtual, true};
      }
      if (turn && plain) {
        return {Action::Turn, true};
      }
      return {};
    }
    if (k.key == Key::Modifier) {
      return {};
    }
    return {Action::StopPhysical, true};
  }
  return {};
}

} // namespace vcube
