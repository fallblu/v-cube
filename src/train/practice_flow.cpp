#include "train/practice_flow.hpp"

namespace vcube {

namespace {

using Step = PracticeFlow::Step;
using Action = PracticeFlow::Action;
using Phase = PracticePhase;
using Key = TrainKey;

Step used(Action a = Action::None) { return {true, a}; }

Step chooseKey(const TrainKeyEvent &k, const PracticeFlow::State &s) {
  switch (k.key) {
  case Key::Enter:
    return used(Action::ConfirmChoice);
  case Key::Escape:
    // Only a change after a try can be left: a new case needs them.
    return used(s.choosingAgain ? Action::CancelChoice : Action::None);
  case Key::Space:
  case Key::Tab:
  case Key::Backspace:
    return used();
  default:
    break;
  }
  if (k.key == Key::Digit && k.digit >= 1 && k.digit <= 9) {
    Step st = used(Action::ChoiceKey);
    st.digit = k.digit;
    return st;
  }
  if (k.move) {
    return used(Action::ChooseFirst);
  }
  return {};
}

} // namespace

Step PracticeFlow::key(const TrainKeyEvent &k, const State &s) {
  if (!k.press) {
    return {};
  }
  if (k.key == Key::Undo) {
    return s.tab == Tab::Learn ? used(Action::Undo) : Step{};
  }
  if (s.phase == Phase::Idle) {
    return k.key == Key::Space || k.key == Key::Enter ? used(Action::NextEarly)
                                                      : Step{};
  }
  if (s.phase == Phase::Ready) {
    return k.key == Key::Space ? used(Action::Start) : Step{};
  }
  if (s.phase == Phase::Choose) {
    return chooseKey(k, s);
  }
  const bool done = s.phase == Phase::Done;
  const bool solving = s.phase == Phase::Solving;
  switch (k.key) {
  case Key::Space:
    if (done) {
      return used(s.waiting ? Action::StillWorking : Action::Next);
    }
    if (s.phase == Phase::Demo) {
      return used(Action::Introduced);
    }
    if (s.hidden) {
      Step st = used(Action::Reveal);
      st.gaveUp = !s.done;
      return st;
    }
    // Space alone does nothing while solving, so that a double tap after
    // a try can't give up the next one.
    return used(k.shift && s.shiftSpaceGivesUp ? Action::GiveUp : Action::None);
  case Key::Tab:
    if (solving && s.hints && !s.hidden) {
      return used(Action::Hint);
    }
    return used(done ? Action::ToggleDetail : Action::None);
  case Key::Backspace:
    // Drill's give-up key, which is Shift+Space unless set otherwise: one
    // press of Backspace mustn't record a DNF that can't be taken back.
    if (!solving) {
      return used();
    }
    return used(s.shiftSpaceGivesUp ? Action::GiveUpKeyHint : Action::GiveUp);
  case Key::Delete:
    return used(done && !s.waiting ? Action::DeleteTry : Action::None);
  case Key::Escape:
    if (done) {
      return used(s.waiting ? Action::None : Action::Retry);
    }
    // Inspection: the clock again too, and not recorded.
    return used(s.tab == Tab::Inspection ? Action::Retry : Action::ResetCube);
  case Key::Enter:
    return used(s.phase == Phase::Demo ? Action::PlayDemo
                : done                 ? Action::PlayBest
                                       : Action::None);
  default:
    break;
  }
  if (done && !s.waiting && s.graded && k.key == Key::Digit && k.digit >= 1 &&
      k.digit <= 4) {
    Step st = used(Action::Regrade);
    st.grade = static_cast<Grade>(k.digit);
    return st;
  }
  if (done && s.chooser && k.key == Key::Digit && k.digit == 0) {
    return used(Action::ChooseAgain);
  }
  if (done && !s.waiting && !k.modified) {
    // Not handled unless the mode takes it or it turns the cube.
    Step st;
    st.action = Action::ModeKey;
    st.move = k.move;
    return st;
  }
  if (k.move) {
    Step st = used(Action::Turn);
    st.move = k.move;
    return st;
  }
  return {};
}

} // namespace vcube
