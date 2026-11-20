#include "io/backup.hpp"
#include "test_files.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

using namespace vcube;
using namespace vcube::testing;

TEST_CASE("a daily backup copies the data files once a day") {
  QTemporaryDir tmp;
  QDir d(tmp.path());
  writeText(d.filePath("sessions.json"), "s1");
  writeText(d.filePath("algsets/pll.json"), "pll");
  writeText(d.filePath("algsets/progress/pll.json"), "progress");
  writeText(d.filePath("algsets/deleted/old.20260101-000000.json"), "old");

  QDate day(2026, 11, 10);
  REQUIRE(makeDailyBackup(d.path(), {}, day, 3));
  QDir b(d.filePath("backups/2026-11-10"));
  CHECK(readText(b.filePath("sessions.json")) == "s1");
  CHECK(readText(b.filePath("algsets/progress/pll.json")) == "progress");
  CHECK_FALSE(QFile::exists(b.filePath("algsets/deleted")));

  // The same day again leaves the first copy; the backups are not copied.
  writeText(d.filePath("sessions.json"), "s2");
  REQUIRE(makeDailyBackup(d.path(), {}, day, 3));
  CHECK(readText(b.filePath("sessions.json")) == "s1");
  REQUIRE(makeDailyBackup(d.path(), {}, day.addDays(1), 3));
  CHECK(readText(d.filePath("backups/2026-11-11/sessions.json")) == "s2");
  CHECK_FALSE(QFile::exists(d.filePath("backups/2026-11-11/backups")));

  // Only the newest days are kept.
  REQUIRE(makeDailyBackup(d.path(), {}, day.addDays(2), 2));
  CHECK_FALSE(QFile::exists(d.filePath("backups/2026-11-10")));
  CHECK(QFile::exists(d.filePath("backups/2026-11-11")));
  CHECK(QFile::exists(d.filePath("backups/2026-11-12")));
}

TEST_CASE("a data folder with nothing in it yet is backed up as empty") {
  QTemporaryDir tmp;
  QDir d(tmp.path());
  writeText(d.filePath("v-cube.lock"), "pid"); // never copied
  QString error;
  REQUIRE(makeDailyBackup(d.path(), {}, QDate(2026, 11, 10), 3, &error));
  CHECK(error.isEmpty());
  CHECK(QDir(d.filePath("backups/2026-11-10")).exists());
  CHECK(QDir(d.filePath("backups/2026-11-10")).isEmpty());
}

TEST_CASE("a daily backup copies the settings file too") {
  QTemporaryDir tmp;
  QDir d(tmp.path());
  QString data = d.filePath("data");
  writeText(QDir(data).filePath("sessions.json"), "s1");
  QString ini = d.filePath("config/v-cube.ini");
  writeText(ini, "[keys]\n");
  REQUIRE(makeDailyBackup(data, ini, QDate(2026, 11, 10), 3));
  QDir b(QDir(data).filePath("backups/2026-11-10"));
  CHECK(readText(b.filePath("settings/v-cube.ini")) == "[keys]\n");
  CHECK(readText(b.filePath("sessions.json")) == "s1");
  // The original is left as it was.
  CHECK(readText(ini) == "[keys]\n");
}

TEST_CASE("a settings file inside the data folder is backed up once") {
  QTemporaryDir tmp;
  QDir d(tmp.path());
  writeText(d.filePath("sessions.json"), "s1");
  QString ini = d.filePath("settings/v-cube/v-cube.ini");
  writeText(ini, "ini");
  REQUIRE(makeDailyBackup(d.path(), ini, QDate(2026, 11, 10), 3));
  QDir b(d.filePath("backups/2026-11-10"));
  CHECK(readText(b.filePath("settings/v-cube.ini")) == "ini");
  CHECK_FALSE(QFile::exists(b.filePath("settings/v-cube/v-cube.ini")));
  CHECK(readText(b.filePath("sessions.json")) == "s1");
}

TEST_CASE("save temporaries are told from the user's own files") {
  CHECK(isSaveTemporary("sessions.json.aBcDeF"));
  CHECK(isSaveTemporary("oll.json.Xy3kQp"));
  CHECK(isSaveTemporary("v-cube.ini.qWeRtY"));
  CHECK_FALSE(isSaveTemporary("sessions.json"));
  CHECK_FALSE(isSaveTemporary("oll.json.backup"));
  CHECK_FALSE(isSaveTemporary("oll.json.Backup"));
  CHECK_FALSE(isSaveTemporary("oll.json.BACKUP"));
  CHECK_FALSE(isSaveTemporary("oll.json.old123"));
  CHECK_FALSE(isSaveTemporary("oll.txt.aBcDeF"));
  CHECK_FALSE(isSaveTemporary("oll.json.unreadable-20261110-093005"));
}

TEST_CASE("save temporaries are moved aside and left out of backups") {
  QTemporaryDir tmp;
  QDir d(tmp.path());
  writeText(d.filePath("sessions.json"), "s1");
  writeText(d.filePath("sessions.json.aBcDeF"), "partial");
  writeText(d.filePath("algsets/oll.json"), "oll");
  writeText(d.filePath("algsets/oll.json.Xy3kQp"), "partial oll");
  writeText(d.filePath("algsets/oll.json.backup"), "mine");

  // Not copied into a backup.
  REQUIRE(makeDailyBackup(d.path(), {}, QDate(2026, 11, 10), 3));
  QDir b(d.filePath("backups/2026-11-10"));
  CHECK(QFile::exists(b.filePath("sessions.json")));
  CHECK_FALSE(QFile::exists(b.filePath("sessions.json.aBcDeF")));
  CHECK_FALSE(QFile::exists(b.filePath("algsets/oll.json.Xy3kQp")));
  CHECK(readText(b.filePath("algsets/oll.json.backup")) == "mine");

  // Moved aside, kept whole, and the data files left alone.
  QDateTime when(QDate(2026, 11, 10), QTime(9, 30, 5));
  CHECK(moveSaveTemporaries(d.path(), when) == 2);
  CHECK_FALSE(QFile::exists(d.filePath("sessions.json.aBcDeF")));
  CHECK_FALSE(QFile::exists(d.filePath("algsets/oll.json.Xy3kQp")));
  CHECK(readText(d.filePath("sessions.json")) == "s1");
  CHECK(readText(d.filePath("algsets/oll.json")) == "oll");
  CHECK(readText(d.filePath("algsets/oll.json.backup")) == "mine");
  QDir unsaved(d.filePath("deleted/unsaved"));
  CHECK(readText(unsaved.filePath("sessions.json.aBcDeF.20261110-093005")) ==
        "partial");
  CHECK(readText(unsaved.filePath("algsets-oll.json.Xy3kQp.20261110-093005")) ==
        "partial oll");
  // Nothing is left to move.
  CHECK(moveSaveTemporaries(d.path(), when) == 0);
}

TEST_CASE("a deleted file is moved aside with the date") {
  QTemporaryDir tmp;
  QDir d(tmp.path());
  writeText(d.filePath("zz.json"), "zz");
  QDateTime when(QDate(2026, 11, 10), QTime(9, 30, 5));
  QString moved =
      moveToDeleted(d.filePath("zz.json"), d.filePath("deleted"), when);
  CHECK(moved == d.filePath("deleted/zz.20261110-093005.json"));
  CHECK(readText(moved) == "zz");
  CHECK_FALSE(QFile::exists(d.filePath("zz.json")));
  // A second one at the same second doesn't overwrite it.
  writeText(d.filePath("zz.json"), "again");
  QString second =
      moveToDeleted(d.filePath("zz.json"), d.filePath("deleted"), when);
  CHECK(second != moved);
  CHECK(readText(second) == "again");
  CHECK(moveToDeleted(d.filePath("missing.json"), d.filePath("deleted"), when)
            .isEmpty());
}

TEST_CASE("export copies the data and the settings to a new folder") {
  QTemporaryDir tmp;
  QDir d(tmp.path());
  writeText(d.filePath("data/sessions.json"), "s");
  writeText(d.filePath("data/backups/2026-11-10/sessions.json"), "old");
  writeText(d.filePath("config/v-cube.ini"), "ini");
  QString target = d.filePath("export");
  REQUIRE(exportAllData(d.filePath("data"), d.filePath("config/v-cube.ini"),
                        target));
  CHECK(readText(target + "/data/sessions.json") == "s");
  CHECK(readText(target + "/settings/v-cube.ini") == "ini");
  CHECK_FALSE(QFile::exists(target + "/data/backups"));
  QString error;
  CHECK_FALSE(exportAllData(d.filePath("data"), {}, target, &error));
  CHECK_FALSE(error.isEmpty());

  // Not into the data folder itself: the copy would find its own files.
  error.clear();
  CHECK_FALSE(
      exportAllData(d.filePath("data"), {}, d.filePath("data/out"), &error));
  CHECK_FALSE(error.isEmpty());
  CHECK_FALSE(QFile::exists(d.filePath("data/out")));
}

TEST_CASE("an export reaching the data folder through a link is refused") {
  QTemporaryDir tmp;
  QDir d(tmp.path());
  writeText(d.filePath("data/sessions.json"), "s");
  if (!QFile::link(d.filePath("data"), d.filePath("link"))) {
    SKIP("links can't be made here");
  }
  QString error;
  CHECK_FALSE(
      exportAllData(d.filePath("data"), {}, d.filePath("link/out"), &error));
  CHECK(error.contains("inside the data folder"));
  CHECK_FALSE(QFile::exists(d.filePath("data/out")));
  // Deeper, through a folder not made yet.
  CHECK_FALSE(exportAllData(d.filePath("data"), {}, d.filePath("link/a/b/out"),
                            &error));
  CHECK_FALSE(QFile::exists(d.filePath("data/a")));
  // The data folder reached through the link is refused too.
  CHECK_FALSE(
      exportAllData(d.filePath("link"), {}, d.filePath("data/out"), &error));
  CHECK_FALSE(QFile::exists(d.filePath("data/out")));
  CHECK(readText(d.filePath("data/sessions.json")) == "s");

  // A link to elsewhere is fine.
  QDir().mkpath(d.filePath("elsewhere"));
  REQUIRE(QFile::link(d.filePath("elsewhere"), d.filePath("away")));
  REQUIRE(
      exportAllData(d.filePath("data"), {}, d.filePath("away/out"), &error));
  CHECK(readText(d.filePath("elsewhere/out/data/sessions.json")) == "s");
}
