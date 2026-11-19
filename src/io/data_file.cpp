#include "io/data_file.hpp"

#include "io/algset_json.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPointer>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace vcube {

FileStatus checkFileFormat(const QJsonObject &o, const FileFormat &format,
                           QString *error) {
  auto fail = [error](FileStatus status, const QString &why) {
    if (error) {
      *error = why;
    }
    return status;
  };
  QJsonValue f = o.value("format");
  if (f.isUndefined() ? !format.formatOptional
                      : f.toString() != format.format) {
    QString found = f.toString();
    return fail(FileStatus::Unreadable,
                found.startsWith("v-cube.")
                    ? QString("it is a %1 file, not a %2")
                          .arg(found.mid(7), format.what)
                    : QString("it is not a v-cube %1").arg(format.what));
  }
  QJsonValue v = o.value("version");
  if (!v.isUndefined()) {
    double version = v.toDouble(-1);
    if (!v.isDouble() || version < 1 ||
        version != static_cast<double>(static_cast<int>(version))) {
      return fail(FileStatus::Unreadable, "its version isn't a version number");
    }
    if (version > format.version) {
      return fail(FileStatus::Newer,
                  QString("it was written by a newer version of v-cube: file "
                          "version %1, and this version reads up to %2")
                      .arg(static_cast<int>(version))
                      .arg(format.version));
    }
  }
  for (const FileField &field : format.fields) {
    QJsonValue value = o.value(QLatin1String(field.key));
    if (value.isUndefined()) {
      if (field.required) {
        return fail(FileStatus::Unreadable,
                    QString("it has no \"%1\"").arg(field.key));
      }
    } else if (field.list ? !value.isArray() : !value.isObject()) {
      return fail(FileStatus::Unreadable,
                  QString("its \"%1\" is not %2")
                      .arg(field.key, field.list ? "a list" : "an object"));
    }
  }
  return FileStatus::Read;
}

FileRead readDataFile(const QString &path, const FileFormat &format) {
  finishBackgroundWrites(path);
  FileRead r;
  QFile f(path);
  if (!f.exists()) {
    return r;
  }
  if (!f.open(QIODevice::ReadOnly)) {
    // Not the file's fault, and maybe passing: never taken for unreadable.
    r.status = FileStatus::CannotOpen;
    QString why = f.errorString();
    r.error = why.isEmpty() || why == "Unknown error"
                  ? QString("it can't be opened")
                  : QString("it can't be opened: %1").arg(why);
    return r;
  }
  r.status = FileStatus::Unreadable;
  QJsonParseError pe;
  QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
  if (pe.error != QJsonParseError::NoError) {
    r.error = pe.errorString();
    return r;
  }
  if (!doc.isObject()) {
    r.error = QString("it is not a v-cube %1").arg(format.what);
    return r;
  }
  r.status = checkFileFormat(doc.object(), format, &r.error);
  if (r.status == FileStatus::Read) {
    r.object = doc.object();
  }
  return r;
}

DataFile::DataFile(QString path, FileFormat format)
    : path_(std::move(path)), format_(format) {}

FileStatus DataFile::load(QJsonObject &out, QString *error) {
  setAside_.clear();
  keptOffReason_.clear();
  FileRead r = readDataFile(path_, format_);
  if (error) {
    *error = r.error;
  }
  if (r.status == FileStatus::Read) {
    out = std::move(r.object);
  } else if (r.status == FileStatus::Newer) {
    keptOffReason_ =
        QString("%1 was written by a newer version of v-cube, so it isn't "
                "saved over")
            .arg(QDir::toNativeSeparators(path_));
  } else if (r.status == FileStatus::CannotOpen) {
    keptOffReason_ = QString("%1 can't be opened, so it isn't saved over; "
                             "restart v-cube once it can be")
                         .arg(QDir::toNativeSeparators(path_));
  } else if (r.status == FileStatus::Unreadable) {
    setAside_ = setAside(path_);
    if (setAside_.isEmpty()) {
      keptOffReason_ = QString("%1 couldn't be read, so it isn't saved over")
                           .arg(QDir::toNativeSeparators(path_));
    }
  }
  return r.status;
}

bool DataFile::save(const QJsonObject &o, QString *error) const {
  if (keptOff()) {
    if (error) {
      *error = keptOffReason_;
    }
    return false;
  }
  QDir().mkpath(QFileInfo(path_).absolutePath());
  return writeFileAtomically(
      path_, QJsonDocument(o).toJson(QJsonDocument::Compact), error);
}

// --- background writes ------------------------------------------------------

namespace {

thread_local bool tOnWriter = false;

std::mutex gReporterMutex;
WriteReporter gReporter;

class Writer {
public:
  static Writer &instance() {
    static Writer w;
    return w;
  }

  ~Writer() {
    {
      std::lock_guard lock(mutex_);
      stop_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) {
      thread_.join(); // after the writes still waiting
    }
  }

  void add(const QString &path, std::function<bool(QString *)> write,
           QObject *context, std::function<void(const QString &)> onError) {
    std::lock_guard lock(mutex_);
    Job job{path, std::move(write), context, std::move(onError)};
    auto waiting = std::find_if(queue_.begin(), queue_.end(),
                                [&](const Job &j) { return j.path == path; });
    if (waiting != queue_.end()) {
      *waiting = std::move(job);
    } else {
      queue_.push_back(std::move(job));
    }
    if (!thread_.joinable()) {
      thread_ = std::thread([this] { run(); });
    }
    wake_.notify_all();
  }

  void wait(const QString &path) {
    if (tOnWriter) {
      return; // a write waiting for itself
    }
    std::unique_lock lock(mutex_);
    done_.wait(lock, [&] { return !busy(path); });
  }

private:
  struct Job {
    QString path;
    std::function<bool(QString *)> write;
    QPointer<QObject> context;
    std::function<void(const QString &)> onError;
  };

  std::mutex mutex_;
  std::condition_variable wake_, done_;
  std::deque<Job> queue_;
  bool running_ = false;
  QString runningPath_;
  bool stop_ = false;
  std::thread thread_;

  bool busy(const QString &path) const {
    if (path.isEmpty()) {
      return running_ || !queue_.empty();
    }
    return (running_ && runningPath_ == path) ||
           std::any_of(queue_.begin(), queue_.end(),
                       [&](const Job &j) { return j.path == path; });
  }

  void run() {
    tOnWriter = true;
    std::unique_lock lock(mutex_);
    for (;;) {
      wake_.wait(lock, [this] { return stop_ || !queue_.empty(); });
      if (queue_.empty()) {
        return;
      }
      Job job = std::move(queue_.front());
      queue_.pop_front();
      running_ = true;
      runningPath_ = job.path;
      lock.unlock();
      QString error;
      bool ok = job.write(&error);
      if (!ok && error.isEmpty()) {
        error = QString("%1 couldn't be written")
                    .arg(QDir::toNativeSeparators(job.path));
      }
      reportWrite(job.path, ok, error);
      if (!ok && job.onError) {
        if (QCoreApplication *app = QCoreApplication::instance()) {
          QMetaObject::invokeMethod(
              app,
              [context = job.context, onError = std::move(job.onError), error] {
                if (context) {
                  onError(error);
                }
              },
              Qt::QueuedConnection);
        }
      }
      job = Job{}; // its copy of the data goes before the next starts
      lock.lock();
      running_ = false;
      runningPath_.clear();
      done_.notify_all();
    }
  }
};

} // namespace

void writeInBackground(const QString &path,
                       std::function<bool(QString *error)> write,
                       QObject *context,
                       std::function<void(const QString &)> onError) {
  Writer::instance().add(path, std::move(write), context, std::move(onError));
}

void finishBackgroundWrites(const QString &path) {
  Writer::instance().wait(path);
}

void setWriteReporter(WriteReporter reporter) {
  std::lock_guard lock(gReporterMutex);
  gReporter = std::move(reporter);
}

void reportWrite(const QString &path, bool ok, const QString &error) {
  QCoreApplication *app = QCoreApplication::instance();
  if (!app) {
    return;
  }
  QString why = ok ? QString() : error;
  if (!ok && why.isEmpty()) {
    why = QString("%1 couldn't be written").arg(QDir::toNativeSeparators(path));
  }
  // On the GUI thread: at once from there, queued from the writer.
  QMetaObject::invokeMethod(
      app,
      [path, why] {
        WriteReporter report;
        {
          std::lock_guard lock(gReporterMutex);
          report = gReporter;
        }
        if (report) {
          report(path, why);
        }
      },
      Qt::AutoConnection);
}

} // namespace vcube
