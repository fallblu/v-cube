#include "train/drill_session.hpp"

#include <algorithm>
#include <map>

namespace vcube {

std::size_t DrillSession::add(const SessionAttempt &a) {
  attempts_.push_back(a);
  if (a.failed) {
    attempts_.back().newBest = false;
  }
  return attempts_.size() - 1;
}

void DrillSession::setFailed(std::size_t index, bool failed, bool newBest) {
  if (index >= attempts_.size()) {
    return;
  }
  attempts_[index].failed = failed;
  attempts_[index].newBest = newBest && !failed;
}

void DrillSession::removeLast() {
  if (!attempts_.empty()) {
    attempts_.pop_back();
  }
}

DrillSummary DrillSession::summary(std::size_t from,
                                   std::size_t slowest) const {
  DrillSummary s;
  double sum = 0;
  int good = 0;
  std::map<std::string, std::size_t> slowestOf; // case -> attempt index
  for (std::size_t i = from; i < attempts_.size(); ++i) {
    const SessionAttempt &a = attempts_[i];
    ++s.count;
    if (a.failed) {
      ++s.dnfs;
      continue;
    }
    s.newBests += a.newBest ? 1 : 0;
    sum += static_cast<double>(a.timeMs);
    ++good;
    auto [it, added] = slowestOf.try_emplace(a.caseId, i);
    if (!added && attempts_[it->second].timeMs < a.timeMs) {
      it->second = i;
    }
  }
  if (good > 0) {
    s.meanMs = sum / good;
  }
  for (const auto &[id, i] : slowestOf) {
    s.slowest.push_back(attempts_[i]);
  }
  std::stable_sort(s.slowest.begin(), s.slowest.end(),
                   [](const SessionAttempt &a, const SessionAttempt &b) {
                     return a.timeMs > b.timeMs;
                   });
  if (s.slowest.size() > slowest) {
    s.slowest.resize(slowest);
  }
  return s;
}

} // namespace vcube
