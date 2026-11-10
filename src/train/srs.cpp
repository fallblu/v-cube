#include "train/srs.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <limits>

namespace vcube {

namespace {

std::int64_t daysToSeconds(double days) {
  return static_cast<std::int64_t>(std::llround(days * kSecondsPerDay));
}

// Intervals this long or longer are spread out a little.
constexpr double kFuzzFromDays = 3.0;

void scheduleReview(SrsCard &card, std::int64_t now, const SrsConfig &config,
                    std::mt19937_64 *rng) {
  card.intervalDays = std::clamp(card.intervalDays, 1.0, config.maximumDays);
  if (rng && card.intervalDays >= kFuzzFromDays) {
    double range = std::max(1.0, card.intervalDays * 0.05);
    std::uniform_real_distribution<double> fuzz(-range, range);
    card.intervalDays = std::clamp(card.intervalDays + fuzz(*rng), 1.0,
                                   config.maximumDays);
  }
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

// Hard in the learning or relearning steps: on to the next step (never
// past the last), or the same step again a little later.
void hardStep(SrsCard &card, const std::vector<std::int64_t> &steps,
              std::int64_t now, const SrsConfig &config) {
  if (config.hardAdvancesSteps &&
      card.step + 1 < static_cast<int>(steps.size())) {
    ++card.step;
    card.due = now + stepDelay(steps, card.step);
  } else {
    card.due = now + stepDelay(steps, card.step) * 3 / 2;
  }
}

std::uint64_t mix(std::uint64_t x) {
  // splitmix64's finaliser.
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

// The same on every platform and run, unlike std::hash.
std::uint64_t hashId(const std::string &id, std::uint64_t seed) {
  std::uint64_t h = 1469598103934665603ULL; // FNV-1a
  for (char ch : id) {
    h = (h ^ static_cast<std::uint8_t>(ch)) * 1099511628211ULL;
  }
  return mix(h ^ mix(seed));
}

} // namespace

void review(SrsCard &card, Grade grade, std::int64_t now,
            const SrsConfig &config, std::mt19937_64 *rng) {
  ++card.reps;
  if (card.introduced == 0) {
    card.introduced = std::max<std::int64_t>(now, 1);
  }
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
      hardStep(card, steps, now, config);
      break;
    case Grade::Good:
      ++card.step;
      if (card.step >= static_cast<int>(steps.size())) {
        card.state = CardState::Review;
        card.intervalDays = config.graduatingDays;
        scheduleReview(card, now, config, rng);
      } else {
        card.due = now + stepDelay(steps, card.step);
      }
      break;
    case Grade::Easy:
      card.state = CardState::Review;
      card.intervalDays = config.easyDays;
      scheduleReview(card, now, config, rng);
      break;
    }
    break;
  }
  case CardState::Review: {
    // Days past the due time, credited in part to a review done late.
    double late = config.creditLateReviews && now > card.due
                      ? static_cast<double>(now - card.due) / kSecondsPerDay
                      : 0.0;
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
      card.intervalDays =
          std::max(card.intervalDays + 1.0,
                   (card.intervalDays + late / 4) * config.hardFactor);
      scheduleReview(card, now, config, rng);
      break;
    case Grade::Good:
      card.intervalDays = std::max(card.intervalDays + 1.0,
                                   (card.intervalDays + late / 2) * card.ease);
      if (card.ease < config.startingEase) {
        card.ease =
            std::min(config.startingEase, card.ease + config.easeRecovery);
      }
      scheduleReview(card, now, config, rng);
      break;
    case Grade::Easy:
      card.ease += 0.15;
      card.intervalDays =
          std::max(card.intervalDays + 1.0, (card.intervalDays + late) *
                                                card.ease * config.easyBonus);
      scheduleReview(card, now, config, rng);
      break;
    }
    break;
  }
  case CardState::Relearning: {
    const auto &steps = config.relearningSteps;
    switch (grade) {
    case Grade::Again:
      card.step = 0;
      card.due = now + stepDelay(steps, 0);
      break;
    case Grade::Hard:
      hardStep(card, steps, now, config);
      break;
    case Grade::Good:
      ++card.step;
      if (card.step >= static_cast<int>(steps.size())) {
        card.state = CardState::Review;
        scheduleReview(card, now, config, rng);
      } else {
        card.due = now + stepDelay(steps, card.step);
      }
      break;
    case Grade::Easy:
      card.state = CardState::Review;
      card.intervalDays += 1.0;
      scheduleReview(card, now, config, rng);
      break;
    }
    break;
  }
  }
}

std::int64_t nextInterval(const SrsCard &card, Grade grade, std::int64_t now,
                          const SrsConfig &config) {
  SrsCard c = card;
  review(c, grade, now, config);
  return c.due - now;
}

void introduce(SrsCard &card, std::int64_t now, const SrsConfig &config) {
  if (card.introduced == 0) {
    card.introduced = std::max<std::int64_t>(now, 1);
  }
  card.state = CardState::Learning;
  card.step = 0;
  card.ease = config.startingEase;
  card.due = now + stepDelay(config.learningSteps, 0);
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
  if (card.introduced == 0) {
    card.introduced = std::max<std::int64_t>(now, 1);
    card.marked = true;
  }
  card.state = CardState::Review;
  card.step = 0;
  if (card.ease < config.minimumEase) {
    card.ease = config.startingEase;
  }
  card.intervalDays = std::max(card.intervalDays, config.easyDays);
  scheduleReview(card, now, config, nullptr);
}

void resetCard(SrsCard &card) { card = SrsCard{}; }

void bury(SrsCard &card, std::int64_t until) { card.buriedUntil = until; }

bool isBuried(const SrsCard &card, std::int64_t now) {
  return card.buriedUntil > now;
}

bool isHeld(const SrsCard &card, std::int64_t now) {
  return card.suspended || isBuried(card, now);
}

bool isLeech(const SrsCard &card, const SrsConfig &config) {
  return config.leechLapses > 0 && card.lapses >= config.leechLapses;
}

void resetLearning(CaseProgress &progress) {
  resetCard(progress.card);
  std::erase_if(progress.attempts, [](const Attempt &a) {
    return a.mode == AttemptMode::Learn;
  });
}

Grade learnGrade(bool hinted, bool gaveUp, bool leftAlgs) {
  return hinted || gaveUp || leftAlgs ? Grade::Again : Grade::Good;
}

std::int64_t localDay(std::int64_t unixSeconds) {
  auto t = static_cast<std::time_t>(unixSeconds);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  using namespace std::chrono;
  year_month_day ymd{year(tm.tm_year + 1900),
                     month(static_cast<unsigned>(tm.tm_mon + 1)),
                     day(static_cast<unsigned>(tm.tm_mday))};
  return sys_days(ymd).time_since_epoch().count();
}

std::optional<double> DayRollup::meanMs() const {
  if (good() <= 0) {
    return std::nullopt;
  }
  return static_cast<double>(sumMs) / good();
}

namespace {

bool isDrill(AttemptMode m) { return m != AttemptMode::Learn; }

// Adds an attempt to the roll-ups (oldest first), on its day.
void addToDays(std::vector<DayRollup> &days, const Attempt &a) {
  std::int64_t day = localDay(a.timestamp);
  auto it = std::lower_bound(
      days.begin(), days.end(), day,
      [](const DayRollup &r, std::int64_t d) { return r.day < d; });
  if (it == days.end() || it->day != day) {
    it = days.insert(it, DayRollup{day, 0, 0, 0, 0});
  }
  ++it->count;
  if (a.failed) {
    ++it->fails;
  } else {
    it->sumMs += a.timeMs;
    if (it->bestMs == 0 || a.timeMs < it->bestMs) {
      it->bestMs = std::max<std::int64_t>(a.timeMs, 1);
    }
  }
}

} // namespace

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
    if (isDrill(oldest->mode)) {
      DrillArchive &kept = archive[oldest->mode];
      if (!oldest->failed &&
          (kept.bestMs == 0 || oldest->timeMs < kept.bestMs)) {
        kept.bestMs = std::max<std::int64_t>(oldest->timeMs, 1);
      }
      addToDays(kept.days, *oldest);
    }
    attempts.erase(oldest);
  }
}

std::optional<double> CaseProgress::recentMedian(AttemptMode mode,
                                                 std::size_t n) const {
  std::vector<std::int64_t> times;
  for (auto it = attempts.rbegin(); it != attempts.rend() && times.size() < n;
       ++it) {
    if (it->mode == mode && !it->failed) {
      times.push_back(it->timeMs);
    }
  }
  if (times.empty()) {
    return std::nullopt;
  }
  std::sort(times.begin(), times.end());
  std::size_t mid = times.size() / 2;
  if (times.size() % 2 == 1) {
    return static_cast<double>(times[mid]);
  }
  return (static_cast<double>(times[mid - 1]) +
          static_cast<double>(times[mid])) /
         2.0;
}

std::vector<DayRollup> CaseProgress::days(AttemptMode mode) const {
  std::vector<DayRollup> out;
  if (auto it = archive.find(mode); it != archive.end()) {
    out = it->second.days;
  }
  for (const Attempt &a : attempts) {
    if (a.mode == mode) {
      addToDays(out, a);
    }
  }
  return out;
}

bool CaseProgress::removeLast(AttemptMode mode) {
  for (auto it = attempts.rbegin(); it != attempts.rend(); ++it) {
    if (it->mode == mode) {
      attempts.erase(std::next(it).base());
      return true;
    }
  }
  return false;
}

void CaseProgress::clearDrill(AttemptMode mode) {
  std::erase_if(attempts, [mode](const Attempt &a) { return a.mode == mode; });
  archive.erase(mode);
}

std::optional<double> CaseProgress::recentMean(AttemptMode mode,
                                               std::size_t n) const {
  double sum = 0;
  std::size_t count = 0;
  for (auto it = attempts.rbegin(); it != attempts.rend() && count < n; ++it) {
    if (it->mode == mode && !it->failed) {
      sum += static_cast<double>(it->timeMs);
      ++count;
    }
  }
  if (count == 0) {
    return std::nullopt;
  }
  return sum / static_cast<double>(count);
}

std::optional<std::int64_t> CaseProgress::best(AttemptMode mode) const {
  std::optional<std::int64_t> b;
  if (auto it = archive.find(mode);
      it != archive.end() && it->second.bestMs > 0) {
    b = it->second.bestMs;
  }
  for (const auto &a : attempts) {
    if (a.mode == mode && !a.failed && (!b || a.timeMs < *b)) {
      b = a.timeMs;
    }
  }
  return b;
}

double CaseProgress::failRate(AttemptMode mode, std::size_t n) const {
  std::size_t count = 0, fails = 0;
  for (auto it = attempts.rbegin(); it != attempts.rend() && count < n; ++it) {
    if (it->mode != mode) {
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

std::size_t CaseProgress::drillCount(AttemptMode mode) const {
  return static_cast<std::size_t>(
      std::count_if(attempts.begin(), attempts.end(),
                    [mode](const Attempt &a) { return a.mode == mode; }));
}

std::optional<double> CaseProgress::recognitionMean(std::size_t n) const {
  return vcube::recognitionMean(attempts, n);
}

std::vector<Attempt> caseAttempts(const CaseProgress *own,
                                  const CaseProgress *familyCard) {
  std::vector<Attempt> out;
  if (own) {
    out = own->attempts;
  }
  if (familyCard) {
    for (const Attempt &a : familyCard->attempts) {
      if (a.mode == AttemptMode::Learn) {
        out.push_back(a);
      }
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const Attempt &a, const Attempt &b) {
                       return a.timestamp < b.timestamp;
                     });
  }
  return out;
}

std::optional<double> recognitionMean(const std::vector<Attempt> &attempts,
                                      std::size_t n) {
  double sum = 0;
  std::size_t count = 0;
  for (auto it = attempts.rbegin(); it != attempts.rend() && count < n; ++it) {
    if (it->recognitionMs > 0) {
      sum += static_cast<double>(it->recognitionMs);
      ++count;
    }
  }
  if (count == 0) {
    return std::nullopt;
  }
  return sum / static_cast<double>(count);
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
    if (p.card.introduced != 0 && !p.card.marked &&
        p.card.introduced >= dayStart) {
      ++n;
    }
  }
  return n;
}

bool isQueued(const SrsCard &card) {
  return card.state == CardState::Learning && card.reps == 0 &&
         card.introduced == 0;
}

int learningCount(const std::vector<std::string> &pool,
                  const SetProgress &progress) {
  int n = 0;
  for (const auto &id : pool) {
    auto it = progress.find(id);
    if (it != progress.end() &&
        it->second.card.state == CardState::Learning &&
        !isQueued(it->second.card) && !it->second.card.suspended) {
      ++n;
    }
  }
  return n;
}

std::vector<std::string> orderNewCases(const std::vector<NewCase> &cases,
                                       NewOrder order, std::uint64_t seed) {
  std::vector<const NewCase *> sorted;
  for (const auto &c : cases) {
    sorted.push_back(&c);
  }
  auto by = [&sorted](auto &&key) {
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&key](const NewCase *a, const NewCase *b) {
                       return key(*a) < key(*b);
                     });
  };
  switch (order) {
  case NewOrder::ListOrder:
    break;
  case NewOrder::Queued:
    by([](const NewCase &c) { return c.queued; });
    break;
  case NewOrder::Shuffled:
    by([seed](const NewCase &c) { return hashId(c.id, seed); });
    break;
  case NewOrder::ShortestAlg:
    by([](const NewCase &c) { return c.algMoves; });
    break;
  case NewOrder::FewestSteps:
    by([](const NewCase &c) { return c.stepsLeft; });
    break;
  case NewOrder::RoundRobin: {
    // Groups in the order they first come, one case of each in turn.
    std::vector<std::string> groups;
    std::vector<std::vector<const NewCase *>> members;
    for (const NewCase *c : sorted) {
      auto g = std::find(groups.begin(), groups.end(), c->group);
      if (g == groups.end()) {
        groups.push_back(c->group);
        members.emplace_back();
        g = groups.end() - 1;
      }
      members[static_cast<std::size_t>(g - groups.begin())].push_back(c);
    }
    sorted.clear();
    for (std::size_t round = 0; sorted.size() < cases.size(); ++round) {
      for (const auto &m : members) {
        if (round < m.size()) {
          sorted.push_back(m[round]);
        }
      }
    }
    break;
  }
  }
  std::vector<std::string> out;
  out.reserve(sorted.size());
  for (const NewCase *c : sorted) {
    out.push_back(c->id);
  }
  return out;
}

bool isDueNow(const SrsCard &card, std::int64_t now, std::int64_t dayEnd,
              const SrsConfig &config) {
  if (card.state == CardState::New || isQueued(card) || isHeld(card, now)) {
    return false;
  }
  if (card.state == CardState::Review) {
    return isDue(card, now, config, dayEnd);
  }
  return card.due <= now + config.learnAheadSeconds;
}

LearnPick pickLearnCase(const std::vector<std::string> &pool,
                        const SetProgress &progress, std::int64_t now,
                        std::int64_t dayStart, const SrsConfig &config,
                        const std::string &last) {
  LearnPickOptions options;
  options.last = last;
  return pickLearnCase(pool, progress, now, dayStart, config, options);
}

LearnPick pickLearnCase(const std::vector<std::string> &pool,
                        const SetProgress &progress, std::int64_t now,
                        std::int64_t dayStart, const SrsConfig &config,
                        const LearnPickOptions &options) {
  LearnPick r;
  const std::string &last = options.last;
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
  // Any card of one kind due by `limit`, at random, avoiding `last` if
  // another one is due.
  auto anyDue = [&](auto &&wanted, std::int64_t limit) -> const std::string * {
    if (!options.rng) {
      return earliest(wanted, limit);
    }
    std::vector<const std::string *> due;
    bool lastDue = false;
    for (const auto &id : pool) {
      const auto *p = find(id);
      if (!p || !wanted(p->card) || p->card.due > limit) {
        continue;
      }
      if (id == last) {
        lastDue = true;
      } else {
        due.push_back(&id);
      }
    }
    if (due.empty()) {
      return lastDue ? &last : nullptr;
    }
    std::uniform_int_distribution<std::size_t> pick(0, due.size() - 1);
    return due[pick(*options.rng)];
  };
  auto inReview = [now](const SrsCard &c) {
    return c.state == CardState::Review && !isHeld(c, now);
  };
  auto inSteps = [now](const SrsCard &c) {
    return (c.state == CardState::Learning ||
            c.state == CardState::Relearning) &&
           !isQueued(c) && !isHeld(c, now);
  };
  const std::int64_t dayEnd =
      options.dayEnd != 0 ? options.dayEnd : dayStart + kSecondsPerDay;
  std::vector<std::string> queued;
  for (const auto &id : pool) {
    const auto *p = find(id);
    if (!p || p->card.state == CardState::New || isHeld(p->card, now)) {
      continue;
    }
    if (isQueued(p->card)) {
      queued.push_back(id);
    } else if (options.reviews && !isDue(p->card, now, config, dayEnd) &&
               (!r.nextDue || p->card.due < *r.nextDue)) {
      r.nextDue = p->card.due;
    }
  }
  r.queued = static_cast<int>(queued.size());
  bool underDaily =
      newCasesIntroducedSince(progress, dayStart) < config.newPerDay;
  bool underLearning = config.maxLearning <= 0 ||
                       learningCount(pool, progress) < config.maxLearning;
  bool canIntroduce = options.newCases && underDaily && underLearning;

  const std::string *id = nullptr;
  if (options.reviews && (id = earliest(inSteps, now))) {
    r.caseId = *id;
  } else if (options.reviews &&
             (id = anyDue(inReview, std::max(now, dayEnd - 1)))) {
    r.caseId = *id;
  } else if (!queued.empty() && canIntroduce) {
    r.caseId = queued.front();
    r.isNew = true;
  } else if (options.reviews &&
             (id = earliest(inSteps,
                            options.early
                                ? std::numeric_limits<std::int64_t>::max()
                                : now + config.learnAheadSeconds))) {
    // Nothing else to do: learning cards due soon come up early.
    r.caseId = *id;
  }
  if (!r.caseId && !queued.empty() && options.newCases) {
    r.newLimitReached = !underDaily;
    r.learningLimitReached = underDaily && !underLearning;
  }
  if (r.caseId) {
    r.nextDue.reset();
  }
  return r;
}

LearnCounts learnCounts(const std::vector<std::string> &pool,
                        const SetProgress &progress, std::int64_t now,
                        std::int64_t dayStart, const SrsConfig &config,
                        std::int64_t dayEnd) {
  LearnCounts n;
  if (dayEnd == 0) {
    dayEnd = dayStart + kSecondsPerDay;
  }
  for (const auto &id : pool) {
    auto it = progress.find(id);
    if (it == progress.end()) {
      continue;
    }
    const CaseProgress &p = it->second;
    for (const Attempt &a : p.attempts) {
      if (a.mode == AttemptMode::Learn && a.timestamp >= dayStart) {
        ++n.doneToday;
        n.againToday += a.failed ? 1 : 0;
      }
    }
    const SrsCard &c = p.card;
    if (c.state == CardState::New) {
      continue;
    }
    if (isQueued(c)) {
      ++n.queued;
      continue;
    }
    if (isHeld(c, now)) {
      ++n.held;
    }
    if (c.state == CardState::Review) {
      ++n.learned;
      if (!c.suspended && c.due >= dayEnd && c.due < dayEnd + kSecondsPerDay) {
        ++n.dueTomorrow;
      }
    } else {
      ++n.learning;
    }
    n.due += isDueNow(c, now, dayEnd, config) ? 1 : 0;
  }
  return n;
}

std::optional<std::size_t> pickSetToReview(const std::vector<SetDue> &sets,
                                           std::mt19937_64 &rng) {
  std::vector<double> weights;
  bool any = false;
  for (const SetDue &s : sets) {
    weights.push_back(std::max(0, s.due));
    any = any || s.due > 0;
  }
  if (!any) {
    return std::nullopt;
  }
  std::discrete_distribution<std::size_t> pick(weights.begin(), weights.end());
  return pick(rng);
}

double drillReference(const std::vector<std::string> &pool,
                      const SetProgress &progress, AttemptMode mode,
                      double targetMs) {
  if (targetMs > 0) {
    return targetMs;
  }
  double total = 0;
  int counted = 0;
  for (const auto &id : pool) {
    auto it = progress.find(id);
    if (it == progress.end()) {
      continue;
    }
    if (auto m = it->second.recentMean(mode)) {
      total += *m;
      ++counted;
    }
  }
  return counted > 0 ? total / counted : 0.0;
}

std::optional<double> drillWeakness(const CaseProgress *p, AttemptMode mode,
                                    double referenceMs) {
  if (!p || p->drillCount(mode) == 0) {
    return std::nullopt;
  }
  // The median, so that one slow time among good ones counts for little.
  auto m = p->recentMedian(mode);
  double ratio = (m && referenceMs > 0) ? *m / referenceMs : 1.5;
  ratio = std::clamp(ratio, 0.5, 3.0);
  return ratio * ratio * (1.0 + 3.0 * p->failRate(mode));
}

std::vector<std::string> weakestCases(const std::vector<std::string> &pool,
                                      const SetProgress &progress,
                                      AttemptMode mode, double targetMs,
                                      std::size_t n) {
  double reference = drillReference(pool, progress, mode, targetMs);
  std::vector<std::pair<double, std::string>> weak;
  for (const auto &id : pool) {
    auto it = progress.find(id);
    if (auto w = drillWeakness(it == progress.end() ? nullptr : &it->second,
                               mode, reference)) {
      weak.emplace_back(*w, id);
    }
  }
  std::stable_sort(weak.begin(), weak.end(), [](const auto &a, const auto &b) {
    return a.first > b.first;
  });
  std::vector<std::string> out;
  for (std::size_t i = 0; i < weak.size() && i < n; ++i) {
    out.push_back(weak[i].second);
  }
  return out;
}

void CasePicker::setMode(PickMode m) {
  if (m != mode_) {
    mode_ = m;
    orderPos_ = 0;
    restartRecap();
  }
}

void CasePicker::setPool(std::vector<std::string> caseIds) {
  if (caseIds == pool_) {
    return;
  }
  // In order goes on with the first case still in the pool from where it
  // was, rather than from the top.
  std::size_t pos = 0;
  for (std::size_t k = 0; k < pool_.size(); ++k) {
    const std::string &id = pool_[(orderPos_ + k) % pool_.size()];
    auto it = std::find(caseIds.begin(), caseIds.end(), id);
    if (it != caseIds.end()) {
      pos = static_cast<std::size_t>(it - caseIds.begin());
      break;
    }
  }
  orderPos_ = pos;
  auto inPool = [&caseIds](const std::string &id) {
    return std::find(caseIds.begin(), caseIds.end(), id) != caseIds.end();
  };
  std::erase_if(recapLeft_,
                [&inPool](const std::string &id) { return !inPool(id); });
  std::erase_if(requeued_,
                [&inPool](const auto &r) { return !inPool(r.first); });
  pool_ = std::move(caseIds);
}

void CasePicker::requeue(const std::string &caseId, int after) {
  std::erase_if(requeued_,
                [&caseId](const auto &r) { return r.first == caseId; });
  if (after > 0) {
    requeued_.emplace_back(caseId, after);
  }
}

bool CasePicker::isRequeued(const std::string &caseId) const {
  return std::any_of(requeued_.begin(), requeued_.end(),
                     [&caseId](const auto &r) { return r.first == caseId; });
}

void CasePicker::restartRecap() {
  recapLeft_.clear();
  recapStarted_ = false;
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
  const bool several = pool_.size() > 1;
  if (mode_ == PickMode::Recap && !recapStarted_) {
    recapLeft_ = pool_;
    recapStarted_ = true;
  }
  const bool recapOver = mode_ == PickMode::Recap && recapLeft_.empty();
  if (recapOver && requeued_.empty()) {
    return std::nullopt;
  }
  // A requeued case whose wait is over comes first (at the end of a recap,
  // whether it is over or not), but not two in a row, so that a run of
  // failures doesn't hold up the other cases.
  std::string pick;
  for (auto it = requeued_.begin(); it != requeued_.end(); ++it) {
    if ((it->second <= 0 || recapOver) && (!lastRequeued_ || recapOver) &&
        (it->first != last_ || !several)) {
      pick = it->first;
      requeued_.erase(it);
      break;
    }
  }
  if (pick.empty() && recapOver) {
    // Only the last case is waiting: it comes even so.
    pick = requeued_.front().first;
    requeued_.erase(requeued_.begin());
  }
  for (auto &r : requeued_) {
    r.second = std::max(0, r.second - 1);
  }
  lastRequeued_ = !pick.empty();
  if (!pick.empty()) {
    std::erase(recapLeft_, pick);
    last_ = pick;
    return pick;
  }
  // Uniform over `ids` without the last case.
  auto uniform = [this, several, &rng](const std::vector<std::string> &ids) {
    auto last = std::find(ids.begin(), ids.end(), last_);
    bool skip = several && ids.size() > 1 && last != ids.end();
    std::uniform_int_distribution<std::size_t> dist(0, ids.size() -
                                                           (skip ? 2 : 1));
    std::size_t i = dist(rng);
    if (skip && i >= static_cast<std::size_t>(last - ids.begin())) {
      ++i;
    }
    return ids[i];
  };
  switch (mode_) {
  case PickMode::Random:
    pick = uniform(pool_);
    break;
  case PickMode::Weighted: {
    double reference = drillReference(pool_, progress, attemptMode_, targetMs_);
    std::vector<double> weights;
    for (const auto &id : pool_) {
      // Never drilled: favour it.
      double w = drillWeakness(find(id), attemptMode_, reference).value_or(2.0);
      // Never the same case twice in a row.
      weights.push_back(id == last_ && several ? 0.0 : std::max(w, 0.05));
    }
    std::discrete_distribution<std::size_t> dist(weights.begin(),
                                                 weights.end());
    pick = pool_[dist(rng)];
    break;
  }
  case PickMode::InOrder:
    pick = pool_[orderPos_ % pool_.size()];
    if (pick == last_ && several) {
      // Just done (started from the list): on to the one after it.
      orderPos_ = (orderPos_ + 1) % pool_.size();
      pick = pool_[orderPos_];
    }
    orderPos_ = (orderPos_ + 1) % pool_.size();
    break;
  case PickMode::Recap:
    pick = uniform(recapLeft_);
    std::erase(recapLeft_, pick);
    break;
  }
  last_ = pick;
  return pick;
}

} // namespace vcube
