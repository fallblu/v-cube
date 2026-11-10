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
  double lapseFactor = 0.2; // review interval kept after a lapse
  double maximumDays = 365.0;
  // A Good review raises the ease by this much, up to the starting ease,
  // so that a case that once went badly can recover with Learn's
  // automatic grades (which give Good or Again, rarely Easy). 0: never.
  double easeRecovery = 0.05;
  // A review done days late is credited with part of the delay, as in
  // Anki: a quarter of it for Hard, half for Good, all of it for Easy.
  bool creditLateReviews = true;
  // Hard moves a card on to its next learning step (but never graduates
  // it), rather than repeating the step.
  bool hardAdvancesSteps = true;
  int newPerDay = 5;
  // New cases are introduced only while fewer cards than this are in
  // their learning steps (0: no limit).
  int maxLearning = 3;
  // Learning cards due within this many seconds count as due now.
  std::int64_t learnAheadSeconds = 0;
  // A case forgotten this many times is a leech (0: never).
  int leechLapses = 6;
};

struct SrsCard {
  CardState state = CardState::New;
  int step = 0;
  double ease = 2.5;
  double intervalDays = 0.0;
  std::int64_t due = 0; // Unix seconds
  int reps = 0;
  int lapses = 0;
  // When the case was introduced: first reviewed, or marked learned
  // (Unix seconds); 0 while it never was, so that a case marked learned
  // isn't taken for one never seen.
  std::int64_t introduced = 0;
  // Introduced by "Mark as learned" rather than in Learn: it doesn't count
  // against the day's new cases.
  bool marked = false;
  // Left out of Learn until unsuspended (Drill still has it).
  bool suspended = false;
  // Left out of Learn until this time (the end of the day it was buried).
  std::int64_t buriedUntil = 0;
};

inline constexpr std::int64_t kSecondsPerDay = 86400;

// `rng`, when given, spreads review intervals of three days or more by a
// few percent, so that cases learned together drift apart.
void review(SrsCard &card, Grade grade, std::int64_t now,
            const SrsConfig &config, std::mt19937_64 *rng = nullptr);
// How long after `now` the card would be due after `grade` (without the
// spread), e.g. to show on the grade buttons.
std::int64_t nextInterval(const SrsCard &card, Grade grade, std::int64_t now,
                          const SrsConfig &config);
// A guided walkthrough introduced the card without grading it: it comes
// back at its first learning step to be recalled unaided.
void introduce(SrsCard &card, std::int64_t now, const SrsConfig &config);
// Whether a card is due at `now`. Cards in their learning steps are due at
// their time (or up to `config.learnAheadSeconds` before it); review cards
// are due all day on the day they fall due, as in Anki, i.e. before
// `dayEnd` (the start of the next local day) when that is given.
bool isDue(const SrsCard &card, std::int64_t now, const SrsConfig &config,
           std::int64_t dayEnd = 0);

// Manual status changes from the case list.
void startLearning(SrsCard &card, std::int64_t now, const SrsConfig &config);
void markLearned(SrsCard &card, std::int64_t now, const SrsConfig &config);
void resetCard(SrsCard &card);
// Out of Learn until `until` (the end of the day).
void bury(SrsCard &card, std::int64_t until);
bool isBuried(const SrsCard &card, std::int64_t now);
// Suspended or buried: Learn leaves it out.
bool isHeld(const SrsCard &card, std::int64_t now);
// Forgotten `config.leechLapses` times or more.
bool isLeech(const SrsCard &card, const SrsConfig &config);

// Learn-mode grade, from correctness rather than time: Good when the case
// was solved from memory with one of its algs, Again after a hint, giving
// up or leaving the algs.
Grade learnGrade(bool hinted, bool gaveUp, bool leftAlgs);

// Which mode an attempt was made in. Learn attempts are untimed and drive
// the SRS card; drill attempts are timed and feed the time statistics,
// kept apart for drills on a physical cube and on the virtual cube.
enum class AttemptMode : std::uint8_t { PhysicalDrill, Learn, VirtualDrill };

// Per-case history plus its SRS card.
struct Attempt {
  std::int64_t timestamp = 0; // Unix seconds
  std::int64_t timeMs = 0;    // 0 for learn attempts
  bool failed = false;        // wrong alg (drill) or graded Again (learn)
  AttemptMode mode = AttemptMode::PhysicalDrill;
  // From the case being set up to the first turn of a layer off the U-D
  // axis (or to Space in a recognition review), and from there to the
  // end; 0 when not measured (physical drills, hints, older files).
  std::int64_t recognitionMs = 0;
  std::int64_t executionMs = 0;
  // A recognition review: the case was recognised, not solved.
  bool recognitionOnly = false;
};

// The local calendar day of a Unix time, as days since 1970-01-01.
std::int64_t localDayNumber(std::int64_t unixSeconds);

// One day's drill attempts of a case in one mode: how many, how many
// failed, and the sum and best of the times of those that didn't.
struct DayRollup {
  std::int64_t day = 0; // localDayNumber()
  int count = 0;
  int fails = 0;
  std::int64_t sumMs = 0;  // of the good attempts
  std::int64_t bestMs = 0; // 0: none was good
  int good() const { return count - fails; }
  std::optional<double> meanMs() const;
  friend bool operator==(const DayRollup &, const DayRollup &) = default;
};

// What is kept of a drill mode's attempts once they no longer are: the
// best time, and the attempts summed up day by day (oldest first).
struct DrillArchive {
  std::int64_t bestMs = 0; // 0: none
  std::vector<DayRollup> days;
  bool empty() const { return bestMs == 0 && days.empty(); }
  friend bool operator==(const DrillArchive &, const DrillArchive &) = default;
};

struct CaseProgress {
  SrsCard card;
  std::vector<Attempt> attempts; // oldest first, capped per mode
  // Per drill mode, the attempts that dropped out of `attempts`.
  std::map<AttemptMode, DrillArchive> archive;

  static constexpr std::size_t kMaxAttempts = 100; // per mode
  // Adds the attempt; a drill attempt that drops out of the cap is kept in
  // its mode's archive.
  void record(const Attempt &a);
  // Statistics of the drill attempts made in `mode` (a drill mode); other
  // attempts are ignored.
  // Mean of the last n successful drill attempts, in ms.
  std::optional<double> recentMean(AttemptMode mode, std::size_t n = 5) const;
  // Median of the last n successful drill attempts, in ms.
  std::optional<double> recentMedian(AttemptMode mode, std::size_t n = 5) const;
  // The best time ever, archived attempts included.
  std::optional<std::int64_t> best(AttemptMode mode) const;
  double failRate(AttemptMode mode, std::size_t n = 10) const;
  std::size_t drillCount(AttemptMode mode) const;
  // Every attempt in `mode` summed up by day, archived ones included,
  // oldest first.
  std::vector<DayRollup> days(AttemptMode mode) const;
  // Deletes the last attempt made in `mode`; false if there is none.
  bool removeLast(AttemptMode mode);
  // Forgets every drill attempt in `mode`, archived ones too.
  void clearDrill(AttemptMode mode);
  // Mean recognition time of the last n attempts that measured it, in any
  // mode, in ms.
  std::optional<double> recognitionMean(std::size_t n = 5) const;
};

// A case's attempts when it may share its card with its mirror family:
// its own attempts and, from `familyCard` (null when the case has its own
// card), the family's Learn attempts, oldest first.
std::vector<Attempt> caseAttempts(const CaseProgress *own,
                                  const CaseProgress *familyCard);
// Mean recognition time of the last n attempts that measured it, in ms.
std::optional<double> recognitionMean(const std::vector<Attempt> &attempts,
                                      std::size_t n = 5);

// Learning status shown in the case list.
enum class CaseStatus : std::uint8_t { Unstarted, Learning, Learned };
CaseStatus statusOf(const CaseProgress *p);

// The card back to new and its Learn attempts gone; drill attempts stay.
void resetLearning(CaseProgress &progress);

using SetProgress = std::map<std::string, CaseProgress>; // by case id

// Whether a card is in the learn queue: started with "Start learning" but
// not introduced yet. Its `due` is when it was queued.
bool isQueued(const SrsCard &card);

// The order new cases are introduced in (and queued in by "Start
// learning"), chosen for each set.
enum class NewOrder : std::uint8_t {
  ListOrder,   // as the case list shows them
  Queued,      // the order they were put in the learn queue
  Shuffled,    // a fixed random order
  RoundRobin,  // one case from each group in turn
  ShortestAlg, // fewest moves in the main alg first
  FewestSteps, // in a step-at-a-time set: fewest steps left first
};

// What ordering a case for introduction needs to know.
struct NewCase {
  std::string id;
  std::string group;       // its group, for RoundRobin
  int algMoves = 0;        // moves in its main alg, for ShortestAlg
  int stepsLeft = 0;       // for FewestSteps
  std::int64_t queued = 0; // when it was queued, for Queued
};

// The cases in the order to introduce them. Ties keep the given (list)
// order; Shuffled is the same order every time for the same `seed`.
std::vector<std::string> orderNewCases(const std::vector<NewCase> &cases,
                                       NewOrder order, std::uint64_t seed);

// Learn mode's next case.
struct LearnPick {
  std::optional<std::string> caseId;
  bool isNew = false;                  // introducing a case from the queue
  std::optional<std::int64_t> nextDue; // nothing due: when the next card is
  bool newLimitReached = false;        // queued cases wait for tomorrow
  // Queued cases wait until fewer cards are in their learning steps.
  bool learningLimitReached = false;
  int queued = 0; // cases waiting in the learn queue
};

// What pickLearnCase may choose from, and how.
struct LearnPickOptions {
  std::string last;     // avoided when another case is due
  bool reviews = true;  // reviews and learning steps
  bool newCases = true; // cases from the learn queue
  // Nothing else being left, learning cards come up however far off they
  // are due (not only within the learn-ahead time).
  bool early = false;
  // Picks among the reviews due today at random rather than earliest
  // first, so that cases learned together don't come up in the same
  // order every day.
  std::mt19937_64 *rng = nullptr;
  // The start of the next local day (0: `dayStart` plus a day, which is
  // off by an hour on the days clocks change).
  std::int64_t dayEnd = 0;
};

// Due cards in their learning steps first, so that their short steps keep
// to time (earliest first), then reviews due today, then new cases from
// the learn queue (in pool order, see orderNewCases()) while fewer than
// `config.newPerDay` cases were introduced since `dayStart` (the start of
// the local day) and fewer than `config.maxLearning` are in their learning
// steps. Only when none of those is left do learning cards due within
// `config.learnAheadSeconds` come up early. Suspended and buried cards
// never come up.
LearnPick pickLearnCase(const std::vector<std::string> &pool,
                        const SetProgress &progress, std::int64_t now,
                        std::int64_t dayStart, const SrsConfig &config,
                        const LearnPickOptions &options);
LearnPick pickLearnCase(const std::vector<std::string> &pool,
                        const SetProgress &progress, std::int64_t now,
                        std::int64_t dayStart, const SrsConfig &config,
                        const std::string &last = {});

// The counts Learn shows over a pool of cards.
struct LearnCounts {
  int queued = 0;      // in the learn queue
  int learning = 0;    // in their (re)learning steps
  int learned = 0;     // in review
  int due = 0;         // of those two, due now (reviews: today)
  int held = 0;        // suspended or buried
  int doneToday = 0;   // Learn attempts since the day started
  int againToday = 0;  // of which graded Again
  int dueTomorrow = 0; // reviews falling due tomorrow
};
// `dayEnd` as in LearnPickOptions.
LearnCounts learnCounts(const std::vector<std::string> &pool,
                        const SetProgress &progress, std::int64_t now,
                        std::int64_t dayStart, const SrsConfig &config,
                        std::int64_t dayEnd = 0);
// Whether Learn would offer the card now (due and not held): learning
// steps due now, reviews due today.
bool isDueNow(const SrsCard &card, std::int64_t now, std::int64_t dayEnd,
              const SrsConfig &config);

// One set's due cards, for reviewing across sets.
struct SetDue {
  std::string setId;
  int due = 0;
};
// The set to take the next review from: at random, weighted by how many
// are due in each, so that sets are interleaved; nullopt when none is due.
std::optional<std::size_t> pickSetToReview(const std::vector<SetDue> &sets,
                                           std::mt19937_64 &rng);

// Number of cases introduced in Learn at or after `dayStart` (not those
// marked learned).
int newCasesIntroducedSince(const SetProgress &progress, std::int64_t dayStart);
// Cards of the pool in their learning steps as new cases (not queued, not
// relearning), which `config.maxLearning` limits.
int learningCount(const std::vector<std::string> &pool,
                  const SetProgress &progress);

} // namespace vcube
