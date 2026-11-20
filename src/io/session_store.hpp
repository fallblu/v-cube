#pragma once

#include "io/data_file.hpp"

#include <QJsonArray>
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
    keepOne();
  }

protected:
  explicit SessionList(std::vector<Session> sessions = {Session{}})
      : sessions_(std::move(sessions)) {
    keepOne();
  }

  // Sets the sessions read from a file.
  void assign(std::vector<Session> sessions, std::size_t current) {
    sessions_ = std::move(sessions);
    current_ = current;
    keepOne();
  }

  // Keeps one session at least and the current index within them.
  void keepOne() {
    if (sessions_.empty()) {
      sessions_.push_back(Session{});
    }
    current_ = std::min(current_, sessions_.size() - 1);
  }

  std::vector<Session> sessions_;
  std::size_t current_ = 0;
};

// Each session's list of items (solves, attempts) as JSON, built again
// only from the first item that changed since the last time: recording a
// solve adds one item to the JSON, rather than making the whole file's
// again. The JSON it returns shares its data with what it keeps (Qt's
// implicit sharing), so handing it to the writer's thread copies nothing;
// changing the kept JSON later makes a copy first. Items are compared
// whole, so what it returns is always what building afresh would give.
template <typename Item> class ItemJsonCache {
public:
  template <typename ToJson>
  QJsonArray array(std::size_t list, const std::vector<Item> &items,
                   ToJson toJson) {
    if (lists_.size() <= list) {
      lists_.resize(list + 1);
    }
    List &kept = lists_[list];
    auto same = static_cast<std::size_t>(
        std::mismatch(kept.items.begin(), kept.items.end(), items.begin(),
                      items.end())
            .first -
        kept.items.begin());
    kept.items.erase(kept.items.begin() + static_cast<std::ptrdiff_t>(same),
                     kept.items.end());
    if (same == 0) {
      kept.json = QJsonArray();
    }
    while (static_cast<std::size_t>(kept.json.size()) > same) {
      kept.json.removeLast();
    }
    for (std::size_t i = same; i < items.size(); ++i) {
      kept.items.push_back(items[i]);
      kept.json.append(toJson(items[i]));
    }
    return kept.json;
  }
  // Forgets the lists from `lists` on (sessions that are gone).
  void trim(std::size_t lists) {
    if (lists_.size() > lists) {
      lists_.resize(lists);
    }
  }

private:
  struct List {
    std::vector<Item> items;
    QJsonArray json;
  };
  std::vector<List> lists_;
};

// A mode's sessions saved as one JSON data file (see FileStore), whose
// fromJson() assign()s the sessions. A missing or unreadable file gives
// defaults(), the store's own sessions for a new file.
template <typename Session>
class SessionFileStore : public SessionList<Session>, public FileStore {
public:
  // A save of the file as it is now that can run on another thread: the
  // JSON is made here, and the write and the rest of the work there. Worth
  // it over copying the store when toJson() keeps what it built (see
  // ItemJsonCache).
  std::function<bool(QString *)> saveJob() const {
    return [file = file_, o = toJson()](QString *error) {
      return file.save(o, error);
    };
  }

protected:
  SessionFileStore(QString path, FileFormat format,
                   std::vector<Session> defaults = {Session{}})
      : SessionList<Session>(std::move(defaults)),
        FileStore(std::move(path), format) {}

  virtual std::vector<Session> defaults() const { return {Session{}}; }
  void reset() override { this->assign(defaults(), 0); }
};

// Writes `store` on the background writer from a copy of it, so that
// saving never holds up the next attempt; `onError` gets the reason a
// write failed, while `context` lives. A store with a JSON cache (it says
// so with kCachesJson) hands over its JSON instead of a copy of itself.
template <typename Store>
void saveInBackground(const Store &store, QObject *context,
                      std::function<void(const QString &)> onError) {
  if constexpr (requires { requires Store::kCachesJson; }) {
    writeInBackground(store.path(), store.saveJob(), context,
                      std::move(onError));
  } else {
    writeInBackground(
        store.path(),
        [copy = store](QString *error) { return copy.save(error); }, context,
        std::move(onError));
  }
}

} // namespace vcube
