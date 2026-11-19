#include "app/practice_keys.hpp"

#include <QCoreApplication>

namespace vcube {

namespace {

QString tr(const char *text) {
  return QCoreApplication::translate("vcube::PracticePage", text);
}

KeyHint detailKey(bool open) {
  return {"Tab", open ? tr("hide the details") : tr("details")};
}

} // namespace

PracticeKeys untimedKeys(const PracticeFlow::State &s,
                         const QList<KeyHint> &modeKeys) {
  QList<KeyHint> keys;
  QString message;
  const QString giveUpKey = giveUpKeyName(s.shiftSpaceGivesUp);
  switch (s.phase) {
  case PracticePhase::Idle:
    keys = {{"Space", tr("look again")}};
    break;
  case PracticePhase::Ready:
    keys = {{"Space", tr("start")}};
    break;
  case PracticePhase::Choose:
    keys = {
        {"1-9", tr("tick or untick")},
        {"Enter", s.choosingAgain ? tr("keep these") : tr("done choosing")}};
    if (s.choosingAgain) {
      keys << KeyHint{"Esc", tr("leave them as they were")};
    }
    break;
  case PracticePhase::Demo:
    keys = {{"Enter", tr("show it")},
            {"Space", tr("skip: I know this case")},
            {"Esc", tr("start again")}};
    break;
  case PracticePhase::Solving:
    if (s.hidden) {
      message = tr("The cube is hidden: do what you planned.");
      keys = {{"Space", tr("show the cube")},
              {giveUpKey, tr("give up")},
              {"Esc", tr("start again")}};
      break;
    }
    if (s.inspecting) {
      message = tr("Plan: the cube hides at your first face turn.");
    }
    if (s.hints) {
      keys << KeyHint{"Tab", tr("hint")};
    }
    keys << KeyHint{giveUpKey, tr("give up")}
         << KeyHint{"Esc", tr("start again")};
    break;
  case PracticePhase::Done:
    if (s.waiting) {
      message = tr("Working out the best solutions…");
      break;
    }
    keys << KeyHint{"Space", tr("next")};
    if (s.best) {
      keys << KeyHint{"Enter", tr("play the best")};
    }
    keys << detailKey(s.detailOpen);
    if (s.graded) {
      keys << KeyHint{"1-4", tr("change the grade")};
    }
    keys << modeKeys;
    if (s.chooser) {
      keys << KeyHint{"0", tr("change your solutions")};
    }
    if (s.recorded) {
      keys << KeyHint{"Delete", tr("delete it")};
    }
    keys << KeyHint{"Esc", tr("retry, not recorded")};
    break;
  }
  return {message, keys};
}

QString giveUpKeyName(bool shiftSpaceGivesUp) {
  return shiftSpaceGivesUp ? tr("Shift+Space") : tr("Backspace");
}

QString giveUpKeyName(const DrillSettings &drill) {
  return giveUpKeyName(drill.shiftSpaceGivesUp);
}

PracticeKeys timedKeys(const DrillFlow &flow, const DrillSettings &drill,
                       const TimedKeysState &s) {
  QList<KeyHint> keys;
  QString message;
  const KeyHint giveUp{giveUpKeyName(drill), tr("give up")};
  switch (flow.phase()) {
  case DrillFlow::Phase::NoCase:
    keys = {{"Space", tr("look again")}};
    break;
  case DrillFlow::Phase::Waiting:
    if (s.physical) {
      message =
          s.scrambleShown ? tr("Scramble, then start the timer.") : QString();
      keys = {{tr("hold Space"), tr("start on letting go")}, giveUp};
    } else if (flow.armed()) {
      message = tr("Ready: the first face turn starts the timer.");
      keys = {{"Esc", tr("not yet")}, giveUp};
    } else if (drill.tapToArm) {
      message = tr("Turn the cube as a whole to look at it.");
      keys = {{"Space", tr("ready: the first face turn starts the timer")},
              giveUp};
    } else {
      message = tr("Turn the cube as a whole to look at it.");
      keys = {{tr("hold Space"), tr("start on letting go or turning")}, giveUp};
    }
    break;
  case DrillFlow::Phase::Running:
    if (s.physical) {
      keys = {{tr("any key but Shift, Ctrl, Alt"), tr("stop")},
              giveUp,
              {"Esc", tr("cancel")}};
    } else if (drill.autoStop) {
      message = tr("The timer stops when the step is done.");
      keys = {{"Space", tr("stop now (a DNF unless done)")},
              giveUp,
              {"Esc", tr("cancel")}};
    } else {
      keys = {{"Space", tr("stop")}, giveUp, {"Esc", tr("cancel")}};
    }
    break;
  case DrillFlow::Phase::Revealed:
    keys << KeyHint{"Space", tr("next")};
    if (s.best) {
      keys << KeyHint{"Enter", tr("play the best")};
    }
    keys << detailKey(s.detailOpen);
    if (s.recorded && !flow.gaveUp()) {
      keys << KeyHint{"Backspace", flow.failed() ? tr("not a DNF") : tr("DNF")};
    }
    if (s.recorded) {
      keys << KeyHint{"Delete", flow.gaveUp() && flow.gaveUpBeforeStart()
                                    ? tr("take it back")
                                    : tr("delete it")};
    }
    keys << KeyHint{"Esc", tr("retry, not recorded")};
    break;
  case DrillFlow::Phase::Aided:
  case DrillFlow::Phase::Unaided:
    break;
  }
  return {message, keys};
}

QString timedHintText(DrillFlow::Hint hint, const DrillSettings &drill) {
  switch (hint) {
  case DrillFlow::Hint::GiveUpKey:
    return tr("Shift+Space gives the solve up.");
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

} // namespace vcube
