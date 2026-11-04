#include "train/timer.hpp"

namespace vcube {

void SolveTimer::pressStart(std::int64_t now) {
  switch (state_) {
  case State::Idle:
  case State::Stopped:
    if (config_.inspection) {
      // The release starts inspection.
      armedForInspection_ = true;
      state_ = State::Idle;
      return;
    }
    [[fallthrough]];
  case State::Inspecting:
    holdStart_ = now;
    state_ = State::Holding;
    tick(now);
    return;
  default:
    return;
  }
}

void SolveTimer::releaseStart(std::int64_t now) {
  switch (state_) {
  case State::Idle:
    if (armedForInspection_) {
      armedForInspection_ = false;
      inspectStart_ = now;
      state_ = State::Inspecting;
    }
    return;
  case State::Holding:
    state_ = inspectStart_ ? State::Inspecting : State::Idle;
    return;
  case State::Ready:
    begin(now);
    return;
  default:
    return;
  }
}

void SolveTimer::pressStop(std::int64_t now) {
  if (state_ == State::Running) {
    stopNow(now);
  }
}

void SolveTimer::tick(std::int64_t now) {
  if (state_ == State::Holding && now - holdStart_ >= config_.holdMs) {
    state_ = State::Ready;
  }
}

void SolveTimer::cancel() {
  state_ = State::Idle;
  inspectStart_.reset();
  armedForInspection_ = false;
}

void SolveTimer::arm(std::int64_t now) {
  result_.reset();
  armedForInspection_ = false;
  if (config_.inspection) {
    inspectStart_ = now;
    state_ = State::Inspecting;
  } else {
    inspectStart_.reset();
    state_ = State::Ready;
  }
}

void SolveTimer::startNow(std::int64_t now) {
  if (state_ == State::Running) {
    return;
  }
  begin(now);
}

void SolveTimer::stopNow(std::int64_t now) {
  if (state_ != State::Running) {
    return;
  }
  final_ = now - start_;
  state_ = State::Stopped;
  result_ = Result{final_, penalty_};
}

void SolveTimer::begin(std::int64_t now) {
  penalty_ = inspectionPenalty(now);
  inspectStart_.reset();
  start_ = now;
  state_ = State::Running;
  result_.reset();
}

std::int64_t SolveTimer::elapsed(std::int64_t now) const {
  if (state_ == State::Running) {
    return now - start_;
  }
  if (state_ == State::Stopped) {
    return final_;
  }
  return 0;
}

std::optional<std::int64_t>
SolveTimer::inspectionElapsed(std::int64_t now) const {
  if (!inspectStart_) {
    return std::nullopt;
  }
  return now - *inspectStart_;
}

Penalty SolveTimer::inspectionPenalty(std::int64_t now) const {
  if (!inspectStart_) {
    return Penalty::None;
  }
  std::int64_t t = now - *inspectStart_;
  if (t > config_.inspectionMs + 2000) {
    return Penalty::Dnf;
  }
  if (t > config_.inspectionMs) {
    return Penalty::Plus2;
  }
  return Penalty::None;
}

std::optional<SolveTimer::Result> SolveTimer::takeResult() {
  auto r = result_;
  result_.reset();
  return r;
}

} // namespace vcube
