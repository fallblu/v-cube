#include "train/learn_session.hpp"

#include <algorithm>
#include <iterator>

namespace vcube {

namespace {

using Step = LearnSession::Step;
using Action = LearnSession::Action;
using Key = LearnSession::Key;

Step used(Action a = Action::None) {
  Step s;
  s.handled = true;
  s.action = a;
  return s;
}

bool isNext(Key k) { return k == Key::Space || k == Key::Enter; }

bool isLearn(const Attempt &a) { return a.mode == AttemptMode::Learn; }

std::vector<Attempt> learnAttemptsOf(const CaseProgress *cp) {
  std::vector<Attempt> out;
  if (cp) {
    std::copy_if(cp->attempts.begin(), cp->attempts.end(),
                 std::back_inserter(out), isLearn);
  }
  return out;
}

const CaseProgress *findCard(const SetProgress &progress,
                             const std::string &id) {
  auto it = progress.find(id);
  return it == progress.end() ? nullptr : &it->second;
}

} // namespace

Step LearnSession::key(const KeyEvent &k, std::int64_t now) const {
  if (k.key == Key::Undo) {
    return used(k.press ? Action::Undo : Action::None);
  }
  if (!k.press) {
    return {k.key == Key::Space, Action::None, {}, 0, {}, false, 0};
  }
  const bool next = isNext(k.key);
  // Recognition and the quiz: the cube only turns as a whole (and U and
  // D), to look at the case from another side.
  auto look = [&k] {
    Step s;
    s.handled = k.move.has_value();
    if (k.move) {
      s.action = Action::Look;
      s.move = k.move;
    }
    return s;
  };
  auto grade = [&k]() -> std::optional<Grade> {
    if (k.key == Key::Digit && k.digit >= 1 && k.digit <= 4) {
      return static_cast<Grade>(k.digit);
    }
    return std::nullopt;
  };
  switch (phase_) {
  case Phase::NoCase:
    return next ? used(Action::NextEarly) : Step{};
  case Phase::Context:
    if (k.key == Key::Tab) {
      return used(Action::ContextHint);
    }
    if (k.key == Key::Space) {
      return used(k.shift ? Action::ContextGiveUp : Action::None);
    }
    if (k.key == Key::Escape) {
      return used(Action::ContextReset);
    }
    if (k.key == Key::Backspace) {
      return used(Action::ContextLeave);
    }
    break;
  case Phase::ContextDone:
    if (next) {
      return used(Action::ContextNext);
    }
    if (k.key == Key::Escape) {
      return used(Action::ContextReset);
    }
    if (k.key == Key::Backspace) {
      return used(Action::ContextLeave);
    }
    break;
  case Phase::Demo:
    if (k.key == Key::Right || k.key == Key::Left) {
      Step s = used(Action::DemoStep);
      s.step = k.key == Key::Right ? 1 : -1;
      return s;
    }
    if (k.key == Key::Home || k.key == Key::Escape) {
      return used(Action::DemoRestart);
    }
    if (next) {
      return used(Action::StartFollowing);
    }
    // The turning keys wait for the try, so that the demo stays in step.
    return {k.move.has_value() || k.key == Key::Tab,
            Action::None,
            {},
            0,
            {},
            false,
            0};
  case Phase::Solving:
    if (k.key == Key::Tab) {
      return used(Action::Hint);
    }
    if (k.key == Key::Space) {
      // Space alone goes on to the next case once this one is done, so a
      // double tap mustn't give up the next one too.
      return used(k.shift ? Action::GiveUp : Action::None);
    }
    if (k.key == Key::Escape) {
      return used(Action::ResetCase);
    }
    break;
  case Phase::Done:
    if (k.key == Key::Space && k.shift) {
      return used(); // Shift still held from giving up: keep the reveal up
    }
    if (next) {
      return used(Action::NextCase);
    }
    if (auto g = grade()) {
      Step s = used(Action::Regrade);
      s.grade = g;
      return s;
    }
    if (k.key == Key::Escape) {
      return used(Action::ResetCube);
    }
    break;
  case Phase::Recognise:
    if (k.key == Key::Space && now < guardUntil_) {
      return used(); // a double tap's second press, meant for the case before
    }
    if (k.key == Key::Space) {
      Step s = used(Action::Recalled);
      s.knew = !k.shift;
      return s;
    }
    if (k.key == Key::Escape) {
      return used(Action::ResetCube);
    }
    return look();
  case Phase::Recalled:
    if (auto g = grade()) {
      Step s = used(Action::GradeRecognition);
      s.grade = g;
      return s;
    }
    if (next) {
      return used(k.shift ? Action::None : Action::GradeRecognition);
    }
    if (k.key == Key::Escape) {
      return used(Action::ResetCube);
    }
    return look();
  case Phase::Quiz:
    if (k.key == Key::Digit && k.digit >= 1 &&
        static_cast<std::size_t>(k.digit) <= quizChoices_) {
      Step s = used(Action::Answer);
      s.choice = static_cast<std::size_t>(k.digit - 1);
      return s;
    }
    if (k.key == Key::Escape) {
      return used(Action::ResetCube);
    }
    return look();
  case Phase::QuizDone:
    if (next) {
      return used(Action::NextQuiz);
    }
    return look();
  }
  // Elsewhere the turning keys turn the cube.
  if (k.move) {
    Step s = used(Action::Turn);
    s.move = k.move;
    return s;
  }
  return {};
}

void LearnSession::answered(bool right, std::int64_t ms) {
  ++quiz_.asked;
  quiz_.right += right ? 1 : 0;
  quiz_.totalMs += ms;
}

void LearnUndoStack::push(LearnUndo entry, bool keepOpen) {
  entries_.push_back(std::move(entry));
  while (entries_.size() > kDepth) {
    entries_.pop_front();
  }
  open_ = keepOpen;
}

LearnUndo *LearnUndoStack::open() {
  return open_ && !entries_.empty() ? &entries_.back() : nullptr;
}

std::optional<LearnUndo> LearnUndoStack::pop() {
  open_ = false;
  if (entries_.empty()) {
    return std::nullopt;
  }
  LearnUndo u = std::move(entries_.back());
  entries_.pop_back();
  return u;
}

void takeBack(SetProgress &progress, const LearnUndo &undo) {
  for (auto c = undo.cards.rbegin(); c != undo.cards.rend(); ++c) {
    CaseProgress &cp = progress[c->cardId];
    cp.card = c->before;
    if (c->learnAttempts) {
      std::erase_if(cp.attempts, isLearn);
      cp.attempts.insert(cp.attempts.end(), c->learnAttempts->begin(),
                         c->learnAttempts->end());
      std::stable_sort(cp.attempts.begin(), cp.attempts.end(),
                       [](const Attempt &a, const Attempt &b) {
                         return a.timestamp < b.timestamp;
                       });
    }
    if (c->attempt != 0) {
      for (auto a = cp.attempts.rbegin(); a != cp.attempts.rend(); ++a) {
        if (a->mode == AttemptMode::Learn && a->timestamp == c->attempt) {
          cp.attempts.erase(std::next(a).base());
          break;
        }
      }
    }
  }
}

LearnUndo listChange(const SetProgress &progress, const std::string &setId,
                     const std::vector<std::string> &cardIds) {
  LearnUndo u;
  u.setId = setId;
  u.fromList = true;
  for (const std::string &id : cardIds) {
    const CaseProgress *cp = findCard(progress, id);
    u.cards.push_back({id, cp ? cp->card : SrsCard{}, 0, learnAttemptsOf(cp)});
  }
  return u;
}

bool keepChanged(LearnUndo &undo, const SetProgress &progress) {
  std::erase_if(undo.cards, [&progress](const LearnUndo::Card &c) {
    const CaseProgress *cp = findCard(progress, c.cardId);
    return (cp ? cp->card : SrsCard{}) == c.before &&
           learnAttemptsOf(cp) == c.learnAttempts;
  });
  return !undo.cards.empty();
}

} // namespace vcube
