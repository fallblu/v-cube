#include "io/inspection_store.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

using namespace vcube;

namespace {

void writeText(const QString &path, const QByteArray &text) {
  QFile f(path);
  REQUIRE(f.open(QIODevice::WriteOnly));
  f.write(text);
}

InspectionSession planSession() {
  InspectionSession s;
  s.name = "EO+2 practice";
  s.drill = InspectionDrill::Plan;
  s.step = SolveStep::EOPlus2;
  s.limitSeconds = 15;
  s.wca = true;
  InspectionAttempt a;
  a.scramble = "R U F' D2";
  a.timestamp = 1794000000;
  a.ms = 11230;
  a.moves = 7;
  a.solution = "y F R' D L2";
  a.optimal = 6;
  a.comment = "close";
  s.attempts.push_back(a);
  InspectionAttempt dnf;
  dnf.scramble = "L2 B";
  dnf.timestamp = 1794000100;
  dnf.ms = 15000;
  dnf.moves = 4;
  dnf.dnf = true;
  s.attempts.push_back(dnf);
  return s;
}

InspectionSession edgesSession() {
  InspectionSession s;
  s.name = "Edges";
  s.drill = InspectionDrill::Edges;
  s.axes = EdgeAxes::Both;
  s.limitSeconds = 0;
  s.wca = false;
  InspectionAttempt a;
  a.scramble = "F B";
  a.timestamp = 1794000200;
  a.ms = 5400;
  a.hold = Hold{3, 4};
  a.answer = {8, 6};
  a.correct = {8, 4};
  s.attempts.push_back(a);
  return s;
}

} // namespace

TEST_CASE("inspection sessions survive a JSON round trip", "[inspection]") {
  std::vector<InspectionSession> sessions = {planSession(), edgesSession()};
  QJsonObject o = inspectionToJson(sessions, 1);
  CHECK(o.value("format").toString() == "v-cube.inspection");
  std::size_t current = 0;
  auto back = inspectionFromJson(o, &current);
  CHECK(current == 1);
  REQUIRE(back.size() == 2);

  const InspectionSession &p = back[0];
  CHECK(p.name == "EO+2 practice");
  CHECK(p.drill == InspectionDrill::Plan);
  CHECK(p.step == SolveStep::EOPlus2);
  CHECK(p.limitSeconds == 15);
  CHECK(p.wca);
  REQUIRE(p.attempts.size() == 2);
  CHECK(p.attempts[0].scramble == "R U F' D2");
  CHECK(p.attempts[0].timestamp == 1794000000);
  CHECK(p.attempts[0].ms == 11230);
  CHECK(p.attempts[0].moves == 7);
  CHECK_FALSE(p.attempts[0].dnf);
  CHECK(p.attempts[0].solution == "y F R' D L2");
  CHECK(p.attempts[0].optimal == 6);
  CHECK(p.attempts[0].comment == "close");
  CHECK(p.attempts[1].dnf);
  CHECK_FALSE(p.attempts[1].optimal);

  const InspectionSession &e = back[1];
  CHECK(e.drill == InspectionDrill::Edges);
  CHECK(e.axes == EdgeAxes::Both);
  CHECK(e.limitSeconds == 0);
  REQUIRE(e.attempts.size() == 1);
  const InspectionAttempt &a = e.attempts[0];
  CHECK(a.hold == Hold{3, 4});
  CHECK(a.answer == std::array<int, 2>{8, 6});
  CHECK(a.correct == std::array<int, 2>{8, 4});
  CHECK(a.ms == 5400);
  CHECK_FALSE(a.right());
}

TEST_CASE("inspection JSON leaves out what doesn't apply", "[inspection]") {
  InspectionSession plan = planSession();
  plan.limitSeconds = 0;
  plan.wca = false;
  InspectionSession edges = edgesSession();
  edges.axes = EdgeAxes::RL;
  edges.attempts[0].answer = {-1, 4};
  edges.attempts[0].correct = {-1, 4};
  QJsonObject o = inspectionToJson({plan, edges}, 0);
  QJsonObject po = o.value("sessions").toArray()[0].toObject();
  CHECK_FALSE(po.contains("limit"));
  CHECK_FALSE(po.contains("wca"));
  CHECK_FALSE(po.contains("axes"));
  QJsonObject dnf = po.value("attempts").toArray()[1].toObject();
  CHECK_FALSE(dnf.contains("optimal"));
  CHECK_FALSE(dnf.contains("solution"));
  CHECK_FALSE(dnf.contains("comment"));
  QJsonObject eo = o.value("sessions").toArray()[1].toObject();
  CHECK_FALSE(eo.contains("step"));
  CHECK(eo.value("axes").toString() == "rl");
  QJsonObject ea = eo.value("attempts").toArray()[0].toObject();
  CHECK_FALSE(ea.contains("fb"));
  CHECK_FALSE(ea.contains("fbCorrect"));
  CHECK_FALSE(ea.contains("moves"));
  CHECK(ea.value("hold").toString() == "34");

  auto back = inspectionFromJson(o);
  REQUIRE(back.size() == 2);
  CHECK(back[0].limitSeconds == 0);
  CHECK_FALSE(back[0].wca);
  CHECK(back[1].attempts[0].right());
}

TEST_CASE("inspection JSON with missing or unknown fields reads safely",
          "[inspection]") {
  QJsonObject o = QJsonDocument::fromJson(R"({
    "format": "v-cube.inspection", "version": 1, "current": 7,
    "sessions": [
      {"attempts": [{"scramble": "R"}]},
      {"name": "x", "drill": "juggling", "step": "zzf2l", "wca": true,
       "limit": 20},
      {"name": "e", "drill": "edges", "axes": "diagonal",
       "attempts": [{"hold": "33", "fb": 40}]}
    ]})")
                      .object();
  std::size_t current = 0;
  auto s = inspectionFromJson(o, &current);
  REQUIRE(s.size() == 3);
  CHECK(current == 7); // the store clamps it
  CHECK(s[0].name == "Session");
  CHECK(s[0].drill == InspectionDrill::Plan);
  CHECK(s[0].step == SolveStep::EOCross);
  CHECK(s[0].limitSeconds == 0);
  REQUIRE(s[0].attempts.size() == 1);
  CHECK(s[0].attempts[0].moves == 0);
  CHECK(s[1].drill == InspectionDrill::Plan);
  CHECK(s[1].step == SolveStep::EOCross); // ZZ F2L can't be planned
  CHECK(s[1].limitSeconds == 20);
  CHECK_FALSE(s[1].wca); // calls only go with 15 s
  CHECK(s[2].axes == EdgeAxes::Both);
  CHECK(s[2].attempts[0].hold == kWcaHold); // not a real hold
  CHECK(s[2].attempts[0].answer[0] == kEdges);
  CHECK(s[2].attempts[0].answer[1] == -1);
  CHECK_FALSE(s[2].attempts[0].right()); // no right count was asked
}

TEST_CASE("a new inspection store has the default sessions", "[inspection]") {
  QTemporaryDir tmp;
  InspectionStore store(tmp.filePath("inspection.json"));
  REQUIRE(store.load());
  auto defaults = defaultInspectionSessions();
  REQUIRE(store.sessions().size() == 4);
  CHECK(store.sessions()[0].step == SolveStep::EO);
  CHECK(store.sessions()[1].step == SolveStep::EOPlus2);
  CHECK(store.sessions()[2].step == SolveStep::EOCross);
  for (std::size_t i = 0; i < 3; ++i) {
    CHECK(store.sessions()[i].drill == InspectionDrill::Plan);
    CHECK(store.sessions()[i].limitSeconds == 15);
    CHECK(store.sessions()[i].wca);
  }
  CHECK(store.sessions()[3].drill == InspectionDrill::Edges);
  CHECK(store.sessions()[3].axes == EdgeAxes::Both);
  CHECK(store.sessions()[3].limitSeconds == 0);

  store.current().attempts.push_back(planSession().attempts[0]);
  store.setCurrentIndex(2);
  REQUIRE(store.save());
  InspectionStore again(tmp.filePath("inspection.json"));
  REQUIRE(again.load());
  CHECK(again.currentIndex() == 2);
  CHECK(again.sessions()[0].attempts.size() == 1);

  again.removeSession(0);
  again.removeSession(0);
  again.removeSession(0);
  again.removeSession(0);
  CHECK(again.sessions().size() == 1);
  CHECK(again.sessions()[0].attempts.empty());
}

TEST_CASE("an unreadable inspection file is set aside, not saved over",
          "[inspection]") {
  QTemporaryDir tmp;
  QString path = tmp.filePath("inspection.json");
  writeText(path, "{\"sessions\": [");
  InspectionStore store(path);
  QString error;
  CHECK_FALSE(store.load(&error));
  CHECK_FALSE(error.isEmpty());
  REQUIRE_FALSE(store.setAsidePath().isEmpty());
  QFile kept(store.setAsidePath());
  REQUIRE(kept.open(QIODevice::ReadOnly));
  CHECK(kept.readAll() == "{\"sessions\": [");
  CHECK(store.sessions().size() == defaultInspectionSessions().size());
  CHECK(store.save());
  CHECK(QDir(tmp.path())
            .entryList({"inspection.json.unreadable-*"}, QDir::Files)
            .size() == 1);

  // JSON that isn't an inspection file is set aside too.
  writeText(path, R"({"format": "v-cube.movecount", "sessions": []})");
  InspectionStore other(path);
  CHECK_FALSE(other.load(&error));
}

TEST_CASE("inspection attempts become statistics", "[inspection]") {
  InspectionSession plan = planSession();
  auto solves = planAsSolves(plan.attempts);
  REQUIRE(solves.size() == 2);
  CHECK(solves[0].timeMs == 7 * kMoveUnit);
  CHECK(solves[0].penalty == Penalty::None);
  CHECK(solves[1].penalty == Penalty::Dnf);

  InspectionSession edges = edgesSession();
  InspectionAttempt right = edges.attempts[0];
  right.answer = right.correct;
  edges.attempts.push_back(right);
  auto times = edgesAsSolves(edges.attempts);
  REQUIRE(times.size() == 2);
  CHECK(times[0].penalty == Penalty::Dnf);
  CHECK(times[1].penalty == Penalty::None);
  CHECK(times[1].timeMs == 5400);
  CHECK(asksAxis(EdgeAxes::Both, 1));
  CHECK_FALSE(asksAxis(EdgeAxes::FB, 1));
  CHECK(asksAxis(EdgeAxes::RL, 1));
}
