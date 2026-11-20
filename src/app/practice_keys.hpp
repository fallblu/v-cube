#pragma once

#include "app/drill_settings.hpp"
#include "app/page_parts.hpp"
#include "app/timed_driver.hpp"
#include "train/drill_flow.hpp"
#include "train/practice_flow.hpp"

#include <QList>
#include <QString>

namespace vcube {

// What a practice mode's hint bar says: a message and the keys that do
// something now.
using PracticeKeys = TimedSolveKeys;

// The keys of a tab that isn't timed, from the state its keys are read in
// (PracticeFlow), and the mode's own keys once a try is done.
PracticeKeys untimedKeys(const PracticeFlow::State &s,
                         const QList<KeyHint> &modeKeys = {});

// Where the Timed tab is, beside its flow.
struct TimedKeysState {
  bool scrambleShown = false; // physical: the scramble is worked out
  bool best = false;
  bool detailOpen = false;
  bool recorded = false; // the attempt is kept
};
PracticeKeys timedKeys(const DrillFlow &flow, const DrillSettings &drill,
                       const TimedKeysState &s);

} // namespace vcube
