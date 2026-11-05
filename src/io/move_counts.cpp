#include "io/move_counts.hpp"

#include "io/algset_json.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

namespace vcube {

std::vector<MoveCountSession> defaultMoveCountSessions() {
  return {
      {"EO", "333", {}}, {"EO Cross", "333", {}}, {"ZZ F2L", "eocross", {}}};
}

MoveCountStore::MoveCountStore(QString path) : path_(std::move(path)) {}

bool MoveCountStore::load(QString *error) {
  QFile f(path_);
  if (!f.exists()) {
    sessions_ = defaultMoveCountSessions();
    current_ = 0;
    return true;
  }
  if (!f.open(QIODevice::ReadOnly)) {
    return unreadable(error, f.errorString());
  }
  QJsonParseError pe;
  auto doc = QJsonDocument::fromJson(f.readAll(), &pe);
  f.close();
  if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
    return unreadable(error, pe.error != QJsonParseError::NoError
                                 ? pe.errorString()
                                 : QString("not a move count file"));
  }
  sessions_ = moveCountsFromJson(doc.object(), &current_);
  if (sessions_.empty()) {
    sessions_.push_back(MoveCountSession{});
  }
  current_ = std::min(current_, sessions_.size() - 1);
  return true;
}

bool MoveCountStore::unreadable(QString *error, const QString &why) {
  if (error) {
    *error = why;
  }
  sessions_ = defaultMoveCountSessions();
  current_ = 0;
  setAside_ = setAside(path_);
  keepOff_ = setAside_.isEmpty();
  return false;
}

bool MoveCountStore::save(QString *error) const {
  if (keepOff_) {
    if (error) {
      *error =
          QString("%1 couldn't be read, so it isn't saved over").arg(path_);
    }
    return false;
  }
  QDir().mkpath(QFileInfo(path_).absolutePath());
  return writeFileAtomically(
      path_,
      QJsonDocument(moveCountsToJson(sessions_, current_))
          .toJson(QJsonDocument::Compact),
      error);
}

void MoveCountStore::setCurrentIndex(std::size_t i) {
  if (i < sessions_.size()) {
    current_ = i;
  }
}

MoveCountSession &MoveCountStore::addSession(const std::string &name) {
  MoveCountSession s;
  s.name = name;
  sessions_.push_back(std::move(s));
  current_ = sessions_.size() - 1;
  return sessions_.back();
}

void MoveCountStore::removeSession(std::size_t i) {
  if (i >= sessions_.size()) {
    return;
  }
  sessions_.erase(sessions_.begin() + static_cast<std::ptrdiff_t>(i));
  if (sessions_.empty()) {
    sessions_.push_back(MoveCountSession{});
  }
  current_ = std::min(current_, sessions_.size() - 1);
}

QJsonObject moveCountsToJson(const std::vector<MoveCountSession> &sessions,
                             std::size_t current) {
  QJsonArray arr;
  for (const auto &s : sessions) {
    QJsonArray attempts;
    for (const auto &a : s.attempts) {
      QJsonObject o{{"moves", a.moves},
                    {"scramble", qs(a.scramble)},
                    {"timestamp", static_cast<double>(a.timestamp)}};
      if (a.dnf) {
        o["dnf"] = true;
      }
      if (!a.comment.empty()) {
        o["comment"] = qs(a.comment);
      }
      attempts.append(o);
    }
    arr.append(QJsonObject{{"name", qs(s.name)},
                           {"scrambleType", qs(s.scrambleType)},
                           {"attempts", attempts}});
  }
  return QJsonObject{{"format", "v-cube.movecount"},
                     {"version", 1},
                     {"current", static_cast<int>(current)},
                     {"sessions", arr}};
}

std::vector<MoveCountSession> moveCountsFromJson(const QJsonObject &o,
                                                 std::size_t *current) {
  std::vector<MoveCountSession> out;
  for (const auto &v : o.value("sessions").toArray()) {
    QJsonObject so = v.toObject();
    MoveCountSession s;
    s.name = ss(so.value("name").toString("Session"));
    s.scrambleType = ss(so.value("scrambleType").toString("333"));
    if (!isMoveCountScrambleType(s.scrambleType)) {
      s.scrambleType = "333";
    }
    for (const auto &av : so.value("attempts").toArray()) {
      QJsonObject ao = av.toObject();
      MoveCountAttempt a;
      a.moves = std::clamp(ao.value("moves").toInt(), 0, kMaxMoveCount);
      a.dnf = ao.value("dnf").toBool(false);
      a.scramble = ss(ao.value("scramble").toString());
      a.comment = ss(ao.value("comment").toString());
      a.timestamp = static_cast<std::int64_t>(ao.value("timestamp").toDouble());
      s.attempts.push_back(std::move(a));
    }
    out.push_back(std::move(s));
  }
  if (current) {
    *current =
        static_cast<std::size_t>(std::max(0, o.value("current").toInt()));
  }
  return out;
}

} // namespace vcube
