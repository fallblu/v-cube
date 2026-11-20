#include "app/timed_driver.hpp"
#include "app/ui_text.hpp"

#include "app/keymap.hpp"
#include "app/train_keys.hpp"
#include "app/widgets.hpp"

#include <QCoreApplication>

namespace vcube {

namespace {

constexpr char kTrContext[] = "vcube::TimedDriver";
constexpr auto tr = trIn<kTrContext>;

} // namespace

void TimedDriver::configure(bool virtualCube, bool practiseAfterDnf) {
  flow_.setConfig({virtualCube, drill_.tapToArm, drill_.autoStop,
                   drill_.shiftSpaceGivesUp, practiseAfterDnf, drill_.holdMs});
}

bool TimedDriver::key(const QKeyEvent *e, bool press, const Keymap &keymap,
                      Scramble scramble) {
  TrainKeyEvent k = trainKeyEvent(e, press, KeyChords::Undo);
  if (flow_.config().virtualCube) {
    k.move = keymap.lookup(e);
  }
  return key(k, monotonicMs(), scramble);
}

bool TimedDriver::key(const TrainKeyEvent &k, std::int64_t now,
                      Scramble scramble) {
  const DrillFlow::Step step = flow_.key(k, now, scramble);
  using A = DrillFlow::Action;
  const bool virt = flow_.config().virtualCube;
  switch (step.action) {
  case A::None:
    break;
  case A::NextFromIdle:
  case A::NextCase:
    host_.timedNext(step.action == A::NextFromIdle);
    break;
  case A::Retry:
    host_.timedRetry();
    break;
  case A::ResetWaiting:
    display_->showTime(0);
    if (virt) {
      host_.timedResetCube();
    }
    host_.timedShowKeys();
    break;
  case A::Rotate:
    host_.timedRotate(*step.move);
    break;
  case A::Start:
    host_.timedStarted(now);
    host_.timedShowKeys();
    if (step.move) {
      turn(*step.move, now); // the turn that started it counts
    } else {
      display_->showTimer(flow_.timer(), now);
    }
    break;
  case A::Armed:
    display_->showTimer(flow_.timer(), now);
    host_.timedShowKeys();
    break;
  case A::TimerShown:
    display_->showTimer(flow_.timer(), now);
    break;
  case A::Turn:
    turn(*step.move, now);
    break;
  case A::Stopped:
    // Virtual: a DNF unless the cube shows the step done.
    host_.timedFinishAnimations();
    host_.timedFinish(step.timeMs, !virt || host_.timedStepDone()
                                       ? DrillFlow::Result::Solved
                                       : DrillFlow::Result::Failed);
    break;
  case A::CancelNoTurn:
    cancel(cancelledNoTurnText());
    break;
  case A::Cancel:
    cancel(cancelledText());
    break;
  case A::GiveUp: {
    const std::int64_t ms = flow_.giveUp(now);
    host_.timedFinishAnimations();
    host_.timedFinish(ms, DrillFlow::Result::GaveUp);
    break;
  }
  case A::Practise:
  case A::PracticeReset:
    host_.timedPractise(step.aided);
    break;
  case A::PracticeTurn:
    host_.timedPracticeTurn(*step.move);
    break;
  case A::ToggleWrong:
    host_.timedToggleWrong();
    break;
  case A::Delete:
    host_.timedDelete();
    break;
  case A::Restore:
    host_.timedRestore();
    break;
  case A::Hint:
    host_.timedSay(timedHintText(step.hint, drill_));
    break;
  }
  return step.handled;
}

void TimedDriver::turn(Move m, std::int64_t now) {
  const std::optional<DrillFlow::Turn> kind = host_.timedTurn(m, now);
  if (!kind) {
    return;
  }
  // A face turn off the U-D axis ends recognition.
  const std::optional<std::int64_t> stopped =
      flow_.turned(*kind, now, [this] { return host_.timedStepDone(); });
  if (stopped) {
    host_.timedFinish(*stopped, DrillFlow::Result::Solved);
  }
}

void TimedDriver::cancel(const QString &why) {
  flow_.cancelSolve();
  host_.timedFinishAnimations();
  display_->showTime(0);
  if (flow_.config().virtualCube) {
    host_.timedResetCube();
  }
  host_.timedCancelled(why);
}

bool TimedDriver::tick(std::int64_t now, bool keyboardTaken) {
  // A popup or dialog took the keyboard: Space's release won't come.
  if (flow_.holding() && keyboardTaken) {
    flow_.loseKeyboard();
    display_->showTimer(flow_.timer(), now);
    return true;
  }
  flow_.tick(now);
  if (flow_.busy()) {
    display_->showTimer(flow_.timer(), now);
  }
  return false;
}

QString giveUpKeyName(bool shiftSpaceGivesUp) {
  return shiftSpaceGivesUp ? tr("Shift+Space") : tr("Backspace");
}

QString giveUpKeyName(const DrillSettings &drill) {
  return giveUpKeyName(drill.shiftSpaceGivesUp);
}

QString timedHintText(DrillFlow::Hint hint, const DrillSettings &drill) {
  switch (hint) {
  case DrillFlow::Hint::GiveUpKey:
    return tr("%1 gives the case up.").arg(giveUpKeyName(drill));
  case DrillFlow::Hint::KeepHolding:
    return tr("Keep holding Space until the time turns green, then let go "
              "or start turning.");
  case DrillFlow::Hint::OnlyRotations:
    return drill.tapToArm
               ? tr("Only rotations until the timer starts: tap Space first.")
               : tr("Only rotations until the timer starts (hold Space).");
  case DrillFlow::Hint::None:
    break;
  }
  return {};
}

QString cancelledNoTurnText() {
  return tr("Stopped before any face turn: cancelled, nothing was recorded.");
}

QString cancelledText() { return tr("Solve cancelled; nothing was recorded."); }

TimedSolveKeys timedSolveKeys(const DrillFlow &flow, const DrillSettings &drill,
                              bool scrambleShown) {
  TimedSolveKeys out;
  const KeyHint giveUp{giveUpKeyName(drill), tr("give up")};
  const bool physical = !flow.config().virtualCube;
  switch (flow.phase()) {
  case DrillFlow::Phase::Waiting:
    if (physical) {
      out.message =
          scrambleShown ? tr("Scramble, then start the timer.") : QString();
      out.keys = {{tr("hold Space"), tr("start on letting go")}, giveUp};
    } else if (flow.armed()) {
      out.message = tr("Ready: the first face turn starts the timer.");
      out.keys = {{"Esc", tr("not yet")}, giveUp};
    } else if (drill.tapToArm) {
      out.message = tr("Turn the cube as a whole to look at it.");
      out.keys = {{"Space", tr("ready: the first face turn starts the timer")},
                  giveUp};
    } else {
      out.message = tr("Turn the cube as a whole to look at it.");
      out.keys = {{tr("hold Space"), tr("start on letting go or turning")},
                  giveUp};
    }
    break;
  case DrillFlow::Phase::Running:
    if (physical) {
      out.keys = {{tr("any key but Shift, Ctrl, Alt"), tr("stop")},
                  giveUp,
                  {"Esc", tr("cancel")}};
    } else if (drill.autoStop) {
      out.message = tr("The timer stops when the step is done.");
      out.keys = {{"Space", tr("stop now (a DNF unless done)")},
                  giveUp,
                  {"Esc", tr("cancel")}};
    } else {
      out.keys = {{"Space", tr("stop")}, giveUp, {"Esc", tr("cancel")}};
    }
    break;
  default:
    break;
  }
  return out;
}

} // namespace vcube
