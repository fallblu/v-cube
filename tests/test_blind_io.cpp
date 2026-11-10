#include "io/blind_store.hpp"

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

QByteArray readText(const QString &path) {
  QFile f(path);
  REQUIRE(f.open(QIODevice::ReadOnly));
  return f.readAll();
}

BlindData sample() {
  BlindData d;
  d.sessions.clear();
  BlindSession virt;
  virt.name = "Virtual";
  virt.cube = CubeKind::Virtual;
  virt.attempts.push_back({83210, 31500, false, "R U F", "z2 R U' R'", "", 10});
  virt.attempts.push_back({120000, 50000, true, "D L", "", "slow", 20});
  BlindSession phys;
  phys.name = "Physical";
  phys.cube = CubeKind::Physical;
  phys.attempts.push_back({99000, 40000, false, "B2", "", "", 30});
  d.sessions = {virt, phys};
  d.current = 1;
  d.quiz.of(BldPiece::Edge, 0).record(true, 900);
  d.quiz.of(BldPiece::Edge, 0).record(false, 3000);
  d.quiz.of(BldPiece::Corner, 23).record(true, 1500);
  d.setups.of(BldPiece::Corner, 6).record(true, 4000);
  d.traces.push_back({61200, true, false, 40});
  d.traces.push_back({55000, true, true, 50});
  return d;
}

} // namespace

TEST_CASE("blind data round-trips through JSON", "[blind]") {
  BlindData d = sample();
  BlindData back = blindFromJson(blindToJson(d));
  CHECK(back == d);
  QJsonObject o = blindToJson(d);
  CHECK(o.value("format").toString() == "v-cube.blind");
  // Only stickers with attempts are written, by their Speffz letter.
  QJsonObject edges = o.value("quiz").toObject().value("edges").toObject();
  CHECK(edges.keys() == QStringList{"A"});
  CHECK(edges.value("A").toObject().value("correct").toInt() == 1);
}

TEST_CASE("letter statistics", "[blind]") {
  LetterStat s;
  CHECK_FALSE(s.accuracy());
  CHECK_FALSE(s.meanMs());
  s.record(true, 1000);
  s.record(true, 2000);
  s.record(false, 500);
  CHECK(s.attempts == 3);
  CHECK(s.correct == 2);
  CHECK(*s.accuracy() > 0.66);
  CHECK(*s.meanMs() == 1500.0);
  // A right answer counts for at most the cap.
  LetterStat slow;
  slow.record(true, 10 * 60 * 1000);
  CHECK(slow.correctMs == LetterStat::kMaxMs);
}

TEST_CASE("letter times from before the cap are read capped", "[blind]") {
  // Letter M (12) with a 9:06 mean over two right answers.
  QJsonObject stats{
      {"edges",
       QJsonObject{
           {"M", QJsonObject{{"attempts", 3}, {"correct", 2}, {"ms", 1092000}}},
           {"A", QJsonObject{{"attempts", 1}, {"correct", 1}, {"ms", 1200}}}}}};
  QJsonObject o{{"format", "v-cube.blind"}, {"version", 1}, {"quiz", stats}};
  BlindData d = blindFromJson(o);
  CHECK(d.quiz.of(BldPiece::Edge, 12).correctMs == 2 * LetterStat::kMaxMs);
  CHECK(d.quiz.of(BldPiece::Edge, 0).correctMs == 1200);
}

TEST_CASE("blind sessions as solves", "[blind]") {
  BlindData d = sample();
  auto solves = blindSolves(d.sessions[0]);
  REQUIRE(solves.size() == 2);
  CHECK(solves[0].timeMs == 83210);
  CHECK(solves[0].penalty == Penalty::None);
  CHECK(solves[1].penalty == Penalty::Dnf);
}

TEST_CASE("blind files with missing fields read sensibly", "[blind]") {
  QJsonObject o{{"format", "v-cube.blind"}, {"version", 1}};
  BlindData d = blindFromJson(o);
  REQUIRE(d.sessions.size() == 1);
  CHECK(d.sessions[0].cube == CubeKind::Virtual);
  CHECK(d.traces.empty());
  QJsonObject s{
      {"name", "Old"},
      {"attempts", QJsonArray{QJsonObject{{"time", 5000}, {"memo", 9000}}}}};
  o["sessions"] = QJsonArray{s};
  o["current"] = 7;
  d = blindFromJson(o);
  REQUIRE(d.sessions.size() == 1);
  CHECK(d.current == 0);
  CHECK(d.sessions[0].cube == CubeKind::Virtual);
  // The memo never takes longer than the whole solve.
  CHECK(d.sessions[0].attempts[0].memoMs == 5000);
}

TEST_CASE("a new blind store starts with one virtual session", "[blind]") {
  QTemporaryDir tmp;
  BlindStore store(tmp.filePath("blind.json"));
  REQUIRE(store.load());
  REQUIRE(store.sessions().size() == 1);
  CHECK(store.current().cube == CubeKind::Virtual);
  store.addSession("Two");
  CHECK(store.currentIndex() == 1);
  store.removeSession(0);
  store.removeSession(0);
  CHECK(store.sessions().size() == 1);
  for (int i = 0; i < 1005; ++i) {
    store.addTrace({i, true, true, i});
  }
  CHECK(store.data().traces.size() == BlindData::kMaxTraces);
  CHECK(store.data().traces.front().timeMs == 5);
  REQUIRE(store.save());
  BlindStore again(tmp.filePath("blind.json"));
  REQUIRE(again.load());
  CHECK(again.data() == store.data());
}

TEST_CASE("unreadable blind files are set aside, not saved over", "[blind]") {
  QTemporaryDir tmp;
  QString path = tmp.filePath("blind.json");
  writeText(path, "{\"sessions\": [");
  BlindStore store(path);
  QString error;
  CHECK_FALSE(store.load(&error));
  CHECK_FALSE(error.isEmpty());
  REQUIRE_FALSE(store.setAsidePath().isEmpty());
  CHECK(readText(store.setAsidePath()) == "{\"sessions\": [");
  CHECK(store.sessions().size() == 1);
  CHECK(store.save());
  QStringList kept =
      QDir(tmp.path()).entryList({"blind.json.unreadable-*"}, QDir::Files);
  CHECK(kept.size() == 1);

  // Another program's JSON isn't read as blind data either.
  writeText(path, "{\"format\": \"v-cube.movecount\"}");
  BlindStore other(path);
  CHECK_FALSE(other.load(&error));
}
