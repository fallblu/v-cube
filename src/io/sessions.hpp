#pragma once

#include "train/stats.hpp"

#include <QJsonObject>
#include <QString>

#include <optional>
#include <string>
#include <vector>

namespace vcube {

struct Session {
  std::string name = "Session 1";
  std::string scrambleType = "333";
  std::vector<Solve> solves;
  CubeKind cube = CubeKind::Virtual;
};

// The timer's sessions, saved as one JSON file.
class SessionStore {
public:
  explicit SessionStore(QString path);

  // A missing file gives one empty session. A file that can't be read is
  // set aside (see setAside()) and also gives one empty session; if it
  // can't be moved, nothing is saved over it.
  bool load(QString *error = nullptr);
  bool save(QString *error = nullptr) const;
  // Where load() moved an unreadable file, if it did.
  const QString &setAsidePath() const { return setAside_; }

  std::vector<Session> &sessions() { return sessions_; }
  const std::vector<Session> &sessions() const { return sessions_; }
  std::size_t currentIndex() const { return current_; }
  void setCurrentIndex(std::size_t i);
  Session &current() { return sessions_[current_]; }

  Session &addSession(const std::string &name);
  void removeSession(std::size_t i);

  QString path() const { return path_; }

private:
  QString path_;
  std::vector<Session> sessions_{Session{}};
  std::size_t current_ = 0;
  QString setAside_;
  bool keepOff_ = false; // the unreadable file is still there

  bool unreadable(QString *error, const QString &why);
};

// {"version": 1, "current": 0, "sessions": [{"name": ..., "scrambleType":
// ..., "cube": "virtual", "solves": [[ms, penalty, scramble, comment,
// unixSeconds, moves?], ...]}]}. A session without "cube" was saved before
// sessions had one, when the timer was for physical cubes only.
QJsonObject sessionsToJson(const std::vector<Session> &sessions,
                           std::size_t current);
std::vector<Session> sessionsFromJson(const QJsonObject &o,
                                      std::size_t *current = nullptr);

// cstimer export files: {"session1": [[[penalty, ms], scramble, comment,
// unixSeconds], ...], ..., "properties": {"sessionData": "<json>"}}, where
// penalty is 0, 2000 (+2) or -1 (DNF). Imported sessions are for a
// physical cube.
std::optional<std::vector<Session>> sessionsFromCstimer(const QJsonObject &o,
                                                        QString *error);
QJsonObject sessionsToCstimer(const std::vector<Session> &sessions);

} // namespace vcube
