#include "io/tsle_store.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QJsonArray>
#include <QTemporaryDir>

using namespace vcube;

TEST_CASE("TSLE progress survives the file", "[tsle][io]") {
  TsleProgress p;
  const std::vector<TsleState> &cases = tsleCases();
  PairAttempt a;
  a.timestamp = 1794000000;
  a.ms = 1800;
  a.moves = 4;
  a.bestMoves = 3;
  a.excess = 1;
  a.hint = true;
  a.drill = F2LDrill::Full;
  p.add(tsleKey(cases[20]), a);
  p.add(tsleKey(cases[20]), PairAttempt{});
  p.add(tsleKey(cases[50]), PairAttempt{});
  p.introduced = {tsleKey(cases[20]), tsleKey(cases[21])};
  p.add(FullAttempt{1794000300, 6100, 14, 3, 2.0});
  p.ocll["antisune"] = "L' U' L U' L' U2 L";
  CHECK(tsleFromJson(tsleToJson(p)) == p);

  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  QString path = QDir(dir.path()).filePath("tsle.json");
  TsleStore store(path);
  CHECK(store.load());
  CHECK(store.progress == TsleProgress{});
  store.progress = p;
  REQUIRE(store.save());
  TsleStore again(path);
  REQUIRE(again.load());
  CHECK(again.progress == p);
  CHECK(again.progress.ocllAlgs()[1].alg == "L' U' L U' L' U2 L");
}

TEST_CASE("a TSLE file is read with care", "[tsle][io]") {
  QJsonObject o = tsleToJson(TsleProgress{});
  QJsonObject cases;
  cases["not-a-case"] = QJsonArray{QJsonObject{{"ms", 5}}};
  QJsonArray many;
  for (int i = 0; i < 70; ++i) {
    many.append(QJsonObject{{"timestamp", i}});
  }
  std::string key = tsleKey(tsleCases().back());
  cases[QString::fromStdString(key)] = many;
  o["cases"] = cases;
  o["introduced"] = QJsonArray{"nonsense", QString::fromStdString(key)};
  o["ocll"] =
      QJsonObject{{"sune", "R U R'"}, {"pi", "R U2 R2 U' R2 U' R2 U2 R U4"}};
  TsleProgress p = tsleFromJson(o);
  REQUIRE(p.cases.size() == 1);
  CHECK(p.cases.at(key).size() == TsleProgress::kPerCase);
  CHECK(p.cases.at(key).front().timestamp == 20);
  CHECK(p.introduced == std::set<std::string>{key});
  // An alg that doesn't solve its OCLL is dropped.
  CHECK_FALSE(p.ocll.contains("sune"));
  CHECK(p.ocll.contains("pi"));
}
