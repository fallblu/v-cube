#pragma once

#include "app/drill_settings.hpp"
#include "app/page_parts.hpp"
#include "train/drill_flow.hpp"
#include "train/practice_flow.hpp"

#include <QList>
#include <QString>

namespace vcube {

// What a practice mode's hint bar says: a message and the keys that do
// something now.
struct PracticeKeys {
  QString message;
  QList<KeyHint> keys;
};

// The keys of a tab that isn't timed, from the state its keys are read in
// (PracticeFlow), and the mode's own keys once a try is done.
PracticeKeys untimedKeys(const PracticeFlow::State &s,
                         const QList<KeyHint> &modeKeys = {});

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
QString giveUpKeyName(bool shiftSpaceGivesUp);
QString giveUpKeyName(const DrillSettings &drill);

// What is said about a key the Timed flow didn't take.
QString timedHintText(DrillFlow::Hint hint, const DrillSettings &drill);

} // namespace vcube
