#include "train/inspection_flow.hpp"

#include "cube/cubie.hpp"

#include <algorithm>
#include <vector>

namespace vcube {

using P = InspectionFlow::Phase;

bool InspectionFlow::isBusy() const {
  return phase_ == P::Inspecting || phase_ == P::Executing ||
         phase_ == P::Counting;
}

InspectionFlow::Effects InspectionFlow::key(const KeyInput &k, std::int64_t now,
                                            bool scrambleReady) {
  Effects e;
  bool counting = phase_ == P::Counting;
  bool entryKey = counting && (k.key == Key::Digit || k.key == Key::Backspace ||
                               k.key == Key::Enter);
  if (entryKey && k.modifiers) {
    return e; // e.g. Ctrl+7 switches modes
  }
  bool known = k.turn || entryKey || k.key == Key::Space ||
               k.key == Key::Escape ||
               (k.key == Key::Tab && phase_ == P::Result);
  if (!known) {
    return e;
  }
  e.used = true;
  if (!k.press || (k.autoRepeat && k.key != Key::Backspace)) {
    return e;
  }
  if (entryKey) {
    if (k.key == Key::Digit) {
      typeDigit(k.digit);
      e.entryChanged = true;
    } else if (k.key == Key::Backspace) {
      if (!entry_.empty()) {
        entry_.pop_back();
      }
      e.entryChanged = true;
    } else {
      Effects entered = enterCount();
      entered.used = true;
      return entered;
    }
    return e;
  }
  switch (k.key) {
  case Key::Space: {
    Effects s = space(now, scrambleReady);
    s.used = true;
    return s;
  }
  case Key::Escape:
    if (isBusy()) {
      cancel();
      e.cancel = true;
    } else if (retry_) {
      next();
      e.next = true;
    }
    return e;
  case Key::Tab:
    if (!config_.plan && config_.axes[0] && config_.axes[1] && ringAxis_ >= 0) {
      ringAxis_ = 1 - ringAxis_;
      e.rings = true;
    }
    return e;
  default:
    break;
  }
  if (k.turn) {
    switch (phase_) {
    case P::Inspecting:
    case P::Counting:
      e.turn = Turn::Inspect;
      break;
    case P::Executing:
      e.turn = Turn::Hidden;
      break;
    case P::Result:
      e.turn = Turn::Look;
      break;
    case P::Ready:
      break;
    }
  }
  return e;
}

InspectionFlow::Effects InspectionFlow::space(std::int64_t now,
                                              bool scrambleReady) {
  Effects e;
  switch (phase_) {
  case P::Ready:
    if (!(config_.plan && retry_) && !scrambleReady) {
      e.waitScramble = true;
      break;
    }
    started_ = now;
    if (config_.plan) {
      usedMs_ = 0;
      phase_ = P::Inspecting;
      e.startInspection = true;
    } else {
      answers_ = {-1, -1};
      entry_.clear();
      axis_ = config_.axes[0] ? 0 : 1;
      phase_ = P::Counting;
      e.startCounting = true;
    }
    break;
  case P::Inspecting:
    hide(now);
    e.startExecution = true;
    break;
  case P::Executing:
    if (autoHiddenAt_ >= 0 && now - autoHiddenAt_ < kHideGraceMs) {
      break; // meant to hide the cube, which the limit just did
    }
    e.finishPlan = true;
    break;
  case P::Counting:
    break;
  case P::Result:
    next();
    e.next = true;
    break;
  }
  return e;
}

void InspectionFlow::hide(std::int64_t now) {
  std::int64_t used = now - started_;
  if (config_.limitSeconds > 0) {
    used = std::min<std::int64_t>(
        used, static_cast<std::int64_t>(config_.limitSeconds) * 1000);
  }
  usedMs_ = used;
  autoHiddenAt_ = -1;
  phase_ = P::Executing;
}

InspectionFlow::Effects InspectionFlow::tick(std::int64_t now) {
  Effects e;
  if (phase_ != P::Inspecting || config_.limitSeconds <= 0) {
    return e;
  }
  if (now - started_ >=
      static_cast<std::int64_t>(config_.limitSeconds) * 1000) {
    hide(now);
    autoHiddenAt_ = now;
    e.startExecution = true;
  }
  return e;
}

InspectionFlow::Countdown InspectionFlow::countdown(std::int64_t now) const {
  // With a limit, the seconds left, counted down as Timed mode's inspection
  // is; without one, the seconds used.
  std::int64_t elapsedMs = now - started_;
  int limit = config_.limitSeconds;
  std::int64_t limitMs = static_cast<std::int64_t>(limit) * 1000;
  std::int64_t seconds = elapsedMs / 1000;
  Countdown c;
  c.shown = limit > 0 ? (limitMs - elapsedMs + 999) / 1000 : seconds;
  if (config_.wca) {
    if (seconds >= 12) {
      c.alert = Alert::Twelve;
    } else if (seconds >= 8) {
      c.alert = Alert::Eight;
    }
  } else if (limit > 0 && limit - seconds <= 3) {
    c.alert = Alert::Ending;
  }
  return c;
}

void InspectionFlow::typeDigit(int digit) {
  std::string next = entry_ == "0" ? std::string() : entry_;
  next += static_cast<char>('0' + digit);
  if (next.size() <= 2 && std::stoi(next) <= kEdges) {
    entry_ = next;
  }
}

InspectionFlow::Effects InspectionFlow::enterCount() {
  Effects e;
  if (entry_.empty()) {
    e.needEntry = true;
    return e;
  }
  answers_[static_cast<std::size_t>(axis_)] = std::stoi(entry_);
  entry_.clear();
  if (axis_ == 0 && config_.axes[1]) {
    axis_ = 1;
    e.entryChanged = true;
    return e;
  }
  e.finishEdges = true;
  return e;
}

void InspectionFlow::planDone() {
  if (phase_ == P::Executing) {
    phase_ = P::Result;
  }
}

void InspectionFlow::edgesDone(const std::array<int, 2> &correct) {
  ringAxis_ = ringedAxis(answers_, correct);
  phase_ = P::Result;
}

bool InspectionFlow::startRetry(std::size_t index) {
  if (isBusy() || !config_.plan) {
    return false;
  }
  retry_ = index;
  phase_ = P::Ready;
  return true;
}

InspectionFlow::Skip InspectionFlow::skip() {
  Skip s;
  if (isBusy()) {
    return s;
  }
  s.done = true;
  s.advanceFeed = !retry_ && phase_ == P::Ready;
  next();
  return s;
}

void InspectionFlow::next() {
  retry_.reset();
  phase_ = P::Ready;
}

void InspectionFlow::cancel() {
  entry_.clear();
  phase_ = P::Ready;
}

bool InspectionFlow::leave() {
  if (!isBusy()) {
    return false;
  }
  cancel();
  return true;
}

int ringedAxis(const std::array<int, 2> &answers,
               const std::array<int, 2> &correct) {
  int ring = -1;
  bool wrongRinged = false;
  for (int axis = 0; axis < 2; ++axis) {
    auto i = static_cast<std::size_t>(axis);
    if (correct[i] < 0) {
      continue;
    }
    bool right = answers[i] == correct[i];
    if (ring < 0 || (!right && !wrongRinged)) {
      ring = axis;
      wrongRinged = !right;
    }
  }
  return ring;
}

std::optional<Hold> doneHold(const FaceletCube &cube, SolveStep step) {
  std::vector<Hold> holds = stepReachedHolds(cube, step);
  if (holds.empty()) {
    return std::nullopt;
  }
  if (std::optional<Hold> now = holdOf(cube)) {
    if (std::find(holds.begin(), holds.end(), *now) != holds.end()) {
      return *now;
    }
    for (Hold h : holds) {
      if (h.top == now->top) {
        return h;
      }
    }
  }
  return holds.front();
}

} // namespace vcube
