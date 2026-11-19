#pragma once

#include "app/drill_settings.hpp"
#include "app/page_parts.hpp"
#include "train/drill_flow.hpp"

#include <QList>
#include <QString>

namespace vcube {

// The phases of a practice mode's tabs that aren't timed (the Timed tab's
// are DrillFlow's).
//   Idle     no card is due; Space looks again
//   Ready    Inspection: Space shows a state and starts the clock
//   Choose   a new case's solutions are chosen (see choicePanel())
//   Demo     a new case: Enter shows its solution, then it is done
//   Solving  Tab a hint, Drill's give-up key gives up, Esc starts again
//   Done     the verdict is up; Delete drops a try just recorded
enum class PracticePhase { Idle, Ready, Choose, Demo, Solving, Done };

// What a practice mode's hint bar says: a message and the keys that do
// something now.
struct PracticeKeys {
  QString message;
  QList<KeyHint> keys;
};

// Where a tab that isn't timed is.
struct UntimedKeysState {
  PracticePhase phase = PracticePhase::Idle;
  bool choosingAgain = false; // Choose: of a card already tried
  bool hidden = false;        // the cube is out of sight
  bool inspecting = false;    // the Inspection clock runs
  bool hints = false;         // Tab gives a hint
  bool waiting = false;       // Done, the verdict still being worked out
  bool best = false;          // there is a best solution to play
  bool detailOpen = false;
  bool graded = false;   // the try gave its card a grade
  bool chooser = false;  // the card's solutions can be changed
  bool recorded = false; // Done: an Untimed or Inspection try was kept
  QString giveUpKey = "Shift+Space"; // see giveUpKeyName()
  QList<KeyHint> modeKeys;           // the mode's own, once a try is done
};
PracticeKeys untimedKeys(const UntimedKeysState &s);

// Where the Timed tab is, beside its flow.
struct TimedKeysState {
  bool physical = false;
  bool scrambleShown = false; // physical: the scramble is worked out
  bool best = false;
  bool detailOpen = false;
  bool recorded = false; // the attempt is kept
};
PracticeKeys timedKeys(const DrillFlow &flow, const DrillSettings &drill,
                       const TimedKeysState &s);

// The key that gives a case up, on every tab: Drill's setting.
QString giveUpKeyName(const DrillSettings &drill);

// What is said about a key the Timed flow didn't take.
QString timedHintText(DrillFlow::Hint hint, const DrillSettings &drill);

} // namespace vcube
