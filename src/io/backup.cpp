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

// Copies the files under `from` to `to`, keeping the layout and skipping
// the backup and deleted folders at any depth.
bool copyTree(const QString &from, const QString &to, QString *error) {
  QDir source(from);
  QDirIterator it(from, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    QString path = it.next();
    QString rel = source.relativeFilePath(path);
    QStringList parts = rel.split('/');
    parts.removeLast();
    if (parts.contains(kBackupsFolder) || parts.contains(kDeletedFolder)) {
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
    ok = copyTree(dataDir, partial, error) &&
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
  if (!QDir().mkpath(target)) {
    return fail(
        error,
        QString("%1 couldn't be made").arg(QDir::toNativeSeparators(target)));
  }
  if (!copyTree(dataDir, QDir(target).filePath("data"), error)) {
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
