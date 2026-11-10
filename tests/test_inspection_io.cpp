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

TEST_CASE("plan attempts keep their hold and tracked pair", "[inspection]") {
  InspectionSession s = planSession();
  s.step = SolveStep::EOCross;
  s.pair = Slot::BL;
  s.attempts[0].solvedIn = Hold{3, 4};
  s.attempts[0].pairSolved = true;
  s.attempts[0].pairMoves = 9;
  s.attempts[1].pairSolved = false;
  InspectionSession line = planSession();
  line.step = SolveStep::EOLine;
  line.pair = Slot::FR; // only for EO Cross
  QJsonObject o = inspectionToJson({s, line}, 0);
  auto sessions = o.value("sessions").toArray();
  CHECK(sessions[0].toObject().value("pair").toString() == "bl");
  CHECK_FALSE(sessions[1].toObject().contains("pair"));
  CHECK(sessions[1].toObject().value("step").toString() == "eoline");
  QJsonObject first =
      sessions[0].toObject().value("attempts").toArray()[0].toObject();
  CHECK(first.value("hold").toString() == "34");
  CHECK(first.value("pairMoves").toInt() == 9);

  auto back = inspectionFromJson(o);
  REQUIRE(back.size() == 2);
  CHECK(back[0].pair == Slot::BL);
  CHECK(back[0].attempts[0].solvedIn == Hold{3, 4});
  CHECK(back[0].attempts[0].pairSolved == true);
  CHECK(back[0].attempts[0].pairMoves == 9);
  CHECK(back[0].attempts[1].pairSolved == false);
  CHECK_FALSE(back[0].attempts[1].solvedIn);
  CHECK(back[1].step == SolveStep::EOLine);
  CHECK_FALSE(back[1].pair);
  CHECK_FALSE(back[1].attempts[0].pairSolved);
  CHECK(slotFromId("fr") == Slot::FR);
  CHECK(slotFromId("FR") == std::nullopt);
}

namespace {
InspectionAttempt planned(int moves, std::optional<int> optimal,
                          std::int64_t ms, bool dnf = false) {
  InspectionAttempt a;
  a.moves = moves;
  a.optimal = optimal;
  a.ms = ms;
  a.dnf = dnf;
  return a;
}
} // namespace

TEST_CASE("plan attempts are charted against optimal", "[inspection]") {
  std::vector<InspectionAttempt> list = {
      planned(7, 6, 9000), planned(6, 6, 14000), planned(5, std::nullopt, 3000),
      planned(4, 7, 15000, true), planned(8, 8, 2000)};
  auto over = overOptimalAsSolves(list);
  REQUIRE(over.size() == 4); // the one without an optimum is left out
  CHECK(over[0].timeMs == 1 * kMoveUnit);
  CHECK(over[1].timeMs == 0);
  CHECK(over[2].penalty == Penalty::Dnf);
  CHECK(over[3].timeMs == 0);

  auto rate = optimalRateAsSolves(list, 2);
  REQUIRE(rate.size() == 4);
  CHECK(rate[0].timeMs == 0);     // 0 of 1
  CHECK(rate[1].timeMs == 50000); // 1 of 2: 50.000 %
  CHECK(rate[2].timeMs == 50000); // optimal, then a DNF
  CHECK(rate[3].timeMs == 50000); // a DNF, then optimal
  CHECK(rate[0].penalty == Penalty::None);

  PlanSummary all = summarizePlan(list);
  CHECK(all.attempts == 5);
  CHECK(all.dnfs == 1);
  CHECK(all.compared == 3);
  CHECK(all.atOptimal == 2);
  CHECK(*all.meanOver() > 0.33);
  CHECK(*all.optimalRate() > 0.66);
  CHECK_FALSE(PlanSummary{}.meanOver());

  auto byOptimal = planByOptimal(list);
  REQUIRE(byOptimal.size() == 3);
  CHECK(byOptimal[0].first == 6);
  CHECK(byOptimal[0].second.attempts == 2);
  CHECK(byOptimal[1].first == 7);
  CHECK(byOptimal[1].second.dnfs == 1);
  CHECK(byOptimal[2].first == 8);

  auto byTime = planByInspection(list);
  // 2 and 3 s, 9 s, 14 s, 15 s.
  REQUIRE(byTime.size() == 4);
  CHECK(byTime[0].from == 0);
  CHECK(byTime[0].to == 5);
  CHECK(byTime[0].summary.attempts == 2);
  CHECK(byTime[1].from == 8);
  CHECK(byTime[2].from == 12);
  CHECK(byTime[3].from == 15);
  CHECK(byTime[3].to == 20);
  CHECK(byTime[3].summary.dnfs == 1);
}
