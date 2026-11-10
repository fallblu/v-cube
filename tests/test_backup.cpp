#include "io/backup.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

using namespace vcube;

namespace {

void write(const QString &path, const QByteArray &data) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  REQUIRE(f.open(QIODevice::WriteOnly));
  f.write(data);
}

QByteArray read(const QString &path) {
  QFile f(path);
  return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

TEST_CASE("a daily backup copies the data files once a day") {
  QTemporaryDir tmp;
  QDir d(tmp.path());
  write(d.filePath("sessions.json"), "s1");
  write(d.filePath("algsets/pll.json"), "pll");
  write(d.filePath("algsets/progress/pll.json"), "progress");
  write(d.filePath("algsets/deleted/old.20260101-000000.json"), "old");

  QDate day(2026, 11, 10);
  REQUIRE(makeDailyBackup(d.path(), day, 3));
  QDir b(d.filePath("backups/2026-11-10"));
  CHECK(read(b.filePath("sessions.json")) == "s1");
  CHECK(read(b.filePath("algsets/progress/pll.json")) == "progress");
  CHECK_FALSE(QFile::exists(b.filePath("algsets/deleted")));

  // The same day again leaves the first copy; the backups are not copied.
  write(d.filePath("sessions.json"), "s2");
  REQUIRE(makeDailyBackup(d.path(), day, 3));
  CHECK(read(b.filePath("sessions.json")) == "s1");
  REQUIRE(makeDailyBackup(d.path(), day.addDays(1), 3));
  CHECK(read(d.filePath("backups/2026-11-11/sessions.json")) == "s2");
  CHECK_FALSE(QFile::exists(d.filePath("backups/2026-11-11/backups")));

  // Only the newest days are kept.
  REQUIRE(makeDailyBackup(d.path(), day.addDays(2), 2));
  CHECK_FALSE(QFile::exists(d.filePath("backups/2026-11-10")));
  CHECK(QFile::exists(d.filePath("backups/2026-11-11")));
  CHECK(QFile::exists(d.filePath("backups/2026-11-12")));
}

TEST_CASE("a deleted file is moved aside with the date") {
  QTemporaryDir tmp;
  QDir d(tmp.path());
  write(d.filePath("zz.json"), "zz");
  QDateTime when(QDate(2026, 11, 10), QTime(9, 30, 5));
  QString moved =
      moveToDeleted(d.filePath("zz.json"), d.filePath("deleted"), when);
  CHECK(moved == d.filePath("deleted/zz.20261110-093005.json"));
  CHECK(read(moved) == "zz");
  CHECK_FALSE(QFile::exists(d.filePath("zz.json")));
  // A second one at the same second doesn't overwrite it.
  write(d.filePath("zz.json"), "again");
  QString second =
      moveToDeleted(d.filePath("zz.json"), d.filePath("deleted"), when);
  CHECK(second != moved);
  CHECK(read(second) == "again");
  CHECK(moveToDeleted(d.filePath("missing.json"), d.filePath("deleted"), when)
            .isEmpty());
}

TEST_CASE("export copies the data and the settings to a new folder") {
  QTemporaryDir tmp;
  QDir d(tmp.path());
  write(d.filePath("data/sessions.json"), "s");
  write(d.filePath("data/backups/2026-11-10/sessions.json"), "old");
  write(d.filePath("config/v-cube.ini"), "ini");
  QString target = d.filePath("export");
  REQUIRE(exportAllData(d.filePath("data"), d.filePath("config/v-cube.ini"),
                        target));
  CHECK(read(target + "/data/sessions.json") == "s");
  CHECK(read(target + "/settings/v-cube.ini") == "ini");
  CHECK_FALSE(QFile::exists(target + "/data/backups"));
  QString error;
  CHECK_FALSE(exportAllData(d.filePath("data"), {}, target, &error));
  CHECK_FALSE(error.isEmpty());
}
