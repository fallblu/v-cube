#pragma once

#include <QJsonObject>
#include <QString>

#include <functional>
#include <span>
#include <utility>

class QObject;

namespace vcube {

// A top-level value a data file holds: its key, whether it is a list (else
// an object), and whether a file without it can still be read. A file in
// which one is missing (when it must be there) or of the wrong kind is
// unreadable: read as if it were empty, it would be saved over as empty.
struct FileField {
  const char *key;
  bool list = false;
  bool required = true;
};

// What a JSON data file is: the "format" it is written with and the newest
// "version" this build reads. Files written before a format had a "format"
// key are accepted without one when `formatOptional` is set. A file without
// "version" is version 1.
struct FileFormat {
  const char *format;
  int version = 1;
  bool formatOptional = false;
  const char *what = "data file"; // for messages, e.g. "sessions file"
  std::span<const FileField> fields = {};
};

enum class FileStatus {
  Missing,    // there is no file
  Read,       // read, and of this format and a version this build reads
  Unreadable, // can't be parsed, or isn't of this format
  Newer,      // of this format, but written by a newer version of v-cube
  CannotOpen, // there, but can't be opened (permissions, a lock, I/O)
};

// Checks a parsed file's format and version: Read, Unreadable or Newer,
// with the reason in `error` for the last two.
FileStatus checkFileFormat(const QJsonObject &o, const FileFormat &format,
                           QString *error = nullptr);

// Reads and checks a JSON data file, without changing it.
struct FileRead {
  FileStatus status = FileStatus::Missing;
  QJsonObject object; // when Read
  QString error;      // why it isn't Read (empty when Missing)
};
FileRead readDataFile(const QString &path, const FileFormat &format);

// One data file that is loaded whole and saved whole. A file that can't be
// read is set aside (see setAside()) so that saving afresh doesn't destroy
// it; one written by a newer version of v-cube, or one that can't be
// opened (which may be fine once it can), is left as it is. Either way,
// when the file is still there, save() refuses to write over it.
class DataFile {
public:
  DataFile(QString path, FileFormat format);

  // Reads the file. On Read the contents are in `out`; otherwise, but for
  // Missing, the reason is in `error` and the caller starts afresh.
  FileStatus load(QJsonObject &out, QString *error = nullptr);
  bool save(const QJsonObject &o, QString *error = nullptr) const;

  const QString &path() const { return path_; }
  // Where load() moved an unreadable file, if it did.
  const QString &setAsidePath() const { return setAside_; }
  // Whether save() refuses to write, because the file load() couldn't use
  // is still there.
  bool keptOff() const { return !keptOffReason_.isEmpty(); }

private:
  QString path_;
  FileFormat format_;
  QString setAside_;
  QString keptOffReason_;
};

// --- background writes ------------------------------------------------------

// Runs `write` (which serializes and writes the file at `path`) on a
// background thread, so that saving never holds up the GUI. Writes run one
// at a time in the order asked for, and a write still waiting is replaced
// by a later one for the same path, so a burst of saves writes the file
// once, with the latest data. `write` must work on its own copy of the
// data. A write that throws counts as failed. When it fails, `onError`
// runs with the reason on the GUI thread, if `context` (which must be given
// for it) still exists.
void writeInBackground(const QString &path,
                       std::function<bool(QString *error)> write,
                       QObject *context = nullptr,
                       std::function<void(const QString &)> onError = {});

// Waits until the background writes to `path` (or to every path, when it
// is empty) are done. Reading or writing a file directly waits for this
// first, so that a background write never lands on top of a newer file.
void finishBackgroundWrites(const QString &path = {});

// --- write reports ----------------------------------------------------------

// Receives the result of every write to a data file, background or direct
// (DataFile::save() reports itself; other direct writes call
// reportWrite()): `error` is empty when the write succeeded. It runs on the
// GUI thread, e.g. to keep a banner up while a file can't be saved and take
// it down once it can. The writer and the reporter are one per process,
// which suits an app with one main window: a second reporter replaces the
// first.
using WriteReporter =
    std::function<void(const QString &path, const QString &error)>;
void setWriteReporter(WriteReporter reporter);
// Reports a write done directly; from any thread.
void reportWrite(const QString &path, bool ok, const QString &error = {});

// One value saved as a JSON data file: `ToJson` writes the whole file from
// it and `FromJson` reads it back.
template <typename Data, const FileFormat &Format, auto ToJson, auto FromJson>
class JsonStore {
public:
  explicit JsonStore(QString path) : file_(std::move(path), Format) {}

  // A missing file gives an empty value. A file that can't be read is set
  // aside (see DataFile) and also gives an empty one.
  bool load(QString *error = nullptr) {
    data = {};
    QJsonObject o;
    FileStatus status = file_.load(o, error);
    if (status != FileStatus::Read) {
      return status == FileStatus::Missing;
    }
    data = FromJson(o);
    return true;
  }
  bool save(QString *error = nullptr) const {
    return file_.save(ToJson(data), error);
  }
  const QString &setAsidePath() const { return file_.setAsidePath(); }
  QString path() const { return file_.path(); }

  Data data;

private:
  DataFile file_;
};

} // namespace vcube
