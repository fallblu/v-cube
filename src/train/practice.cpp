#include "train/practice.hpp"

#include <algorithm>

namespace vcube {

namespace {

// A mean of the first kRecentTries values added.
struct Mean {
  double sum = 0.0;
  std::size_t n = 0;
  bool full() const { return n >= kRecentTries; }
  void add(double v) {
    if (!full()) {
      sum += v;
      ++n;
    }
  }
  std::optional<double> value() const {
    return n ? std::optional(sum / static_cast<double>(n)) : std::nullopt;
  }
};

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

PracticeTry *newestTry(PracticeTries &tries, const std::string &key) {
  auto it = tries.find(key);
  return it == tries.end() || it->second.empty() ? nullptr : &it->second.back();
}

void dropNewestTry(PracticeTries &tries, const std::string &key) {
  auto it = tries.find(key);
  if (it != tries.end() && !it->second.empty()) {
    it->second.pop_back();
    if (it->second.empty()) {
      tries.erase(it);
    }
  }
}

void clearTimed(PracticeTries &timed, CubeKind cube) {
  const std::string prefix = timedKey(cube, {});
  std::erase_if(timed, [&prefix](const auto &entry) {
    return entry.first.starts_with(prefix);
  });
}

PracticeSummary summarize(const std::vector<PracticeTry> &tries) {
  return summarize(tries, 0);
}

PracticeSummary summarize(const std::vector<PracticeTry> &tries,
                          std::int64_t since) {
  PracticeSummary s;
  Mean ms, moves, extra, inspect, tps;
  // Newest first, so that the means are of the last few.
  for (auto it = tries.rbegin(); it != tries.rend(); ++it) {
    const PracticeTry &t = *it;
    if (t.timestamp < since) {
      continue;
    }
    ++s.tries;
    s.overTime += t.over ? 1 : 0;
    if (t.inspectMs > 0) {
      inspect.add(static_cast<double>(t.inspectMs));
    }
    if (t.dnf) {
      continue;
    }
    if (t.ms > 0 && t.moves > 0) {
      tps.add(t.moves * 1000.0 / static_cast<double>(t.ms));
    }
    ++s.solved;
    if (t.ms > 0) {
      if (!s.bestMs || t.ms < *s.bestMs) {
        s.bestMs = t.ms;
      }
      ms.add(static_cast<double>(t.ms));
    }
    if (t.moves > 0 && !moves.full()) {
      moves.add(t.moves);
      if (t.extra >= 0.0) {
        extra.add(t.extra);
      }
    }
  }
  s.meanMs = ms.value();
  s.meanMoves = moves.value();
  s.meanExtra = extra.value();
  s.meanInspectMs = inspect.value();
  s.meanTps = tps.value();
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
