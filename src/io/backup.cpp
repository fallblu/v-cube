#include "io/backup.hpp"

#include "io/data_file.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

namespace vcube {

namespace {

bool fail(QString *error, const QString &why) {
  if (error) {
    *error = why;
  }
  return false;
}

// Whether `path` is `dir` or inside it, both canonical: by case where the
// file system usually ignores it.
bool isInside(const QString &path, const QString &dir) {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
  constexpr Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
  constexpr Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
  QString prefix = dir.endsWith('/') ? dir : dir + "/";
  return path.compare(dir, cs) == 0 || path.startsWith(prefix, cs);
}

// The canonical path `path` has, or will have once made: links and case
// are resolved in the part of it that exists.
QString resolvedPath(const QString &path) {
  QString p = QDir::cleanPath(QDir(path).absolutePath());
  QStringList rest;
  for (QFileInfo info(p); !info.exists(); info = QFileInfo(p)) {
    QString parent = info.absolutePath();
    if (parent == p) {
      return p;
    }
    rest.prepend(info.fileName());
    p = parent;
  }
  QString base = QFileInfo(p).canonicalFilePath();
  if (base.isEmpty()) {
    base = p;
  }
  for (const QString &part : rest) {
    base = QDir(base).filePath(part);
  }
  return QDir::cleanPath(base);
}

// Copies the files under `from` to `to`, keeping the layout and skipping
// the backup and deleted folders at any depth, and anything in `skip` (a
// canonical path), such as the folder being copied to.
bool copyTree(const QString &from, const QString &to, QString *error,
              const QString &skip = {}) {
  QDir source(from);
  QDirIterator it(from, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    QString path = it.next();
    QString rel = source.relativeFilePath(path);
    QStringList parts = rel.split('/');
    parts.removeLast();
    if (parts.contains(kBackupsFolder) || parts.contains(kDeletedFolder) ||
        rel.endsWith(".lock")) {
      continue;
    }
    if (!skip.isEmpty() &&
        isInside(QFileInfo(path).canonicalFilePath(), skip)) {
      continue;
    }
    QString dest = QDir(to).filePath(rel);
    QDir().mkpath(QFileInfo(dest).absolutePath());
    QFile::remove(dest);
    if (!QFile::copy(path, dest)) {
      return fail(error, QString("%1 couldn't be copied to %2")
                             .arg(QDir::toNativeSeparators(path),
                                  QDir::toNativeSeparators(dest)));
    }
  }
  return true;
}

} // namespace

bool makeDailyBackup(const QString &dataDir, const QDate &today, int keep,
                     QString *error) {
  finishBackgroundWrites();
  QDir backups(QDir(dataDir).filePath(kBackupsFolder));
  QString name = today.toString(Qt::ISODate);
  bool ok = true;
  if (!backups.exists(name)) {
    // Written under a temporary name first, so that a copy cut short is
    // never taken for today's.
    QString partial = backups.filePath(name + ".partial");
    QDir(partial).removeRecursively();
    // Made first: a data folder with nothing to copy yet is backed up as
    // an empty one.
    ok = QDir().mkpath(partial) && copyTree(dataDir, partial, error) &&
         (backups.rename(name + ".partial", name) ||
          fail(error, QString("%1 couldn't be renamed")
                          .arg(QDir::toNativeSeparators(partial))));
    if (!ok) {
      QDir(partial).removeRecursively();
    }
  }
  // The newest `keep` dated folders stay (names sort by date).
  QStringList dated;
  for (const QString &d :
       backups.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
    if (QDate::fromString(d, Qt::ISODate).isValid()) {
      dated << d;
    }
  }
  for (qsizetype i = 0; i < dated.size() - std::max(keep, 1); ++i) {
    QDir(backups.filePath(dated[i])).removeRecursively();
  }
  return ok;
}

QString moveToDeleted(const QString &file, const QString &folder,
                      const QDateTime &when, QString *error) {
  QFileInfo info(file);
  if (!info.exists()) {
    return {};
  }
  QDir().mkpath(folder);
  QString base =
      info.completeBaseName() + "." + when.toString("yyyyMMdd-hhmmss");
  QString suffix = info.suffix().isEmpty() ? QString() : "." + info.suffix();
  QString dest = QDir(folder).filePath(base + suffix);
  for (int n = 2; QFile::exists(dest); ++n) {
    dest =
        QDir(folder).filePath(QString("%1-%2%3").arg(base).arg(n).arg(suffix));
  }
  if (!QFile::rename(file, dest)) {
    fail(error, QString("%1 couldn't be moved to %2")
                    .arg(QDir::toNativeSeparators(file),
                         QDir::toNativeSeparators(folder)));
    return {};
  }
  return dest;
}

bool exportAllData(const QString &dataDir, const QString &settingsFile,
                   const QString &target, QString *error) {
  finishBackgroundWrites();
  if (QFileInfo::exists(target)) {
    return fail(
        error,
        QString("%1 already exists").arg(QDir::toNativeSeparators(target)));
  }
  // The copy would find its own files to copy. Compared once links (and,
  // where it is ignored, case) are resolved, before anything is made.
  QString data = resolvedPath(dataDir);
  if (isInside(resolvedPath(target), data)) {
    return fail(error, QString("%1 is inside the data folder")
                           .arg(QDir::toNativeSeparators(target)));
  }
  if (!QDir().mkpath(target)) {
    return fail(
        error,
        QString("%1 couldn't be made").arg(QDir::toNativeSeparators(target)));
  }
  // And the copy never walks into the folder it is filling.
  QString to = QFileInfo(target).canonicalFilePath();
  if (!to.isEmpty() && isInside(to, data)) {
    return fail(error, QString("%1 is inside the data folder")
                           .arg(QDir::toNativeSeparators(target)));
  }
  if (!copyTree(dataDir, QDir(target).filePath("data"), error, to)) {
    return false;
  }
  if (!settingsFile.isEmpty() && QFile::exists(settingsFile)) {
    QString dest =
        QDir(target).filePath("settings/" + QFileInfo(settingsFile).fileName());
    QDir().mkpath(QFileInfo(dest).absolutePath());
    if (!QFile::copy(settingsFile, dest)) {
      return fail(error, QString("%1 couldn't be copied")
                             .arg(QDir::toNativeSeparators(settingsFile)));
    }
  }
  return true;
}

} // namespace vcube
