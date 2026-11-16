#include "io/f2l_store.hpp"

#include <QJsonArray>

#include <algorithm>

namespace vcube {

namespace {

const char *drillName(F2LDrill d) {
  switch (d) {
  case F2LDrill::Choice:
    return "choice";
  case F2LDrill::Full:
    return "full";
  case F2LDrill::Pair:
    break;
  }
  return "pair";
}

F2LDrill drillFrom(const QString &name) {
  return name == "choice" ? F2LDrill::Choice
         : name == "full" ? F2LDrill::Full
                          : F2LDrill::Pair;
}

std::int64_t intOf(const QJsonObject &o, const char *key) {
  return static_cast<std::int64_t>(o.value(key).toDouble());
}

} // namespace

QJsonObject f2lToJson(const F2LStats &stats) {
  QJsonObject cases;
  for (const auto &[key, attempts] : stats.cases) {
    QJsonArray list;
    for (const PairAttempt &a : attempts) {
      QJsonObject o{{"timestamp", static_cast<double>(a.timestamp)},
                    {"ms", static_cast<double>(a.ms)},
                    {"moves", a.moves},
                    {"best", a.bestMoves},
                    {"excess", a.excess}};
      if (a.hint) {
        o["hint"] = true;
      }
      if (a.gaveUp) {
        o["gaveUp"] = true;
      }
      if (a.drill != F2LDrill::Pair) {
        o["drill"] = drillName(a.drill);
      }
      list.append(o);
    }
    cases[QString::fromStdString(key)] = list;
  }
  QJsonArray choices;
  for (const ChoiceAttempt &a : stats.choices) {
    choices.append(QJsonObject{{"timestamp", static_cast<double>(a.timestamp)},
                               {"rank", a.rank},
                               {"of", a.of},
                               {"regret", a.regret}});
  }
  QJsonArray fulls;
  for (const FullAttempt &a : stats.fulls) {
    fulls.append(QJsonObject{{"timestamp", static_cast<double>(a.timestamp)},
                             {"ms", static_cast<double>(a.ms)},
                             {"moves", a.moves},
                             {"pairs", a.pairs},
                             {"excess", a.excess}});
  }
  return {{"format", kF2LFile.format},
          {"version", kF2LFile.version},
          {"cases", cases},
          {"choices", choices},
          {"fulls", fulls}};
}

F2LStats f2lFromJson(const QJsonObject &o) {
  F2LStats stats;
  const QJsonObject cases = o.value("cases").toObject();
  for (auto it = cases.begin(); it != cases.end(); ++it) {
    std::vector<PairAttempt> attempts;
    for (const QJsonValue &v : it.value().toArray()) {
      QJsonObject j = v.toObject();
      PairAttempt a;
      a.timestamp = intOf(j, "timestamp");
      a.ms = std::max<std::int64_t>(0, intOf(j, "ms"));
      a.moves = std::max(0, j.value("moves").toInt());
      a.bestMoves = std::max(0, j.value("best").toInt());
      a.excess = std::max(0.0, j.value("excess").toDouble());
      a.hint = j.value("hint").toBool();
      a.gaveUp = j.value("gaveUp").toBool();
      a.drill = drillFrom(j.value("drill").toString());
      attempts.push_back(a);
    }
    if (attempts.size() > F2LStats::kPerCase) {
      attempts.erase(attempts.begin(),
                     attempts.end() -
                         static_cast<std::ptrdiff_t>(F2LStats::kPerCase));
    }
    if (!attempts.empty()) {
      stats.cases[it.key().toStdString()] = std::move(attempts);
    }
  }
  for (const QJsonValue &v : o.value("choices").toArray()) {
    QJsonObject j = v.toObject();
    ChoiceAttempt a;
    a.timestamp = intOf(j, "timestamp");
    a.of = std::max(1, j.value("of").toInt(1));
    a.rank = std::clamp(j.value("rank").toInt(1), 1, a.of);
    a.regret = std::max(0.0, j.value("regret").toDouble());
    stats.choices.push_back(a);
  }
  for (const QJsonValue &v : o.value("fulls").toArray()) {
    QJsonObject j = v.toObject();
    FullAttempt a;
    a.timestamp = intOf(j, "timestamp");
    a.ms = std::max<std::int64_t>(0, intOf(j, "ms"));
    a.moves = std::max(0, j.value("moves").toInt());
    a.pairs = std::max(0, j.value("pairs").toInt());
    a.excess = std::max(0.0, j.value("excess").toDouble());
    stats.fulls.push_back(a);
  }
  return stats;
}

F2LStore::F2LStore(QString path) : file_(std::move(path), kF2LFile) {}

bool F2LStore::load(QString *error) {
  stats = {};
  QJsonObject o;
  FileStatus status = file_.load(o, error);
  if (status != FileStatus::Read) {
    return status == FileStatus::Missing;
  }
  stats = f2lFromJson(o);
  return true;
}

bool F2LStore::save(QString *error) const {
  return file_.save(f2lToJson(stats), error);
}

} // namespace vcube
