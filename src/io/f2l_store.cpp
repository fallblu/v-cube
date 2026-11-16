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

QJsonObject pairAttemptToJson(const PairAttempt &a) {
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
  return o;
}

PairAttempt pairAttemptFromJson(const QJsonObject &j) {
  PairAttempt a;
  a.timestamp = intOf(j, "timestamp");
  a.ms = std::max<std::int64_t>(0, intOf(j, "ms"));
  a.moves = std::max(0, j.value("moves").toInt());
  a.bestMoves = std::max(0, j.value("best").toInt());
  a.excess = std::max(0.0, j.value("excess").toDouble());
  a.hint = j.value("hint").toBool();
  a.gaveUp = j.value("gaveUp").toBool();
  a.drill = drillFrom(j.value("drill").toString());
  return a;
}

QJsonObject fullAttemptToJson(const FullAttempt &a) {
  return {{"timestamp", static_cast<double>(a.timestamp)},
          {"ms", static_cast<double>(a.ms)},
          {"moves", a.moves},
          {"pairs", a.pairs},
          {"excess", a.excess}};
}

FullAttempt fullAttemptFromJson(const QJsonObject &j) {
  FullAttempt a;
  a.timestamp = intOf(j, "timestamp");
  a.ms = std::max<std::int64_t>(0, intOf(j, "ms"));
  a.moves = std::max(0, j.value("moves").toInt());
  a.pairs = std::max(0, j.value("pairs").toInt());
  a.excess = std::max(0.0, j.value("excess").toDouble());
  return a;
}

QJsonObject
attemptsToJson(const std::map<std::string, std::vector<PairAttempt>> &byKey) {
  QJsonObject out;
  for (const auto &[key, attempts] : byKey) {
    QJsonArray list;
    for (const PairAttempt &a : attempts) {
      list.append(pairAttemptToJson(a));
    }
    out[QString::fromStdString(key)] = list;
  }
  return out;
}

QJsonObject f2lToJson(const F2LStats &stats) {
  QJsonArray choices;
  for (const ChoiceAttempt &a : stats.choices) {
    choices.append(QJsonObject{{"timestamp", static_cast<double>(a.timestamp)},
                               {"rank", a.rank},
                               {"of", a.of},
                               {"regret", a.regret}});
  }
  QJsonArray fulls;
  for (const FullAttempt &a : stats.fulls) {
    fulls.append(fullAttemptToJson(a));
  }
  return {{"format", kF2LFile.format},
          {"version", kF2LFile.version},
          {"cases", attemptsToJson(stats.cases)},
          {"choices", choices},
          {"fulls", fulls}};
}

F2LStats f2lFromJson(const QJsonObject &o) {
  F2LStats stats;
  const QJsonObject cases = o.value("cases").toObject();
  for (auto it = cases.begin(); it != cases.end(); ++it) {
    for (const QJsonValue &v : it.value().toArray()) {
      stats.add(it.key().toStdString(), pairAttemptFromJson(v.toObject()));
    }
  }
  for (const QJsonValue &v : o.value("choices").toArray()) {
    QJsonObject j = v.toObject();
    ChoiceAttempt a;
    a.timestamp = intOf(j, "timestamp");
    a.of = std::max(1, j.value("of").toInt(1));
    a.rank = std::clamp(j.value("rank").toInt(1), 1, a.of);
    a.regret = std::max(0.0, j.value("regret").toDouble());
    stats.add(a);
  }
  for (const QJsonValue &v : o.value("fulls").toArray()) {
    stats.add(fullAttemptFromJson(v.toObject()));
  }
  return stats;
}

} // namespace vcube
