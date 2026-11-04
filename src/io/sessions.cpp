#include "io/sessions.hpp"

#include "cube/scramble.hpp"
#include "io/algset_json.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

namespace vcube {

namespace {

int penaltyCode(Penalty p) {
  return p == Penalty::Plus2 ? 1 : p == Penalty::Dnf ? 2 : 0;
}

Penalty penaltyFromCode(int c) {
  return c == 1 ? Penalty::Plus2 : c == 2 ? Penalty::Dnf : Penalty::None;
}

QJsonArray solveToJson(const Solve &s) {
  QJsonArray a{static_cast<double>(s.timeMs), penaltyCode(s.penalty),
               qs(s.scramble), qs(s.comment), static_cast<double>(s.timestamp)};
  if (!s.moves.empty()) {
    a.append(qs(s.moves));
  }
  return a;
}

Solve solveFromJson(const QJsonArray &a) {
  Solve s;
  s.timeMs = static_cast<std::int64_t>(a.at(0).toDouble());
  s.penalty = penaltyFromCode(a.at(1).toInt());
  s.scramble = ss(a.at(2).toString());
  s.comment = ss(a.at(3).toString());
  s.timestamp = static_cast<std::int64_t>(a.at(4).toDouble());
  s.moves = ss(a.at(5).toString());
  return s;
}

} // namespace

SessionStore::SessionStore(QString path) : path_(std::move(path)) {}

bool SessionStore::load(QString *error) {
  QFile f(path_);
  if (!f.exists()) {
    sessions_ = {Session{}};
    current_ = 0;
    return true;
  }
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = f.errorString();
    }
    return false;
  }
  QJsonParseError pe;
  auto doc = QJsonDocument::fromJson(f.readAll(), &pe);
  if (pe.error != QJsonParseError::NoError) {
    if (error) {
      *error = pe.errorString();
    }
    return false;
  }
  sessions_ = sessionsFromJson(doc.object(), &current_);
  if (sessions_.empty()) {
    sessions_.push_back(Session{});
  }
  current_ = std::min(current_, sessions_.size() - 1);
  return true;
}

bool SessionStore::save(QString *error) const {
  QDir().mkpath(QFileInfo(path_).absolutePath());
  return writeFileAtomically(
      path_, QJsonDocument(sessionsToJson(sessions_, current_)).toJson(QJsonDocument::Compact),
      error);
}

void SessionStore::setCurrentIndex(std::size_t i) {
  if (i < sessions_.size()) {
    current_ = i;
  }
}

Session &SessionStore::addSession(const std::string &name) {
  Session s;
  s.name = name;
  sessions_.push_back(std::move(s));
  current_ = sessions_.size() - 1;
  return sessions_.back();
}

void SessionStore::removeSession(std::size_t i) {
  if (i >= sessions_.size()) {
    return;
  }
  sessions_.erase(sessions_.begin() + static_cast<std::ptrdiff_t>(i));
  if (sessions_.empty()) {
    sessions_.push_back(Session{});
  }
  current_ = std::min(current_, sessions_.size() - 1);
}

QJsonObject sessionsToJson(const std::vector<Session> &sessions,
                           std::size_t current) {
  QJsonArray arr;
  for (const auto &s : sessions) {
    QJsonArray solves;
    for (const auto &solve : s.solves) {
      solves.append(solveToJson(solve));
    }
    arr.append(QJsonObject{{"name", qs(s.name)},
                           {"scrambleType", qs(s.scrambleType)},
                           {"solves", solves}});
  }
  return QJsonObject{{"version", 1},
                     {"current", static_cast<int>(current)},
                     {"sessions", arr}};
}

std::vector<Session> sessionsFromJson(const QJsonObject &o,
                                      std::size_t *current) {
  std::vector<Session> out;
  for (const auto &v : o.value("sessions").toArray()) {
    QJsonObject so = v.toObject();
    Session s;
    s.name = ss(so.value("name").toString("Session"));
    s.scrambleType = ss(so.value("scrambleType").toString("333"));
    for (const auto &sv : so.value("solves").toArray()) {
      s.solves.push_back(solveFromJson(sv.toArray()));
    }
    out.push_back(std::move(s));
  }
  if (current) {
    *current = static_cast<std::size_t>(std::max(0, o.value("current").toInt()));
  }
  return out;
}

std::optional<std::vector<Session>> sessionsFromCstimer(const QJsonObject &o,
                                                        QString *error) {
  // Session names and order live in properties.sessionData, which cstimer
  // stores as a JSON string (older exports may have an object).
  QJsonObject sessionData;
  QJsonValue sd = o.value("properties").toObject().value("sessionData");
  if (sd.isString()) {
    sessionData = QJsonDocument::fromJson(sd.toString().toUtf8()).object();
  } else {
    sessionData = sd.toObject();
  }
  struct Entry {
    int number;
    int rank;
    Session session;
  };
  std::vector<Entry> entries;
  for (auto it = o.begin(); it != o.end(); ++it) {
    if (!it.key().startsWith("session")) {
      continue;
    }
    bool ok = false;
    int number = it.key().mid(7).toInt(&ok);
    if (!ok) {
      continue;
    }
    QJsonValue value = it.value();
    // Some exports store each session as a JSON string.
    QJsonArray solves = value.isString()
                            ? QJsonDocument::fromJson(value.toString().toUtf8()).array()
                            : value.toArray();
    QJsonObject meta = sessionData.value(QString::number(number)).toObject();
    Entry e{number, meta.value("rank").toInt(number), {}};
    e.session.name = ss(meta.value("name").toVariant().toString());
    if (e.session.name.empty()) {
      e.session.name = "Session " + std::to_string(number);
    }
    e.session.scrambleType =
        ss(meta.value("opt").toObject().value("scrType").toString("333"));
    if (!findScrambleType(e.session.scrambleType)) {
      e.session.scrambleType = "333";
    }
    for (const auto &sv : solves) {
      QJsonArray a = sv.toArray();
      QJsonArray time = a.at(0).toArray();
      if (time.size() < 2) {
        continue;
      }
      Solve s;
      int pen = time.at(0).toInt();
      s.timeMs = static_cast<std::int64_t>(time.at(1).toDouble());
      s.penalty = pen == -1 ? Penalty::Dnf : pen > 0 ? Penalty::Plus2 : Penalty::None;
      s.scramble = ss(a.at(1).toString());
      s.comment = ss(a.at(2).toString());
      s.timestamp = static_cast<std::int64_t>(a.at(3).toDouble());
      e.session.solves.push_back(std::move(s));
    }
    entries.push_back(std::move(e));
  }
  if (entries.empty()) {
    if (error) {
      *error = "no cstimer sessions found";
    }
    return std::nullopt;
  }
  std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
    return a.rank != b.rank ? a.rank < b.rank : a.number < b.number;
  });
  std::vector<Session> out;
  for (auto &e : entries) {
    out.push_back(std::move(e.session));
  }
  return out;
}

QJsonObject sessionsToCstimer(const std::vector<Session> &sessions) {
  QJsonObject o;
  QJsonObject sessionData;
  for (std::size_t i = 0; i < sessions.size(); ++i) {
    const Session &s = sessions[i];
    int number = static_cast<int>(i) + 1;
    QJsonArray solves;
    for (const auto &solve : s.solves) {
      int pen = solve.penalty == Penalty::Dnf ? -1 : solve.penalty == Penalty::Plus2 ? 2000 : 0;
      solves.append(QJsonArray{QJsonArray{pen, static_cast<double>(solve.timeMs)},
                               qs(solve.scramble), qs(solve.comment),
                               static_cast<double>(solve.timestamp)});
    }
    o[QString("session%1").arg(number)] = solves;
    sessionData[QString::number(number)] =
        QJsonObject{{"name", qs(s.name)},
                    {"opt", s.scrambleType == "333" ? QJsonObject{}
                                                    : QJsonObject{{"scrType", qs(s.scrambleType)}}},
                    {"rank", number}};
  }
  o["properties"] = QJsonObject{
      {"sessionData", QString::fromUtf8(QJsonDocument(sessionData).toJson(QJsonDocument::Compact))}};
  return o;
}

} // namespace vcube
