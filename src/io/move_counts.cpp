#include "io/move_counts.hpp"

#include "io/algset_json.hpp"

#include <QJsonArray>

#include <algorithm>

namespace vcube {

FileFormat moveCountFormat(SolveLog mode) {
  return mode == SolveLog::MoveCount
             ? FileFormat{"v-cube.movecount", 1, false, "Move Count file"}
             : FileFormat{"v-cube.untimed", 1, false, "Untimed file"};
}

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
    : SessionFileStore(std::move(path), moveCountFormat(mode),
                       defaultMoveCountSessions(mode)),
      mode_(mode) {}

std::vector<MoveCountSession> MoveCountStore::defaults() const {
  return defaultMoveCountSessions(mode_);
}

QJsonObject MoveCountStore::toJson() const {
  return moveCountsToJson(sessions_, current_, mode_);
}

void MoveCountStore::fromJson(const QJsonObject &o) {
  std::size_t current = 0;
  std::vector<MoveCountSession> sessions =
      moveCountsFromJson(o, &current, mode_);
  assign(std::move(sessions), current);
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
  return QJsonObject{{"format", moveCountFormat(mode).format},
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
