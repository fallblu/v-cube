#include "train/timed_flow.hpp"

namespace vcube {

using S = SolveTimer::State;

void TimedFlow::setConfig(const Config &c) {
  config_ = c;
  timer_.setConfig(c.timer);
}

bool TimedFlow::isBusy() const {
  S s = timer_.state();
  return s == S::Running || s == S::Inspecting || s == S::Holding ||
         s == S::Ready;
}

bool TimedFlow::isIdle() const {
  return timer_.state() == S::Idle || timer_.state() == S::Stopped;
}

bool TimedFlow::isHolding() const {
  return timer_.state() == S::Holding || timer_.state() == S::Ready;
}

bool TimedFlow::cubeHidden() const {
  if (!config_.virtualCube || !config_.timer.inspection ||
      !config_.hideUntilInspection || finished_) {
    return false;
  }
  return isIdle();
}

TimedFlow::Effects TimedFlow::escape() {
  Effects e;
  e.used = true;
  if (isRunning()) {
    timer_.cancel();
    e.showLastResult = true;
    e.cancelled = true;
    e.restartCube = config_.virtualCube;
    return e;
  }
  bool was = isBusy();
  timer_.cancel();
  e.showLastResult = was;
  e.clearStatus = true;
  if (finished_) {
    finished_ = false;
    e.nextScramble = true;
  } else {
    e.restartCube = config_.virtualCube;
  }
  return e;
}

TimedFlow::Effects TimedFlow::space(bool press, std::int64_t now,
                                    bool scrambleReady) {
  Effects e;
  e.used = true;
  if (isRunning()) {
    if (press) {
      if (config_.virtualCube) {
        e.stopVirtual = true;
      } else {
        return stopKey(now);
      }
    }
    return e;
  }
  if (finished_) {
    // Space after a DNF: on to the next scramble; the release that follows
    // does nothing.
    if (press) {
      finished_ = false;
      e.nextScramble = true;
    }
    return e;
  }
  if (press) {
    if (!scrambleReady && timer_.state() != S::Inspecting) {
      return e; // wait for a scramble
    }
    timer_.pressStart(now);
  } else {
    timer_.releaseStart(now);
    if (timer_.state() == S::Inspecting) {
      e.clearStatus = true;
    }
    if (timer_.state() == S::Running) {
      e.clearStatus = true;
      e.startAttempt = config_.virtualCube;
    }
  }
  e.showTimer = true;
  return e;
}

TimedFlow::Effects TimedFlow::enter() {
  Effects e;
  if (finished_ && !isRunning()) {
    e.used = true;
    finished_ = false;
    e.nextScramble = true;
  }
  return e;
}

TimedFlow::Effects TimedFlow::stopKey(std::int64_t now) {
  Effects e;
  if (!isRunning()) {
    return e;
  }
  e.used = true;
  timer_.pressStop(now);
  e.record = timer_.takeResult();
  return e;
}

TimedFlow::MoveAnswer TimedFlow::move(bool rotation) const {
  if (isRunning()) {
    return MoveAnswer::Turn;
  }
  if (isHolding()) {
    return MoveAnswer::Ignore;
  }
  if (cubeHidden()) {
    return MoveAnswer::Hidden;
  }
  if (finished_ && !rotation) {
    return MoveAnswer::NextScramble;
  }
  return MoveAnswer::Turn;
}

std::optional<SolveTimer::Result> TimedFlow::stopVirtual(std::int64_t now,
                                                         bool solved) {
  timer_.pressStop(now);
  std::optional<SolveTimer::Result> r = timer_.takeResult();
  if (r && !solved) {
    r->penalty = Penalty::Dnf;
  }
  return r;
}

void TimedFlow::tick(std::int64_t now, bool popupOpen) {
  timer_.tick(now);
  if (isHolding() && popupOpen) {
    timer_.cancel();
  }
}

bool TimedFlow::leave() {
  if (!isBusy()) {
    return false;
  }
  timer_.cancel();
  return true;
}

bool TimedFlow::dropHold() {
  if (!isHolding()) {
    return false;
  }
  timer_.cancel();
  return true;
}

} // namespace vcube
