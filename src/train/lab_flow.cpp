#include "train/lab_flow.hpp"

namespace vcube {

LabFlow::Step LabFlow::key(const Input &in) const {
  switch (phase_) {
  case Phase::Idle:
  case Phase::Finished:
    if (in.press && in.key == Key::Space && in.plain) {
      return {Action::Next, true};
    }
    return {};
  case Phase::Done:
    if (!in.press) {
      return {Action::None, in.key == Key::Space};
    }
    if (in.now - stoppedAt_ < kStopGraceMs && in.key != Key::Modifier) {
      return {Action::None, true};
    }
    if (!in.plain) {
      return {};
    }
    switch (in.key) {
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
    if (in.key == Key::Escape && in.press) {
      return {Action::ResetReady, true};
    }
    if (virtual_) {
      if (in.press && in.plain && in.key == Key::Move) {
        return {Action::Turn, true};
      }
      return {};
    }
    if (in.key == Key::Space && in.plain) {
      return {in.press ? Action::HoldPress : Action::HoldRelease, true};
    }
    return {};
  case Phase::Running:
    if (!in.press) {
      return {Action::None, in.key == Key::Space};
    }
    if (in.key == Key::Escape) {
      return {Action::Cancel, true};
    }
    if (virtual_) {
      if (in.key == Key::Space && in.plain) {
        return {Action::StopVirtual, true};
      }
      if (in.key == Key::Move && in.plain) {
        return {Action::Turn, true};
      }
      return {};
    }
    if (in.key == Key::Modifier) {
      return {};
    }
    return {Action::StopPhysical, true};
  }
  return {};
}

} // namespace vcube
