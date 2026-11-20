#include "io/algset_json.hpp"

#include "io/data_file.hpp"
#include "io/json_values.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <utility>

namespace vcube {

namespace {

const char *attemptModeId(AttemptMode m) {
  switch (m) {
  case AttemptMode::Learn:
    return "learn";
  case AttemptMode::VirtualDrill:
    return "vdrill";
  case AttemptMode::PhysicalDrill:
    break;
  }
  return "drill";
}

constexpr FileFormat kAlgSetFile{"v-cube.algset", 1, false, "alg set"};
// Progress files were written without a "format" at first.
constexpr FileField kProgressFields[] = {{"cases"}};
constexpr FileFormat kProgressFile{"v-cube.progress", 1, true, "progress file",
                                   kProgressFields};

bool fail(QString *error, const QString &why) {
  if (error) {
    *error = why;
  }
  return false;
}

QJsonObject maskToJson(const DiagramMask &m) {
  QJsonObject o{{"llCorners", qs(std::string(stickerModeName(m.llCorners)))},
                {"llEdges", qs(std::string(stickerModeName(m.llEdges)))},
                {"rest", qs(std::string(stickerModeName(m.rest)))}};
  setIf(o, "by", m.byPiece, "piece");
  return o;
}

DiagramMask maskFromJson(const QJsonObject &o) {
  DiagramMask m;
  auto read = [&o](const char *key, StickerMode fallback) {
    return parseStickerMode(ss(o.value(key).toString())).value_or(fallback);
  };
  m.llCorners = read("llCorners", StickerMode::Full);
  m.llEdges = read("llEdges", StickerMode::Full);
  m.rest = read("rest", StickerMode::Full);
  m.byPiece = o.value("by").toString() == "piece";
  return m;
}

constexpr std::array<std::pair<DiagramView, const char *>, 5> kViewNames = {{
    {DiagramView::TopLayer, "top"},
    {DiagramView::Iso3D, "3d"},
    {DiagramView::Iso3DFrontLeft, "3d-front-left"},
    {DiagramView::Iso3DBackLeft, "3d-back-left"},
    {DiagramView::Iso3DBackRight, "3d-back-right"},
}};

QString viewName(DiagramView v) {
  for (const auto &[view, name] : kViewNames) {
    if (view == v) {
      return name;
    }
  }
  return "top";
}

DiagramView viewFromName(const QString &s) {
  for (const auto &[view, name] : kViewNames) {
    if (s == name) {
      return view;
    }
  }
  return DiagramView::TopLayer;
}

// Reads the free/mask/view keys present in `o` over those of `spec`.
bool readSpec(const QJsonObject &o, StepSpec &spec, QString *error) {
  if (o.contains("free")) {
    auto m = parseFree(ss(o.value("free").toString()));
    if (!m) {
      if (error) {
        *error = QString("invalid \"free\" spec: %1")
                     .arg(o.value("free").toString());
      }
      return false;
    }
    spec.free = *m;
  }
  if (o.contains("mask")) {
    spec.mask = maskFromJson(o.value("mask").toObject());
  }
  if (o.contains("view")) {
    spec.view = viewFromName(o.value("view").toString());
  }
  return true;
}

void writeSpec(QJsonObject &o, const StepSpec &spec) {
  o["free"] = qs(freeToString(spec.free));
  o["mask"] = maskToJson(spec.mask);
  o["view"] = viewName(spec.view);
}

} // namespace

std::optional<AlgSet> algSetFromJson(const QJsonObject &o, QString *error) {
  if (checkFileFormat(o, kAlgSetFile, error) != FileStatus::Read) {
    return std::nullopt;
  }
  AlgSet set;
  set.id = ss(o.value("id").toString());
  set.name = ss(o.value("name").toString());
  set.description = ss(o.value("description").toString());
  set.builtin = o.value("builtin").toBool(false);
  set.aufBefore = o.value("aufBefore").toBool(true);
  set.aufAfter = o.value("aufAfter").toBool(true);
  set.targetSeconds = o.value("targetSeconds").toDouble(3.0);
  if (o.contains("targetSeconds") &&
      (!o.value("targetSeconds").isDouble() || !(set.targetSeconds > 0) ||
       !std::isfinite(set.targetSeconds))) {
    fail(error, "\"targetSeconds\" isn't a positive number of seconds");
    return std::nullopt;
  }
  set.guided = o.value("guided").toBool(true);
  set.families = o.value("families").toBool(false);
  set.reduction = o.value("reduction").toBool(false);
  for (const auto &mv : o.value("mirrors").toArray()) {
    QString name = mv.toString();
    Mirror m{name == "left-right" || name == "both",
             name == "front-back" || name == "both"};
    if (!m.none() && std::find(set.mirrors.begin(), set.mirrors.end(), m) ==
                         set.mirrors.end()) {
      set.mirrors.push_back(m);
    }
  }
  if (set.id.empty()) {
    if (error) {
      *error = "alg set has no id";
    }
    return std::nullopt;
  }
  if (!readSpec(o, set.spec, error)) {
    return std::nullopt;
  }
  std::set<std::string> caseIds;
  for (const auto &gv : o.value("groups").toArray()) {
    QJsonObject go = gv.toObject();
    CaseGroup g;
    g.name = ss(go.value("name").toString());
    if (go.contains("free") || go.contains("mask") || go.contains("view")) {
      StepSpec spec = set.spec;
      if (!readSpec(go, spec, error)) {
        return std::nullopt;
      }
      g.spec = spec;
    }
    for (const auto &cv : go.value("cases").toArray()) {
      QJsonObject co = cv.toObject();
      AlgCase c;
      c.id = ss(co.value("id").toString());
      c.name = ss(co.value("name").toString());
      if (c.id.empty()) {
        fail(error,
             QString("a case in group \"%1\" has no id").arg(qs(g.name)));
        return std::nullopt;
      }
      if (!caseIds.insert(c.id).second) {
        fail(error, QString("two cases have the id \"%1\"").arg(qs(c.id)));
        return std::nullopt;
      }
      auto facelets = FaceletCube::fromString(ss(co.value("state").toString()));
      std::optional<CubieCube> state;
      if (facelets) {
        state = CubieCube::fromFacelets(*facelets);
      }
      if (!state) {
        if (error) {
          *error = facelets
                       ? QString("case \"%1\" has stickers no real cube has")
                             .arg(qs(c.id))
                       : QString("case \"%1\" has an unreadable state")
                             .arg(qs(c.id));
        }
        return std::nullopt;
      }
      c.state = *state;
      for (const auto &av : co.value("algs").toArray()) {
        if (!av.isString()) {
          fail(error,
               QString("case \"%1\" has an alg that isn't text").arg(qs(c.id)));
          return std::nullopt;
        }
        c.algs.push_back(ss(av.toString()));
      }
      g.cases.push_back(std::move(c));
    }
    set.groups.push_back(std::move(g));
  }
  return set;
}

QJsonObject algSetToJson(const AlgSet &set) {
  QJsonObject o;
  o["format"] = kAlgSetFile.format;
  o["version"] = kAlgSetFile.version;
  o["id"] = qs(set.id);
  o["name"] = qs(set.name);
  o["description"] = qs(set.description);
  o["builtin"] = set.builtin;
  o["aufBefore"] = set.aufBefore;
  o["aufAfter"] = set.aufAfter;
  o["targetSeconds"] = set.targetSeconds;
  setIf(o, "guided", !set.guided, false);
  setIf(o, "families", set.families, true);
  setIf(o, "reduction", set.reduction, true);
  setIf(o, "mirrors", !set.mirrors.empty(),
        jsonArray(set.mirrors, [](Mirror m) {
          return m.leftRight && m.frontBack ? "both"
                 : m.leftRight              ? "left-right"
                                            : "front-back";
        }));
  writeSpec(o, set.spec);
  o["groups"] = jsonArray(set.groups, [](const CaseGroup &g) {
    QJsonObject go;
    go["name"] = qs(g.name);
    if (g.spec) {
      writeSpec(go, *g.spec);
    }
    go["cases"] = jsonArray(g.cases, [](const AlgCase &c) {
      return QJsonObject{{"id", qs(c.id)},
                         {"name", qs(c.name)},
                         {"state", qs(c.state.toFacelets().toString())},
                         {"algs", jsonArray(c.algs, qs)}};
    });
    return go;
  });
  return o;
}

QStringList unknownAlgSetKeys(const QJsonObject &o) {
  static const QStringList kSetKeys = {
      "format",   "version",   "id",       "name",          "description",
      "builtin",  "aufBefore", "aufAfter", "targetSeconds", "guided",
      "families", "reduction", "mirrors",  "free",          "mask",
      "view",     "groups"};
  static const QStringList kGroupKeys = {"name", "free", "mask", "view",
                                         "cases"};
  static const QStringList kCaseKeys = {"id", "name", "state", "algs"};
  QStringList out;
  auto check = [&out](const QJsonObject &obj, const QStringList &known,
                      const QString &where) {
    for (auto it = obj.begin(); it != obj.end(); ++it) {
      if (!known.contains(it.key())) {
        QString key = QString("\"%1\"").arg(it.key());
        if (!out.contains(where + key)) {
          out << where + key;
        }
      }
    }
  };
  check(o, kSetKeys, QString());
  for (const auto &gv : o.value("groups").toArray()) {
    QJsonObject go = gv.toObject();
    check(go, kGroupKeys, "in a group ");
    for (const auto &cv : go.value("cases").toArray()) {
      check(cv.toObject(), kCaseKeys, "in a case ");
    }
  }
  return out;
}

std::optional<AlgSet> readAlgSetFile(const QString &path, QString *error,
                                     FileStatus *status,
                                     QStringList *unknownKeys) {
  FileRead r = readDataFile(path, kAlgSetFile);
  if (status) {
    *status = r.status;
  }
  if (r.status != FileStatus::Read) {
    fail(error, r.status == FileStatus::Missing ? QString("there is no file")
                                                : r.error);
    return std::nullopt;
  }
  if (unknownKeys) {
    *unknownKeys = unknownAlgSetKeys(r.object);
  }
  return algSetFromJson(r.object, error);
}

bool writeFileAtomically(const QString &path, const QByteArray &data,
                         QString *error) {
  finishBackgroundWrites(path); // so that an older write can't land after
  QSaveFile f(path);
  auto fail = [&] {
    if (error) {
      QString why = f.errorString();
      *error = why.isEmpty() || why == "Unknown error"
                   ? QString("%1 couldn't be written")
                         .arg(QDir::toNativeSeparators(path))
                   : QString("%1: %2").arg(QDir::toNativeSeparators(path), why);
    }
    return false;
  };
  if (!f.open(QIODevice::WriteOnly)) {
    return fail();
  }
  if (f.write(data) != data.size()) {
    f.cancelWriting();
    return fail();
  }
  if (!f.commit()) {
    return fail();
  }
  return true;
}

QString setAside(const QString &path) {
  QString base = path + ".unreadable-" +
                 QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
  QString target = base;
  for (int i = 2; QFile::exists(target); ++i) {
    target = base + "-" + QString::number(i);
  }
  return QFile::rename(path, target) ? target : QString();
}

bool writeAlgSetFile(const AlgSet &set, const QString &path, QString *error) {
  return writeFileAtomically(
      path, QJsonDocument(algSetToJson(set)).toJson(QJsonDocument::Indented),
      error);
}

namespace {

QJsonObject caseProgressToJson(const CaseProgress &cp) {
  const SrsCard &c = cp.card;
  QJsonObject card{{"state", static_cast<int>(c.state)},
                   {"step", c.step},
                   {"ease", c.ease},
                   {"interval", c.intervalDays},
                   {"due", static_cast<double>(c.due)},
                   {"reps", c.reps},
                   {"lapses", c.lapses},
                   {"introduced", static_cast<double>(c.introduced)}};
  setIf(card, "marked", c.marked, true);
  setIf(card, "suspended", c.suspended, true);
  setIf(card, "buriedUntil", c.buriedUntil != 0,
        static_cast<double>(c.buriedUntil));
  QJsonArray attempts = jsonArray(cp.attempts, [](const Attempt &a) {
    QJsonArray row{static_cast<double>(a.timestamp),
                   static_cast<double>(a.timeMs), a.failed ? 1 : 0,
                   attemptModeId(a.mode)};
    // Recognition and execution times, when measured, follow.
    if (a.recognitionMs != 0 || a.executionMs != 0 || a.recognitionOnly) {
      row.append(static_cast<double>(a.recognitionMs));
      row.append(static_cast<double>(a.executionMs));
    }
    if (a.recognitionOnly) {
      row.append(1);
    }
    return row;
  });
  QJsonObject caseObject{{"card", card}, {"attempts", attempts}};
  // What is kept of the drill attempts that no longer are, per mode: the
  // best time and [day, attempts, fails, sum, best] per day.
  QJsonObject archive;
  for (const auto &[mode, kept] : cp.archive) {
    if (kept.empty()) {
      continue;
    }
    QJsonArray days = jsonArray(kept.days, [](const DayRollup &d) {
      return QJsonArray{static_cast<double>(d.day), d.count, d.fails,
                        static_cast<double>(d.sumMs),
                        static_cast<double>(d.bestMs)};
    });
    archive[attemptModeId(mode)] =
        QJsonObject{{"best", static_cast<double>(kept.bestMs)}, {"days", days}};
  }
  setIf(caseObject, "drillArchive", !archive.isEmpty(), archive);
  return caseObject;
}

} // namespace

QJsonObject progressToJson(const SetProgress &p,
                           KeyedJsonCache<CaseProgress> *cache) {
  QJsonObject cases;
  if (cache) {
    cases = cache->object(p, caseProgressToJson);
  } else {
    for (const auto &[id, cp] : p) {
      cases[qs(id)] = caseProgressToJson(cp);
    }
  }
  return QJsonObject{{"format", kProgressFile.format},
                     {"version", kProgressFile.version},
                     {"cases", cases}};
}

FileRead readProgressFile(const QString &path) {
  return readDataFile(path, kProgressFile);
}

SetProgress progressFromJson(const QJsonObject &o) {
  SetProgress p;
  QJsonObject cases = o.value("cases").toObject();
  for (auto it = cases.begin(); it != cases.end(); ++it) {
    QJsonObject co = it.value().toObject();
    QJsonObject card = co.value("card").toObject();
    CaseProgress cp;
    cp.card.state =
        static_cast<CardState>(std::clamp(card.value("state").toInt(), 0, 3));
    cp.card.step = std::clamp(card.value("step").toInt(), 0, 1000);
    cp.card.ease = std::clamp(card.value("ease").toDouble(2.5), 1.0, 10.0);
    cp.card.intervalDays =
        std::clamp(card.value("interval").toDouble(), 0.0, 36500.0);
    cp.card.due = jsonInt64(card.value("due"));
    cp.card.reps = std::max(0, card.value("reps").toInt());
    cp.card.lapses = std::max(0, card.value("lapses").toInt());
    for (const auto &av : co.value("attempts").toArray()) {
      QJsonArray a = av.toArray();
      // Files written before Learn mode existed have no mode, and files
      // written before virtual drills existed have only "drill": their
      // attempts were all timed on a physical cube.
      QString mode = a.at(3).toString();
      Attempt at{jsonInt64(a.at(0)), jsonInt64(a.at(1)), a.at(2).toInt() != 0,
                 mode == "learn"    ? AttemptMode::Learn
                 : mode == "vdrill" ? AttemptMode::VirtualDrill
                                    : AttemptMode::PhysicalDrill};
      // Older files have no recognition times.
      at.recognitionMs = jsonInt64(a.at(4));
      at.executionMs = jsonInt64(a.at(5));
      at.recognitionOnly = a.at(6).toInt() != 0;
      cp.attempts.push_back(at);
    }
    // Older files have no archive.
    QJsonObject archive = co.value("drillArchive").toObject();
    for (auto mt = archive.begin(); mt != archive.end(); ++mt) {
      AttemptMode mode = mt.key() == "vdrill"  ? AttemptMode::VirtualDrill
                         : mt.key() == "drill" ? AttemptMode::PhysicalDrill
                                               : AttemptMode::Learn;
      if (mode == AttemptMode::Learn) {
        continue;
      }
      QJsonObject ko = mt.value().toObject();
      DrillArchive kept;
      kept.bestMs = jsonInt64(ko.value("best"));
      for (const auto &dv : ko.value("days").toArray()) {
        QJsonArray d = dv.toArray();
        DayRollup r{jsonInt64(d.at(0)), std::max(0, d.at(1).toInt()),
                    std::max(0, d.at(2).toInt()), jsonInt64(d.at(3)),
                    jsonInt64(d.at(4))};
        if (r.count > 0) {
          r.fails = std::min(r.fails, r.count);
          kept.days.push_back(r);
        }
      }
      std::sort(
          kept.days.begin(), kept.days.end(),
          [](const DayRollup &a, const DayRollup &b) { return a.day < b.day; });
      if (!kept.empty()) {
        cp.archive[mode] = std::move(kept);
      }
    }
    cp.card.suspended = card.value("suspended").toBool(false);
    cp.card.buriedUntil = jsonInt64(card.value("buriedUntil"));
    if (card.contains("introduced")) {
      cp.card.introduced = jsonInt64(card.value("introduced"));
      cp.card.marked = card.value("marked").toBool(false);
    } else if (cp.card.reps > 0) {
      // Older files: a reviewed case was introduced at its first Learn
      // attempt, and a case in review that never was had been marked
      // learned. Only Learn reviewed a case, so one with reviews but no
      // Learn attempt was learned too, at a time not known (its attempts
      // were read as drill, from before they had a mode): it wasn't marked.
      auto first = std::find_if(
          cp.attempts.begin(), cp.attempts.end(),
          [](const Attempt &a) { return a.mode == AttemptMode::Learn; });
      cp.card.introduced = first != cp.attempts.end()
                               ? std::max<std::int64_t>(first->timestamp, 1)
                               : 1;
    } else if (cp.card.state == CardState::Review) {
      cp.card.introduced = 1;
      cp.card.marked = true;
    }
    p[ss(it.key())] = std::move(cp);
  }
  return p;
}

} // namespace vcube
