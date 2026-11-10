#include "train/blind_flow.hpp"

#include <algorithm>
#include <random>
#include <utility>

namespace vcube {

// --- letters
// -------------------------------------------------------------------

std::vector<LetterChoice> letterPool(bool edges, bool corners, bool setups) {
  std::vector<LetterChoice> pool;
  for (BldPiece kind : {BldPiece::Edge, BldPiece::Corner}) {
    if ((kind == BldPiece::Edge && !edges) ||
        (kind == BldPiece::Corner && !corners)) {
      continue;
    }
    for (int i = 0; i < kBldStickers; ++i) {
      // The swap spot needs no setup, and the buffer can't be a target.
      if (setups && (!op::isTarget(kind, i) || i == op::swapSpot(kind))) {
        continue;
      }
      pool.push_back({kind, i});
    }
  }
  return pool;
}

std::size_t pickLetter(std::span<const LetterChoice> pool,
                       std::span<const LetterRecord> records,
                       const std::optional<LetterChoice> &last, Rng &rng) {
  // Slow letters are compared with the mean over all letters.
  double total = 0;
  int timed = 0;
  for (const LetterRecord &r : records) {
    if (r.meanMs) {
      total += *r.meanMs;
      ++timed;
    }
  }
  double overall = timed ? total / timed : 0.0;
  std::vector<double> weights;
  for (std::size_t i = 0; i < pool.size(); ++i) {
    const LetterRecord &r = records[i];
    double w = 1.0;
    if (r.attempts == 0) {
      w = 3.0; // never asked yet
    } else {
      w += 4.0 * (1.0 - r.accuracy.value_or(0.0));
      if (r.meanMs && overall > 0) {
        w += 2.0 * std::clamp(*r.meanMs / overall - 1.0, 0.0, 3.0);
      }
    }
    if (last && *last == pool[i] && pool.size() > 1) {
      w = 0.0; // never the same letter twice in a row
    }
    weights.push_back(w);
  }
  std::discrete_distribution<std::size_t> pick(weights.begin(), weights.end());
  return pick(rng);
}

FaceletCube numberedFacelets() {
  FaceletCube::Stickers ids{};
  for (int i = 0; i < kFaceletCount; ++i) {
    ids[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
  }
  return FaceletCube(ids);
}

int whereIs(const FaceletCube &ids, int facelet) {
  for (int i = 0; i < kFaceletCount; ++i) {
    if (ids[i] == facelet) {
      return i;
    }
  }
  return facelet;
}

// --- AwayClock
// -------------------------------------------------------------------

std::optional<std::int64_t> AwayClock::back(std::int64_t now) {
  if (!at_) {
    return std::nullopt;
  }
  std::int64_t away = now - *at_;
  at_.reset();
  return away;
}

// --- LetterFlow
// ------------------------------------------------------------------

void LetterFlow::show(LetterChoice c, bool toLetter, std::int64_t now,
                      bool away) {
  LetterItem item;
  item.kind = c.kind;
  item.sticker = c.sticker;
  item.toLetter = toLetter;
  item.shownAt = now;
  item_ = item;
  if (away) {
    away_.leave(now); // shown while away: its clock starts on return
  }
}

void LetterFlow::reset() {
  item_.reset();
  away_.reset();
}

std::optional<LetterAnswer> LetterFlow::finish(bool right, std::int64_t now) {
  if (!open()) {
    return std::nullopt;
  }
  item_->answered = true;
  item_->right = right;
  return LetterAnswer{right, now - item_->shownAt};
}

void LetterFlow::pause(std::int64_t now) {
  if (open()) {
    away_.leave(now);
  }
}

void LetterFlow::resume(std::int64_t now) {
  std::optional<std::int64_t> away = away_.back(now);
  if (away && item_) {
    // A letter shown while away (e.g. moved on to by itself) starts now.
    item_->shownAt = std::min(now, item_->shownAt + *away);
  }
}

// --- QuizFlow
// --------------------------------------------------------------------

namespace {

// Drops the last character (UTF-8) of `s`.
void dropLast(std::string &s) {
  while (!s.empty()) {
    auto c = static_cast<unsigned char>(s.back());
    s.pop_back();
    if ((c & 0xC0) != 0x80) {
      return;
    }
  }
}

} // namespace

void QuizFlow::setFold(std::function<std::string(std::string_view)> fold) {
  fold_ = std::move(fold);
}

void QuizFlow::show(LetterChoice c, bool toLetter, std::int64_t now,
                    bool away) {
  LetterFlow::show(c, toLetter, now, away);
  typed_.clear();
}

void QuizFlow::reset() {
  LetterFlow::reset();
  typed_.clear();
}

QuizFlow::Effects QuizFlow::answer(bool right, std::int64_t now) {
  Effects e;
  e.used = true;
  e.answer = finish(right, now);
  e.autoNext = e.answer && right; // or Space now
  return e;
}

QuizFlow::Effects QuizFlow::key(Key k, std::string_view text, bool press,
                                bool repeat, std::int64_t now,
                                const LetterScheme &letters) {
  Effects e;
  if (k == Key::Other) {
    return e;
  }
  e.used = true;
  if (!press || repeat) {
    return e;
  }
  if (!item_) {
    e.next = true;
    return e;
  }
  if (item_->answered) {
    e.next = k == Key::Space || k == Key::Enter;
    return e;
  }
  if (k == Key::Space) {
    if (now < graceUntil_) {
      return e; // meant for the letter before, which moved on
    }
    e = answer(false, now);
    e.givenUp = true;
    return e;
  }
  if (!item_->toLetter) {
    return e; // a click answers
  }
  auto fold = [this](std::string_view s) {
    return fold_ ? fold_(s) : foldLetter(s);
  };
  auto typedAnswer = [&] {
    return answer(
        fold(typed_) == fold(letters.letter(item_->kind, item_->sticker)), now);
  };
  if (k == Key::Backspace) {
    dropLast(typed_);
  } else if (k == Key::Enter) {
    return typed_.empty() ? e : typedAnswer();
  } else {
    typed_ += text;
  }
  // A letter that only one sticker's letter starts with, and is all of
  // it, answers at once; one that no letter starts with is wrong.
  int starts = 0;
  bool whole = false;
  std::string mine = fold(typed_);
  for (int i = 0; i < kBldStickers; ++i) {
    std::string l = fold(letters.letter(item_->kind, i));
    if (!typed_.empty() && l.starts_with(mine)) {
      ++starts;
      whole = whole || l == mine;
    }
  }
  if (!typed_.empty() && (starts == 0 || (starts == 1 && whole))) {
    return typedAnswer();
  }
  e.showTyped = true;
  return e;
}

QuizFlow::Effects QuizFlow::click(std::optional<int> sticker,
                                  std::int64_t now) {
  if (!open() || item_->toLetter) {
    return {};
  }
  return answer(sticker && *sticker == item_->sticker, now);
}

// --- SetupFlow
// -------------------------------------------------------------------

void SetupFlow::show(LetterChoice c, std::int64_t now, bool away) {
  LetterFlow::show(c, true, now, away);
}

void SetupFlow::reset() {
  LetterFlow::reset();
  resetCube();
}

void SetupFlow::resetCube() {
  cube_ = FaceletCube();
  ids_ = numberedFacelets();
  moves_.clear();
}

int SetupFlow::target() const {
  if (!item_) {
    return 0;
  }
  return whereIs(ids_, bldFacelet(item_->kind, item_->sticker));
}

SetupFlow::Effects SetupFlow::key(Key k, std::optional<Move> move, bool press,
                                  bool repeat, std::int64_t now) {
  Effects e;
  if (k == Key::Other || (k == Key::Move && !move)) {
    return e;
  }
  e.used = true;
  if (!press || repeat) {
    return e;
  }
  if (!item_) {
    e.next = true;
    return e;
  }
  if (item_->answered) {
    if (k == Key::Space || k == Key::Enter) {
      e.next = true;
    } else if (k == Key::Escape) {
      // The same letter again.
      item_->answered = false;
      item_->shownAt = now;
      e.again = true;
    }
    return e;
  }
  auto skip = [&] {
    Effects s;
    s.used = true;
    s.answer = finish(false, now);
    s.skipped = true;
    return s;
  };
  if (typed_) {
    // Typed: the keys belong to the field; one pressed outside it goes
    // back to it, and Esc there gives the letter up.
    if (k == Key::Escape) {
      return skip();
    }
    e.focusField = k == Key::Space || k == Key::Enter;
    return e;
  }
  if (k == Key::Space) {
    return skip();
  }
  if (k == Key::Escape) {
    resetCube();
    e.resetCube = true;
    return e;
  }
  if (k == Key::Move) {
    return turn(*move, now);
  }
  return e;
}

SetupFlow::Effects SetupFlow::turn(Move m, std::int64_t now) {
  Effects e;
  e.used = true;
  if (!open()) {
    return e;
  }
  if (isRotation(m.type)) {
    e.onlyLayers = true;
    return e;
  }
  cube_.apply(m);
  ids_.apply(m);
  moves_.push_back(m);
  e.turned = m;
  if (op::isValidSetup(item_->kind, item_->sticker, moves_)) {
    e.answer = finish(true, now);
    return e;
  }
  int swap = bldFacelet(item_->kind, op::swapSpot(item_->kind));
  e.notValid = target() == swap;
  return e;
}

std::optional<LetterAnswer> SetupFlow::typedSetup(const Alg &alg,
                                                  std::int64_t now) {
  if (!open()) {
    return std::nullopt;
  }
  // Shows where the typed setup takes the sticker.
  cube_ = FaceletCube();
  ids_ = numberedFacelets();
  cube_.apply(alg);
  ids_.apply(alg);
  moves_ = alg;
  return finish(op::isValidSetup(item_->kind, item_->sticker, alg), now);
}

// --- TraceFlow
// -------------------------------------------------------------------

TraceFlow::Effects TraceFlow::start(bool scrambleReady, std::int64_t now) {
  Effects e;
  e.used = true;
  if (!scrambleReady) {
    waiting_ = true;
    e.waiting = true;
    return e;
  }
  waiting_ = false;
  phase_ = Phase::Memo;
  startMs_ = now;
  e.start = true;
  return e;
}

TraceFlow::Effects TraceFlow::key(Key k, bool rotation, bool press, bool repeat,
                                  bool scrambleReady, std::int64_t now) {
  Effects e;
  if (k == Key::Other) {
    return e;
  }
  e.used = true;
  if (!press || repeat) {
    return e;
  }
  if (phase_ == Phase::Ready || phase_ == Phase::Done) {
    if (k == Key::Space) {
      return start(scrambleReady, now);
    }
    e.look = k == Key::Move && phase_ == Phase::Done;
    return e;
  }
  // Tracing.
  if (k == Key::Escape) {
    phase_ = Phase::Ready;
    e.giveUp = true;
  } else if (k == Key::Enter || k == Key::Tab) {
    e.focusFields = true;
  } else if (k == Key::Move) {
    e.rotate = rotation;
    e.onlyRotations = !rotation;
  }
  return e;
}

TraceFlow::Effects TraceFlow::scrambleReady(std::int64_t now) {
  if (!waiting_) {
    return {};
  }
  return start(true, now);
}

std::optional<std::int64_t> TraceFlow::finish(std::int64_t now) {
  if (phase_ != Phase::Memo) {
    return std::nullopt;
  }
  phase_ = Phase::Done;
  return now - startMs_;
}

void TraceFlow::reset() {
  phase_ = Phase::Ready;
  waiting_ = false;
  away_.reset();
}

void TraceFlow::pause(std::int64_t now) {
  if (phase_ == Phase::Memo) {
    away_.leave(now);
  }
}

void TraceFlow::resume(std::int64_t now) {
  std::optional<std::int64_t> away = away_.back(now);
  if (away && phase_ == Phase::Memo) {
    startMs_ += *away;
  }
}

// --- BlindSolveFlow
// ----------------------------------------------------------------

BlindSolveFlow::Effects BlindSolveFlow::space(std::int64_t now,
                                              const Context &c) {
  Effects e;
  e.used = true;
  switch (phase_) {
  case Phase::Ready:
    if (!c.scrambleReady) {
      e.waitForScramble = true;
      return e;
    }
    phase_ = Phase::Memo;
    startMs_ = now;
    memoMs_ = 0;
    justRecorded_.reset();
    e.startMemo = true;
    return e;
  case Phase::Memo:
    memoMs_ = now - startMs_;
    phase_ = Phase::Executing;
    e.startExecution = true;
    return e;
  case Phase::Executing:
    phase_ = Phase::Done;
    e.stop = Stop{now - startMs_, memoMs_};
    return e;
  case Phase::Done:
    phase_ = Phase::Ready;
    justRecorded_.reset();
    e.next = true;
    return e;
  }
  return e;
}

BlindSolveFlow::Effects BlindSolveFlow::key(Key k, bool press, bool repeat,
                                            std::int64_t now,
                                            const Context &c) {
  Effects e;
  if (!virtual_) {
    if (phase_ == Phase::Executing) {
      // Any key stops the timer, except Esc, which cancels.
      e.used = true;
      if (!press || repeat) {
        return e;
      }
      return k == Key::Escape ? cancel() : space(now, c);
    }
    if (k != Key::Space && k != Key::Escape && k != Key::Backspace) {
      return e;
    }
    e.used = true;
    if (!press || repeat) {
      return e;
    }
    if (k == Key::Escape) {
      return phase_ == Phase::Memo ? cancel() : e;
    }
    if (k == Key::Backspace) {
      // Only the solve just recorded, while it is still the last one.
      e.toggleDnf = phase_ == Phase::Done && justRecorded_ &&
                    justRecorded_->session == c.session &&
                    c.lastTimestamp == justRecorded_->timestamp;
      return e;
    }
    return space(now, c);
  }
  if (k != Key::Move && k != Key::Space && k != Key::Escape) {
    return e;
  }
  e.used = true;
  if (!press || repeat) {
    return e;
  }
  if (k == Key::Escape) {
    return isBusy() ? cancel() : e;
  }
  if (k == Key::Space) {
    return space(now, c);
  }
  e.turn = isBusy();
  e.look = phase_ == Phase::Done;
  return e;
}

BlindSolveFlow::Effects BlindSolveFlow::cancel() {
  Effects e;
  e.used = true;
  phase_ = Phase::Ready;
  e.cancel = true;
  return e;
}

void BlindSolveFlow::reset() {
  phase_ = Phase::Ready;
  justRecorded_.reset();
}

} // namespace vcube
