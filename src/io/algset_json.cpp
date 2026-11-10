#include "io/algset_json.hpp"

#include "io/data_file.hpp"

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
constexpr FileFormat kProgressFile{"v-cube.progress", 1, true,
                                   "progress file"};

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
  if (m.byPiece) {
    o["by"] = "piece";
  }
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

// Reads free/mask/view keys present in `o` over `base`.
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
        fail(error, QString("a case in group \"%1\" has no id")
                        .arg(qs(g.name)));
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
  if (!set.guided) {
    o["guided"] = false;
  }
  if (set.families) {
    o["families"] = true;
  }
  if (set.reduction) {
    o["reduction"] = true;
  }
  if (!set.mirrors.empty()) {
    QJsonArray mirrors;
    for (Mirror m : set.mirrors) {
      mirrors.append(m.leftRight && m.frontBack ? "both"
                     : m.leftRight              ? "left-right"
                                                : "front-back");
    }
    o["mirrors"] = mirrors;
  }
  writeSpec(o, set.spec);
  QJsonArray groups;
  for (const auto &g : set.groups) {
    QJsonObject go;
    go["name"] = qs(g.name);
    if (g.spec) {
      writeSpec(go, *g.spec);
    }
    QJsonArray cases;
    for (const auto &c : g.cases) {
      QJsonArray algs;
      for (const auto &a : c.algs) {
        algs.append(qs(a));
      }
      cases.append(QJsonObject{{"id", qs(c.id)},
                               {"name", qs(c.name)},
                               {"state", qs(c.state.toFacelets().toString())},
                               {"algs", algs}});
    }
    go["cases"] = cases;
    groups.append(go);
  }
  o["groups"] = groups;
  return o;
}

std::optional<AlgSet> readAlgSetFile(const QString &path, QString *error,
                                     FileStatus *status) {
  FileRead r = readDataFile(path, kAlgSetFile);
  if (status) {
    *status = r.status;
  }
  if (r.status != FileStatus::Read) {
    fail(error, r.status == FileStatus::Missing ? QString("there is no file")
                                                : r.error);
    return std::nullopt;
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
                   : why;
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

QJsonObject progressToJson(const SetProgress &p) {
  QJsonObject cases;
  for (const auto &[id, cp] : p) {
    const SrsCard &c = cp.card;
    QJsonObject card{{"state", static_cast<int>(c.state)},
                     {"step", c.step},
                     {"ease", c.ease},
                     {"interval", c.intervalDays},
                     {"due", static_cast<double>(c.due)},
                     {"reps", c.reps},
                     {"lapses", c.lapses},
                     {"introduced", static_cast<double>(c.introduced)}};
    if (c.marked) {
      card["marked"] = true;
    }
    QJsonArray attempts;
    for (const auto &a : cp.attempts) {
      attempts.append(QJsonArray{static_cast<double>(a.timestamp),
                                 static_cast<double>(a.timeMs),
                                 a.failed ? 1 : 0, attemptModeId(a.mode)});
    }
    cases[qs(id)] = QJsonObject{{"card", card}, {"attempts", attempts}};
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
    cp.card.step = card.value("step").toInt();
    cp.card.ease = card.value("ease").toDouble(2.5);
    cp.card.intervalDays = card.value("interval").toDouble();
    cp.card.due = static_cast<std::int64_t>(card.value("due").toDouble());
    cp.card.reps = card.value("reps").toInt();
    cp.card.lapses = card.value("lapses").toInt();
    for (const auto &av : co.value("attempts").toArray()) {
      QJsonArray a = av.toArray();
      // Files written before Learn mode existed have no mode, and files
      // written before virtual drills existed have only "drill": their
      // attempts were all timed on a physical cube.
      QString mode = a.at(3).toString();
      cp.attempts.push_back({static_cast<std::int64_t>(a.at(0).toDouble()),
                             static_cast<std::int64_t>(a.at(1).toDouble()),
                             a.at(2).toInt() != 0,
                             mode == "learn"    ? AttemptMode::Learn
                             : mode == "vdrill" ? AttemptMode::VirtualDrill
                                                : AttemptMode::PhysicalDrill});
    }
    if (card.contains("introduced")) {
      cp.card.introduced =
          static_cast<std::int64_t>(card.value("introduced").toDouble());
      cp.card.marked = card.value("marked").toBool(false);
    } else if (cp.card.reps > 0) {
      // Older files: a reviewed case was introduced at its first Learn
      // attempt, and a case in review that never was had been marked
      // learned.
      auto first = std::find_if(
          cp.attempts.begin(), cp.attempts.end(),
          [](const Attempt &a) { return a.mode == AttemptMode::Learn; });
      cp.card.introduced =
          first != cp.attempts.end() ? std::max<std::int64_t>(first->timestamp, 1)
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
