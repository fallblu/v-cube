#pragma once

#include "io/data_file.hpp"

#include <QJsonObject>
#include <QObject>
#include <QString>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace vcube {

// A mode's sessions, one of them current. There is always at least one:
// removing the last leaves one empty session.
template <typename Session> class SessionList {
public:
  std::vector<Session> &sessions() { return sessions_; }
  const std::vector<Session> &sessions() const { return sessions_; }
  std::size_t currentIndex() const { return current_; }
  // Nothing for an index that isn't there.
  void setCurrentIndex(std::size_t i) {
    if (i < sessions_.size()) {
      current_ = i;
    }
  }
  Session &current() { return sessions_[current_]; }
  const Session &current() const { return sessions_[current_]; }

  // A new empty session with this name, made current.
  Session &addSession(const std::string &name) {
    Session s;
    s.name = name;
    sessions_.push_back(std::move(s));
    current_ = sessions_.size() - 1;
    return sessions_.back();
  }
  void removeSession(std::size_t i) {
    if (i >= sessions_.size()) {
      return;
    }
    sessions_.erase(sessions_.begin() + static_cast<std::ptrdiff_t>(i));
    if (i < current_) {
      --current_; // the same session stays current
    }
    if (sessions_.empty()) {
      sessions_.push_back(Session{});
    }
    clampCurrent();
  }

protected:
  explicit SessionList(std::vector<Session> sessions = {Session{}})
      : sessions_(std::move(sessions)) {
    if (sessions_.empty()) {
      sessions_.push_back(Session{});
    }
  }

  // Sets the sessions read from a file, keeping one at least and the
  // current index within them.
  void assign(std::vector<Session> sessions, std::size_t current) {
    sessions_ = std::move(sessions);
    if (sessions_.empty()) {
      sessions_.push_back(Session{});
    }
    current_ = current;
    clampCurrent();
  }

  void clampCurrent() { current_ = std::min(current_, sessions_.size() - 1); }

  std::vector<Session> sessions_;
  std::size_t current_ = 0;
};

// A mode's sessions saved as one JSON data file (see DataFile). The store
// for a mode says what a new file starts with and how the file is written
// and read:
//
//   defaults()  the sessions of a missing or unreadable file
//   toJson()    the whole file
//   fromJson()  reads a file of this format (assign() the sessions); the
//               rest of the store has been reset() before
//   reset()     back to a new file; the default sets defaults()
template <typename Session>
class SessionFileStore : public SessionList<Session> {
public:
  // A missing file gives the default sessions. A file that can't be read
  // is set aside (see DataFile) and also gives the default sessions; if
  // it can't be moved, nothing is saved over it.
  bool load(QString *error = nullptr) {
    reset();
    QJsonObject o;
    FileStatus status = file_.load(o, error);
    if (status != FileStatus::Read) {
      return status == FileStatus::Missing;
    }
    fromJson(o);
    return true;
  }
  bool save(QString *error = nullptr) const {
    return file_.save(toJson(), error);
  }
  // Where load() moved an unreadable file, if it did.
  const QString &setAsidePath() const { return file_.setAsidePath(); }
  QString path() const { return file_.path(); }

  virtual ~SessionFileStore() = default;

protected:
  SessionFileStore(QString path, FileFormat format,
                   std::vector<Session> defaults = {Session{}})
      : SessionList<Session>(std::move(defaults)),
        file_(std::move(path), format) {}
  SessionFileStore(const SessionFileStore &) = default;
  SessionFileStore &operator=(const SessionFileStore &) = default;

  virtual std::vector<Session> defaults() const { return {Session{}}; }
  virtual QJsonObject toJson() const = 0;
  virtual void fromJson(const QJsonObject &o) = 0;
  virtual void reset() { this->assign(defaults(), 0); }

private:
  DataFile file_;
};

// Writes `store` on the background writer from a copy of it, so that
// saving never holds up the next attempt; `onError` gets the reason a
// write failed, while `context` lives.
template <typename Store>
void saveInBackground(const Store &store, QObject *context,
                      std::function<void(const QString &)> onError) {
  writeInBackground(
      store.path(), [copy = store](QString *error) { return copy.save(error); },
      context, std::move(onError));
}

} // namespace vcube
