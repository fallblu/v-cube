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

// Automatic grade: Again on a fail or DNF; otherwise by time against the
// set's target time (Easy <= 60%, Good <= 100%, else Hard).
Grade gradeFromTime(double seconds, double targetSeconds, bool failed);

// Per-case drill history plus its SRS card.
struct Attempt {
  std::int64_t timestamp = 0; // Unix seconds
  std::int64_t timeMs = 0;
  bool failed = false;
};

struct CaseProgress {
  SrsCard card;
  std::vector<Attempt> attempts; // oldest first, capped

  static constexpr std::size_t kMaxAttempts = 100;
  void record(const Attempt &a);
  // Mean of the last n successful attempts, in ms.
  std::optional<double> recentMean(std::size_t n = 5) const;
  std::optional<std::int64_t> best() const;
  double failRate(std::size_t n = 10) const;
};

// Learning status shown in the case list.
enum class CaseStatus : std::uint8_t { Unstarted, Learning, Learned };
CaseStatus statusOf(const CaseProgress *p);

using SetProgress = std::map<std::string, CaseProgress>; // by case id

// How the trainer picks the next case.
enum class PickMode : std::uint8_t {
  Random,   // uniform, never the same case twice in a row
  Weighted, // slow and often-failed cases come up more
  Recap,    // every case once, in random order
  InOrder,  // in list order
  Srs,      // due reviews, then new cases up to the daily limit
};

struct PickResult {
  std::optional<std::string> caseId;
  bool isNew = false;              // SRS: introducing a new case
  std::optional<std::int64_t> nextDue; // SRS: when the next card is due
  bool roundComplete = false;      // Recap: everything has been seen
};

class CasePicker {
public:
  void setMode(PickMode m);
  PickMode mode() const { return mode_; }
  // Cases to choose from, in list order. Resets recap/in-order progress if
  // the pool changed.
  void setPool(std::vector<std::string> caseIds);
  const std::vector<std::string> &pool() const { return pool_; }

  // `dayStart`: Unix time of the start of the local day, for the SRS
  // new-cases-per-day limit.
  PickResult next(const SetProgress &progress, std::int64_t now,
                  std::int64_t dayStart, const SrsConfig &config,
                  std::mt19937_64 &rng);

  // Recap progress, e.g. "4 / 21".
  std::size_t recapDone() const { return recapPos_; }
  std::size_t recapTotal() const { return recapOrder_.size(); }

private:
  PickMode mode_ = PickMode::Random;
  std::vector<std::string> pool_;
  std::vector<std::string> recapOrder_;
  std::size_t recapPos_ = 0;
  std::size_t orderPos_ = 0;
  std::string last_;
};

// Number of cases whose first attempt was at or after `dayStart`.
int newCasesIntroducedSince(const SetProgress &progress, std::int64_t dayStart);

} // namespace vcube
