#include "train/srs.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vcube {

namespace {

std::int64_t daysToSeconds(double days) {
  return static_cast<std::int64_t>(std::llround(days * kSecondsPerDay));
}

void scheduleReview(SrsCard &card, std::int64_t now, const SrsConfig &config) {
  card.intervalDays = std::clamp(card.intervalDays, 1.0, config.maximumDays);
  card.due = now + daysToSeconds(card.intervalDays);
}

std::int64_t stepDelay(const std::vector<std::int64_t> &steps, int step) {
  if (steps.empty()) {
    return 60;
  }
  auto i = static_cast<std::size_t>(
      std::clamp(step, 0, static_cast<int>(steps.size()) - 1));
  return steps[i];
}

} // namespace

void review(SrsCard &card, Grade grade, std::int64_t now,
            const SrsConfig &config) {
  ++card.reps;
  if (card.state == CardState::New) {
    card.state = CardState::Learning;
    card.step = 0;
    card.ease = config.startingEase;
  }
  switch (card.state) {
  case CardState::New:
  case CardState::Learning: {
    const auto &steps = config.learningSteps;
    switch (grade) {
    case Grade::Again:
      card.step = 0;
      card.due = now + stepDelay(steps, 0);
      break;
    case Grade::Hard:
      card.due = now + stepDelay(steps, card.step) * 3 / 2;
      break;
    case Grade::Good:
      ++card.step;
      if (card.step >= static_cast<int>(steps.size())) {
        card.state = CardState::Review;
        card.intervalDays = config.graduatingDays;
        scheduleReview(card, now, config);
      } else {
        card.due = now + stepDelay(steps, card.step);
      }
      break;
    case Grade::Easy:
      card.state = CardState::Review;
      card.intervalDays = config.easyDays;
      scheduleReview(card, now, config);
      break;
    }
    break;
  }
  case CardState::Review:
    switch (grade) {
    case Grade::Again:
      ++card.lapses;
      card.ease = std::max(config.minimumEase, card.ease - 0.2);
      card.intervalDays = std::max(1.0, card.intervalDays * config.lapseFactor);
      card.state = CardState::Relearning;
      card.step = 0;
      card.due = now + stepDelay(config.relearningSteps, 0);
      break;
    case Grade::Hard:
      card.ease = std::max(config.minimumEase, card.ease - 0.15);
      card.intervalDays = std::max(card.intervalDays + 1.0,
                                   card.intervalDays * config.hardFactor);
      scheduleReview(card, now, config);
      break;
    case Grade::Good:
      card.intervalDays =
          std::max(card.intervalDays + 1.0, card.intervalDays * card.ease);
      scheduleReview(card, now, config);
      break;
    case Grade::Easy:
      card.ease += 0.15;
      card.intervalDays =
          std::max(card.intervalDays + 1.0,
                   card.intervalDays * card.ease * config.easyBonus);
      scheduleReview(card, now, config);
      break;
    }
    break;
  case CardState::Relearning: {
    const auto &steps = config.relearningSteps;
    switch (grade) {
    case Grade::Again:
      card.step = 0;
      card.due = now + stepDelay(steps, 0);
      break;
    case Grade::Hard:
      card.due = now + stepDelay(steps, card.step) * 3 / 2;
      break;
    case Grade::Good:
      ++card.step;
      if (card.step >= static_cast<int>(steps.size())) {
        card.state = CardState::Review;
        scheduleReview(card, now, config);
      } else {
        card.due = now + stepDelay(steps, card.step);
      }
      break;
    case Grade::Easy:
      card.state = CardState::Review;
      card.intervalDays += 1.0;
      scheduleReview(card, now, config);
      break;
    }
    break;
  }
  }
}

bool isDue(const SrsCard &card, std::int64_t now, const SrsConfig &config,
           std::int64_t dayEnd) {
  if (card.state == CardState::New) {
    return false;
  }
  if (card.state == CardState::Review) {
    return card.due <= now || card.due < dayEnd;
  }
  return card.due <= now + config.learnAheadSeconds;
}

void startLearning(SrsCard &card, std::int64_t now, const SrsConfig &config) {
  card = SrsCard{};
  card.state = CardState::Learning;
  card.ease = config.startingEase;
  card.due = now;
}

void markLearned(SrsCard &card, std::int64_t now, const SrsConfig &config) {
  if (card.state == CardState::Review) {
    return;
  }
  card.state = CardState::Review;
  card.step = 0;
  if (card.ease < config.minimumEase) {
    card.ease = config.startingEase;
  }
  card.intervalDays = std::max(card.intervalDays, config.easyDays);
  scheduleReview(card, now, config);
}

void resetCard(SrsCard &card) { card = SrsCard{}; }

Grade learnGrade(bool hinted, bool gaveUp, bool leftAlgs) {
  return hinted || gaveUp || leftAlgs ? Grade::Again : Grade::Good;
}

void CaseProgress::record(const Attempt &a) {
  attempts.push_back(a);
  std::size_t same = 0;
  for (const auto &x : attempts) {
    same += x.mode == a.mode ? 1 : 0;
  }
  if (same > kMaxAttempts) {
    auto oldest =
        std::find_if(attempts.begin(), attempts.end(),
                     [&a](const Attempt &x) { return x.mode == a.mode; });
    attempts.erase(oldest);
  }
}

std::optional<double> CaseProgress::recentMean(std::size_t n) const {
  double sum = 0;
  std::size_t count = 0;
  for (auto it = attempts.rbegin(); it != attempts.rend() && count < n; ++it) {
    if (it->mode == AttemptMode::Drill && !it->failed) {
      sum += static_cast<double>(it->timeMs);
      ++count;
    }
  }
  if (count == 0) {
    return std::nullopt;
  }
  return sum / static_cast<double>(count);
}

std::optional<std::int64_t> CaseProgress::best() const {
  std::optional<std::int64_t> b;
  for (const auto &a : attempts) {
    if (a.mode == AttemptMode::Drill && !a.failed && (!b || a.timeMs < *b)) {
      b = a.timeMs;
    }
  }
  return b;
}

double CaseProgress::failRate(std::size_t n) const {
  std::size_t count = 0, fails = 0;
  for (auto it = attempts.rbegin(); it != attempts.rend() && count < n; ++it) {
    if (it->mode != AttemptMode::Drill) {
      continue;
    }
    ++count;
    if (it->failed) {
      ++fails;
    }
  }
  return count == 0 ? 0.0
                    : static_cast<double>(fails) / static_cast<double>(count);
}

std::size_t CaseProgress::drillCount() const {
  return static_cast<std::size_t>(
      std::count_if(attempts.begin(), attempts.end(), [](const Attempt &a) {
        return a.mode == AttemptMode::Drill;
      }));
}

CaseStatus statusOf(const CaseProgress *p) {
  if (!p) {
    return CaseStatus::Unstarted;
  }
  switch (p->card.state) {
  case CardState::New:
    return CaseStatus::Unstarted;
  case CardState::Learning:
  case CardState::Relearning:
    return CaseStatus::Learning;
  case CardState::Review:
    return CaseStatus::Learned;
  }
  return CaseStatus::Unstarted;
}

int newCasesIntroducedSince(const SetProgress &progress,
                            std::int64_t dayStart) {
  int n = 0;
  for (const auto &[id, p] : progress) {
    auto first = std::find_if(
        p.attempts.begin(), p.attempts.end(),
        [](const Attempt &a) { return a.mode == AttemptMode::Learn; });
    if (first != p.attempts.end() && first->timestamp >= dayStart) {
      ++n;
    }
  }
  return n;
}

bool isQueued(const SrsCard &card) {
  return card.state == CardState::Learning && card.reps == 0;
}

LearnPick pickLearnCase(const std::vector<std::string> &pool,
                        const SetProgress &progress, std::int64_t now,
                        std::int64_t dayStart, const SrsConfig &config,
                        const std::string &last) {
  LearnPick r;
  auto find = [&progress](const std::string &id) -> const CaseProgress * {
    auto it = progress.find(id);
    return it == progress.end() ? nullptr : &it->second;
  };
  // Earliest card of one kind due by `limit`, avoiding `last` if another
  // one is due.
  auto earliest = [&](auto &&wanted,
                      std::int64_t limit) -> const std::string * {
    const std::string *best = nullptr;
    std::pair<bool, std::int64_t> bestKey;
    for (const auto &id : pool) {
      const auto *p = find(id);
      if (!p || !wanted(p->card) || p->card.due > limit) {
        continue;
      }
      std::pair<bool, std::int64_t> key{id == last, p->card.due};
      if (!best || key < bestKey) {
        bestKey = key;
        best = &id;
      }
    }
    return best;
  };
  auto inReview = [](const SrsCard &c) { return c.state == CardState::Review; };
  auto inSteps = [](const SrsCard &c) {
    return (c.state == CardState::Learning ||
            c.state == CardState::Relearning) &&
           !isQueued(c);
  };
  const std::int64_t dayEnd = dayStart + kSecondsPerDay;
  std::vector<std::string> queued;
  for (const auto &id : pool) {
    const auto *p = find(id);
    if (!p || p->card.state == CardState::New) {
      continue;
    }
    if (isQueued(p->card)) {
      queued.push_back(id);
    } else if (!isDue(p->card, now, config, dayEnd) &&
               (!r.nextDue || p->card.due < *r.nextDue)) {
      r.nextDue = p->card.due;
    }
  }
  r.queued = static_cast<int>(queued.size());
  bool canIntroduce =
      newCasesIntroducedSince(progress, dayStart) < config.newPerDay;

  if (const auto *id = earliest(inSteps, now)) {
    r.caseId = *id;
  } else if (const auto *id2 = earliest(inReview, std::max(now, dayEnd - 1))) {
    r.caseId = *id2;
  } else if (!queued.empty() && canIntroduce) {
    r.caseId = queued.front();
    r.isNew = true;
  } else if (const auto *id3 =
                 earliest(inSteps, now + config.learnAheadSeconds)) {
    // Nothing else to do: learning cards due soon come up early.
    r.caseId = *id3;
  }
  r.newLimitReached = !r.caseId && !queued.empty() && !canIntroduce;
  if (r.caseId) {
    r.nextDue.reset();
  }
  return r;
}

void CasePicker::setMode(PickMode m) {
  if (m != mode_) {
    mode_ = m;
    orderPos_ = 0;
  }
}

void CasePicker::setPool(std::vector<std::string> caseIds) {
  if (caseIds == pool_) {
    return;
  }
  pool_ = std::move(caseIds);
  orderPos_ = 0;
}

std::optional<std::string> CasePicker::next(const SetProgress &progress,
                                            std::mt19937_64 &rng) {
  if (pool_.empty()) {
    return std::nullopt;
  }
  auto find = [&progress](const std::string &id) -> const CaseProgress * {
    auto it = progress.find(id);
    return it == progress.end() ? nullptr : &it->second;
  };
  std::string pick;
  switch (mode_) {
  case PickMode::Random: {
    // Uniform over the pool without the last case.
    auto last = std::find(pool_.begin(), pool_.end(), last_);
    bool skip = pool_.size() > 1 && last != pool_.end();
    std::uniform_int_distribution<std::size_t> dist(0, pool_.size() -
                                                           (skip ? 2 : 1));
    std::size_t i = dist(rng);
    if (skip && i >= static_cast<std::size_t>(last - pool_.begin())) {
      ++i;
    }
    pick = pool_[i];
    break;
  }
  case PickMode::Weighted: {
    // Compare cases with the target time, or with the pool's overall mean
    // when there is no target.
    double reference = targetMs_;
    if (reference <= 0) {
      double total = 0;
      int counted = 0;
      for (const auto &id : pool_) {
        if (const auto *p = find(id)) {
          if (auto m = p->recentMean()) {
            total += *m;
            ++counted;
          }
        }
      }
      reference = counted > 0 ? total / counted : 0.0;
    }
    std::vector<double> weights;
    for (const auto &id : pool_) {
      const auto *p = find(id);
      double w = 2.0; // never drilled: favour it
      if (p && p->drillCount() > 0) {
        auto m = p->recentMean();
        double ratio = (m && reference > 0) ? *m / reference : 1.5;
        w = ratio * ratio * (1.0 + 3.0 * p->failRate());
      }
      if (id == last_ && pool_.size() > 1) {
        w *= 0.1;
      }
      weights.push_back(std::max(w, 0.05));
    }
    std::discrete_distribution<std::size_t> dist(weights.begin(),
                                                 weights.end());
    pick = pool_[dist(rng)];
    break;
  }
  case PickMode::InOrder:
    pick = pool_[orderPos_ % pool_.size()];
    orderPos_ = (orderPos_ + 1) % pool_.size();
    break;
  }
  last_ = pick;
  return pick;
}

} // namespace vcube
