#include "train/today.hpp"

#include <algorithm>
#include <set>

namespace vcube {

std::vector<ModeActivity>
summarizeActivity(const std::vector<ActivityEvent> &events, int modeCount,
                  std::int64_t todayStart, std::int64_t weekStart) {
  std::vector<ModeActivity> out(
      static_cast<std::size_t>(std::max(0, modeCount)));
  for (const ActivityEvent &e : events) {
    if (e.mode < 0 || e.mode >= modeCount || e.timestamp < weekStart) {
      continue;
    }
    ModeActivity &m = out[static_cast<std::size_t>(e.mode)];
    ++m.week.count;
    m.week.ms += e.ms;
    if (e.timestamp >= todayStart) {
      ++m.today.count;
      m.today.ms += e.ms;
    }
  }
  return out;
}

Streak practiceStreak(const std::vector<std::int64_t> &timestamps,
                      std::int64_t today,
                      const std::function<std::int64_t(std::int64_t)> &dayOf) {
  std::set<std::int64_t> days;
  for (std::int64_t t : timestamps) {
    if (t > 0) {
      days.insert(dayOf(t));
    }
  }
  Streak s;
  s.doneToday = days.contains(today);
  std::int64_t day = s.doneToday ? today : today - 1;
  while (days.contains(day)) {
    ++s.days;
    --day;
  }
  return s;
}

SetWaiting setWaiting(const std::vector<std::string> &pool,
                      const SetProgress &progress, std::int64_t now,
                      std::int64_t dayStart, std::int64_t dayEnd,
                      const SrsConfig &config) {
  SetWaiting d;
  for (const std::string &id : pool) {
    auto it = progress.find(id);
    if (it == progress.end()) {
      ++d.unstarted;
      continue;
    }
    const SrsCard &card = it->second.card;
    if (isQueued(card)) {
      ++d.queued;
    } else if (card.state == CardState::New) {
      ++d.unstarted;
    } else {
      if (card.state == CardState::Review) {
        ++d.learned;
      }
      if (isDueNow(card, now, dayEnd, config)) { // not suspended or buried
        ++d.due;
      }
    }
  }
  int introduced = newCasesIntroducedSince(progress, dayStart);
  d.newLeft = std::clamp(config.newPerDay - introduced, 0, d.queued);
  return d;
}

} // namespace vcube
