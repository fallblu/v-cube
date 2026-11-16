#include "io/eo_cross_store.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>

using namespace vcube;

TEST_CASE("EO Cross statistics survive the file", "[eocross][io]") {
  EoCrossStats s;
  PairAttempt a;
  a.timestamp = 1794000000;
  a.ms = 9300;
  a.moves = 13;
  a.bestMoves = 11;
  a.excess = 2;
  a.hint = true;
  s.add({EoDrill::Solve, SolveStep::EOCross, 6}, a);
  s.add({EoDrill::Solve, SolveStep::EOCross, 6}, PairAttempt{});
  PairAttempt dnf;
  dnf.gaveUp = true;
  s.add({EoDrill::Plan, SolveStep::EOPlus2, 4}, dnf);
  s.add({EoDrill::Edges, SolveStep::EOCross, 8}, a);
  s.add({EoDrill::Axis, SolveStep::EOLine, -1}, a);
  s.add({EoDrill::Full, SolveStep::EOCross, 12}, a);
  CHECK(eoCrossFromJson(eoCrossToJson(s)) == s);

  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  QString path = QDir(dir.path()).filePath("eocross.json");
  EoCrossStore store(path);
  CHECK(store.load());
  CHECK(store.data == EoCrossStats{});
  store.data = s;
  REQUIRE(store.save());
  EoCrossStore again(path);
  REQUIRE(again.load());
  CHECK(again.data == s);
}

TEST_CASE("an EO Cross file is read with care", "[eocross][io]") {
  QJsonObject o = eoCrossToJson(EoCrossStats{});
  QJsonObject kinds;
  kinds["not-a-kind"] = QJsonArray{QJsonObject{{"ms", 5}}};
  kinds["solve/eocross/5"] = QJsonArray{QJsonObject{{"ms", 5}}};
  QJsonArray many;
  for (int i = 0; i < 70; ++i) {
    many.append(QJsonObject{{"timestamp", i}});
  }
  kinds["full/6"] = many;
  o["kinds"] = kinds;
  EoCrossStats s = eoCrossFromJson(o);
  REQUIRE(s.kinds.size() == 1);
  const auto &kept = s.tries({EoDrill::Full, SolveStep::EOCross, 6});
  REQUIRE(kept.size() == EoCrossStats::kPerKind);
  CHECK(kept.back().timestamp == 69);

  // Another mode's file isn't taken for this one's.
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  QString path = QDir(dir.path()).filePath("eocross.json");
  {
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write("{\"format\": \"v-cube.f2l\"}");
  }
  EoCrossStore store(path);
  QString error;
  CHECK_FALSE(store.load(&error));
  CHECK_FALSE(error.isEmpty());
  CHECK(store.data == EoCrossStats{});
}
