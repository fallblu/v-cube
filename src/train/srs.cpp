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

bool isDue(const SrsCard &card, std::int64_t now, const SrsConfig &config) {
  if (card.state == CardState::New) {
    return false;
  }
  bool learning =
      card.state == CardState::Learning || card.state == CardState::Relearning;
  return card.due <= now + (learning ? config.learnAheadSeconds : 0);
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

Grade gradeFromTime(double seconds, double targetSeconds, bool failed) {
  if (failed) {
    return Grade::Again;
  }
  if (targetSeconds <= 0) {
    return Grade::Good;
  }
  if (seconds <= 0.6 * targetSeconds) {
    return Grade::Easy;
  }
  if (seconds <= targetSeconds) {
    return Grade::Good;
  }
  return Grade::Hard;
}

void CaseProgress::record(const Attempt &a) {
  attempts.push_back(a);
  if (attempts.size() > kMaxAttempts) {
    attempts.erase(attempts.begin(),
                   attempts.begin() + static_cast<std::ptrdiff_t>(
                                          attempts.size() - kMaxAttempts));
  }
}

std::optional<double> CaseProgress::recentMean(std::size_t n) const {
  double sum = 0;
  std::size_t count = 0;
  for (auto it = attempts.rbegin(); it != attempts.rend() && count < n; ++it) {
    if (!it->failed) {
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
    if (!a.failed && (!b || a.timeMs < *b)) {
      b = a.timeMs;
    }
  }
  return b;
}

double CaseProgress::failRate(std::size_t n) const {
  std::size_t count = 0, fails = 0;
  for (auto it = attempts.rbegin(); it != attempts.rend() && count < n; ++it) {
    ++count;
    if (it->failed) {
      ++fails;
    }
  }
  return count == 0 ? 0.0
                    : static_cast<double>(fails) / static_cast<double>(count);
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
    if (!p.attempts.empty() && p.attempts.front().timestamp >= dayStart) {
      ++n;
    }
  }
  return n;
}

void CasePicker::setMode(PickMode m) {
  if (m != mode_) {
    mode_ = m;
    recapOrder_.clear();
    recapPos_ = 0;
    orderPos_ = 0;
  }
}

void CasePicker::setPool(std::vector<std::string> caseIds) {
  if (caseIds == pool_) {
    return;
  }
  pool_ = std::move(caseIds);
  recapOrder_.clear();
  recapPos_ = 0;
  orderPos_ = 0;
}

PickResult CasePicker::next(const SetProgress &progress, std::int64_t now,
                            std::int64_t dayStart, const SrsConfig &config,
                            std::mt19937_64 &rng) {
  PickResult r;
  if (pool_.empty()) {
    return r;
  }
  auto find = [&progress](const std::string &id) -> const CaseProgress * {
    auto it = progress.find(id);
    return it == progress.end() ? nullptr : &it->second;
  };

  switch (mode_) {
  case PickMode::Random: {
    std::uniform_int_distribution<std::size_t> dist(0, pool_.size() - 1);
    std::string pick = pool_[dist(rng)];
    for (int tries = 0; tries < 8 && pool_.size() > 1 && pick == last_;
         ++tries) {
      pick = pool_[dist(rng)];
    }
    r.caseId = pick;
    break;
  }
  case PickMode::Weighted: {
    // Overall mean of the pool's recent means, to compare cases against.
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
    double overall = counted > 0 ? total / counted : 0.0;
    std::vector<double> weights;
    for (const auto &id : pool_) {
      const auto *p = find(id);
      double w = 2.0; // never drilled: favour it
      if (p && !p->attempts.empty()) {
        auto m = p->recentMean();
        double ratio = (m && overall > 0) ? *m / overall : 1.5;
        w = ratio * ratio * (1.0 + 3.0 * p->failRate());
      }
      if (id == last_ && pool_.size() > 1) {
        w *= 0.1;
      }
      weights.push_back(std::max(w, 0.05));
    }
    std::discrete_distribution<std::size_t> dist(weights.begin(),
                                                 weights.end());
    r.caseId = pool_[dist(rng)];
    break;
  }
  case PickMode::Recap: {
    if (recapOrder_.empty() || recapPos_ >= recapOrder_.size()) {
      if (!recapOrder_.empty()) {
        r.roundComplete = true;
      }
      recapOrder_ = pool_;
      std::shuffle(recapOrder_.begin(), recapOrder_.end(), rng);
      recapPos_ = 0;
    }
    r.caseId = recapOrder_[recapPos_++];
    break;
  }
  case PickMode::InOrder:
    r.caseId = pool_[orderPos_ % pool_.size()];
    orderPos_ = (orderPos_ + 1) % pool_.size();
    break;
  case PickMode::Srs: {
    const std::string *bestDue = nullptr;
    std::int64_t bestDueTime = std::numeric_limits<std::int64_t>::max();
    std::optional<std::int64_t> nextDue;
    for (const auto &id : pool_) {
      const auto *p = find(id);
      if (!p || p->card.state == CardState::New) {
        continue;
      }
      if (isDue(p->card, now, config)) {
        // Avoid showing the same learning card twice in a row if another
        // one is due.
        std::int64_t key = p->card.due + (id == last_ ? 1000000 : 0);
        if (key < bestDueTime) {
          bestDueTime = key;
          bestDue = &id;
        }
      } else if (!nextDue || p->card.due < *nextDue) {
        nextDue = p->card.due;
      }
    }
    if (bestDue) {
      r.caseId = *bestDue;
      break;
    }
    if (newCasesIntroducedSince(progress, dayStart) < config.newPerDay) {
      for (const auto &id : pool_) {
        const auto *p = find(id);
        if (!p || p->card.state == CardState::New) {
          r.caseId = id;
          r.isNew = true;
          break;
        }
      }
    }
    if (!r.caseId) {
      r.nextDue = nextDue;
    }
    break;
  }
  }
  if (r.caseId) {
    last_ = *r.caseId;
  }
  return r;
}

} // namespace vcube
