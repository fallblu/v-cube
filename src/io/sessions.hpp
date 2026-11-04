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
};

// The timer's sessions, saved as one JSON file.
class SessionStore {
public:
  explicit SessionStore(QString path);

  bool load(QString *error = nullptr); // missing file = one empty session
  bool save(QString *error = nullptr) const;

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
};

QJsonObject sessionsToJson(const std::vector<Session> &sessions,
                           std::size_t current);
std::vector<Session> sessionsFromJson(const QJsonObject &o,
                                      std::size_t *current = nullptr);

// cstimer export files: {"session1": [[[penalty, ms], scramble, comment,
// unixSeconds], ...], ..., "properties": {"sessionData": "<json>"}}, where
// penalty is 0, 2000 (+2) or -1 (DNF).
std::optional<std::vector<Session>> sessionsFromCstimer(const QJsonObject &o,
                                                        QString *error);
QJsonObject sessionsToCstimer(const std::vector<Session> &sessions);

} // namespace vcube
