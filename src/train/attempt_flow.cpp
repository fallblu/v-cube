#include "train/attempt_flow.hpp"

#include <algorithm>

namespace vcube {

namespace {

bool isActionKey(AttemptFlow::Key k) {
  using K = AttemptFlow::Key;
  return k == K::Dnf || k == K::Comment || k == K::Review || k == K::Undo ||
         k == K::Skip || k == K::Delete;
}

} // namespace

void AttemptFlow::setPhase(Phase p, Effects &e) {
  cancelHold(e);
  phase_ = p;
  e.phase = true;
}

void AttemptFlow::cancelHold(Effects &e) {
  if (hold_ == Hold::None) {
    return;
  }
  hold_ = Hold::None;
  e.showHold = true;
}

void AttemptFlow::pressStart(std::int64_t now, Scramble scramble, Effects &e) {
  if (scramble == Scramble::Awaited && !retry_) {
    e.notice = Notice::WaitToStart;
    return;
  }
  hold_ = Hold::Holding;
  holdStart_ = now;
  e.holdStarted = true;
  e.showHold = true;
}

void AttemptFlow::releaseStart(Scramble scramble, Effects &e) {
  Hold was = hold_;
  cancelHold(e);
  if (was != Hold::Ready) {
    return;
  }
  if (scramble == Scramble::Awaited && !retry_) {
    e.notice = Notice::WaitToStart;
    return;
  }
  e.startSolve = true;
  setPhase(Phase::Solving, e);
  e.entryChanged = true;
}

AttemptFlow::Effects AttemptFlow::key(const KeyEvent &k, std::int64_t now,
                                      Scramble scramble) {
  Effects e;
  if (isActionKey(k.key)) {
    e.used = true;
    if (!k.press || k.autoRepeat || isBusy() || isHolding()) {
      return e;
    }
    switch (k.key) {
    case Key::Dnf:
      if (config_.moveCount && !config_.virtualCube) {
        recordTyped(Typed::Dnf, scramble, e);
      }
      break;
    case Key::Comment:
      e.action = Action::Comment;
      break;
    case Key::Review:
      e.action = Action::Review;
      break;
    case Key::Undo:
      e.action = Action::Undo;
      break;
    case Key::Skip:
      skip(e);
      break;
    default:
      e.action = Action::Delete;
      break;
    }
    return e;
  }

  // A key that turns the virtual cube does that, whatever else it is.
  const Key key = k.move ? Key::Other : k.key;
  if (config_.virtualCube) {
    bool known = k.move || key == Key::Space || key == Key::Escape ||
                 (key == Key::Tab && !config_.moveCount) ||
                 (key == Key::Enter && phase_ == Phase::Done);
    if (!known) {
      return e;
    }
    e.used = true;
    if (k.autoRepeat) {
      return e;
    }
    if (!k.press) {
      if (key == Key::Space) {
        releaseStart(scramble, e);
      }
      return e;
    }
    if (isHolding()) {
      // Holding Space to start: Esc lets go, other keys wait.
      if (key == Key::Escape) {
        cancelHold(e);
      }
      return e;
    }
    switch (key) {
    case Key::Other: // a turning key
      e.turn = true;
      break;
    case Key::Tab:
      e.toggleAlgs = true;
      break;
    case Key::Space:
    case Key::Enter:
      if (phase_ == Phase::Inspect) {
        pressStart(now, scramble, e);
      } else if (phase_ == Phase::Solving) {
        e.endSolve = true;
      } else {
        next(e);
      }
      break;
    default: // Esc
      if (phase_ == Phase::Done) {
        e.solvedCube = true;
      } else {
        e.restartCube = true;
        setPhase(Phase::Inspect, e);
        e.entryChanged = true;
      }
      break;
    }
    return e;
  }

  if (!config_.moveCount) {
    // Untimed on a physical cube: scrambles only.
    if (key == Key::Tab && k.press) {
      e.used = true;
      e.toggleAlgs = true;
      return e;
    }
    bool forward = key == Key::Space || key == Key::Right;
    bool back = key == Key::Backspace || key == Key::Left;
    if (!forward && !back) {
      return e;
    }
    e.used = true;
    if (k.press && !k.autoRepeat) {
      if (forward) {
        next(e);
      } else {
        e.back = true;
      }
    }
    return e;
  }

  // Move Count on a physical cube: the count is typed in.
  bool known = key == Key::Digit || key == Key::Backspace ||
               key == Key::Enter || key == Key::Escape;
  if (!known || k.modified) {
    return e;
  }
  e.used = true;
  if (!k.press) {
    return e;
  }
  switch (key) {
  case Key::Digit:
    if (!k.autoRepeat && entry_.add(k.digit)) {
      e.entryChanged = true;
    }
    break;
  case Key::Backspace:
    entry_.backspace();
    e.entryChanged = true;
    break;
  case Key::Escape:
    entry_.clear();
    e.entryChanged = true;
    break;
  default: // Enter
    if (!k.autoRepeat) {
      recordTyped(Typed::Count, scramble, e);
    }
    break;
  }
  return e;
}

AttemptFlow::Effects AttemptFlow::tick(std::int64_t now, bool popupOpen) {
  Effects e;
  if (isHolding() && popupOpen) {
    cancelHold(e); // the Space release goes to the popup
  } else if (hold_ == Hold::Holding && now - holdStart_ >= config_.holdMs) {
    hold_ = Hold::Ready;
    e.showHold = true;
  }
  return e;
}

std::int64_t AttemptFlow::holdLeft(std::int64_t now) const {
  return std::max<std::int64_t>(0, config_.holdMs - (now - holdStart_));
}

AttemptFlow::Effects AttemptFlow::dropHold() {
  Effects e;
  cancelHold(e);
  return e;
}

AttemptFlow::Effects AttemptFlow::turned(bool solved) {
  Effects e;
  // Untimed can end the solve as soon as the cube is solved.
  if (phase_ == Phase::Solving && !config_.moveCount && config_.autoStop &&
      solved) {
    e.endSolve = true;
  }
  return e;
}

AttemptFlow::Effects AttemptFlow::solveEnded(bool reached, int count,
                                             const std::string &solution) {
  Effects e;
  if (retry_) {
    // Compared with the attempt it repeats, and not recorded.
    retry_->result.moves = count;
    retry_->result.dnf = !reached;
    retry_->result.solution = solution;
    retry_->done = true;
    e.retryResult = true;
    setPhase(Phase::Done, e);
    e.entryChanged = true;
    return e;
  }
  e.record = Kept{count, !reached, solution};
  if (config_.moveCount) {
    setPhase(Phase::Inspect, e);
    next(e); // on to the next scramble
  } else {
    setPhase(Phase::Done, e);
    e.entryChanged = true;
    e.analyse = true;
  }
  e.notReached = !reached;
  return e;
}

void AttemptFlow::next(Effects &e) {
  if (retry_) {
    endRetry(e);
    return;
  }
  if (scramblesOnly()) {
    e.forward = true;
    entry_.clear();
    return;
  }
  entry_.clear();
  if (phase_ == Phase::Done) {
    setPhase(Phase::Inspect, e); // the scramble shown was solved
  }
  e.nextScramble = true;
}

void AttemptFlow::endRetry(Effects &e) {
  if (!queue_.empty()) {
    e.retryNext = queue_.front();
    queue_.pop_front();
    return;
  }
  bool feedSolved = retry_ && retry_->feedSolved;
  retry_.reset();
  retryCount_ = 0;
  setPhase(Phase::Inspect, e);
  if (feedSolved) {
    e.nextScramble = true; // the scramble shown before was solved already
  } else {
    e.showScramble = true; // the scramble that was shown before the retry
  }
  e.retryEnded = true;
}

void AttemptFlow::recordTyped(Typed typed, Scramble scramble, Effects &e) {
  const bool dnf = typed == Typed::Dnf;
  if (scramble == Scramble::Awaited) {
    e.notice = Notice::WaitToRecord;
    return;
  }
  if (!dnf && entry_.empty()) {
    e.notice = Notice::TypeFirst;
    return;
  }
  e.record = Kept{entry_.value(), dnf, {}};
  entry_.clear();
  next(e);
}

AttemptFlow::Effects AttemptFlow::recordTyped(Typed typed, Scramble scramble) {
  Effects e;
  recordTyped(typed, scramble, e);
  return e;
}

void AttemptFlow::skip(Effects &e) {
  if (isBusy()) {
    return;
  }
  if (retry_) {
    queue_.clear(); // back to new scrambles
    endRetry(e);
    return;
  }
  entry_.clear();
  e.historyToEnd = true;
  setPhase(Phase::Inspect, e);
  e.nextScramble = true;
}

AttemptFlow::Effects AttemptFlow::skip() {
  Effects e;
  skip(e);
  return e;
}

AttemptFlow::Effects AttemptFlow::scrambleShown() {
  Effects e;
  if (config_.virtualCube) {
    setPhase(Phase::Inspect, e);
  }
  return e;
}

AttemptFlow::Effects AttemptFlow::freshScramble() {
  Effects e;
  if (phase_ == Phase::Done) {
    // The scramble shown was solved already.
    setPhase(Phase::Inspect, e);
    e.nextScramble = true;
  } else {
    e.showScramble = true;
  }
  return e;
}

AttemptFlow::Effects AttemptFlow::retryOne(std::size_t index) {
  Effects e;
  queue_.clear();
  retryCount_ = 1;
  e.retryNext = index;
  return e;
}

AttemptFlow::Effects
AttemptFlow::retryQueue(const std::vector<std::size_t> &indices) {
  Effects e;
  if (indices.empty()) {
    return e;
  }
  queue_.assign(indices.begin(), indices.end());
  retryCount_ = queue_.size();
  e.retryNext = queue_.front();
  queue_.pop_front();
  return e;
}

AttemptFlow::Effects AttemptFlow::beginRetry(std::size_t index,
                                             const MoveCountAttempt &original) {
  Effects e;
  Retry r;
  r.index = index;
  r.feedSolved = retry_ ? retry_->feedSolved : phase_ == Phase::Done;
  r.original = original;
  retry_ = r;
  setPhase(Phase::Inspect, e);
  e.retryStarted = true;
  return e;
}

void AttemptFlow::attemptDeleted() {
  if (retry_) {
    queue_.clear(); // the attempts after it have moved up one
  }
}

void AttemptFlow::resetRetry() {
  retry_.reset();
  queue_.clear();
  retryCount_ = 0;
}

} // namespace vcube
