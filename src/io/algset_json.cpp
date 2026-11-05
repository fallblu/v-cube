#include "io/algset_json.hpp"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <array>
#include <utility>

namespace vcube {

namespace {

constexpr auto kFormat = "v-cube.algset";

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
  if (o.value("format").toString() != kFormat) {
    if (error) {
      *error = "not a v-cube alg set";
    }
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
  if (set.id.empty()) {
    if (error) {
      *error = "alg set has no id";
    }
    return std::nullopt;
  }
  if (!readSpec(o, set.spec, error)) {
    return std::nullopt;
  }
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
      auto facelets = FaceletCube::fromString(ss(co.value("state").toString()));
      std::optional<CubieCube> state;
      if (facelets) {
        state = CubieCube::fromFacelets(*facelets);
      }
      if (!state) {
        if (error) {
          *error = QString("case \"%1\" has an invalid state").arg(qs(c.id));
        }
        return std::nullopt;
      }
      c.state = *state;
      for (const auto &av : co.value("algs").toArray()) {
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
  o["format"] = kFormat;
  o["version"] = 1;
  o["id"] = qs(set.id);
  o["name"] = qs(set.name);
  o["description"] = qs(set.description);
  o["builtin"] = set.builtin;
  o["aufBefore"] = set.aufBefore;
  o["aufAfter"] = set.aufAfter;
  o["targetSeconds"] = set.targetSeconds;
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

std::optional<AlgSet> readAlgSetFile(const QString &path, QString *error) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = f.errorString();
    }
    return std::nullopt;
  }
  QJsonParseError pe;
  auto doc = QJsonDocument::fromJson(f.readAll(), &pe);
  if (pe.error != QJsonParseError::NoError) {
    if (error) {
      *error = pe.errorString();
    }
    return std::nullopt;
  }
  return algSetFromJson(doc.object(), error);
}

bool writeFileAtomically(const QString &path, const QByteArray &data,
                         QString *error) {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) {
    if (error) {
      *error = f.errorString();
    }
    return false;
  }
  f.write(data);
  if (!f.commit()) {
    if (error) {
      *error = f.errorString();
    }
    return false;
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
                     {"lapses", c.lapses}};
    QJsonArray attempts;
    for (const auto &a : cp.attempts) {
      attempts.append(QJsonArray{
          static_cast<double>(a.timestamp), static_cast<double>(a.timeMs),
          a.failed ? 1 : 0, a.mode == AttemptMode::Learn ? "learn" : "drill"});
    }
    cases[qs(id)] = QJsonObject{{"card", card}, {"attempts", attempts}};
  }
  return QJsonObject{{"version", 1}, {"cases", cases}};
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
      // Files written before Learn mode existed have no mode: their
      // attempts were all timed, so they count as drill attempts.
      cp.attempts.push_back(
          {static_cast<std::int64_t>(a.at(0).toDouble()),
           static_cast<std::int64_t>(a.at(1).toDouble()), a.at(2).toInt() != 0,
           a.at(3).toString() == "learn" ? AttemptMode::Learn
                                         : AttemptMode::Drill});
    }
    p[ss(it.key())] = std::move(cp);
  }
  return p;
}

} // namespace vcube
