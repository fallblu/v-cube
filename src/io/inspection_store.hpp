#pragma once

#include "cube/hold.hpp"
#include "train/move_count.hpp"
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
  // EO axes, once it is worked out.
  int moves = 0;
  bool dnf = false;
  std::string solution;
  std::optional<int> optimal;

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
  std::vector<InspectionAttempt> attempts;
};

// The steps a Plan session can be for (all but ZZ F2L).
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
// "moves": 8, "dnf": true, "solution": ..., "optimal": 7, "comment": ...}]},
// {"name": ..., "drill": "edges", "axes": "both", "attempts":
// [{"scramble": ..., "timestamp": ..., "ms": 5400, "hold": "02", "fb": 4,
// "fbCorrect": 4, "rl": 6, "rlCorrect": 8}]}]}. "step" is written for Plan
// sessions only and "axes" for Edges sessions only; "limit" is left out
// when there is none, and "wca", "dnf", "solution", "optimal" and
// "comment" when false, empty or unknown, as are the counts of an axis
// that wasn't asked. "hold" is the top and front colours (0 white, 1 red,
// 2 green, 3 yellow, 4 orange, 5 blue). A session with an unknown drill
// reads as Plan, a Plan session with an unknown step (or ZZ F2L) as EO
// Cross.
QJsonObject inspectionToJson(const std::vector<InspectionSession> &sessions,
                             std::size_t current);
std::vector<InspectionSession>
inspectionFromJson(const QJsonObject &o, std::size_t *current = nullptr);

} // namespace vcube
