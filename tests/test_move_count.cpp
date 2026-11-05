#include "cube/scramble.hpp"
#include "io/move_counts.hpp"
#include "train/move_count.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

using namespace vcube;

namespace {
std::vector<MoveCountAttempt> attempts(std::initializer_list<int> counts) {
  std::vector<MoveCountAttempt> out;
  for (int c : counts) {
    MoveCountAttempt a;
    a.moves = c < 0 ? -c : c;
    a.dnf = c < 0;
    out.push_back(a);
  }
  return out;
}
} // namespace

TEST_CASE("move count statistics use WCA averages in thousandths of a move") {
  auto a = attempts({7, 9, 8, 6, 12});
  auto s = attemptsAsSolves(a);
  REQUIRE(s.size() == 5);
  CHECK(averageOf(s) == 8000); // drops 6 and 12
  CHECK(meanOf(std::span(s).first(3)) == 8000);
  CHECK(currentMean(s, 3) == 8667); // (8 + 6 + 12) / 3
  CHECK(formatMoves(currentMean(s, 3)) == "8.67");
  CHECK(formatMoves(currentMean(s, 3), 1) == "8.7");
  CHECK(formatMoves(currentMean(s, 3), 0) == "9");
  CHECK(formatMoves(8000) == "8.00");
  CHECK(formatMoves(8005) == "8.01");
  CHECK(formatMoves(std::nullopt) == "-");
  CHECK(formatMoves(kDnfTime) == "DNF");
  CHECK(formatMoves(summarize(s).best, 0) == "6");

  auto withDnf = attemptsAsSolves(attempts({7, -9, 8, 6, 12}));
  CHECK(averageOf(withDnf) == 9000); // DNF is the dropped worst: 7, 8, 12
  CHECK(meanOf(std::span(withDnf).first(3)) == kDnfTime);
  CHECK(summarize(withDnf).dnfs == 1);
}

TEST_CASE("move count attempts are listed as counts or DNF") {
  MoveCountAttempt a;
  a.moves = 23;
  CHECK(formatAttempt(a) == "23");
  a.dnf = true;
  CHECK(formatAttempt(a) == "DNF(23)");
  a.moves = 0;
  CHECK(formatAttempt(a) == "DNF");
}

TEST_CASE("Move Count offers a few existing scramble types") {
  CHECK(moveCountScrambleTypes() ==
        std::vector<std::string>{"333", "eoline", "eocross", "f2l"});
  for (const auto &id : moveCountScrambleTypes()) {
    CHECK(findScrambleType(id) != nullptr);
  }
  CHECK(isMoveCountScrambleType("eocross"));
  CHECK_FALSE(isMoveCountScrambleType("pll"));
  CHECK_FALSE(isMoveCountScrambleType("set:oll"));
}

TEST_CASE("move count sessions round-trip through JSON") {
  MoveCountSession s;
  s.name = "EO Cross";
  s.scrambleType = "eocross";
  s.attempts.push_back({9, false, "R U F", "nice", 1700000000});
  s.attempts.push_back({0, true, "D2 L", "", 1700000100});
  MoveCountSession other;
  other.name = "Old";
  other.scrambleType = "pll"; // not offered: read back as 333
  std::size_t current = 0;
  auto json = moveCountsToJson({s, other}, 1);
  CHECK(json.value("format").toString() == "v-cube.movecount");
  auto back = moveCountsFromJson(json, &current);
  CHECK(current == 1);
  REQUIRE(back.size() == 2);
  CHECK(back[0].name == "EO Cross");
  CHECK(back[0].scrambleType == "eocross");
  REQUIRE(back[0].attempts.size() == 2);
  CHECK(back[0].attempts[0].moves == 9);
  CHECK_FALSE(back[0].attempts[0].dnf);
  CHECK(back[0].attempts[0].scramble == "R U F");
  CHECK(back[0].attempts[0].comment == "nice");
  CHECK(back[0].attempts[0].timestamp == 1700000000);
  CHECK(back[0].attempts[1].dnf);
  CHECK(back[1].scrambleType == "333");
}

TEST_CASE("the move count store starts with EO, EO Cross and ZZ F2L") {
  QTemporaryDir dir;
  QString path = dir.filePath("movecounts.json");
  MoveCountStore store(path);
  REQUIRE(store.load());
  REQUIRE(store.sessions().size() == 3);
  CHECK(store.sessions()[0].name == "EO");
  CHECK(store.sessions()[0].scrambleType == "333");
  CHECK(store.sessions()[1].name == "EO Cross");
  CHECK(store.sessions()[1].scrambleType == "333");
  CHECK(store.sessions()[2].name == "ZZ F2L");
  CHECK(store.sessions()[2].scrambleType == "eocross");

  store.setCurrentIndex(2);
  store.current().attempts.push_back({14, false, "F2 U", "", 1});
  store.addSession("EOLine").scrambleType = "333";
  CHECK(store.currentIndex() == 3);
  REQUIRE(store.save());

  MoveCountStore again(path);
  REQUIRE(again.load());
  REQUIRE(again.sessions().size() == 4);
  CHECK(again.currentIndex() == 3);
  CHECK(again.sessions()[2].attempts.size() == 1);
  CHECK(again.sessions()[2].attempts[0].moves == 14);

  for (int i = 0; i < 4; ++i) {
    again.removeSession(0);
  }
  REQUIRE(again.sessions().size() == 1); // never empty
  CHECK(again.sessions()[0].attempts.empty());
  CHECK(again.currentIndex() == 0);

  QFile bad(path);
  REQUIRE(bad.open(QIODevice::WriteOnly));
  bad.write("{not json");
  bad.close();
  QString error;
  CHECK_FALSE(MoveCountStore(path).load(&error));
  CHECK_FALSE(error.isEmpty());
}
