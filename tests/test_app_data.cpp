#include "io/app_data.hpp"
#include "io/backup.hpp"
#include "io/data_file.hpp"
#include "test_files.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QDate>
#include <QDir>
#include <QTemporaryDir>

using namespace vcube;
using namespace vcube::testing;

TEST_CASE("the app data loads the practice files") {
  // The practice modes' files are read with the others, without a page:
  // an old TSLE file's OCLL algs are carried over and saved, and a file
  // that can't be read is reported with the rest.
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  writeText(dir.filePath("tsle.json"),
            R"({"format": "v-cube.tsle", "version": 1,
    "ocll": {"antisune": "L' U' L U' L' U2 L"}})");
  writeText(dir.filePath("f2l.json"), "not json");
  AppData data(dir.path());
  const AppData::LoadReport report = data.load();
  CHECK(data.tsle.data.notes.at("ocll/antisune") == "L' U' L U' L' U2 L");
  CHECK(data.eoCross.data.cards.empty());
  REQUIRE(report.warnings.size() == 1);
  CHECK(report.warnings.front().contains("f2l.json"));
  CHECK(report.backupError.isEmpty());
  finishBackgroundWrites(data.tsle.path());
  PracticeStore again(QDir(dir.path()).filePath("tsle.json"));
  REQUIRE(again.load());
  CHECK(again.data.notes == data.tsle.data.notes);
}

TEST_CASE("the app data backs up the files before it reads them") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  writeText(dir.filePath("sessions.json"), "not json");
  writeText(dir.filePath("untimed.json.aBcDeF"), "partial");
  AppData data(dir.path());
  const AppData::LoadReport report = data.load();
  CHECK(report.backupError.isEmpty());
  // Today's copy has the file as it was, before it was set aside, and not
  // the save temporary, which was moved out of the way first.
  QDir today(QDir(data.backupsDir())
                 .filePath(QDate::currentDate().toString(Qt::ISODate)));
  CHECK(readText(today.filePath("sessions.json")) == "not json");
  CHECK_FALSE(QFile::exists(today.filePath("untimed.json.aBcDeF")));
  CHECK_FALSE(QFile::exists(dir.filePath("untimed.json.aBcDeF")));
  REQUIRE(report.warnings.size() == 1);
  CHECK(report.warnings.front().contains("timer sessions"));
  CHECK(report.warnings.front().contains(
      QDir::toNativeSeparators(data.sessions.setAsidePath())));
}
