#include "io/f2l_store.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>

using namespace vcube;

namespace {

F2LStats someStats() {
  F2LStats s;
  PairAttempt a;
  a.timestamp = 1794000000;
  a.ms = 2310;
  a.moves = 8;
  a.bestMoves = 7;
  a.excess = 0.45;
  a.hint = true;
  a.drill = F2LDrill::Choice;
  s.add("URF1-UB0", a);
  PairAttempt gaveUp;
  gaveUp.timestamp = 1794000100;
  gaveUp.gaveUp = true;
  gaveUp.drill = F2LDrill::Full;
  s.add("URF1-UB0", gaveUp);
  s.add("DFR1-FR0", PairAttempt{});
  s.add(ChoiceAttempt{1794000200, 2, 3, 1.25});
  s.add(FullAttempt{1794000300, 14200, 31, 3, 4.5});
  return s;
}

} // namespace

TEST_CASE("F2L statistics survive the file", "[f2l][io]") {
  F2LStats stats = someStats();
  CHECK(f2lFromJson(f2lToJson(stats)) == stats);

  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  QString path = QDir(dir.path()).filePath("f2l.json");
  F2LStore store(path);
  CHECK(store.load()); // no file yet
  CHECK(store.stats == F2LStats{});
  store.stats = stats;
  REQUIRE(store.save());
  F2LStore again(path);
  REQUIRE(again.load());
  CHECK(again.stats == stats);
}

TEST_CASE("an F2L file is read with care", "[f2l][io]") {
  QJsonObject o = f2lToJson(someStats());
  // Too many tries of a case: the newest are kept.
  QJsonArray many;
  for (int i = 0; i < 80; ++i) {
    many.append(QJsonObject{{"timestamp", i}, {"ms", -5}, {"excess", -1.0}});
  }
  QJsonObject cases;
  cases["URF0-UF0"] = many;
  cases["empty"] = QJsonArray{};
  o["cases"] = cases;
  o["choices"] = QJsonArray{QJsonObject{{"rank", 9}, {"of", 3}}};
  F2LStats s = f2lFromJson(o);
  REQUIRE(s.cases.size() == 1);
  const std::vector<PairAttempt> &kept = s.cases.at("URF0-UF0");
  CHECK(kept.size() == F2LStats::kPerCase);
  CHECK(kept.front().timestamp == 30);
  CHECK(kept.front().ms == 0);
  CHECK(kept.front().excess == 0.0);
  CHECK(kept.front().drill == F2LDrill::Pair);
  REQUIRE(s.choices.size() == 1);
  CHECK(s.choices[0].rank == 3);

  // A file that isn't an F2L file is set aside, not read.
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  QString path = QDir(dir.path()).filePath("f2l.json");
  QFile f(path);
  REQUIRE(f.open(QIODevice::WriteOnly));
  f.write("{\"format\": \"v-cube.blind\"}");
  f.close();
  F2LStore store(path);
  QString error;
  CHECK_FALSE(store.load(&error));
  CHECK_FALSE(error.isEmpty());
  CHECK(store.stats == F2LStats{});
}
