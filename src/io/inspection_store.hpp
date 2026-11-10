#pragma once

#include "cube/hold.hpp"
#include "train/move_count.hpp"
#include "train/solve_review.hpp"
#include "train/stats.hpp"

#include "io/data_file.hpp"

#include <QJsonObject>
#include <QString>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vcube {

// What an Inspection session practises: planning a step (EO, EO+1, EO+2,
// EO Cross) in inspection and doing it with the cube hidden, or counting
// the bad edges of a scramble.
enum class InspectionDrill : std::uint8_t { Plan, Edges };
// "plan" or "edges"; nullopt for any other text.
std::string_view drillId(InspectionDrill d);
std::optional<InspectionDrill> drillFromId(std::string_view id);

// The axes the bad edges are counted for, as the cube is held: F/B (the
// ZZ axis), R/L, or F/B then R/L.
enum class EdgeAxes : std::uint8_t { FB, RL, Both };
// "fb", "rl" or "both"; nullopt for any other text.
std::string_view axesId(EdgeAxes a);
std::optional<EdgeAxes> axesFromId(std::string_view id);
// Whether the axis (0 F/B, 1 R/L) is asked for.
bool asksAxis(EdgeAxes a, int axis);

// One scramble of an Inspection session.
struct InspectionAttempt {
  std::string scramble;
  std::int64_t timestamp = 0; // Unix seconds when it was recorded
  std::string comment;
  // Plan: the inspection time used. Edges: the time to the last answer.
  std::int64_t ms = 0;

  // Plan: the moves done with the cube hidden (HTM), DNF when the step
  // wasn't done, the moves as done (VirtualAttempt::solution()) and the
  // optimal length within the holds of the colour neutrality, with both
  // EO axes, once it is worked out. With a pair tracked after EO Cross,
  // `moves` are those to EO Cross and `pairMoves` the rest.
  int moves = 0;
  bool dnf = false;
  std::string solution;
  std::optional<int> optimal;
  // Plan: the hold the step was done in (cross colour on the bottom, the
  // EO axis front-back), when it was done.
  std::optional<Hold> solvedIn;
  // Plan with a pair tracked: whether its slot was solved too.
  std::optional<bool> pairSolved;
  int pairMoves = 0;

  // Edges: the hold the cube was shown in, and for F/B ([0]) and R/L ([1])
  // the count given and the right count (-1 for an axis not asked).
  Hold hold;
  std::array<int, 2> answer{-1, -1};
  std::array<int, 2> correct{-1, -1};

  // Edges: every count asked for was right.
  bool right() const;
};

struct InspectionSession {
  std::string name = "Session 1";
  InspectionDrill drill = InspectionDrill::Plan;
  SolveStep step = SolveStep::EOCross; // Plan: EO, EO+1, EO+2 or EO Cross
  EdgeAxes axes = EdgeAxes::Both;      // Edges
  // Plan: inspection ends after this many seconds (0: no limit). With
  // `wca` (only for 15 s) the 8 and 12 second calls are shown.
  int limitSeconds = 15;
  bool wca = true;
  // Plan, EO Cross only: the F2L pair to solve (blind) after EO Cross, by
  // its slot as the cube is held when the cross is done.
  std::optional<Slot> pair;
  std::vector<InspectionAttempt> attempts;
};

// The steps a Plan session can be for (all but ZZ F2L): EO, EOLine, EO+1,
// EO+2 and EO Cross.
const std::vector<SolveStep> &planSteps();

// The sessions a new file starts with: EO, EO+2 and EO Cross planning with
// WCA inspection, and bad edges for both axes with no limit.
std::vector<InspectionSession> defaultInspectionSessions();

// Plan attempts as solves of one "millisecond" per thousandth of a move (as
// attemptsAsSolves() does for Move Count), DNF kept, so that the timer's
// statistics work on them.
std::vector<Solve> planAsSolves(std::span<const InspectionAttempt> attempts);
// Edges attempts as timed solves, with a wrong count as DNF.
std::vector<Solve> edgesAsSolves(std::span<const InspectionAttempt> attempts);

// Plan attempts with an optimum, as moves over it (in thousandths of a
// move, as planAsSolves()), DNF kept; those without one are left out.
std::vector<Solve>
overOptimalAsSolves(std::span<const InspectionAttempt> attempts);
// For each Plan attempt with an optimum, the share of the last `window`
// of them (up to it) that were optimal, as a "time" of one millisecond per
// thousandth of a percent (so that 50% charts as 50.0). A DNF is not
// optimal.
std::vector<Solve>
optimalRateAsSolves(std::span<const InspectionAttempt> attempts,
                    std::size_t window = 12);

// How a group of Plan attempts went.
struct PlanSummary {
  int attempts = 0;
  int dnfs = 0;
  int compared = 0;  // done, with an optimum known
  int atOptimal = 0; // of those, the ones that were optimal
  int over = 0;      // of those, the moves over optimal in all
  std::int64_t inspectionMs = 0;

  void add(const InspectionAttempt &a);
  // Mean moves over optimal and the share at optimal (0..1), of the
  // attempts compared; nullopt without any.
  std::optional<double> meanOver() const;
  std::optional<double> optimalRate() const;
};
PlanSummary summarizePlan(std::span<const InspectionAttempt> attempts);

// Plan attempts by their optimal length, shortest first (attempts without
// one are left out).
std::vector<std::pair<int, PlanSummary>>
planByOptimal(std::span<const InspectionAttempt> attempts);

// Plan attempts by the inspection time used, in the ranges [from, to)
// seconds (to < 0: no end) of kInspectionRanges; ranges with no attempts
// are left out.
struct InspectionRange {
  int from = 0;
  int to = -1;
  PlanSummary summary;
};
inline constexpr std::array<int, 6> kInspectionRanges = {0, 5, 8, 12, 15, 20};
std::vector<InspectionRange>
planByInspection(std::span<const InspectionAttempt> attempts);

// "fr", "fl", "bl", "br" for the slots; nullopt for any other text.
std::string slotId(Slot s);
std::optional<Slot> slotFromId(std::string_view id);

// Inspection mode's sessions, saved as one JSON file.
class InspectionStore {
public:
  explicit InspectionStore(QString path);

  // A missing file gives the default sessions. A file that can't be read
  // is set aside (see setAside()) and also gives the default sessions; if
  // it can't be moved, nothing is saved over it.
  bool load(QString *error = nullptr);
  // Where load() moved an unreadable file, if it did.
  const QString &setAsidePath() const { return file_.setAsidePath(); }
  bool save(QString *error = nullptr) const;

  std::vector<InspectionSession> &sessions() { return sessions_; }
  const std::vector<InspectionSession> &sessions() const { return sessions_; }
  std::size_t currentIndex() const { return current_; }
  void setCurrentIndex(std::size_t i);
  InspectionSession &current() { return sessions_[current_]; }

  InspectionSession &addSession(const std::string &name);
  // Removing the last session leaves one empty session.
  void removeSession(std::size_t i);

  QString path() const { return path_; }

private:
  QString path_;
  std::vector<InspectionSession> sessions_;
  std::size_t current_ = 0;
  DataFile file_;
};

// {"format": "v-cube.inspection", "version": 1, "current": 0, "sessions":
// [{"name": ..., "drill": "plan", "step": "eocross", "limit": 15, "wca":
// true, "attempts": [{"scramble": ..., "timestamp": ..., "ms": 11230,
// "moves": 8, "dnf": true, "solution": ..., "optimal": 7, "hold": "02",
// "pair": true, "pairMoves": 9, "comment": ...}]},
// {"name": ..., "drill": "edges", "axes": "both", "attempts":
// [{"scramble": ..., "timestamp": ..., "ms": 5400, "hold": "02", "fb": 4,
// "fbCorrect": 4, "rl": 6, "rlCorrect": 8}]}]}. "step" is written for Plan
// sessions only and "axes" for Edges sessions only; a Plan session tracking
// a pair after EO Cross has "pair": "bl" (its slot). "limit" is left out
// when there is none, and "wca", "dnf", "solution", "optimal", "hold",
// "pair", "pairMoves" and "comment" when false, empty or unknown, as are
// the counts of an axis that wasn't asked (a Plan attempt's "pair" is
// written when a pair was tracked, true or false). "hold" is the top and front
// colours (0 white, 1 red, 2 green, 3 yellow, 4 orange, 5 blue). A session with
// an unknown drill reads as Plan, a Plan session with an unknown step (or ZZ
// F2L) as EO Cross.
QJsonObject inspectionToJson(const std::vector<InspectionSession> &sessions,
                             std::size_t current);
std::vector<InspectionSession>
inspectionFromJson(const QJsonObject &o, std::size_t *current = nullptr);

} // namespace vcube
