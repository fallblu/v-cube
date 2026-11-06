#pragma once

#include "train/move_count.hpp"

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
std::vector<MoveCountSession> defaultMoveCountSessions(
    SolveLog mode = SolveLog::MoveCount);

// Move Count or Untimed mode's sessions, saved as one JSON file.
class MoveCountStore {
public:
  explicit MoveCountStore(QString path,
                          SolveLog mode = SolveLog::MoveCount);

  // A missing file gives the default sessions. A file that can't be read
  // is set aside (see setAside()) and also gives the default sessions; if
  // it can't be moved, nothing is saved over it.
  bool load(QString *error = nullptr);
  // Where load() moved an unreadable file, if it did.
  const QString &setAsidePath() const { return setAside_; }
  bool save(QString *error = nullptr) const;

  SolveLog mode() const { return mode_; }
  std::vector<MoveCountSession> &sessions() { return sessions_; }
  const std::vector<MoveCountSession> &sessions() const { return sessions_; }
  std::size_t currentIndex() const { return current_; }
  void setCurrentIndex(std::size_t i);
  MoveCountSession &current() { return sessions_[current_]; }

  MoveCountSession &addSession(const std::string &name);
  // Removing the last session leaves one empty session.
  void removeSession(std::size_t i);

  QString path() const { return path_; }

private:
  QString path_;
  SolveLog mode_;
  std::vector<MoveCountSession> sessions_;
  std::size_t current_ = 0;
  QString setAside_;
  bool keepOff_ = false; // the unreadable file is still there

  bool unreadable(QString *error, const QString &why);
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
