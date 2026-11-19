#include "train/practice.hpp"

#include <algorithm>

namespace vcube {

namespace {

void addCapped(std::vector<PracticeTry> &tries, const PracticeTry &t) {
  tries.push_back(t);
  if (tries.size() > PracticeData::kPerGroup) {
    tries.erase(tries.begin(), tries.end() - static_cast<std::ptrdiff_t>(
                                                 PracticeData::kPerGroup));
  }
}

} // namespace

void PracticeData::addLearn(const std::string &group, const PracticeTry &t) {
  addCapped(learn[group], t);
}

void PracticeData::addInspection(const std::string &group,
                                 const PracticeTry &t) {
  addCapped(inspection[group], t);
}

void PracticeData::addTimed(const std::string &key, const PracticeTry &t) {
  addCapped(timed[key], t);
}

std::string timedKey(CubeKind cube, const std::string &group) {
  return (cube == CubeKind::Physical ? "physical/" : "virtual/") + group;
}

PracticeSummary summarize(const std::vector<PracticeTry> &tries) {
  return summarize(tries, 0);
}

PracticeSummary summarize(const std::vector<PracticeTry> &tries,
                          std::int64_t since) {
  PracticeSummary s;
  double msSum = 0.0, moveSum = 0.0, extraSum = 0.0, inspectSum = 0.0,
         tpsSum = 0.0;
  std::size_t msN = 0, moveN = 0, extraN = 0, inspectN = 0, tpsN = 0;
  // Newest first, so that the means are of the last few.
  for (auto it = tries.rbegin(); it != tries.rend(); ++it) {
    const PracticeTry &t = *it;
    if (t.timestamp < since) {
      continue;
    }
    ++s.tries;
    s.overTime += t.over ? 1 : 0;
    if (t.inspectMs > 0 && inspectN < kRecentTries) {
      inspectSum += static_cast<double>(t.inspectMs);
      ++inspectN;
    }
    if (t.dnf) {
      continue;
    }
    if (t.ms > 0 && t.moves > 0 && tpsN < kRecentTries) {
      tpsSum += t.moves * 1000.0 / static_cast<double>(t.ms);
      ++tpsN;
    }
    ++s.solved;
    if (t.ms > 0) {
      if (!s.bestMs || t.ms < *s.bestMs) {
        s.bestMs = t.ms;
      }
      if (msN < kRecentTries) {
        msSum += static_cast<double>(t.ms);
        ++msN;
      }
    }
    if (t.moves > 0 && moveN < kRecentTries) {
      moveSum += t.moves;
      ++moveN;
      if (t.extra >= 0.0) {
        extraSum += t.extra;
        ++extraN;
      }
    }
  }
  if (msN) {
    s.meanMs = msSum / static_cast<double>(msN);
  }
  if (moveN) {
    s.meanMoves = moveSum / static_cast<double>(moveN);
  }
  if (extraN) {
    s.meanExtra = extraSum / static_cast<double>(extraN);
  }
  if (inspectN) {
    s.meanInspectMs = inspectSum / static_cast<double>(inspectN);
  }
  if (tpsN) {
    s.meanTps = tpsSum / static_cast<double>(tpsN);
  }
  return s;
}

bool queueUnstarted(const std::vector<std::string> &pool, SetProgress &cards,
                    std::int64_t now, const SrsConfig &config) {
  bool any = false;
  for (const std::string &id : pool) {
    CaseProgress &p = cards[id];
    if (p.card.state == CardState::New) {
      startLearning(p.card, now, config);
      any = true;
    }
  }
  return any;
}

PracticeWaiting practiceWaiting(const std::vector<std::string> &pool,
                                const SetProgress &cards, std::int64_t now,
                                std::int64_t dayStart, std::int64_t dayEnd,
                                const SrsConfig &config) {
  // A case never started is as good as queued: the cards are counted as
  // queueUnstarted() would leave them, without a copy of them all (and
  // their attempts) being made to queue them in.
  PracticeWaiting out;
  out.total = static_cast<int>(pool.size());
  int queued = 0;
  int introduced = newCasesIntroducedSince(cards, dayStart);
  for (const std::string &id : pool) {
    auto it = cards.find(id);
    if (it == cards.end()) {
      ++queued;
      continue;
    }
    const SrsCard &card = it->second.card;
    if (card.state == CardState::New) {
      ++queued;
      // Queueing starts the card afresh: it is none of the day's new ones.
      if (card.introduced != 0 && !card.marked && card.introduced >= dayStart) {
        --introduced;
      }
    } else if (isQueued(card)) {
      ++queued;
    } else {
      if (card.state == CardState::Review) {
        ++out.learned;
      }
      if (isDueNow(card, now, dayEnd, config)) {
        ++out.due;
      }
    }
  }
  out.newLeft = std::clamp(config.newPerDay - introduced, 0, queued);
  out.started = out.total - queued;
  return out;
}

Grade practiceGrade(const CardTry &t) {
  return t.solvedWell && !t.hinted && !t.gaveUp ? Grade::Good : Grade::Again;
}

} // namespace vcube
