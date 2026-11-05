#pragma once

#include "train/move_count.hpp"

#include <QJsonObject>
#include <QString>

#include <string>
#include <vector>

namespace vcube {

// A Move Count session: the attempts at one step from one scramble type,
// e.g. "EO Cross" from WCA scrambles or "ZZ F2L" from EO Cross solved.
struct MoveCountSession {
  std::string name = "Session 1";
  std::string scrambleType = "333"; // one of moveCountScrambleTypes()
  std::vector<MoveCountAttempt> attempts;
};

// The sessions a new store starts with: EO and EO Cross from WCA
// scrambles, and ZZ F2L from EO Cross solved.
std::vector<MoveCountSession> defaultMoveCountSessions();

// Move Count mode's sessions, saved as one JSON file.
class MoveCountStore {
public:
  explicit MoveCountStore(QString path);

  // A missing file gives the default sessions. A file that can't be read
  // is set aside (see setAside()) and also gives the default sessions; if
  // it can't be moved, nothing is saved over it.
  bool load(QString *error = nullptr);
  // Where load() moved an unreadable file, if it did.
  const QString &setAsidePath() const { return setAside_; }
  bool save(QString *error = nullptr) const;

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
  std::vector<MoveCountSession> sessions_ = defaultMoveCountSessions();
  std::size_t current_ = 0;
  QString setAside_;
  bool keepOff_ = false; // the unreadable file is still there

  bool unreadable(QString *error, const QString &why);
};

// {"format": "v-cube.movecount", "version": 1, "current": 0, "sessions":
// [{"name": ..., "scrambleType": "eocross", "attempts": [{"moves": 23,
// "scramble": ..., "timestamp": ..., "dnf": true, "comment": ...}]}]};
// "dnf" and "comment" are left out when false or empty. A scramble type
// Move Count doesn't offer reads as "333".
QJsonObject moveCountsToJson(const std::vector<MoveCountSession> &sessions,
                             std::size_t current);
std::vector<MoveCountSession>
moveCountsFromJson(const QJsonObject &o, std::size_t *current = nullptr);

} // namespace vcube
