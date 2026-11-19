#pragma once

#include "cube/cubie.hpp"
#include "cube/scramble.hpp"
#include "train/algset.hpp"
#include "train/case_logic.hpp"
#include "train/stats.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vcube {

// Alg Lab: candidate algs for a case, collected from the case's set, from
// the generator or pasted, compared by move counts and an ergonomic score,
// and timed one alg at a time in test sessions, so that each alg keeps its
// own history of times.

// A case of Alg Lab's own, not in any set: the state a setup alg makes
// from a solved cube, and what its algs must solve.
struct LabCase {
  std::string id; // unique among lab cases
  std::string name;
  std::string setup; // done to a solved cube, it makes the case
  std::string free;  // what the algs may leave unsolved (freeToString)
  bool aufBefore = true;
  bool aufAfter = true;
  friend bool operator==(const LabCase &, const LabCase &) = default;
};

// The case state of a lab case (solved if its setup can't be read).
CubieCube labCaseState(const LabCase &c);
// Its free pieces (none if the text can't be read).
PieceMask labCaseFree(const LabCase &c);

// A case of Alg Lab, by its key: "set:<set id>/<case id>" for a case of an
// alg set, "lab:<id>" for a lab case.
struct LabCaseKey {
  std::string setId; // empty for a lab case
  std::string caseId;

  bool isLab() const { return setId.empty(); }
  std::string text() const;
  static std::optional<LabCaseKey> parse(std::string_view text);
  friend bool operator==(const LabCaseKey &, const LabCaseKey &) = default;
  friend auto operator<=>(const LabCaseKey &, const LabCaseKey &) = default;
};

// An alg as Alg Lab files it: its moves written out plainly
// (toString(parseAlg)), so that spacing, brackets or an R3 written for R'
// don't make another alg; text that can't be read is kept as it is typed.
std::string labAlgKey(std::string_view alg);

// Where a candidate came from.
enum class CandidateSource : std::uint8_t { Set, Generated, Pasted, Typed };
std::string_view candidateSourceId(CandidateSource s);
std::optional<CandidateSource> candidateSourceFromId(std::string_view id);

// A candidate alg kept for a case (the set's own algs are not kept: they
// are shown from the set).
struct Candidate {
  std::string alg; // labAlgKey() form
  CandidateSource source = CandidateSource::Typed;
  std::int64_t added = 0; // Unix seconds
  friend bool operator==(const Candidate &, const Candidate &) = default;
};

struct CaseCandidates {
  LabCaseKey key;
  std::vector<Candidate> candidates;
  friend bool operator==(const CaseCandidates &,
                         const CaseCandidates &) = default;
};

// One timed attempt at one alg.
struct LabAttempt {
  LabCaseKey key;
  std::string alg; // labAlgKey() form
  CubeKind cube = CubeKind::Physical;
  std::int64_t timeMs = 0;
  bool dnf = false;
  // Virtual cube: the moves left the alg. Such a time is left out of the
  // statistics unless `include` is set.
  bool deviated = false;
  bool include = false;
  std::string session; // the session's id
  std::int64_t timestamp = 0;
  std::string scramble; // physical: the scramble that set the case up
  std::string moves;    // virtual: the moves done
  friend bool operator==(const LabAttempt &, const LabAttempt &) = default;
};

// How a test session orders its algs.
enum class LabOrder : std::uint8_t {
  Interleaved, // rounds of every alg once, in a random order
  Blocked,     // every rep of one alg, then the next
};
std::string_view labOrderId(LabOrder o);
std::optional<LabOrder> labOrderFromId(std::string_view id);

struct LabEntry {
  LabCaseKey key;
  std::string alg; // labAlgKey() form
  friend bool operator==(const LabEntry &, const LabEntry &) = default;
};

struct LabSession {
  std::string id; // unique, kept by attempts
  std::string name = "Session 1";
  CubeKind cube = CubeKind::Physical;
  LabOrder order = LabOrder::Interleaved;
  int reps = 12;       // per alg
  int warmup = 1;      // the first reps of each alg left out of statistics
  int trimPercent = 0; // of the times, the best and worst this % left out
  // A random U turn before the case, so that the AUF is timed too.
  // Otherwise each alg's case is set up from the side the alg starts from.
  bool randomAuf = false;
  std::vector<LabEntry> entries;
  friend bool operator==(const LabSession &, const LabSession &) = default;
};

// How many attempts of each entry a session has (by entry index).
std::vector<int> repsDone(const LabSession &s,
                          std::span<const LabAttempt> attempts);

// The entry to time next: in a blocked session the first entry still
// short of its reps; in an interleaved one a random entry among those with
// the fewest attempts, never `last` (the entry just timed) when another
// will do. nullopt once every entry has its reps (or there are none).
std::optional<std::size_t> nextEntry(const LabSession &s,
                                     std::span<const LabAttempt> attempts,
                                     std::optional<std::size_t> last, Rng &rng);

// Which attempts the statistics take.
struct LabWindow {
  enum class Kind : std::uint8_t { All, Session, Recent };
  Kind kind = Kind::All;
  std::string session; // Kind::Session
  int recent = 50;     // Kind::Recent: the last this many
};

// The statistics of one alg on one cube kind.
struct LabStats {
  int attempts = 0; // every attempt in the window
  int used = 0;     // after warm-up, deviations and the trim
  int timed = 0;    // after warm-up and deviations: the times and the DNFs
  int dnfs = 0;     // of those
  int deviated = 0; // left out for leaving the alg
  std::optional<std::int64_t> mean, median, best, worst;
  std::optional<double> sd; // milliseconds
  StatValue ao5, ao12;      // current
  double dnfRate() const;
};

// The warm-up reps of a session (by its id), for the statistics.
using WarmupOf = std::function<int(std::string_view session)>;

// The attempts of one alg (oldest first) that the statistics are worked
// out from: those in the window, without the first warm-up reps of each
// session (each session's own setting, from `warmupOf`; none without it)
// and without deviations unless included.
std::vector<LabAttempt> labTimed(std::span<const LabAttempt> attempts,
                                 const LabCaseKey &key, std::string_view alg,
                                 CubeKind cube, const LabWindow &window,
                                 const WarmupOf &warmupOf = {});

// Statistics of labTimed()'s attempts; `trimPercent` of the times at each
// end are left out of the mean, median and SD (not of the averages, which
// trim as the WCA does).
LabStats labStats(std::span<const LabAttempt> attempts, const LabCaseKey &key,
                  std::string_view alg, CubeKind cube, const LabWindow &window,
                  int trimPercent, const WarmupOf &warmupOf = {});
LabStats statsOf(std::span<const LabAttempt> timed, int trimPercent,
                 int windowAttempts, int deviated);

// The attempts sorted out by case and alg, each alg's oldest first, for a
// table of many algs: labTimed() and labStats() make of an alg's own
// attempts what they make of them all, without going through them all for
// every row.
class LabAttemptGroups {
public:
  LabAttemptGroups() = default;
  explicit LabAttemptGroups(std::span<const LabAttempt> attempts);
  // Only the attempts of one case.
  LabAttemptGroups(std::span<const LabAttempt> attempts, const LabCaseKey &key);
  // Empty for an alg never timed.
  std::span<const LabAttempt> of(const LabCaseKey &key,
                                 const std::string &alg) const;

private:
  std::map<std::pair<LabCaseKey, std::string>, std::vector<LabAttempt>> groups_;
};

// Attempts as solves, oldest first (DNFs kept).
std::vector<Solve> labSolves(std::span<const LabAttempt> timed);

// The case's state with the alg's lead-in done to it, as a test session
// sets it up: the AUF the alg needs before it, and a leading y rotation, so
// that the alg solves it as it is held. With `randomAuf`, a random U turn
// is done after that instead, and the solver must do the AUF.
CubieCube labSetup(const CubieCube &state, const std::string &alg,
                   const PieceMask &free, bool aufBefore, bool aufAfter,
                   bool randomAuf, Rng &rng);

} // namespace vcube
