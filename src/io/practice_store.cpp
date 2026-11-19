#include "io/practice_store.hpp"

#include "io/algset_json.hpp"
#include "io/backup.hpp"
#include "io/json_values.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

namespace vcube {

namespace {

QJsonObject tryToJson(const PracticeTry &t) {
  QJsonObject o = {{"timestamp", static_cast<double>(t.timestamp)}};
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
  if (t.inspectMs != 0) {
    o["inspect"] = static_cast<double>(t.inspectMs);
  }
  if (t.over) {
    o["over"] = true;
  }
  return o;
}

PracticeTry tryFromJson(const QJsonObject &o) {
  PracticeTry t;
  t.timestamp = jsonInt64(o.value("timestamp"));
  t.ms = jsonInt64(o.value("ms"));
  t.moves = std::max(0, o.value("moves").toInt());
  t.best = std::max(0, o.value("best").toInt());
  t.extra =
      o.contains("extra") ? std::max(0.0, o.value("extra").toDouble()) : -1.0;
  t.dnf = o.value("dnf").toBool();
  t.hint = o.value("hint").toBool();
  t.inspectMs = jsonInt64(o.value("inspect"));
  t.over = o.value("over").toBool();
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
  QJsonObject choices;
  for (const auto &[key, algs] : data.choices) {
    QJsonArray list;
    for (const std::string &alg : algs) {
      list.append(QString::fromStdString(alg));
    }
    choices[QString::fromStdString(key)] = list;
  }
  QJsonObject o = {
      {"format", kPracticeFile.format},      {"version", kPracticeFile.version},
      {"cards", progressToJson(data.cards)}, {"learn", triesToJson(data.learn)},
      {"timed", triesToJson(data.timed)},    {"notes", notes}};
  if (!data.inspection.empty()) {
    o["inspection"] = triesToJson(data.inspection);
  }
  if (!choices.isEmpty()) {
    o["choices"] = choices;
  }
  return o;
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
  const QJsonObject inspection = o.value("inspection").toObject();
  for (auto it = inspection.begin(); it != inspection.end(); ++it) {
    for (const QJsonValue &v : it.value().toArray()) {
      data.addInspection(it.key().toStdString(), tryFromJson(v.toObject()));
    }
  }
  const QJsonObject choices = o.value("choices").toObject();
  for (auto it = choices.begin(); it != choices.end(); ++it) {
    std::vector<std::string> algs;
    for (const QJsonValue &v : it.value().toArray()) {
      if (!v.toString().trimmed().isEmpty()) {
        algs.push_back(v.toString().trimmed().toStdString());
      }
    }
    if (!algs.empty()) {
      data.choices[it.key().toStdString()] = std::move(algs);
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
  // Kept beside the data rather than removed, so that what the mode held
  // can still be brought back by hand. If it can't be moved it stays, and
  // loading sets it aside as a file of the wrong format.
  moveToDeleted(path, QFileInfo(path).dir().filePath("deleted"),
                QDateTime::currentDateTime());
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
