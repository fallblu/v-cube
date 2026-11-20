#include "io/algset_json.hpp"
#include "io/data_file.hpp"
#include "io/json_values.hpp"
#include "io/library.hpp"
#include "io/move_counts.hpp"
#include "io/practice_store.hpp"
#include "io/sessions.hpp"
#include "test_files.hpp"

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <atomic>
#include <functional>
#include <new>
#include <string>
#include <utility>
#include <vector>

using namespace vcube;
using namespace vcube::testing;

namespace {

// A newer file is refused, left as it is and never saved over.
template <typename Store>
void checkNewerKept(const QString &path, const QByteArray &text, Store &store) {
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
  CHECK(checkFileFormat({{"format", "v-cube.test"}, {"version", 2}}, strict) ==
        FileStatus::Read);
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

TEST_CASE("a version beyond the range of int is newer, not unreadable") {
  FileFormat strict{"v-cube.test", 2, false, "test file"};
  QString error;
  CHECK(checkFileFormat({{"format", "v-cube.test"}, {"version", 1e10}}, strict,
                        &error) == FileStatus::Newer);
  CHECK(error.contains("10000000000"));
  CHECK(checkFileFormat({{"format", "v-cube.test"}, {"version", 1e300}},
                        strict) == FileStatus::Newer);
  CHECK(checkFileFormat({{"format", "v-cube.test"}, {"version", -1e10}},
                        strict) == FileStatus::Unreadable);
  CHECK(checkFileFormat({{"format", "v-cube.test"}, {"version", 1e10 + 0.5}},
                        strict) == FileStatus::Unreadable);

  // So such a file is left as it is.
  QTemporaryDir tmp;
  QString path = tmp.filePath("sessions.json");
  QByteArray text = R"({"format": "v-cube.sessions", "version": 1e10,
                        "sessions": []})";
  SessionStore store(path);
  checkNewerKept(path, text, store);
}

TEST_CASE("a file without its contents is unreadable, not empty") {
  static constexpr FileField fields[] = {{"sessions", true},
                                         {"extra", false, false}};
  FileFormat f{"v-cube.test", 1, false, "test file", fields};
  QString error;
  CHECK(checkFileFormat({{"format", "v-cube.test"}, {"sessions", QJsonArray{}}},
                        f) == FileStatus::Read);
  CHECK(checkFileFormat({{"format", "v-cube.test"}}, f, &error) ==
        FileStatus::Unreadable);
  CHECK(error.contains("sessions"));
  CHECK(
      checkFileFormat({{"format", "v-cube.test"}, {"sessions", QJsonObject{}}},
                      f) == FileStatus::Unreadable);
  // One that may be left out must still be of its kind when there.
  CHECK(
      checkFileFormat(
          {{"format", "v-cube.test"}, {"sessions", QJsonArray{}}, {"extra", 3}},
          f) == FileStatus::Unreadable);
  // A newer file is still a newer file, whatever it holds.
  CHECK(checkFileFormat({{"format", "v-cube.test"}, {"version", 2}}, f) ==
        FileStatus::Newer);

  // So a sessions file without its sessions is set aside, where it used
  // to be read as one empty session and saved over.
  QTemporaryDir tmp;
  QString path = tmp.filePath("sessions.json");
  const QByteArray text = R"({"format": "v-cube.sessions", "version": 1})";
  writeText(path, text);
  SessionStore store(path);
  CHECK_FALSE(store.load(&error));
  REQUIRE_FALSE(store.setAsidePath().isEmpty());
  CHECK(readText(store.setAsidePath()) == text);
}

TEST_CASE("numbers in a file are kept within what can be worked with") {
  CHECK(jsonInt64(QJsonValue(1500)) == 1500);
  CHECK(jsonInt64(QJsonValue(-5)) == 0);
  CHECK(jsonInt64(QJsonValue("12")) == 0);
  CHECK(jsonInt64(QJsonValue()) == 0);
  CHECK(jsonInt64(QJsonValue(1e300)) == kMaxFileNumber);

  QTemporaryDir tmp;
  QString path = tmp.filePath("sessions.json");
  writeText(path, R"({"format": "v-cube.sessions", "version": 1, "sessions":
    [{"name": "s", "solves": [[1e300, 0, "", "", -7, "", [1e300, -1]]]}]})");
  SessionStore store(path);
  REQUIRE(store.load());
  REQUIRE(store.current().solves.size() == 1);
  const Solve &solve = store.current().solves.front();
  CHECK(solve.timeMs == kMaxFileNumber);
  CHECK(solve.timestamp == 0);
  CHECK(solve.moveTimes == std::vector<std::int64_t>{kMaxFileNumber, 0});

  // A card's numbers too.
  SetProgress p = progressFromJson(QJsonDocument::fromJson(R"({"cases": {"a":
    {"card": {"step": -3, "ease": 1e9, "interval": -2, "reps": -1,
              "lapses": -1, "due": 1e300}}}})")
                                       .object());
  REQUIRE(p.contains("a"));
  CHECK(p.at("a").card.step == 0);
  CHECK(p.at("a").card.ease == 10.0);
  CHECK(p.at("a").card.intervalDays == 0.0);
  CHECK(p.at("a").card.reps == 0);
  CHECK(p.at("a").card.lapses == 0);
  CHECK(p.at("a").card.due == kMaxFileNumber);
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
    PracticeStore s(tmp.filePath("eocross.json"));
    checkNewerKept(tmp.filePath("eocross.json"),
                   R"({"format": "v-cube.practice", "version": 2})", s);
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
  CHECK(error.contains("an Untimed file"));
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
  auto o =
      QJsonDocument::fromJson(readText(tmp.filePath("sessions.json"))).object();
  CHECK(o.value("format").toString() == "v-cube.sessions");
  CHECK(o.value("version").toInt() == 1);
  CHECK(store.load());

  CHECK(progressToJson({}).value("format").toString() == "v-cube.progress");
}

TEST_CASE("progress from a newer version is kept and not saved over") {
  QTemporaryDir tmp;
  loadedLibrary(tmp.path());
  QString path = tmp.filePath("progress/pll.json");
  QByteArray text = R"({"format": "v-cube.progress", "version": 5})";
  writeText(path, text);
  AlgLibrary lib = loadedLibrary(tmp.path());
  // A library with sets but no installed-builtins.json also reports that.
  CHECK(errorsContaining(lib, "newer version") == 1);
  lib.progress("pll")["T"].record({100, 1500, false});
  QString error;
  CHECK_FALSE(lib.saveProgress("pll", &error));
  CHECK_FALSE(error.isEmpty());
  CHECK(readText(path) == text);
}

TEST_CASE("a progress file that can't be opened isn't saved over") {
  QTemporaryDir tmp;
  loadedLibrary(tmp.path());
  // It is left where it is (it may be fine once it can be opened), and
  // saving is refused.
  QString path = tmp.filePath("progress/pll.json");
  writeText(path, R"({"cases": {}})");
  QFile::setPermissions(path, QFileDevice::Permissions{});
  QFile probe(path);
  if (probe.open(QIODevice::ReadOnly)) {
    SKIP("permissions don't stop this user reading the file");
  }
  AlgLibrary lib = loadedLibrary(tmp.path());
  REQUIRE(lib.errors().size() == 1);
  CHECK(lib.errors().front().contains("pll.json"));
  lib.progress("pll")["T"].record({100, 1500, false});
  CHECK_FALSE(lib.saveProgress("pll"));
  CHECK(filesStarting(tmp.filePath("progress"), "pll.json.unreadable-")
            .isEmpty());
  QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  CHECK(readText(path) == R"({"cases": {}})");
}

TEST_CASE("alg sets from a newer version are reported and left alone") {
  QTemporaryDir tmp;
  QString path = tmp.filePath("future.json");
  QByteArray text =
      R"({"format": "v-cube.algset", "version": 2, "id": "future"})";
  writeText(path, text);
  AlgLibrary lib = loadedLibrary(tmp.path());
  // A library with sets but no installed-builtins.json also reports that.
  CHECK(errorsContaining(lib, "newer version") == 1);
  CHECK(lib.idTaken("future"));
  CHECK(readText(path) == text);
}

TEST_CASE("versions that aren't version numbers are refused") {
  FileFormat format{"v-cube.test", 1, false, "test file"};
  QString error;
  for (QJsonValue v : {QJsonValue(0), QJsonValue(-1), QJsonValue(true),
                       QJsonValue(QJsonValue::Null), QJsonValue(2.5)}) {
    CHECK(checkFileFormat({{"format", "v-cube.test"}, {"version", v}}, format,
                          &error) == FileStatus::Unreadable);
    CHECK(error.contains("version number"));
  }
  CHECK(checkFileFormat({{"format", "v-cube.test"}, {"version", 1}}, format) ==
        FileStatus::Read);
  CHECK(checkFileFormat({{"format", "v-cube.test"}, {"version", 1000000}},
                        format) == FileStatus::Newer);
  CHECK(checkFileFormat({{"format", 1}, {"version", 1}}, format, &error) ==
        FileStatus::Unreadable);
  CHECK(error.contains("not a v-cube test file"));

  // Alg sets: newer ones are told from ones that aren't sets at all.
  QTemporaryDir tmp;
  QString path = tmp.filePath("set.json");
  FileStatus status = FileStatus::Read;
  writeText(path, R"({"format": "v-cube.algset", "version": 7, "id": "s"})");
  CHECK_FALSE(readAlgSetFile(path, &error, &status));
  CHECK(status == FileStatus::Newer);
  CHECK(error.contains("version 7"));
  writeText(path, R"({"format": "v-cube.sessions", "version": 1, "id": "s"})");
  CHECK_FALSE(readAlgSetFile(path, &error, &status));
  CHECK(status == FileStatus::Unreadable);
  CHECK(error.contains("sessions file"));
  writeText(path, R"({"version": 1, "id": "s"})"); // sets always have one
  CHECK_FALSE(readAlgSetFile(path, &error, &status));
  CHECK(status == FileStatus::Unreadable);
  // Progress files without a format are from before it was written.
  writeText(path, R"({"version": 1, "cases": {}})");
  CHECK(readProgressFile(path).status == FileStatus::Read);
  writeText(path, R"({"version": 0, "cases": {}})");
  CHECK(readProgressFile(path).status == FileStatus::Unreadable);

  // A store's file with a version of 0 is set aside, not kept as newer.
  QString sessions = tmp.filePath("sessions.json");
  writeText(sessions, R"({"version": 0, "sessions": []})");
  SessionStore store(sessions);
  CHECK_FALSE(store.load(&error));
  CHECK_FALSE(store.setAsidePath().isEmpty());
  CHECK(store.save(&error));
}

TEST_CASE("a cut-short alg set file is set aside") {
  QTemporaryDir tmp;
  QString path = tmp.filePath("mine.json");
  writeText(path, R"({"format": "v-cube.algset", "version": 1, "id": "mi)");
  AlgLibrary lib = loadedLibrary(tmp.path());
  // A library with sets but no installed-builtins.json also reports that.
  CHECK(errorsContaining(lib, "kept as") == 1);
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
  AlgLibrary lib = loadedLibrary(tmp.path());
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
  CHECK(
      QFile::exists(tmp.filePath(QString::fromStdString(imported) + ".json")));
}

TEST_CASE("a failed write says why") {
  QTemporaryDir tmp;
  QString error;
  CHECK_FALSE(
      writeFileAtomically(tmp.filePath(QString(300, 'x')), "{}", &error));
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
  writeInBackground(
      path, [copy = store](QString *error) { return copy.save(error); });
  store.current().solves.clear();
  REQUIRE(store.save());
  finishBackgroundWrites();
  REQUIRE(read.load());
  CHECK(read.current().solves.empty());
}

namespace {

// The write reports made while it lives, under an application of its own
// (the reports go through the application's event queue).
struct ReportLog {
  int argc = 1;
  char name[8] = "test";
  char *argv[2] = {name, nullptr};
  QCoreApplication app{argc, argv};
  std::vector<std::pair<QString, QString>> reports;

  ReportLog() {
    setWriteReporter([this](const QString &path, const QString &error) {
      reports.emplace_back(path, error);
    });
  }
  ~ReportLog() {
    finishBackgroundWrites();
    setWriteReporter({});
  }
  void takeQueued() {
    finishBackgroundWrites();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
  }
};

} // namespace

TEST_CASE("a direct save reports itself, as a background one does") {
  ReportLog log;
  QTemporaryDir tmp;
  QString path = tmp.filePath("sessions.json");
  SessionStore store(path);
  REQUIRE(store.save());
  REQUIRE(log.reports.size() == 1);
  CHECK(log.reports[0].first == path);
  CHECK(log.reports[0].second.isEmpty());

  // One that is kept off a file it couldn't read reports why.
  writeText(path, "{\"format\": \"v-cube.sessions\", \"version\": 99}");
  CHECK_FALSE(store.load());
  log.reports.clear();
  QString error;
  CHECK_FALSE(store.save(&error));
  REQUIRE(log.reports.size() == 1);
  CHECK(log.reports[0].second == error);
  CHECK_FALSE(error.isEmpty());

  // A background write is reported once, by the writer.
  SessionStore other(tmp.filePath("other.json"));
  log.reports.clear();
  saveInBackground(other, nullptr, {});
  log.takeQueued();
  CHECK(log.reports.size() == 1);
}

TEST_CASE("a background write that throws is a failed write") {
  ReportLog log;
  QTemporaryDir tmp;
  QString path = tmp.filePath("sessions.json");
  writeText(path, "kept");
  writeInBackground(path, [](QString *) -> bool { throw std::bad_alloc(); });
  writeInBackground(tmp.filePath("b.json"),
                    [](QString *) -> bool { throw 42; });
  log.takeQueued();
  REQUIRE(log.reports.size() == 2);
  CHECK(log.reports[0].first == path);
  CHECK_FALSE(log.reports[0].second.isEmpty());
  CHECK_FALSE(log.reports[1].second.isEmpty());
  // The file is as it was, and the writer still writes.
  CHECK(readText(path) == "kept");
  SessionStore store(path + "2");
  saveInBackground(store, nullptr, {});
  finishBackgroundWrites();
  CHECK(QFile::exists(path + "2"));
}

TEST_CASE("a file that isn't JSON says where it stops being") {
  QTemporaryDir tmp;
  QString path = tmp.filePath("oll.json");
  writeText(path, "{\n  \"format\": \"v-cube.algset\",\n  \"id\": \"oll\",\n}");
  QString error;
  CHECK_FALSE(readAlgSetFile(path, &error));
  INFO(error.toStdString());
  CHECK(error.contains("at line 4, column "));

  QByteArray cut = "{\"session1\": [";
  QString why;
  CHECK_FALSE(sessionsFromCstimerFile(cut, &why));
  CHECK(why.contains("line 1, column"));
}

TEST_CASE("a file too big to read is unreadable, and not read") {
  QTemporaryDir tmp;
  QString path = tmp.filePath("huge.json");
  FileFormat format{"v-cube.test", 1, false, "test file"};
  writeText(path, R"({"format": "v-cube.test"})");
  CHECK(readDataFile(path, format).status == FileStatus::Read);
  {
    // Sparse where the file system allows, so it costs no disk space.
    QFile f(path);
    REQUIRE(f.resize(kMaxDataFileSize + 1));
  }
  FileRead r = readDataFile(path, format);
  CHECK(r.status == FileStatus::Unreadable);
  CHECK(r.error.contains("too big"));
  QFile::remove(path);
}

TEST_CASE("a file of the wrong kind is named as users know it") {
  QTemporaryDir tmp;
  QString path = tmp.filePath("set.json");
  writeText(path, R"({"format": "v-cube.untimed", "version": 1})");
  QString error;
  CHECK_FALSE(readAlgSetFile(path, &error));
  CHECK(error == "it is an Untimed file, not an alg set");
  writeText(path, R"({"format": "v-cube.alglab", "version": 1})");
  CHECK_FALSE(readAlgSetFile(path, &error));
  CHECK(error == "it is an Alg Lab file, not an alg set");
  writeText(path, R"({"format": "v-cube.whatever", "version": 1})");
  CHECK_FALSE(readAlgSetFile(path, &error));
  CHECK(
      error ==
      R"(it is another kind of v-cube file ("v-cube.whatever"), not an alg set)");
  writeText(path, R"({"format": "other", "version": 1})");
  CHECK_FALSE(readAlgSetFile(path, &error));
  CHECK(error == "it is not a v-cube alg set");
}

TEST_CASE("an unreadable set says where it is and what to do") {
  QTemporaryDir tmp;
  writeText(tmp.filePath("installed-builtins.json"),
            R"(["oll", "pll", "2look-oll", "2look-pll"])");
  writeText(tmp.filePath("mine.json"), R"({"format": "v-cube.algset",)");
  AlgLibrary lib = loadedLibrary(tmp.path());
  REQUIRE(lib.errors().size() == 1);
  QString e = lib.errors().front();
  CHECK(e.contains("mine.json in " + QDir::toNativeSeparators(tmp.path())));
  CHECK(e.contains("line 1, column"));
  CHECK(e.contains("rename it back to mine.json"));
}

TEST_CASE("keys an alg set doesn't use are named, and the set still loads") {
  QTemporaryDir tmp;
  writeText(tmp.filePath("installed-builtins.json"),
            R"(["oll", "pll", "2look-oll", "2look-pll"])");
  writeText(tmp.filePath("mine.json"), R"({"format": "v-cube.algset",
    "version": 1, "id": "mine", "targetSecond": 2, "groups": [{"name": "G",
    "colour": 1, "cases": [{"id": "a", "state":
    "UUUUUUUUURRRRRRRRRFFFFFFFFFDDDDDDDDDLLLLLLLLLBBBBBBBBB", "alg": "R"},
    {"id": "b", "state":
    "UUUUUUUUURRRRRRRRRFFFFFFFFFDDDDDDDDDLLLLLLLLLBBBBBBBBB", "alg": "U"}]}]})");
  AlgLibrary lib = loadedLibrary(tmp.path());
  REQUIRE(lib.find("mine"));
  REQUIRE(lib.errors().size() == 1);
  QString e = lib.errors().front();
  CHECK(e.startsWith("mine.json"));
  CHECK(e.contains(R"("targetSecond")"));
  CHECK(e.contains(R"(in a group "colour")"));
  CHECK(e.count(R"(in a case "alg")") == 1);
  // The built-ins have none.
  QTemporaryDir fresh;
  AlgLibrary builtins = loadedLibrary(fresh.path());
  CHECK(builtins.errors().isEmpty());
}

TEST_CASE("a write that fails names the file") {
  QTemporaryDir tmp;
  QString path = tmp.filePath("missing/folder/oll.json");
  QString error;
  CHECK_FALSE(writeFileAtomically(path, "{}", &error));
  CHECK(error.contains(QDir::toNativeSeparators(path)));
}

namespace {

// What the store writes is what building the file afresh gives.
void checkWritesAfresh(SessionStore &store) {
  REQUIRE(store.save());
  CHECK(readText(store.path()) ==
        QJsonDocument(sessionsToJson(store.sessions(), store.currentIndex()))
            .toJson(QJsonDocument::Compact));
}

void checkWritesAfresh(MoveCountStore &store, SolveLog mode) {
  REQUIRE(store.save());
  CHECK(readText(store.path()) ==
        QJsonDocument(
            moveCountsToJson(store.sessions(), store.currentIndex(), mode))
            .toJson(QJsonDocument::Compact));
}

Solve solveOf(int ms) {
  Solve s;
  s.timeMs = ms;
  s.scramble = "R U R'";
  s.moves = "R U";
  s.moveTimes = {100, 200};
  return s;
}

} // namespace

TEST_CASE("sessions are written right however they change between saves") {
  QTemporaryDir tmp;
  SessionStore store(tmp.filePath("sessions.json"));
  for (int i = 0; i < 5; ++i) {
    store.current().solves.push_back(solveOf(1000 + i));
  }
  checkWritesAfresh(store);
  store.current().solves.push_back(solveOf(2000)); // a new solve
  checkWritesAfresh(store);
  store.current().solves[2].penalty = Penalty::Plus2; // one in the middle
  checkWritesAfresh(store);
  store.current().solves[0].comment = "first"; // the first
  checkWritesAfresh(store);
  store.current().solves.erase(store.current().solves.begin() + 3);
  checkWritesAfresh(store);
  store.current().solves.back().moveTimes.push_back(300);
  checkWritesAfresh(store);
  store.addSession("Two").solves.push_back(solveOf(3000));
  checkWritesAfresh(store);
  std::swap(store.sessions()[0], store.sessions()[1]); // moved about
  checkWritesAfresh(store);
  store.removeSession(0);
  checkWritesAfresh(store);
  store.current().solves.clear();
  checkWritesAfresh(store);

  // And it reads back as it was.
  store.current().solves.push_back(solveOf(4000));
  REQUIRE(store.save());
  SessionStore read(store.path());
  REQUIRE(read.load());
  CHECK(read.sessions().size() == store.sessions().size());
  CHECK(read.current().solves == store.current().solves);
}

TEST_CASE("a background save writes the sessions as they were when asked") {
  QTemporaryDir tmp;
  SessionStore store(tmp.filePath("sessions.json"));
  store.current().solves.push_back(solveOf(1000));
  std::function<bool(QString *)> job = store.saveJob();
  // Changed after the save was asked for, before it runs.
  store.current().solves.push_back(solveOf(2000));
  store.current().solves[0].comment = "later";
  store.save(); // fills the cache with the later data
  QString error;
  REQUIRE(job(&error));
  SessionStore read(store.path());
  REQUIRE(read.load());
  REQUIRE(read.current().solves.size() == 1);
  CHECK(read.current().solves[0].comment.empty());
}

TEST_CASE("move counts are written right however they change between saves") {
  for (SolveLog mode : {SolveLog::MoveCount, SolveLog::Untimed}) {
    QTemporaryDir tmp;
    MoveCountStore store(tmp.filePath("counts.json"), mode);
    MoveCountAttempt a;
    a.moves = 7;
    a.scramble = "R U";
    for (int i = 0; i < 4; ++i) {
      a.timestamp = i;
      store.current().attempts.push_back(a);
    }
    checkWritesAfresh(store, mode);
    a.solution = "R' U'";
    store.current().attempts.push_back(a);
    checkWritesAfresh(store, mode);
    store.current().attempts[1].dnf = true;
    checkWritesAfresh(store, mode);
    store.current().attempts[0].counted = false;
    checkWritesAfresh(store, mode);
    store.current().attempts.erase(store.current().attempts.begin());
    checkWritesAfresh(store, mode);
    store.removeSession(store.currentIndex());
    checkWritesAfresh(store, mode);
  }
}

namespace {

void checkWritesAfresh(PracticeStore &store) {
  REQUIRE(store.save());
  CHECK(
      readText(store.path()) ==
      QJsonDocument(practiceToJson(store.data)).toJson(QJsonDocument::Compact));
}

} // namespace

TEST_CASE("practice is written right however it changes between saves") {
  QTemporaryDir tmp;
  PracticeStore store(tmp.filePath("f2l.json"));
  store.data.cards["a"].record({100, 1500, false});
  store.data.cards["b"].record({101, 1600, true});
  store.data.addLearn("whole", {.timestamp = 1, .moves = 30});
  store.data.addTimed("virtual/f2l", {.timestamp = 2, .ms = 9000});
  checkWritesAfresh(store);
  store.data.cards["a"].record({102, 1400, false}); // one card changed
  checkWritesAfresh(store);
  store.data.cards["c"].record({103, 1300, false}); // a new card
  checkWritesAfresh(store);
  store.data.cards.erase("b"); // a card gone
  checkWritesAfresh(store);
  store.data.addLearn("whole", {.timestamp = 3, .moves = 28});
  store.data.addLearn("cross", {.timestamp = 4, .moves = 8});
  checkWritesAfresh(store);
  store.data.addInspection("eocross", {.timestamp = 5, .inspectMs = 9000});
  checkWritesAfresh(store);
  store.data.timed.clear();
  store.data.inspection.clear();
  checkWritesAfresh(store);
  store.data = {};
  checkWritesAfresh(store);
}

TEST_CASE("a background save writes practice as it was when asked") {
  QTemporaryDir tmp;
  PracticeStore store(tmp.filePath("f2l.json"));
  store.data.cards["a"].record({100, 1500, false});
  std::function<bool(QString *)> job = store.saveJob();
  store.data.cards["a"].record({101, 1400, false});
  store.data.cards["b"].record({102, 1300, false});
  store.save(); // fills the cache with the later data
  QString error;
  REQUIRE(job(&error));
  PracticeStore read(store.path());
  REQUIRE(read.load());
  REQUIRE(read.data.cards.size() == 1);
  CHECK(read.data.cards["a"].attempts.size() == 1);
}

TEST_CASE("progress is written right however it changes between saves") {
  QTemporaryDir tmp;
  AlgLibrary lib = loadedLibrary(tmp.path());
  auto check = [&] {
    REQUIRE(lib.saveProgress("pll"));
    CHECK(readText(lib.progressPath("pll")) ==
          QJsonDocument(progressToJson(lib.progress("pll")))
              .toJson(QJsonDocument::Compact));
  };
  lib.progress("pll")["T"].record({100, 1500, false});
  lib.progress("pll")["Y"].record({101, 1600, false});
  check();
  // Asked for before the next changes, written after them.
  std::function<bool(QString *)> job = lib.progressWriter("pll");
  lib.progress("pll")["T"].record({102, 1400, true});
  check();
  lib.progress("pll")["Aa"].record({103, 1700, false});
  check();
  lib.forgetProgress("pll", {"Y"});
  check();
  REQUIRE(job(nullptr));
  SetProgress was = progressFromJson(
      QJsonDocument::fromJson(readText(lib.progressPath("pll"))).object());
  CHECK(was.size() == 2);
  CHECK(was["T"].attempts.size() == 1);
}
