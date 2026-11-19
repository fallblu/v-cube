#include "train/drill_flow.hpp"

#include <algorithm>

namespace vcube {

namespace {

using Step = DrillFlow::Step;
using Action = DrillFlow::Action;
using Key = DrillFlow::Key;

Step used(Action a = Action::None) { return {true, a, {}, false, 0, {}}; }

Step hint(DrillFlow::Hint h) {
  Step s = used(Action::Hint);
  s.hint = h;
  return s;
}

} // namespace

void DrillFlow::setConfig(const Config &c) {
  config_ = c;
  timer_.setConfig({c.holdMs, false, 15000});
}

bool DrillFlow::isGiveUp(const KeyEvent &k) const {
  return config_.shiftSpaceGivesUp ? k.key == Key::Space && k.shift
                                   : k.key == Key::Backspace;
}

bool DrillFlow::holding() const {
  auto s = timer_.state();
  return s == SolveTimer::State::Holding ||
         (s == SolveTimer::State::Ready && !armed_);
}

bool DrillFlow::busy() const {
  return holding() || timer_.state() == SolveTimer::State::Running;
}

void DrillFlow::cancelTimer() {
  timer_.cancel();
  armed_ = false;
}

void DrillFlow::setUp(std::int64_t now) {
  ++practiceToken_;
  cancelTimer();
  timeMs_ = 0;
  failed_ = false;
  unfinished_ = false;
  gaveUp_ = false;
  gaveUpBeforeStart_ = false;
  recorded_ = false;
  newBest_ = false;
  practiceDone_ = false;
  shownAtMs_ = now;
  firstTurnMs_ = 0;
  timingValid_ = true;
  phase_ = Phase::Waiting;
}

void DrillFlow::noCase() {
  leaveCase();
  phase_ = Phase::NoCase;
}

void DrillFlow::leaveCase() {
  ++practiceToken_;
  cancelTimer();
  recorded_ = false;
}

void DrillFlow::start(std::int64_t now) {
  if (timer_.state() != SolveTimer::State::Running) {
    timer_.startNow(now);
  }
  armed_ = false;
  startMs_ = now;
  faceTurns_ = 0;
  phase_ = Phase::Running;
}

std::optional<std::int64_t>
DrillFlow::turned(Turn turn, std::int64_t now,
                  const std::function<bool()> &stepDone) {
  const bool face = turn != Turn::Rotation;
  const bool offUd = turn == Turn::OffUdFace;
  if (face) {
    ++faceTurns_;
  }
  if (offUd && firstTurnMs_ == 0) {
    firstTurnMs_ = now;
  }
  if (face && config_.autoStop && phase_ == Phase::Running && stepDone()) {
    timer_.stopNow(now);
    if (auto r = timer_.takeResult()) {
      return r->timeMs;
    }
  }
  return std::nullopt;
}

void DrillFlow::cancelSolve() {
  cancelTimer();
  timingValid_ = false; // the case was seen before this try
  phase_ = Phase::Waiting;
}

std::int64_t DrillFlow::giveUp(std::int64_t now) {
  // The time of a failed attempt counts for nothing, so a case given up
  // before the timer started is recorded with none.
  std::int64_t timeMs = 0;
  bool running = timer_.state() == SolveTimer::State::Running;
  if (running) {
    timer_.pressStop(now);
    if (auto r = timer_.takeResult()) {
      timeMs = r->timeMs;
    }
  }
  cancelTimer();
  gaveUpBeforeStart_ = !running;
  return timeMs;
}

void DrillFlow::finished(std::int64_t timeMs, Result result,
                         std::optional<std::int64_t> bestBefore,
                         std::int64_t now) {
  const bool failed = result != Result::Solved;
  const bool gaveUp = result == Result::GaveUp;
  timeMs_ = timeMs;
  failed_ = failed;
  unfinished_ = failed && !gaveUp;
  gaveUp_ = gaveUp;
  bestBefore_ = bestBefore;
  newBest_ = !failed && bestBefore_ && timeMs < *bestBefore_;
  recorded_ = true;
  guardUntil_ = now + kGuardMs;
  phase_ = Phase::Revealed;
}

bool DrillFlow::setWrong(bool wrong) {
  if (!recorded_ || gaveUp_ || !resultShown()) {
    return false;
  }
  failed_ = wrong;
  unfinished_ = false; // marked by hand now
  newBest_ = !wrong && bestBefore_ && timeMs_ < *bestBefore_;
  return true;
}

bool DrillFlow::restored() {
  if (recorded_ || !resultShown()) {
    return false;
  }
  recorded_ = true;
  return true;
}

bool DrillFlow::deleted() {
  recorded_ = false;
  if (!(gaveUp_ && gaveUpBeforeStart_)) {
    return false;
  }
  // Given up by mistake: the case is set up as it was, to try.
  ++practiceToken_;
  gaveUp_ = false;
  failed_ = false;
  timingValid_ = false; // the algs were seen
  phase_ = Phase::Waiting;
  return true;
}

void DrillFlow::startPractice(bool aided) {
  ++practiceToken_;
  practiceDone_ = false;
  phase_ = aided ? Phase::Aided : Phase::Unaided;
}

bool DrillFlow::practiceTurned(bool face,
                               const std::function<bool()> &stepDone) {
  // Once solved it stays solved, whatever turns come after.
  if (practiceDone_ || !face || !stepDone()) {
    return false;
  }
  practiceDone_ = true;
  return true;
}

bool DrillFlow::leave() {
  ++practiceToken_;
  if (phase_ == Phase::Running) {
    return true;
  }
  cancelTimer();
  return false;
}

bool DrillFlow::loseKeyboard() {
  if (!holding()) {
    return false;
  }
  timer_.cancel();
  return true;
}

void DrillFlow::windowLost() {
  if (firstTurnMs_ == 0 &&
      (phase_ == Phase::Waiting || phase_ == Phase::Running)) {
    timingValid_ = false;
  }
}

std::optional<DrillFlow::Timing> DrillFlow::timing(std::int64_t timeMs,
                                                   bool failed) const {
  // Only for a case solved, which was recognised rightly.
  if (!config_.virtualCube || !timingValid_ || firstTurnMs_ <= 0 || failed) {
    return std::nullopt;
  }
  return Timing{std::max<std::int64_t>(1, firstTurnMs_ - shownAtMs_),
                std::max<std::int64_t>(0, timeMs - (firstTurnMs_ - startMs_))};
}

Step DrillFlow::key(const KeyEvent &k, std::int64_t now, Scramble scramble) {
  // A modifier alone does nothing, and never stops the timer.
  if (k.key == Key::Modifier) {
    return {phase_ == Phase::Running, Action::None, {}, false, 0, {}};
  }
  // Just after the stop, a second key meant for it mustn't skip the
  // result, mark it or retry the case.
  if (k.press && now < guardUntil_ && phase_ == Phase::Revealed) {
    return used();
  }
  switch (phase_) {
  case Phase::NoCase:
    if (k.press && (k.key == Key::Space || k.key == Key::Enter)) {
      return used(Action::NextFromIdle);
    }
    return {k.move.has_value(), Action::None, {}, false, 0, {}};
  case Phase::Revealed:
    return revealedKey(k);
  case Phase::Aided:
  case Phase::Unaided:
    return practiceKey(k);
  case Phase::Running:
    return runningKey(k, now);
  case Phase::Waiting:
    return waitingKey(k, now, scramble);
  }
  return {};
}

Step DrillFlow::runningKey(const KeyEvent &k, std::int64_t now) {
  if (k.press && k.key == Key::Escape) {
    return used(Action::Cancel);
  }
  if (k.press && isGiveUp(k)) {
    return used(Action::GiveUp);
  }
  if (config_.virtualCube) {
    // Space stops the timer; the turning keys turn the cube.
    Step s{k.key == Key::Space || k.move.has_value(),
           Action::None,
           {},
           false,
           0,
           {}};
    if (k.press && k.key == Key::Space) {
      if (faceTurns_ == 0) {
        // Nothing was done: a stray Space, not an attempt.
        s.action = Action::CancelNoTurn;
        return s;
      }
      timer_.pressStop(now);
      if (auto r = timer_.takeResult()) {
        s.action = Action::Stopped;
        s.timeMs = r->timeMs;
      }
    } else if (k.press && k.move) {
      s.action = Action::Turn;
      s.move = k.move;
    }
    return s;
  }
  if (!k.press) {
    return {};
  }
  if (now - startMs_ < kMinPhysicalMs) {
    return used(); // still letting go of Space
  }
  timer_.pressStop(now);
  Step s = used();
  if (auto r = timer_.takeResult()) {
    s.action = Action::Stopped;
    s.timeMs = r->timeMs;
  }
  return s;
}

Step DrillFlow::waitingKey(const KeyEvent &k, std::int64_t now,
                           Scramble scramble) {
  using S = SolveTimer::State;
  const bool virt = config_.virtualCube;
  if (k.key == Key::Escape) {
    if (!k.press) {
      return used();
    }
    cancelTimer();
    return used(Action::ResetWaiting);
  }
  if (isGiveUp(k)) {
    return used(k.press ? Action::GiveUp : Action::None);
  }
  if (k.key == Key::Backspace) {
    return k.press ? hint(Hint::GiveUpKey) : used();
  }
  if (k.move) {
    if (!k.press) {
      return used();
    }
    Step s = used();
    s.move = k.move;
    if (isRotation(k.move->type)) {
      // Before the solve the cube can only be turned as a whole.
      s.action = Action::Rotate;
    } else if (virt && (armed_ || timer_.state() == S::Ready)) {
      // The first face turn starts the timer (also while Space is held
      // once it is ready), and counts.
      start(now);
      s.action = Action::Start;
    } else if (timer_.state() == S::Holding) {
      return hint(Hint::KeepHolding);
    } else {
      return hint(Hint::OnlyRotations);
    }
    return s;
  }
  if (k.key != Key::Space) {
    return {};
  }
  if (!virt && scramble == Scramble::Awaited) {
    return used(); // wait for the scramble
  }
  if (virt && config_.tapToArm) {
    if (k.press) {
      if (armed_) {
        cancelTimer();
      } else {
        timer_.arm(now);
        armed_ = true;
      }
      return used(Action::Armed);
    }
    return used();
  }
  if (k.press) {
    timer_.pressStart(now);
  } else {
    timer_.releaseStart(now);
    if (timer_.state() == S::Running) {
      start(now);
      return used(Action::Start);
    }
  }
  return used(Action::TimerShown);
}

Step DrillFlow::revealedKey(const KeyEvent &k) {
  if (!k.press) {
    return {k.key == Key::Space || k.move.has_value(),
            Action::None,
            {},
            false,
            0,
            {}};
  }
  if (k.key == Key::Delete) {
    return used(Action::Delete);
  }
  if (k.key == Key::Undo) {
    return used(Action::Restore);
  }
  if (k.key == Key::Space && k.shift) {
    // With Shift+Space giving up, Shift is still held from that: the
    // result stays up. Else the practice after a DNF is skipped.
    return used(config_.shiftSpaceGivesUp ? Action::None : Action::NextCase);
  }
  if (k.key == Key::Space || k.key == Key::Enter) {
    if (k.key == Key::Space && failed_ && config_.practiseAfterDnf) {
      Step s = used(Action::Practise);
      s.aided = true;
      return s;
    }
    return used(Action::NextCase);
  }
  if (k.key == Key::Tab) {
    Step s = used(Action::Practise);
    s.aided = true;
    return s;
  }
  if (k.key == Key::Backspace) {
    // A case given up has no time to go back to.
    return used(gaveUp_ ? Action::None : Action::ToggleWrong);
  }
  if (k.key == Key::Escape) {
    return used(Action::Retry);
  }
  // The cube stays as the solve left it.
  return {k.move.has_value(), Action::None, {}, false, 0, {}};
}

Step DrillFlow::practiceKey(const KeyEvent &k) {
  const bool aided = phase_ == Phase::Aided;
  const bool handled = k.key == Key::Space || k.key == Key::Enter ||
                       k.key == Key::Tab || k.key == Key::Backspace ||
                       k.key == Key::Escape || k.key == Key::Delete ||
                       k.move.has_value();
  if (!k.press) {
    return {handled, Action::None, {}, false, 0, {}};
  }
  if (k.key == Key::Space || k.key == Key::Enter) {
    return used(Action::NextCase);
  }
  if (k.key == Key::Tab) {
    Step s = used(Action::Practise);
    s.aided = !aided;
    return s;
  }
  if (k.key == Key::Backspace) {
    // The DNF stays open to change.
    return used(gaveUp_ ? Action::None : Action::ToggleWrong);
  }
  if (k.key == Key::Delete) {
    return used(Action::Delete);
  }
  if (k.key == Key::Undo) {
    return used(Action::Restore);
  }
  if (k.key == Key::Escape) {
    Step s = used(config_.virtualCube ? Action::PracticeReset : Action::None);
    s.aided = aided;
    return s;
  }
  Step s{handled, Action::None, {}, false, 0, {}};
  if (k.move && config_.virtualCube) {
    s.action = Action::PracticeTurn;
    s.move = k.move;
  }
  return s;
}

} // namespace vcube
