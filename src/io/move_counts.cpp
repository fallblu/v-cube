#include "io/move_counts.hpp"

#include "io/algset_json.hpp"
#include "io/json_values.hpp"

#include <QJsonArray>

#include <algorithm>

namespace vcube {

FileFormat moveCountFormat(SolveLog mode) {
  static constexpr FileField kFields[] = {{"sessions", true}};
  return mode == SolveLog::MoveCount
             ? FileFormat{"v-cube.movecount", 1, false, "Move Count file",
                          kFields}
             : FileFormat{"v-cube.untimed", 1, false, "Untimed file", kFields};
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

namespace {

QJsonObject attemptToJson(const MoveCountAttempt &a) {
  QJsonObject o{{"scramble", qs(a.scramble)},
                {"timestamp", static_cast<double>(a.timestamp)}};
  setIf(o, "moves", a.counted, a.moves);
  setIf(o, "dnf", a.dnf, true);
  setIf(o, "comment", !a.comment.empty(), qs(a.comment));
  setIf(o, "solution", !a.solution.empty(), qs(a.solution));
  return o;
}

} // namespace

QJsonObject moveCountsToJson(const std::vector<MoveCountSession> &sessions,
                             std::size_t current, SolveLog mode,
                             ItemJsonCache<MoveCountAttempt> *cache) {
  ItemJsonCache<MoveCountAttempt> fresh;
  ItemJsonCache<MoveCountAttempt> &attempts = cache ? *cache : fresh;
  attempts.trim(sessions.size());
  std::size_t i = 0;
  return sessionListJson(
      moveCountFormat(mode).format, sessions, current,
      [&](const MoveCountSession &s, QJsonObject &so) {
        so["attempts"] = attempts.array(i++, s.attempts, attemptToJson);
        setIf(so, "step", mode == SolveLog::MoveCount, qs(stepId(s.step)));
      });
}

std::vector<MoveCountSession>
moveCountsFromJson(const QJsonObject &o, std::size_t *current, SolveLog mode) {
  return sessionListFromJson<MoveCountSession>(
      o, current, [mode](const QJsonObject &so, MoveCountSession &s) {
        if (mode == SolveLog::MoveCount &&
            !isMoveCountScrambleType(s.scrambleType)) {
          s.scrambleType = "333";
        }
        if (auto step = stepFromId(ss(so.value("step").toString()))) {
          s.step = *step;
        } else if (auto named = stepFromName(s.name)) {
          s.step = *named;
        } else {
          s.step = s.scrambleType == "eocross" ? SolveStep::ZZF2L
                                               : SolveStep::EOCross;
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
          a.timestamp = jsonInt64(ao.value("timestamp"));
          s.attempts.push_back(std::move(a));
        }
      });
}

QJsonObject MoveCountStore::toJson() const {
  return moveCountsToJson(sessions_, current_, mode_, &cache_);
}

void MoveCountStore::fromJson(const QJsonObject &o) {
  std::size_t current = 0;
  std::vector<MoveCountSession> sessions =
      moveCountsFromJson(o, &current, mode_);
  assign(std::move(sessions), current);
}

} // namespace vcube
