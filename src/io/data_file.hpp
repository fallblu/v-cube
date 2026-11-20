#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

#include <functional>
#include <mutex>
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

// A JSON parse error with where it is in `bytes`: "unterminated object at
// line 214, column 3".
QString parseErrorText(const QByteArray &bytes, const QJsonParseError &pe);

// The largest data file read: a bigger one is taken for unreadable rather
// than read whole into memory. Far beyond what v-cube itself writes.
inline constexpr qint64 kMaxDataFileSize = qint64{256} * 1024 * 1024;

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

private:
  QString path_;
  FileFormat format_;
  QString setAside_;
  // Why save() refuses to write: the file load() couldn't use is still
  // there.
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

// Held while the data folder is copied (the daily backup, which runs on the
// background writer) and by every direct write of a data file, so that a
// copy never meets a file half replaced, or a save fails because the copy
// has the file open.
std::unique_lock<std::mutex> lockDataFolder();

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

// What is kept as one JSON data file (see DataFile), loaded and saved
// whole:
//
//   reset()     back to what a missing or unreadable file gives
//   toJson()    the whole file
//   fromJson()  reads a file of this format, after reset()
class FileStore {
public:
  // A missing file gives what reset() leaves. A file that can't be read
  // is set aside (see DataFile) and gives the same; if it can't be moved,
  // nothing is saved over it.
  bool load(QString *error = nullptr) {
    reset();
    QJsonObject o;
    FileStatus status = file_.load(o, error);
    if (status != FileStatus::Read) {
      return status == FileStatus::Missing;
    }
    fromJson(o);
    return true;
  }
  bool save(QString *error = nullptr) const {
    return file_.save(toJson(), error);
  }
  // A save of the file as it is now that can run on another thread: the
  // JSON is made here, and the write and the rest of the work there. Worth
  // it over copying the store when toJson() keeps what it built (see
  // ItemJsonCache and KeyedJsonCache).
  std::function<bool(QString *)> saveJob() const {
    return [file = file_, o = toJson()](QString *error) {
      return file.save(o, error);
    };
  }
  // Where load() moved an unreadable file, if it did.
  const QString &setAsidePath() const { return file_.setAsidePath(); }
  QString path() const { return file_.path(); }

  virtual ~FileStore() = default;

protected:
  FileStore(QString path, FileFormat format) : file_(std::move(path), format) {}
  FileStore(const FileStore &) = default;
  FileStore &operator=(const FileStore &) = default;

  virtual void reset() = 0;
  virtual QJsonObject toJson() const = 0;
  virtual void fromJson(const QJsonObject &o) = 0;

  DataFile file_;
};

// One value saved as a JSON data file: `ToJson` writes the whole file from
// it and `FromJson` reads it back. A missing or unreadable file gives an
// empty value.
template <typename Data, const FileFormat &Format, auto ToJson, auto FromJson>
class JsonStore : public FileStore {
public:
  explicit JsonStore(QString path) : FileStore(std::move(path), Format) {}

  Data data;

protected:
  void reset() override { data = {}; }
  QJsonObject toJson() const override { return ToJson(data); }
  void fromJson(const QJsonObject &o) override { data = FromJson(o); }
};

} // namespace vcube
