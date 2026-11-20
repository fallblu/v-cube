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
  setIf(o, "ms", t.ms != 0, static_cast<double>(t.ms));
  setIf(o, "moves", t.moves != 0, t.moves);
  setIf(o, "best", t.best != 0, t.best);
  setIf(o, "extra", t.extra >= 0.0, t.extra);
  setIf(o, "dnf", t.dnf, true);
  setIf(o, "hint", t.hint, true);
  setIf(o, "inspect", t.inspectMs != 0, static_cast<double>(t.inspectMs));
  setIf(o, "over", t.over, true);
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
    out[qs(key)] = jsonArray(list, tryToJson);
  }
  return out;
}

} // namespace

QJsonObject practiceToJson(const PracticeData &data) {
  QJsonObject notes;
  for (const auto &[key, value] : data.notes) {
    notes[qs(key)] = qs(value);
  }
  QJsonObject choices;
  for (const auto &[key, algs] : data.choices) {
    choices[qs(key)] = jsonArray(algs, qs);
  }
  QJsonObject o = {
      {"format", kPracticeFile.format},      {"version", kPracticeFile.version},
      {"cards", progressToJson(data.cards)}, {"learn", triesToJson(data.learn)},
      {"timed", triesToJson(data.timed)},    {"notes", notes}};
  setIf(o, "inspection", !data.inspection.empty(),
        triesToJson(data.inspection));
  setIf(o, "choices", !choices.isEmpty(), choices);
  return o;
}

PracticeData practiceFromJson(const QJsonObject &o) {
  PracticeData data;
  data.cards = progressFromJson(o.value("cards").toObject());
  // Each list of tries under `key`, by its group or key, with `add`.
  auto readTries =
      [&](const char *key,
          void (PracticeData::*add)(const std::string &, const PracticeTry &)) {
        const QJsonObject lists = o.value(key).toObject();
        for (auto it = lists.begin(); it != lists.end(); ++it) {
          for (const QJsonValue &v : it.value().toArray()) {
            (data.*add)(ss(it.key()), tryFromJson(v.toObject()));
          }
        }
      };
  readTries("learn", &PracticeData::addLearn);
  readTries("timed", &PracticeData::addTimed);
  readTries("inspection", &PracticeData::addInspection);
  const QJsonObject choices = o.value("choices").toObject();
  for (auto it = choices.begin(); it != choices.end(); ++it) {
    std::vector<std::string> algs;
    for (const QJsonValue &v : it.value().toArray()) {
      if (!v.toString().trimmed().isEmpty()) {
        algs.push_back(ss(v.toString().trimmed()));
      }
    }
    if (!algs.empty()) {
      data.choices[ss(it.key())] = std::move(algs);
    }
  }
  const QJsonObject notes = o.value("notes").toObject();
  for (auto it = notes.begin(); it != notes.end(); ++it) {
    data.notes[ss(it.key())] = ss(it.value().toString());
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
  moveToDeleted(path, QFileInfo(path).dir().filePath(kDeletedFolder),
                QDateTime::currentDateTime());
  return o;
}

std::map<std::string, std::string> oldOcllNotes(const QJsonObject &oldFile) {
  std::map<std::string, std::string> notes;
  const QJsonObject ocll = oldFile.value("ocll").toObject();
  for (auto it = ocll.begin(); it != ocll.end(); ++it) {
    if (!it.value().toString().isEmpty()) {
      notes["ocll/" + ss(it.key())] = ss(it.value().toString());
    }
  }
  return notes;
}

} // namespace vcube
