#include "io/blind_store.hpp"

#include "io/algset_json.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

namespace vcube {

namespace {

constexpr FileFormat kBlindFile{"v-cube.blind", 1, false, "blind file"};

} // namespace

void LetterStat::record(bool right, std::int64_t ms) {
  ++attempts;
  if (right) {
    ++correct;
    correctMs += std::clamp<std::int64_t>(ms, 0, kMaxMs);
  }
}

std::optional<double> LetterStat::accuracy() const {
  if (attempts <= 0) {
    return std::nullopt;
  }
  return static_cast<double>(correct) / attempts;
}

std::optional<double> LetterStat::meanMs() const {
  if (correct <= 0) {
    return std::nullopt;
  }
  return static_cast<double>(correctMs) / correct;
}

LetterStat &LetterStats::of(BldPiece kind, int sticker) {
  auto &list = kind == BldPiece::Edge ? edges : corners;
  return list[static_cast<std::size_t>(std::clamp(sticker, 0, 23))];
}

const LetterStat &LetterStats::of(BldPiece kind, int sticker) const {
  const auto &list = kind == BldPiece::Edge ? edges : corners;
  return list[static_cast<std::size_t>(std::clamp(sticker, 0, 23))];
}

std::vector<Solve> blindSolves(const BlindSession &session) {
  std::vector<Solve> out;
  out.reserve(session.attempts.size());
  for (const auto &a : session.attempts) {
    Solve s;
    s.timeMs = a.timeMs;
    s.penalty = a.dnf ? Penalty::Dnf : Penalty::None;
    s.scramble = a.scramble;
    s.comment = a.comment;
    s.timestamp = a.timestamp;
    s.moves = a.solution;
    out.push_back(std::move(s));
  }
  return out;
}

BlindStore::BlindStore(QString path)
    : SessionFileStore(std::move(path), kBlindFile) {}

BlindData BlindStore::data() const {
  BlindData d;
  d.sessions = sessions_;
  d.current = current_;
  d.quiz = quiz_;
  d.setups = setups_;
  d.traces = traces_;
  return d;
}

QJsonObject BlindStore::toJson() const { return blindToJson(data()); }

void BlindStore::fromJson(const QJsonObject &o) {
  BlindData d = blindFromJson(o);
  assign(std::move(d.sessions), d.current);
  quiz_ = d.quiz;
  setups_ = d.setups;
  traces_ = std::move(d.traces);
}

void BlindStore::reset() {
  SessionFileStore::reset();
  quiz_ = {};
  setups_ = {};
  traces_.clear();
}

void BlindStore::addTrace(const TraceAttempt &t) {
  traces_.push_back(t);
  if (traces_.size() > BlindData::kMaxTraces) {
    traces_.erase(traces_.begin(),
                  traces_.begin() +
                      static_cast<std::ptrdiff_t>(traces_.size() -
                                                  BlindData::kMaxTraces));
  }
}

void BlindStore::removeTrace(std::size_t i) {
  if (i < traces_.size()) {
    traces_.erase(traces_.begin() + static_cast<std::ptrdiff_t>(i));
  }
}

namespace {

QString speffzKey(int sticker) { return QString(QChar('A' + sticker)); }

QJsonObject statsToJson(const std::array<LetterStat, kBldStickers> &list) {
  QJsonObject o;
  for (int i = 0; i < kBldStickers; ++i) {
    const LetterStat &s = list[static_cast<std::size_t>(i)];
    if (s.attempts > 0) {
      o[speffzKey(i)] = QJsonObject{{"attempts", s.attempts},
                                    {"correct", s.correct},
                                    {"ms", static_cast<double>(s.correctMs)}};
    }
  }
  return o;
}

void statsFromJson(const QJsonObject &o,
                   std::array<LetterStat, kBldStickers> &list) {
  for (int i = 0; i < kBldStickers; ++i) {
    QJsonObject so = o.value(speffzKey(i)).toObject();
    LetterStat &s = list[static_cast<std::size_t>(i)];
    s.attempts = std::max(0, so.value("attempts").toInt());
    s.correct = std::clamp(so.value("correct").toInt(), 0, s.attempts);
    s.correctMs = std::clamp<std::int64_t>(
        static_cast<std::int64_t>(so.value("ms").toDouble()), 0,
        s.correct * LetterStat::kMaxMs);
  }
}

QJsonObject letterStatsToJson(const LetterStats &s) {
  return QJsonObject{{"edges", statsToJson(s.edges)},
                     {"corners", statsToJson(s.corners)}};
}

LetterStats letterStatsFromJson(const QJsonObject &o) {
  LetterStats s;
  statsFromJson(o.value("edges").toObject(), s.edges);
  statsFromJson(o.value("corners").toObject(), s.corners);
  return s;
}

} // namespace

QJsonObject blindToJson(const BlindData &data) {
  QJsonArray sessions;
  for (const auto &s : data.sessions) {
    QJsonArray attempts;
    for (const auto &a : s.attempts) {
      QJsonObject o{{"time", static_cast<double>(a.timeMs)},
                    {"memo", static_cast<double>(a.memoMs)},
                    {"scramble", qs(a.scramble)},
                    {"timestamp", static_cast<double>(a.timestamp)}};
      if (a.dnf) {
        o["dnf"] = true;
      }
      if (!a.solution.empty()) {
        o["solution"] = qs(a.solution);
      }
      if (!a.comment.empty()) {
        o["comment"] = qs(a.comment);
      }
      attempts.append(o);
    }
    sessions.append(QJsonObject{{"name", qs(s.name)},
                                {"cube", qs(std::string(cubeKindId(s.cube)))},
                                {"attempts", attempts}});
  }
  QJsonArray traces;
  for (const auto &t : data.traces) {
    QJsonObject o{{"time", static_cast<double>(t.timeMs)},
                  {"timestamp", static_cast<double>(t.timestamp)}};
    if (t.edgesRight) {
      o["edges"] = true;
    }
    if (t.cornersRight) {
      o["corners"] = true;
    }
    traces.append(o);
  }
  return QJsonObject{{"format", kBlindFile.format},
                     {"version", 1},
                     {"current", static_cast<int>(data.current)},
                     {"sessions", sessions},
                     {"quiz", letterStatsToJson(data.quiz)},
                     {"setups", letterStatsToJson(data.setups)},
                     {"tracing", traces}};
}

BlindData blindFromJson(const QJsonObject &o) {
  BlindData data;
  data.sessions.clear();
  for (const auto &v : o.value("sessions").toArray()) {
    QJsonObject so = v.toObject();
    BlindSession s;
    s.name = ss(so.value("name").toString("Session"));
    s.cube = cubeKindFromId(ss(so.value("cube").toString()))
                 .value_or(CubeKind::Virtual);
    for (const auto &av : so.value("attempts").toArray()) {
      QJsonObject ao = av.toObject();
      BlindAttempt a;
      a.timeMs = std::max<std::int64_t>(
          0, static_cast<std::int64_t>(ao.value("time").toDouble()));
      a.memoMs = std::clamp<std::int64_t>(
          static_cast<std::int64_t>(ao.value("memo").toDouble()), 0, a.timeMs);
      a.dnf = ao.value("dnf").toBool(false);
      a.scramble = ss(ao.value("scramble").toString());
      a.solution = ss(ao.value("solution").toString());
      a.comment = ss(ao.value("comment").toString());
      a.timestamp = static_cast<std::int64_t>(ao.value("timestamp").toDouble());
      s.attempts.push_back(std::move(a));
    }
    data.sessions.push_back(std::move(s));
  }
  if (data.sessions.empty()) {
    data.sessions.push_back(BlindSession{});
  }
  data.current = std::min(
      static_cast<std::size_t>(std::max(0, o.value("current").toInt())),
      data.sessions.size() - 1);
  data.quiz = letterStatsFromJson(o.value("quiz").toObject());
  data.setups = letterStatsFromJson(o.value("setups").toObject());
  for (const auto &v : o.value("tracing").toArray()) {
    QJsonObject to = v.toObject();
    TraceAttempt t;
    t.timeMs = std::max<std::int64_t>(
        0, static_cast<std::int64_t>(to.value("time").toDouble()));
    t.edgesRight = to.value("edges").toBool(false);
    t.cornersRight = to.value("corners").toBool(false);
    t.timestamp = static_cast<std::int64_t>(to.value("timestamp").toDouble());
    data.traces.push_back(t);
  }
  if (data.traces.size() > BlindData::kMaxTraces) {
    data.traces.erase(data.traces.begin(),
                      data.traces.begin() +
                          static_cast<std::ptrdiff_t>(data.traces.size() -
                                                      BlindData::kMaxTraces));
  }
  return data;
}

} // namespace vcube
