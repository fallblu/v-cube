#include "io/practice_store.hpp"

#include "io/algset_json.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

namespace vcube {

namespace {

QJsonObject tryToJson(const PracticeTry &t) {
  QJsonObject o{{"timestamp", static_cast<double>(t.timestamp)}};
  if (t.ms != 0) {
    o["ms"] = static_cast<double>(t.ms);
  }
  if (t.moves != 0) {
    o["moves"] = t.moves;
  }
  if (t.best != 0) {
    o["best"] = t.best;
  }
  if (t.extra >= 0.0) {
    o["extra"] = t.extra;
  }
  if (t.dnf) {
    o["dnf"] = true;
  }
  if (t.hint) {
    o["hint"] = true;
  }
  return o;
}

PracticeTry tryFromJson(const QJsonObject &o) {
  PracticeTry t;
  t.timestamp = static_cast<std::int64_t>(o.value("timestamp").toDouble());
  t.ms = std::max<std::int64_t>(
      0, static_cast<std::int64_t>(o.value("ms").toDouble()));
  t.moves = std::max(0, o.value("moves").toInt());
  t.best = std::max(0, o.value("best").toInt());
  t.extra =
      o.contains("extra") ? std::max(0.0, o.value("extra").toDouble()) : -1.0;
  t.dnf = o.value("dnf").toBool();
  t.hint = o.value("hint").toBool();
  return t;
}

QJsonObject triesToJson(const PracticeTries &tries) {
  QJsonObject out;
  for (const auto &[key, list] : tries) {
    QJsonArray array;
    for (const PracticeTry &t : list) {
      array.append(tryToJson(t));
    }
    out[QString::fromStdString(key)] = array;
  }
  return out;
}

} // namespace

QJsonObject practiceToJson(const PracticeData &data) {
  QJsonObject notes;
  for (const auto &[key, value] : data.notes) {
    notes[QString::fromStdString(key)] = QString::fromStdString(value);
  }
  return {
      {"format", kPracticeFile.format},      {"version", kPracticeFile.version},
      {"cards", progressToJson(data.cards)}, {"learn", triesToJson(data.learn)},
      {"timed", triesToJson(data.timed)},    {"notes", notes}};
}

PracticeData practiceFromJson(const QJsonObject &o) {
  PracticeData data;
  data.cards = progressFromJson(o.value("cards").toObject());
  const QJsonObject learn = o.value("learn").toObject();
  for (auto it = learn.begin(); it != learn.end(); ++it) {
    for (const QJsonValue &v : it.value().toArray()) {
      data.addLearn(it.key().toStdString(), tryFromJson(v.toObject()));
    }
  }
  const QJsonObject timed = o.value("timed").toObject();
  for (auto it = timed.begin(); it != timed.end(); ++it) {
    for (const QJsonValue &v : it.value().toArray()) {
      data.addTimed(it.key().toStdString(), tryFromJson(v.toObject()));
    }
  }
  const QJsonObject notes = o.value("notes").toObject();
  for (auto it = notes.begin(); it != notes.end(); ++it) {
    data.notes[it.key().toStdString()] = it.value().toString().toStdString();
  }
  return data;
}

QJsonObject removeOldPracticeFile(const QString &path) {
  finishBackgroundWrites(path);
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
  file.close();
  const QString format = o.value("format").toString();
  if (format != "v-cube.eocross" && format != "v-cube.f2l" &&
      format != "v-cube.tsle") {
    return {};
  }
  QFile::remove(path);
  return o;
}

std::map<std::string, std::string> oldOcllNotes(const QJsonObject &oldFile) {
  std::map<std::string, std::string> notes;
  const QJsonObject ocll = oldFile.value("ocll").toObject();
  for (auto it = ocll.begin(); it != ocll.end(); ++it) {
    if (!it.value().toString().isEmpty()) {
      notes["ocll/" + it.key().toStdString()] =
          it.value().toString().toStdString();
    }
  }
  return notes;
}

} // namespace vcube
