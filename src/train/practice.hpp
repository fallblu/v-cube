#pragma once

#include "cube/scramble.hpp"
#include "train/srs.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace vcube {

// What EO Cross, F2L and TSLE mode keep, each in the same way. A mode has
// three tabs of these:
//
//   Learn       (F2L, TSLE) the step's cases as cards, graded and spaced
//               as Learn mode's are, one unit of the step at a time.
//   Inspection  (EO Cross) the step planned in inspection and done
//               blind: tries kept by group, with the inspection time.
//   Untimed     the whole step, for efficiency: tries kept by group.
//   Timed       the whole step against the clock, on the virtual cube or
//               a physical one, kept as tries by group and cube.

// One try: an Untimed solve, an Inspection one or a timed attempt.
struct PracticeTry {
  std::int64_t timestamp = 0; // Unix seconds
  std::int64_t ms = 0;        // the time; 0 in Learn
  int moves = 0;              // HTM as simplified; 0 on a physical cube
  int best = 0;               // the best solution's moves; 0 when unknown
  // What the solve was over the best one: moves, or in F2L the fingertrick
  // cost; negative when it isn't known.
  double extra = -1.0;
  bool dnf = false; // given up, or stopped with the step not done
  bool hint = false;
  std::int64_t inspectMs = 0; // Inspection: the time looked before turning
  bool over = false;          // ...which went over the limit
  friend bool operator==(const PracticeTry &, const PracticeTry &) = default;
};

using PracticeTries = std::map<std::string, std::vector<PracticeTry>>;

struct PracticeData {
  // The cards, by case key.
  SetProgress cards;
  // The Untimed tab's solves, by group.
  PracticeTries learn;
  // The Inspection tab's tries, by group.
  PracticeTries inspection;
  // Timed attempts, by timedKey().
  PracticeTries timed;
  // The solutions chosen for a card (F2L), by card.
  std::map<std::string, std::vector<std::string>> choices;
  // What else a mode keeps (TSLE: its OCLL algs, by id).
  std::map<std::string, std::string> notes;

  // The newest tries of a group are kept.
  static constexpr std::size_t kPerGroup = 200;
  void addLearn(const std::string &group, const PracticeTry &t);
  void addInspection(const std::string &group, const PracticeTry &t);
  void addTimed(const std::string &key, const PracticeTry &t);
  friend bool operator==(const PracticeData &, const PracticeData &);
};

// "virtual/eocross", "physical/zz-1".
std::string timedKey(bool physical, const std::string &group);

// How a group's tries went.
struct PracticeSummary {
  int tries = 0;                      // in all
  int solved = 0;                     // of which not DNF
  std::optional<double> meanMs;       // of the last kRecentTries solved, timed
  std::optional<std::int64_t> bestMs; // of every solved one, timed
  std::optional<double> meanMoves;    // of the last kRecentTries with moves
  std::optional<double> meanExtra;    // ...and with a known extra
  // Of the last kRecentTries with an inspection time (solved or not).
  std::optional<double> meanInspectMs;
  int overTime = 0; // tries over the inspection limit, in all
  // Turns per second of the last kRecentTries solved with moves and a time.
  std::optional<double> meanTps;
};
inline constexpr std::size_t kRecentTries = 12;
PracticeSummary summarize(const std::vector<PracticeTry> &tries);
// The same for the tries made at or after `since` (Unix seconds).
PracticeSummary summarize(const std::vector<PracticeTry> &tries,
                          std::int64_t since);

// --- cards -------------------------------------------------------------------

// Cards come in by themselves, in the order of `pool`: every case of the
// pool that was never started is put in the learn queue, from where
// pickLearnCase() introduces a few each day (SrsConfig's newPerDay and
// maxLearning). True if any card was queued.
bool queueUnstarted(const std::vector<std::string> &pool, SetProgress &cards,
                    std::int64_t now, const SrsConfig &config);

// What the cards of a pool have waiting.
struct PracticeWaiting {
  int due = 0;     // learning steps due now and reviews due today
  int newLeft = 0; // cases that can still come in today
  int learned = 0; // in review
  int started = 0; // introduced, learned or not
  int total = 0;
};
PracticeWaiting practiceWaiting(const std::vector<std::string> &pool,
                                const SetProgress &cards, std::int64_t now,
                                std::int64_t dayStart, std::int64_t dayEnd,
                                const SrsConfig &config);

// The grade of a card's try: Good when the case was solved unaided and
// well, Again after a hint, giving up or a poor solution.
Grade practiceGrade(bool solvedWell, bool hinted, bool gaveUp);

} // namespace vcube
