#include "io/move_counts.hpp"

#include "io/algset_json.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

namespace vcube {

std::vector<MoveCountSession> defaultMoveCountSessions(SolveLog mode) {
  if (mode == SolveLog::Untimed) {
    return {MoveCountSession{}};
  }
  std::vector<MoveCountSession> out;
  for (SolveStep step : solveSteps()) {
    MoveCountSession s;
    s.name = std::string(stepName(step));
    s.scrambleType = defaultScrambleType(step);
    s.step = step;
    out.push_back(std::move(s));
  }
  return out;
}

MoveCountStore::MoveCountStore(QString path, SolveLog mode)
    : path_(std::move(path)), mode_(mode),
      sessions_(defaultMoveCountSessions(mode)) {}

bool MoveCountStore::load(QString *error) {
  QFile f(path_);
  if (!f.exists()) {
    sessions_ = defaultMoveCountSessions(mode_);
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
  sessions_ = moveCountsFromJson(doc.object(), &current_, mode_);
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
  sessions_ = defaultMoveCountSessions(mode_);
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
      QJsonDocument(moveCountsToJson(sessions_, current_, mode_))
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
                             std::size_t current, SolveLog mode) {
  QJsonArray arr;
  for (const auto &s : sessions) {
    QJsonArray attempts;
    for (const auto &a : s.attempts) {
      QJsonObject o{{"scramble", qs(a.scramble)},
                    {"timestamp", static_cast<double>(a.timestamp)}};
      if (a.counted) {
        o["moves"] = a.moves;
      }
      if (a.dnf) {
        o["dnf"] = true;
      }
      if (!a.comment.empty()) {
        o["comment"] = qs(a.comment);
      }
      if (!a.solution.empty()) {
        o["solution"] = qs(a.solution);
      }
      attempts.append(o);
    }
    QJsonObject so{{"name", qs(s.name)},
                   {"scrambleType", qs(s.scrambleType)},
                   {"cube", qs(std::string(cubeKindId(s.cube)))},
                   {"attempts", attempts}};
    if (mode == SolveLog::MoveCount) {
      so["step"] = qs(std::string(stepId(s.step)));
    }
    arr.append(so);
  }
  return QJsonObject{{"format", mode == SolveLog::MoveCount ? "v-cube.movecount"
                                                            : "v-cube.untimed"},
                     {"version", 1},
                     {"current", static_cast<int>(current)},
                     {"sessions", arr}};
}

std::vector<MoveCountSession>
moveCountsFromJson(const QJsonObject &o, std::size_t *current, SolveLog mode) {
  std::vector<MoveCountSession> out;
  for (const auto &v : o.value("sessions").toArray()) {
    QJsonObject so = v.toObject();
    MoveCountSession s;
    s.name = ss(so.value("name").toString("Session"));
    s.scrambleType = ss(so.value("scrambleType").toString("333"));
    if (mode == SolveLog::MoveCount &&
        !isMoveCountScrambleType(s.scrambleType)) {
      s.scrambleType = "333";
    }
    s.cube = cubeKindFromId(ss(so.value("cube").toString()))
                 .value_or(CubeKind::Physical);
    if (auto step = stepFromId(ss(so.value("step").toString()))) {
      s.step = *step;
    } else if (auto named = stepFromName(s.name)) {
      s.step = *named;
    } else {
      s.step =
          s.scrambleType == "eocross" ? SolveStep::ZZF2L : SolveStep::EOCross;
    }
    for (const auto &av : so.value("attempts").toArray()) {
      QJsonObject ao = av.toObject();
      MoveCountAttempt a;
      a.counted = ao.contains("moves");
      a.moves = std::clamp(ao.value("moves").toInt(), 0, kMaxMoveCount);
      a.dnf = ao.value("dnf").toBool(false);
      a.scramble = ss(ao.value("scramble").toString());
      a.comment = ss(ao.value("comment").toString());
      a.solution = ss(ao.value("solution").toString());
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
