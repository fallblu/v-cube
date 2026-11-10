#pragma once

#include "train/srs.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace vcube {

// The figures of the Today view, worked out from what the modes record.

// Something done in a mode at a time: a solve, a case attempt, a quiz
// answer. `ms` is the time it took, 0 when it isn't timed.
struct ActivityEvent {
  int mode = 0;
  std::int64_t timestamp = 0; // Unix seconds
  std::int64_t ms = 0;
};

struct ActivityTotals {
  int count = 0;
  std::int64_t ms = 0;
};

struct ModeActivity {
  ActivityTotals today;
  ActivityTotals week; // the last 7 days, today included
};

// Totals per mode (indexed 0..modeCount-1) of the events since `todayStart`
// and since `weekStart`. Events of other modes are ignored.
std::vector<ModeActivity>
summarizeActivity(const std::vector<ActivityEvent> &events, int modeCount,
                  std::int64_t todayStart, std::int64_t weekStart);

// Days in a row with something done, up to today. `dayOf` turns a Unix
// time into a day number (the local date). A streak not yet extended today
// still counts, up to yesterday; `doneToday` says whether it was.
struct Streak {
  int days = 0;
  bool doneToday = false;
};
Streak practiceStreak(const std::vector<std::int64_t> &timestamps,
                      std::int64_t today,
                      const std::function<std::int64_t(std::int64_t)> &dayOf);

// What an alg set has waiting in Learn, for the cases in `pool` (the cases
// Learn can offer).
struct SetWaiting {
  int due = 0;     // reviews and learning steps due now (or today)
  int newLeft = 0; // queued cases that can still be introduced today
  int queued = 0;  // cases in the learn queue
  int learned = 0; // cases with a review interval
  int unstarted = 0;
};
SetWaiting setWaiting(const std::vector<std::string> &pool,
                      const SetProgress &progress, std::int64_t now,
                      std::int64_t dayStart, std::int64_t dayEnd,
                      const SrsConfig &config);

} // namespace vcube
