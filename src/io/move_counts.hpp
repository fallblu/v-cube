#pragma once

#include "train/move_count.hpp"

#include "io/session_store.hpp"

#include <QJsonObject>
#include <QString>

#include <string>
#include <vector>

namespace vcube {

// A Move Count session: the attempts at one step from one scramble type,
// e.g. "EO Cross" from WCA scrambles or "ZZ F2L" from EO Cross solved, on
// the virtual cube (which checks the step) or a physical one (the count is
// typed in). Untimed mode keeps its sessions the same way: whole solves,
// from any scramble type, with no step.
struct MoveCountSession {
  std::string name = "Session 1";
  std::string scrambleType = "333"; // Move Count: moveCountScrambleTypes()
  std::vector<MoveCountAttempt> attempts;
  CubeKind cube = CubeKind::Virtual;
  SolveStep step = SolveStep::EOCross; // Move Count only
};

// Which mode a store is for.
enum class SolveLog { MoveCount, Untimed };

// The sessions a new store starts with. Move Count: one per step on the
// virtual cube, from WCA scrambles (EO Cross solved for ZZ F2L). Untimed:
// one session on the virtual cube.
std::vector<MoveCountSession>
defaultMoveCountSessions(SolveLog mode = SolveLog::MoveCount);

// The file format of each mode: "v-cube.movecount" or "v-cube.untimed".
FileFormat moveCountFormat(SolveLog mode);

// Move Count or Untimed mode's sessions, saved as one JSON file. A new
// file starts with defaultMoveCountSessions().
class MoveCountStore : public SessionFileStore<MoveCountSession> {
public:
  explicit MoveCountStore(QString path, SolveLog mode = SolveLog::MoveCount);

  // Saved in the background from its JSON cache, not a copy of every
  // attempt.
  static constexpr bool kCachesJson = true;

protected:
  std::vector<MoveCountSession> defaults() const override;
  QJsonObject toJson() const override;
  void fromJson(const QJsonObject &o) override;

private:
  SolveLog mode_;
  mutable ItemJsonCache<MoveCountAttempt> cache_;
};

// {"format": "v-cube.movecount", "version": 1, "current": 0, "sessions":
// [{"name": ..., "scrambleType": "eocross", "cube": "virtual", "step":
// "zzf2l", "attempts": [{"moves": 23, "scramble": ..., "timestamp": ...,
// "dnf": true, "comment": ..., "solution": ...}]}]}; "dnf", "comment" and
// "solution" are left out when false or empty, and "moves" when the attempt
// isn't counted. Untimed files are the same with "format": "v-cube.untimed"
// and no "step".
//
// Files from before sessions had a cube were for physical cubes only, so a
// session without "cube" is for a physical cube. A Move Count session
// without "step" takes it from its name, or else from its scramble type.
// In Move Count, a scramble type it doesn't offer reads as "333".
QJsonObject moveCountsToJson(const std::vector<MoveCountSession> &sessions,
                             std::size_t current,
                             SolveLog mode = SolveLog::MoveCount);
std::vector<MoveCountSession>
moveCountsFromJson(const QJsonObject &o, std::size_t *current = nullptr,
                   SolveLog mode = SolveLog::MoveCount);

} // namespace vcube
