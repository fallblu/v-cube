#pragma once

#include "app/drill_settings.hpp"
#include "app/page_parts.hpp"
#include "train/drill_flow.hpp"

#include <QList>
#include <QString>

#include <cstdint>
#include <optional>

namespace vcube {

class TimeDisplay;

// What a page timing a step with DrillFlow does when the flow asks: Drill
// and the practice modes' Timed tabs. The page keeps the case, the cube and
// what is recorded; TimedDriver does the rest.
class TimedHost {
public:
  // Space after the result (or from nothing to time: `fromIdle`).
  virtual void timedNext(bool fromIdle) = 0;
  // The same case again, not recorded.
  virtual void timedRetry() = 0;
  // Virtual: the case back on the cube as it was set up.
  virtual void timedResetCube() = 0;
  // Turns the cube as a whole before the start.
  virtual void timedRotate(Move m) = 0;
  // The timer started at `now`.
  virtual void timedStarted(std::int64_t now) = 0;
  // A turn of the solve, made at `now`: how DrillFlow counts it, or
  // nullopt if it wasn't made.
  virtual std::optional<DrillFlow::Turn> timedTurn(Move m,
                                                   std::int64_t now) = 0;
  // Virtual: the cube shows the step done.
  virtual bool timedStepDone() = 0;
  // The attempt is over: record it and show the result.
  virtual void timedFinish(std::int64_t ms, DrillFlow::Result result) = 0;
  // Tab after the result, or Esc in practice: practise the case (`aided`)
  // or, on a page without practice, show the details.
  virtual void timedPractise(bool aided) = 0;
  // A turn in practice.
  virtual void timedPracticeTurn(Move m) { (void)m; }
  virtual void timedToggleWrong() = 0;
  virtual void timedDelete() = 0;
  // Ctrl+Z after the result: the attempt deleted put back.
  virtual void timedRestore() {}
  // Animations under way are done with.
  virtual void timedFinishAnimations() = 0;
  // The keys of the phase the flow is in.
  virtual void timedShowKeys() = 0;
  // What is said about a key the flow didn't take.
  virtual void timedSay(const QString &text) = 0;
  // The solve was called off, nothing recorded: everything shown again,
  // and `why`.
  virtual void timedCancelled(const QString &why) = 0;

protected:
  ~TimedHost() = default;
};

// Does what DrillFlow asks after a key, the same way for every page that
// times with it: the page passes its keys in, and the driver keeps the
// flow and the time display and calls the host for the rest.
class TimedDriver {
public:
  TimedDriver(TimedHost &host, const DrillSettings &drill)
      : host_(host), drill_(drill) {}

  // The display the timer is shown on (made after the driver).
  void setDisplay(TimeDisplay *display) { display_ = display; }

  DrillFlow &flow() { return flow_; }
  const DrillFlow &flow() const { return flow_; }

  // A key at `now`: whether it was used.
  bool key(const TrainKeyEvent &k, std::int64_t now, Scramble scramble);
  // Does what the flow asked.
  void perform(const DrillFlow::Step &step, std::int64_t now);
  // The solve is called off and nothing recorded; `why` is said.
  void cancel(const QString &why);
  // The timer's tick at `now`; true if a popup or dialog took the
  // keyboard and a hold of Space was called off.
  bool tick(std::int64_t now);

private:
  TimedHost &host_;
  const DrillSettings &drill_;
  TimeDisplay *display_ = nullptr;
  DrillFlow flow_;

  void turn(Move m, std::int64_t now);
};

// The key that gives a case up: Drill's setting, on every page that times
// with DrillFlow and on the practice tabs that aren't timed.
QString giveUpKeyName(bool shiftSpaceGivesUp);
QString giveUpKeyName(const DrillSettings &drill);

// What is said about a key the flow didn't take.
QString timedHintText(DrillFlow::Hint hint, const DrillSettings &drill);

// What is said when a solve is called off.
QString cancelledNoTurnText(); // stopped before any face turn
QString cancelledText();       // Esc

// The keys of the flow's Waiting and Running phases, and what is said:
// the same on every page. `scrambleShown`: a physical cube's scramble is
// up.
struct TimedSolveKeys {
  QString message;
  QList<KeyHint> keys;
};
TimedSolveKeys timedSolveKeys(const DrillFlow &flow, const DrillSettings &drill,
                              bool scrambleShown);

} // namespace vcube
