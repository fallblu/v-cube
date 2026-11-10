#include "io/blind_store.hpp"
#include "io/data_file.hpp"
#include "io/inspection_store.hpp"
#include "io/library.hpp"
#include "io/move_counts.hpp"
#include "io/sessions.hpp"

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <atomic>
#include <string>

using namespace vcube;

namespace {

const QString kBuiltins = QStringLiteral(VCUBE_SOURCE_DIR "/resources/algsets");

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

QStringList filesStarting(const QString &dir, const QString &prefix) {
  return QDir(dir).entryList({prefix + "*"}, QDir::Files);
}

// A newer file is refused, left as it is and never saved over.
template <typename Store>
void checkNewerKept(const QString &path, const QByteArray &text,
                    Store &store) {
  writeText(path, text);
  QString error;
  CHECK_FALSE(store.load(&error));
  CHECK(error.contains("newer version"));
  CHECK(store.setAsidePath().isEmpty());
  CHECK_FALSE(store.save(&error));
  CHECK(error.contains("newer version"));
  CHECK(readText(path) == text);
  CHECK(filesStarting(QFileInfo(path).absolutePath(),
                      QFileInfo(path).fileName() + ".unreadable-")
            .isEmpty());
}

} // namespace

TEST_CASE("file formats and versions are checked in one place") {
  FileFormat strict{"v-cube.test", 2, false, "test file"};
  FileFormat optional{"v-cube.test", 1, true, "test file"};
  QString error;
  CHECK(checkFileFormat({{"format", "v-cube.test"}, {"version", 2}},
                        strict) == FileStatus::Read);
  CHECK(checkFileFormat({{"format", "v-cube.test"}}, strict) ==
        FileStatus::Read);
  CHECK(checkFileFormat({{"version", 1}}, strict, &error) ==
        FileStatus::Unreadable);
  CHECK(checkFileFormat({{"version", 1}}, optional) == FileStatus::Read);
  CHECK(checkFileFormat({{"format", "v-cube.other"}}, optional, &error) ==
        FileStatus::Unreadable);
  CHECK(error.contains("other"));
  CHECK(checkFileFormat({{"format", "v-cube.test"}, {"version", 3}}, strict,
                        &error) == FileStatus::Newer);
  CHECK(error.contains("newer"));
  CHECK(checkFileFormat({{"format", "v-cube.test"}, {"version", "2"}},
                        strict) == FileStatus::Unreadable);
  CHECK(checkFileFormat({{"format", "v-cube.test"}, {"version", 1.5}},
                        strict) == FileStatus::Unreadable);
}

TEST_CASE("files from a newer version are kept and not saved over") {
  QTemporaryDir tmp;
  {
    SessionStore s(tmp.filePath("sessions.json"));
    checkNewerKept(tmp.filePath("sessions.json"),
                   R"({"version": 2, "sessions": []})", s);
  }
  {
    MoveCountStore s(tmp.filePath("movecounts.json"));
    checkNewerKept(tmp.filePath("movecounts.json"),
                   R"({"format": "v-cube.movecount", "version": 9})", s);
  }
  {
    MoveCountStore s(tmp.filePath("untimed.json"), SolveLog::Untimed);
    checkNewerKept(tmp.filePath("untimed.json"),
                   R"({"format": "v-cube.untimed", "version": 2})", s);
  }
  {
    InspectionStore s(tmp.filePath("inspection.json"));
    checkNewerKept(tmp.filePath("inspection.json"),
                   R"({"format": "v-cube.inspection", "version": 2})", s);
  }
  {
    BlindStore s(tmp.filePath("blind.json"));
    checkNewerKept(tmp.filePath("blind.json"),
                   R"({"format": "v-cube.blind", "version": 2})", s);
  }
}

TEST_CASE("a file of another mode isn't read as this one") {
  QTemporaryDir tmp;
  MoveCountStore untimed(tmp.filePath("untimed.json"), SolveLog::Untimed);
  untimed.sessions().front().name = "Mine";
  REQUIRE(untimed.save());

  QString path = tmp.filePath("movecounts.json");
  QFile::copy(tmp.filePath("untimed.json"), path);
  MoveCountStore moves(path);
  QString error;
  CHECK_FALSE(moves.load(&error));
  CHECK(error.contains("untimed"));
  CHECK(moves.sessions().size() == defaultMoveCountSessions().size());
  REQUIRE_FALSE(moves.setAsidePath().isEmpty());
  CHECK(readText(moves.setAsidePath()) ==
        readText(tmp.filePath("untimed.json")));

  QFile::copy(tmp.filePath("untimed.json"), tmp.filePath("sessions.json"));
  SessionStore sessions(tmp.filePath("sessions.json"));
  CHECK_FALSE(sessions.load());
}

TEST_CASE("sessions and progress are written with their format") {
  QTemporaryDir tmp;
  SessionStore store(tmp.filePath("sessions.json"));
  REQUIRE(store.save());
  auto o = QJsonDocument::fromJson(readText(tmp.filePath("sessions.json")))
               .object();
  CHECK(o.value("format").toString() == "v-cube.sessions");
  CHECK(o.value("version").toInt() == 1);
  CHECK(store.load());

  CHECK(progressToJson({}).value("format").toString() == "v-cube.progress");
}

TEST_CASE("progress from a newer version is kept and not saved over") {
  QTemporaryDir tmp;
  {
    AlgLibrary lib(tmp.path(), kBuiltins);
    lib.load();
  }
  QString path = tmp.filePath("progress/pll.json");
  QByteArray text = R"({"format": "v-cube.progress", "version": 5})";
  writeText(path, text);
  AlgLibrary lib(tmp.path(), kBuiltins);
  lib.load();
  // A library with sets but no installed-builtins.json also reports that.
  CHECK(std::ranges::count_if(lib.errors(), [](const QString &e) {
          return e.contains("newer version");
        }) == 1);
  lib.progress("pll")["T"].record({100, 1500, false});
  QString error;
  CHECK_FALSE(lib.saveProgress("pll", &error));
  CHECK_FALSE(error.isEmpty());
  CHECK(readText(path) == text);
}

TEST_CASE("a progress file that can't be opened isn't saved over") {
  QTemporaryDir tmp;
  {
    AlgLibrary lib(tmp.path(), kBuiltins);
    lib.load();
  }
  // It is set aside like a file that can't be parsed, and so survives
  // saving.
  QString path = tmp.filePath("progress/pll.json");
  writeText(path, R"({"cases": {}})");
  QFile::setPermissions(path, QFileDevice::Permissions{});
  QFile probe(path);
  if (probe.open(QIODevice::ReadOnly)) {
    SKIP("permissions don't stop this user reading the file");
  }
  AlgLibrary lib(tmp.path(), kBuiltins);
  lib.load();
  REQUIRE(lib.errors().size() == 1);
  CHECK(lib.errors().front().contains("pll.json"));
  lib.progress("pll")["T"].record({100, 1500, false});
  lib.saveProgress("pll");
  QStringList kept =
      filesStarting(tmp.filePath("progress"), "pll.json.unreadable-");
  REQUIRE(kept.size() == 1);
  QString keptPath = tmp.filePath("progress/" + kept.front());
  QFile::setPermissions(keptPath,
                        QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  CHECK(readText(keptPath) == R"({"cases": {}})");
}

TEST_CASE("alg sets from a newer version are reported and left alone") {
  QTemporaryDir tmp;
  QString path = tmp.filePath("future.json");
  QByteArray text =
      R"({"format": "v-cube.algset", "version": 2, "id": "future"})";
  writeText(path, text);
  AlgLibrary lib(tmp.path(), kBuiltins);
  lib.load();
  // A library with sets but no installed-builtins.json also reports that.
  CHECK(std::ranges::count_if(lib.errors(), [](const QString &e) {
          return e.contains("newer version");
        }) == 1);
  CHECK(lib.idTaken("future"));
  CHECK(readText(path) == text);
}

TEST_CASE("a cut-short alg set file is set aside") {
  QTemporaryDir tmp;
  QString path = tmp.filePath("mine.json");
  writeText(path, R"({"format": "v-cube.algset", "version": 1, "id": "mi)");
  AlgLibrary lib(tmp.path(), kBuiltins);
  lib.load();
  // A library with sets but no installed-builtins.json also reports that.
  CHECK(std::ranges::count_if(lib.errors(), [](const QString &e) {
          return e.contains("kept as");
        }) == 1);
  CHECK_FALSE(QFile::exists(path));
  CHECK(filesStarting(tmp.path(), "mine.json.unreadable-").size() == 1);
}

TEST_CASE("alg sets with bad cases or settings are refused") {
  auto parse = [](const char *groups, const char *extra = "") {
    QByteArray text = QByteArray(R"({"format": "v-cube.algset", "id": "x")") +
                      extra + R"(, "groups": [{"name": "G", "cases": [)" +
                      groups + "]}]}";
    QString error;
    auto set = algSetFromJson(QJsonDocument::fromJson(text).object(), &error);
    return set ? QString() : error;
  };
  const char *solved = "UUUUUUUUURRRRRRRRRFFFFFFFFFDDDDDDDDDLLLLLLLLLBBBBBBBBB";
  auto c = [&](const char *id, const char *algs = R"(["R U R'"])") {
    return QString(R"({"id": "%1", "state": "%2", "algs": %3})")
        .arg(id, solved, algs)
        .toStdString();
  };
  CHECK(parse(c("a").c_str()).isEmpty());
  CHECK(parse((c("a") + "," + c("b")).c_str()).isEmpty());
  CHECK(parse(c("").c_str()).contains("no id"));
  CHECK(parse((c("a") + "," + c("a")).c_str()).contains("\"a\""));
  CHECK(parse(c("a", R"(["R", 3])").c_str()).contains("isn't text"));
  CHECK(parse(c("a").c_str(), R"(, "targetSeconds": -1)")
            .contains("targetSeconds"));
  CHECK(parse(c("a").c_str(), R"(, "targetSeconds": "3")")
            .contains("targetSeconds"));
  CHECK(parse(c("a").c_str(), R"(, "targetSeconds": 2.5)").isEmpty());
}

TEST_CASE("a set with a very long name gets a short id and saves") {
  QTemporaryDir tmp;
  AlgLibrary lib(tmp.path(), kBuiltins);
  lib.load();
  AlgSet set = *lib.find("pll");
  set.builtin = false;
  set.name = std::string(300, 'n');
  set.id = lib.uniqueId(set.name);
  CHECK(set.id.size() <= 80);
  std::string id = lib.add(set)->id;
  QString error;
  CHECK(lib.save(*lib.find(id), &error));
  CHECK(error.isEmpty());

  set.id = std::string(300, 'i'); // e.g. from an imported file
  std::string imported = lib.add(set)->id;
  CHECK(imported.size() <= 80);
  CHECK(QFile::exists(tmp.filePath(QString::fromStdString(imported) +
                                   ".json")));
}

TEST_CASE("a failed write says why") {
  QTemporaryDir tmp;
  QString error;
  CHECK_FALSE(writeFileAtomically(tmp.filePath(QString(300, 'x')), "{}",
                                  &error));
  CHECK_FALSE(error.isEmpty());
}

TEST_CASE("background writes run in order and keep the latest data") {
  QTemporaryDir tmp;
  QString path = tmp.filePath("sessions.json");
  SessionStore store(path);
  std::atomic<int> writes = 0;
  for (int i = 0; i < 50; ++i) {
    Solve s;
    s.timeMs = 1000 + i;
    store.current().solves.push_back(s);
    writeInBackground(path, [copy = store, &writes](QString *error) {
      ++writes;
      return copy.save(error);
    });
  }
  // A direct read waits for the writes.
  SessionStore read(path);
  REQUIRE(read.load());
  CHECK(read.current().solves.size() == 50);
  CHECK(writes.load() >= 1);
  CHECK(writes.load() <= 50);

  // And so does a direct write, so an older background one can't land after.
  writeInBackground(path, [copy = store](QString *error) {
    return copy.save(error);
  });
  store.current().solves.clear();
  REQUIRE(store.save());
  finishBackgroundWrites();
  REQUIRE(read.load());
  CHECK(read.current().solves.empty());
}
