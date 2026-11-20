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
      if (s.tab == PracticeTab::Learn) {
        keys << KeyHint{"Ctrl+Z", tr("take the grade back")};
      }
    }
    keys << modeKeys;
    if (s.chooser) {
      keys << KeyHint{"0", tr("change your solutions")};
    }
    if (s.recorded) {
      keys << KeyHint{"Delete", tr("delete it")};
    }
    // The try just made is kept: only the retry goes unrecorded.
    keys << KeyHint{"Esc", s.recorded ? tr("retry (this one stays)")
                                      : tr("retry, not recorded")};
    break;
  }
  return {message, keys};
}

PracticeKeys timedKeys(const DrillFlow &flow, const DrillSettings &drill,
                       const TimedKeysState &s) {
  QList<KeyHint> keys;
  QString message;
  switch (flow.phase()) {
  case DrillFlow::Phase::NoCase:
    keys = {{"Space", tr("look again")}};
    break;
  case DrillFlow::Phase::Waiting:
  case DrillFlow::Phase::Running: {
    TimedSolveKeys solve = timedSolveKeys(flow, drill, s.scrambleShown);
    message = solve.message;
    keys = solve.keys;
    break;
  }
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
    // The try just made is kept: only the retry goes unrecorded.
    keys << KeyHint{"Esc", s.recorded ? tr("retry (this one stays)")
                                      : tr("retry, not recorded")};
    break;
  case DrillFlow::Phase::Aided:
  case DrillFlow::Phase::Unaided:
    break;
  }
  return {message, keys};
}

} // namespace vcube
