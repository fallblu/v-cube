#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace vcube {

// Anki-style spaced repetition: learning steps in minutes, then reviews
// days apart with a per-card ease factor.
enum class Grade : std::uint8_t { Again = 1, Hard, Good, Easy };
enum class CardState : std::uint8_t { New, Learning, Review, Relearning };

struct SrsConfig {
  std::vector<std::int64_t> learningSteps{60, 600}; // seconds
  std::vector<std::int64_t> relearningSteps{600};   // seconds
  double graduatingDays = 1.0;
  double easyDays = 4.0;
  double startingEase = 2.5;
  double minimumEase = 1.3;
  double hardFactor = 1.2;
  double easyBonus = 1.3;
  double lapseFactor = 0.5; // review interval kept after a lapse
  double maximumDays = 365.0;
  int newPerDay = 5;
  // Learning cards due within this many seconds count as due now.
  std::int64_t learnAheadSeconds = 1200;
};

struct SrsCard {
  CardState state = CardState::New;
  int step = 0;
  double ease = 2.5;
  double intervalDays = 0.0;
  std::int64_t due = 0; // Unix seconds
  int reps = 0;
  int lapses = 0;
};

inline constexpr std::int64_t kSecondsPerDay = 86400;

void review(SrsCard &card, Grade grade, std::int64_t now,
            const SrsConfig &config);
bool isDue(const SrsCard &card, std::int64_t now, const SrsConfig &config);

// Manual status changes from the case list.
void startLearning(SrsCard &card, std::int64_t now, const SrsConfig &config);
void markLearned(SrsCard &card, std::int64_t now, const SrsConfig &config);
void resetCard(SrsCard &card);

// Learn-mode grade, from correctness rather than time: Good when the case
// was solved from memory with one of its algs, Again after a hint, giving
// up or leaving the algs.
Grade learnGrade(bool hinted, bool gaveUp, bool leftAlgs);

// Which mode an attempt was made in. Learn attempts are untimed and drive
// the SRS card; drill attempts are timed and feed the time statistics.
enum class AttemptMode : std::uint8_t { Drill, Learn };

// Per-case history plus its SRS card.
struct Attempt {
  std::int64_t timestamp = 0; // Unix seconds
  std::int64_t timeMs = 0;    // 0 for learn attempts
  bool failed = false;        // wrong alg (drill) or graded Again (learn)
  AttemptMode mode = AttemptMode::Drill;
};

struct CaseProgress {
  SrsCard card;
  std::vector<Attempt> attempts; // oldest first, capped per mode

  static constexpr std::size_t kMaxAttempts = 100; // per mode
  void record(const Attempt &a);
  // Drill statistics; learn attempts are ignored.
  // Mean of the last n successful drill attempts, in ms.
  std::optional<double> recentMean(std::size_t n = 5) const;
  std::optional<std::int64_t> best() const;
  double failRate(std::size_t n = 10) const;
  std::size_t drillCount() const;
};

// Learning status shown in the case list.
enum class CaseStatus : std::uint8_t { Unstarted, Learning, Learned };
CaseStatus statusOf(const CaseProgress *p);

using SetProgress = std::map<std::string, CaseProgress>; // by case id

// How Drill mode picks the next case.
enum class PickMode : std::uint8_t {
  Random,   // uniform, never the same case twice in a row
  Weighted, // slow and often-failed cases come up more
  InOrder,  // in list order
};

class CasePicker {
public:
  void setMode(PickMode m);
  PickMode mode() const { return mode_; }
  // Cases to choose from, in list order. Resets in-order progress if the
  // pool changed.
  void setPool(std::vector<std::string> caseIds);
  const std::vector<std::string> &pool() const { return pool_; }
  // Reference time for Weighted: cases slower than this come up more.
  // 0 compares cases with the pool's overall mean instead.
  void setTargetMs(double ms) { targetMs_ = ms; }

  std::optional<std::string> next(const SetProgress &progress,
                                  std::mt19937_64 &rng);

private:
  PickMode mode_ = PickMode::Random;
  std::vector<std::string> pool_;
  std::size_t orderPos_ = 0;
  double targetMs_ = 0;
  std::string last_;
};

// Whether a card is in the learn queue: started with "Start learning" but
// not reviewed yet.
bool isQueued(const SrsCard &card);

// Learn mode's next case.
struct LearnPick {
  std::optional<std::string> caseId;
  bool isNew = false;                  // introducing a case from the queue
  std::optional<std::int64_t> nextDue; // nothing due: when the next card is
  bool newLimitReached = false;        // queued cases wait for tomorrow
  int queued = 0;                      // cases waiting in the learn queue
};

// Due cards in their learning steps first, so that their short steps keep
// to time, then due reviews (both earliest first), then new cases
// from the learn queue (in pool order) while fewer than `config.newPerDay`
// cases were introduced since `dayStart` (the start of the local day). Only
// when none of those is left do learning cards due within
// `config.learnAheadSeconds` come up early. `last` is avoided when another
// case is due.
LearnPick pickLearnCase(const std::vector<std::string> &pool,
                        const SetProgress &progress, std::int64_t now,
                        std::int64_t dayStart, const SrsConfig &config,
                        const std::string &last = {});

// Number of cases whose first learn attempt was at or after `dayStart`.
int newCasesIntroducedSince(const SetProgress &progress, std::int64_t dayStart);

} // namespace vcube
