#include "io/inspection_store.hpp"

#include "io/algset_json.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

namespace vcube {

namespace {

constexpr const char *kFormat = "v-cube.inspection";

// A count as stored: 0..12, or -1 for none.
int countFrom(const QJsonObject &o, const char *key) {
  if (!o.contains(key)) {
    return -1;
  }
  return std::clamp(o.value(key).toInt(-1), -1, kEdges);
}

QString holdText(Hold h) { return QString("%1%2").arg(h.top).arg(h.front); }

Hold holdFrom(const QString &text) {
  if (text.size() == 2 && text[0].isDigit() && text[1].isDigit()) {
    Hold h{text[0].digitValue(), text[1].digitValue()};
    if (isValidHold(h)) {
      return h;
    }
  }
  return kWcaHold;
}

} // namespace

std::string_view drillId(InspectionDrill d) {
  return d == InspectionDrill::Edges ? "edges" : "plan";
}

std::optional<InspectionDrill> drillFromId(std::string_view id) {
  if (id == "plan") {
    return InspectionDrill::Plan;
  }
  if (id == "edges") {
    return InspectionDrill::Edges;
  }
  return std::nullopt;
}

std::string_view axesId(EdgeAxes a) {
  switch (a) {
  case EdgeAxes::FB:
    return "fb";
  case EdgeAxes::RL:
    return "rl";
  case EdgeAxes::Both:
    return "both";
  }
  return "both";
}

std::optional<EdgeAxes> axesFromId(std::string_view id) {
  for (EdgeAxes a : {EdgeAxes::FB, EdgeAxes::RL, EdgeAxes::Both}) {
    if (axesId(a) == id) {
      return a;
    }
  }
  return std::nullopt;
}

bool asksAxis(EdgeAxes a, int axis) {
  return a == EdgeAxes::Both ||
         (axis == 0 ? a == EdgeAxes::FB : a == EdgeAxes::RL);
}

bool InspectionAttempt::right() const {
  bool asked = false;
  for (std::size_t i = 0; i < 2; ++i) {
    if (correct[i] >= 0) {
      asked = true;
      if (answer[i] != correct[i]) {
        return false;
      }
    }
  }
  return asked;
}

const std::vector<SolveStep> &planSteps() {
  static const std::vector<SolveStep> steps = {
      SolveStep::EO, SolveStep::EOPlus1, SolveStep::EOPlus2,
      SolveStep::EOCross};
  return steps;
}

std::vector<InspectionSession> defaultInspectionSessions() {
  std::vector<InspectionSession> out;
  for (SolveStep step :
       {SolveStep::EO, SolveStep::EOPlus2, SolveStep::EOCross}) {
    InspectionSession s;
    s.name = std::string(stepName(step));
    s.step = step;
    out.push_back(std::move(s));
  }
  InspectionSession edges;
  edges.name = "Bad edges";
  edges.drill = InspectionDrill::Edges;
  edges.axes = EdgeAxes::Both;
  edges.limitSeconds = 0;
  edges.wca = false;
  out.push_back(std::move(edges));
  return out;
}

std::vector<Solve> planAsSolves(std::span<const InspectionAttempt> attempts) {
  std::vector<Solve> out;
  out.reserve(attempts.size());
  for (const auto &a : attempts) {
    Solve s;
    s.timeMs = static_cast<std::int64_t>(a.moves) * kMoveUnit;
    s.penalty = a.dnf ? Penalty::Dnf : Penalty::None;
    out.push_back(s);
  }
  return out;
}

std::vector<Solve> edgesAsSolves(std::span<const InspectionAttempt> attempts) {
  std::vector<Solve> out;
  out.reserve(attempts.size());
  for (const auto &a : attempts) {
    Solve s;
    s.timeMs = a.ms;
    s.penalty = a.right() ? Penalty::None : Penalty::Dnf;
    out.push_back(s);
  }
  return out;
}

InspectionStore::InspectionStore(QString path)
    : path_(std::move(path)), sessions_(defaultInspectionSessions()) {}

bool InspectionStore::load(QString *error) {
  QFile f(path_);
  if (!f.exists()) {
    sessions_ = defaultInspectionSessions();
    current_ = 0;
    return true;
  }
  if (!f.open(QIODevice::ReadOnly)) {
    return unreadable(error, f.errorString());
  }
  QJsonParseError pe;
  auto doc = QJsonDocument::fromJson(f.readAll(), &pe);
  f.close();
  if (pe.error != QJsonParseError::NoError || !doc.isObject() ||
      doc.object().value("format").toString() != kFormat) {
    return unreadable(error, pe.error != QJsonParseError::NoError
                                 ? pe.errorString()
                                 : QString("not an inspection file"));
  }
  sessions_ = inspectionFromJson(doc.object(), &current_);
  if (sessions_.empty()) {
    sessions_.push_back(InspectionSession{});
  }
  current_ = std::min(current_, sessions_.size() - 1);
  return true;
}

bool InspectionStore::unreadable(QString *error, const QString &why) {
  if (error) {
    *error = why;
  }
  sessions_ = defaultInspectionSessions();
  current_ = 0;
  setAside_ = setAside(path_);
  keepOff_ = setAside_.isEmpty();
  return false;
}

bool InspectionStore::save(QString *error) const {
  if (keepOff_) {
    if (error) {
      *error =
          QString("%1 couldn't be read, so it isn't saved over").arg(path_);
    }
    return false;
  }
  QDir().mkpath(QFileInfo(path_).absolutePath());
  return writeFileAtomically(
      path_,
      QJsonDocument(inspectionToJson(sessions_, current_))
          .toJson(QJsonDocument::Compact),
      error);
}

void InspectionStore::setCurrentIndex(std::size_t i) {
  if (i < sessions_.size()) {
    current_ = i;
  }
}

InspectionSession &InspectionStore::addSession(const std::string &name) {
  InspectionSession s;
  s.name = name;
  sessions_.push_back(std::move(s));
  current_ = sessions_.size() - 1;
  return sessions_.back();
}

void InspectionStore::removeSession(std::size_t i) {
  if (i >= sessions_.size()) {
    return;
  }
  sessions_.erase(sessions_.begin() + static_cast<std::ptrdiff_t>(i));
  if (sessions_.empty()) {
    sessions_.push_back(InspectionSession{});
  }
  current_ = std::min(current_, sessions_.size() - 1);
}

QJsonObject inspectionToJson(const std::vector<InspectionSession> &sessions,
                             std::size_t current) {
  QJsonArray arr;
  for (const auto &s : sessions) {
    bool plan = s.drill == InspectionDrill::Plan;
    QJsonArray attempts;
    for (const auto &a : s.attempts) {
      QJsonObject o{{"scramble", qs(a.scramble)},
                    {"timestamp", static_cast<double>(a.timestamp)},
                    {"ms", static_cast<double>(a.ms)}};
      if (plan) {
        o["moves"] = a.moves;
        if (a.dnf) {
          o["dnf"] = true;
        }
        if (!a.solution.empty()) {
          o["solution"] = qs(a.solution);
        }
        if (a.optimal) {
          o["optimal"] = *a.optimal;
        }
      } else {
        o["hold"] = holdText(a.hold);
        const char *keys[2][2] = {{"fb", "fbCorrect"}, {"rl", "rlCorrect"}};
        for (std::size_t i = 0; i < 2; ++i) {
          if (a.answer[i] >= 0) {
            o[keys[i][0]] = a.answer[i];
          }
          if (a.correct[i] >= 0) {
            o[keys[i][1]] = a.correct[i];
          }
        }
      }
      if (!a.comment.empty()) {
        o["comment"] = qs(a.comment);
      }
      attempts.append(o);
    }
    QJsonObject so{{"name", qs(s.name)},
                   {"drill", qs(std::string(drillId(s.drill)))},
                   {"attempts", attempts}};
    if (plan) {
      so["step"] = qs(std::string(stepId(s.step)));
      if (s.limitSeconds > 0) {
        so["limit"] = s.limitSeconds;
        if (s.wca) {
          so["wca"] = true;
        }
      }
    } else {
      so["axes"] = qs(std::string(axesId(s.axes)));
    }
    arr.append(so);
  }
  return QJsonObject{{"format", kFormat},
                     {"version", 1},
                     {"current", static_cast<int>(current)},
                     {"sessions", arr}};
}

std::vector<InspectionSession> inspectionFromJson(const QJsonObject &o,
                                                  std::size_t *current) {
  std::vector<InspectionSession> out;
  for (const auto &v : o.value("sessions").toArray()) {
    QJsonObject so = v.toObject();
    InspectionSession s;
    s.name = ss(so.value("name").toString("Session"));
    s.drill = drillFromId(ss(so.value("drill").toString()))
                  .value_or(InspectionDrill::Plan);
    std::optional<SolveStep> step = stepFromId(ss(so.value("step").toString()));
    s.step = step && *step != SolveStep::ZZF2L ? *step : SolveStep::EOCross;
    s.axes =
        axesFromId(ss(so.value("axes").toString())).value_or(EdgeAxes::Both);
    s.limitSeconds = std::clamp(so.value("limit").toInt(0), 0, 3600);
    s.wca = s.limitSeconds == 15 && so.value("wca").toBool(false);
    bool plan = s.drill == InspectionDrill::Plan;
    for (const auto &av : so.value("attempts").toArray()) {
      QJsonObject ao = av.toObject();
      InspectionAttempt a;
      a.scramble = ss(ao.value("scramble").toString());
      a.timestamp = static_cast<std::int64_t>(ao.value("timestamp").toDouble());
      a.ms = std::max<std::int64_t>(
          0, static_cast<std::int64_t>(ao.value("ms").toDouble()));
      a.comment = ss(ao.value("comment").toString());
      if (plan) {
        a.moves = std::clamp(ao.value("moves").toInt(), 0, kMaxMoveCount);
        a.dnf = ao.value("dnf").toBool(false);
        a.solution = ss(ao.value("solution").toString());
        if (ao.contains("optimal")) {
          a.optimal = std::clamp(ao.value("optimal").toInt(), 0, 99);
        }
      } else {
        a.hold = holdFrom(ao.value("hold").toString());
        a.answer = {countFrom(ao, "fb"), countFrom(ao, "rl")};
        a.correct = {countFrom(ao, "fbCorrect"), countFrom(ao, "rlCorrect")};
      }
      s.attempts.push_back(std::move(a));
    }
    out.push_back(std::move(s));
  }
  if (current) {
    *current =
        static_cast<std::size_t>(std::max(0, o.value("current").toInt()));
  }
  return out;
}

} // namespace vcube
