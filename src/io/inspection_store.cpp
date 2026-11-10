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

constexpr FileFormat kInspectionFile{"v-cube.inspection", 1, false,
                                     "inspection file"};

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
      SolveStep::EO, SolveStep::EOLine, SolveStep::EOPlus1, SolveStep::EOPlus2,
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

std::vector<Solve>
overOptimalAsSolves(std::span<const InspectionAttempt> attempts) {
  std::vector<Solve> out;
  for (const auto &a : attempts) {
    if (!a.optimal) {
      continue;
    }
    Solve s;
    s.timeMs = static_cast<std::int64_t>(std::max(0, a.moves - *a.optimal)) *
               kMoveUnit;
    s.penalty = a.dnf ? Penalty::Dnf : Penalty::None;
    out.push_back(s);
  }
  return out;
}

std::vector<Solve>
optimalRateAsSolves(std::span<const InspectionAttempt> attempts,
                    std::size_t window) {
  window = std::max<std::size_t>(1, window);
  std::vector<bool> optimal;
  std::vector<Solve> out;
  for (const auto &a : attempts) {
    if (!a.optimal) {
      continue;
    }
    optimal.push_back(!a.dnf && a.moves <= *a.optimal);
    std::size_t n = std::min(window, optimal.size());
    auto hits = std::count(optimal.end() - static_cast<std::ptrdiff_t>(n),
                           optimal.end(), true);
    Solve s;
    // Thousandths of a percent.
    s.timeMs =
        static_cast<std::int64_t>(hits) * 100000 / static_cast<std::int64_t>(n);
    out.push_back(s);
  }
  return out;
}

void PlanSummary::add(const InspectionAttempt &a) {
  ++attempts;
  inspectionMs += a.ms;
  if (a.dnf) {
    ++dnfs;
  } else if (a.optimal) {
    ++compared;
    over += std::max(0, a.moves - *a.optimal);
    atOptimal += a.moves <= *a.optimal ? 1 : 0;
  }
}

std::optional<double> PlanSummary::meanOver() const {
  if (compared == 0) {
    return std::nullopt;
  }
  return static_cast<double>(over) / compared;
}

std::optional<double> PlanSummary::optimalRate() const {
  if (compared == 0) {
    return std::nullopt;
  }
  return static_cast<double>(atOptimal) / compared;
}

PlanSummary summarizePlan(std::span<const InspectionAttempt> attempts) {
  PlanSummary out;
  for (const auto &a : attempts) {
    out.add(a);
  }
  return out;
}

std::vector<std::pair<int, PlanSummary>>
planByOptimal(std::span<const InspectionAttempt> attempts) {
  std::vector<std::pair<int, PlanSummary>> out;
  for (const auto &a : attempts) {
    if (!a.optimal) {
      continue;
    }
    auto it = std::lower_bound(
        out.begin(), out.end(), *a.optimal,
        [](const auto &entry, int length) { return entry.first < length; });
    if (it == out.end() || it->first != *a.optimal) {
      it = out.insert(it, {*a.optimal, PlanSummary{}});
    }
    it->second.add(a);
  }
  return out;
}

std::vector<InspectionRange>
planByInspection(std::span<const InspectionAttempt> attempts) {
  std::vector<InspectionRange> ranges;
  for (std::size_t i = 0; i < kInspectionRanges.size(); ++i) {
    InspectionRange r;
    r.from = kInspectionRanges[i];
    r.to = i + 1 < kInspectionRanges.size() ? kInspectionRanges[i + 1] : -1;
    ranges.push_back(r);
  }
  for (const auto &a : attempts) {
    for (auto it = ranges.rbegin(); it != ranges.rend(); ++it) {
      if (a.ms >= static_cast<std::int64_t>(it->from) * 1000) {
        it->summary.add(a);
        break;
      }
    }
  }
  std::erase_if(
      ranges, [](const InspectionRange &r) { return r.summary.attempts == 0; });
  return ranges;
}

std::string slotId(Slot s) {
  std::string id(slotName(s));
  for (char &c : id) {
    c = static_cast<char>(c - 'A' + 'a');
  }
  return id;
}

std::optional<Slot> slotFromId(std::string_view id) {
  for (Slot s : {Slot::FR, Slot::FL, Slot::BL, Slot::BR}) {
    if (slotId(s) == id) {
      return s;
    }
  }
  return std::nullopt;
}

InspectionStore::InspectionStore(QString path)
    : SessionFileStore(std::move(path), kInspectionFile,
                       defaultInspectionSessions()) {}

std::vector<InspectionSession> InspectionStore::defaults() const {
  return defaultInspectionSessions();
}

QJsonObject InspectionStore::toJson() const {
  return inspectionToJson(sessions_, current_);
}

void InspectionStore::fromJson(const QJsonObject &o) {
  std::size_t current = 0;
  std::vector<InspectionSession> sessions = inspectionFromJson(o, &current);
  assign(std::move(sessions), current);
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
        if (a.solvedIn) {
          o["hold"] = holdText(*a.solvedIn);
        }
        if (a.pairSolved) {
          o["pair"] = *a.pairSolved;
          if (a.pairMoves > 0) {
            o["pairMoves"] = a.pairMoves;
          }
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
      if (s.pair && s.step == SolveStep::EOCross) {
        so["pair"] = qs(slotId(*s.pair));
      }
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
  return QJsonObject{{"format", kInspectionFile.format},
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
    if (s.step == SolveStep::EOCross) {
      s.pair = slotFromId(ss(so.value("pair").toString()));
    }
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
        if (ao.contains("hold")) {
          a.solvedIn = holdFrom(ao.value("hold").toString());
        }
        if (ao.contains("pair")) {
          a.pairSolved = ao.value("pair").toBool(false);
          a.pairMoves =
              std::clamp(ao.value("pairMoves").toInt(), 0, kMaxMoveCount);
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
