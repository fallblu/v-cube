#include "io/backup.hpp"

#include "io/data_file.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

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
// the backup and deleted folders at any depth, lock files and save
// temporaries, and anything in `skip` (canonical paths), such as the folder
// being copied to.
bool copyTree(const QString &from, const QString &to, QString *error,
              const QStringList &skip = {}) {
  QDir source(from);
  QDirIterator it(from, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    QString path = it.next();
    QString rel = source.relativeFilePath(path);
    QStringList parts = rel.split('/');
    QString name = parts.takeLast();
    if (parts.contains(kBackupsFolder) || parts.contains(kDeletedFolder) ||
        rel.endsWith(".lock") || isSaveTemporary(name)) {
      continue;
    }
    QString canonical = QFileInfo(path).canonicalFilePath();
    if (std::any_of(skip.begin(), skip.end(), [&](const QString &s) {
          return !s.isEmpty() && isInside(canonical, s);
        })) {
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

// Copies `settingsFile`, if there is one, into <target>/settings/.
bool copySettings(const QString &settingsFile, const QString &target,
                  QString *error) {
  if (settingsFile.isEmpty() || !QFile::exists(settingsFile)) {
    return true;
  }
  QString dest =
      QDir(target).filePath("settings/" + QFileInfo(settingsFile).fileName());
  QDir().mkpath(QFileInfo(dest).absolutePath());
  QFile::remove(dest);
  if (!QFile::copy(settingsFile, dest)) {
    return fail(error, QString("%1 couldn't be copied to %2")
                           .arg(QDir::toNativeSeparators(settingsFile),
                                QDir::toNativeSeparators(dest)));
  }
  return true;
}

// The settings file, when it is inside the data folder (as with
// --data-dir): copied once, as the settings, and not again with the data.
QString canonicalOrEmpty(const QString &path) {
  return path.isEmpty() ? QString() : QFileInfo(path).canonicalFilePath();
}

} // namespace

bool isSaveTemporary(const QString &name) {
  // QSaveFile writes <name>.XXXXXX beside the file, then renames it; the
  // X's are random letters of both cases. A name of one case, or a word
  // such as "Backup", is more likely the user's own copy, and left alone.
  static const QRegularExpression re(R"(^.+\.(json|ini)\.([A-Za-z0-9]{6})$)");
  static const QRegularExpression word(R"(^[A-Z]?[a-z0-9]*$|^[A-Z0-9]*$)");
  QRegularExpressionMatch m = re.match(name);
  return m.hasMatch() && !word.match(m.captured(2)).hasMatch();
}

int moveSaveTemporaries(const QString &dataDir, const QDateTime &when) {
  QDir source(dataDir);
  QStringList found;
  QDirIterator it(dataDir, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    QString path = it.next();
    QStringList parts = source.relativeFilePath(path).split('/');
    QString name = parts.takeLast();
    if (!parts.contains(kBackupsFolder) && !parts.contains(kDeletedFolder) &&
        isSaveTemporary(name)) {
      found << path;
    }
  }
  QString folder = source.filePath(QString(kDeletedFolder) + "/unsaved");
  int moved = 0;
  for (const QString &path : found) {
    // Named for the file it was going to be, with the date moved.
    QString rel = source.relativeFilePath(path);
    rel.replace('/', '-');
    QString dest =
        QDir(folder).filePath(rel + "." + when.toString("yyyyMMdd-hhmmss"));
    for (int n = 2; QFile::exists(dest); ++n) {
      dest =
          QDir(folder).filePath(QString("%1.%2-%3")
                                    .arg(rel, when.toString("yyyyMMdd-hhmmss"))
                                    .arg(n));
    }
    if (QDir().mkpath(folder) && QFile::rename(path, dest)) {
      ++moved;
    }
  }
  return moved;
}

bool makeDailyBackup(const QString &dataDir, const QString &settingsFile,
                     const QDate &today, int keep, QString *error) {
  finishBackgroundWrites();
  QDir backups(QDir(dataDir).filePath(kBackupsFolder));
  QString name = today.toString(Qt::ISODate);
  bool ok = true;
  auto folder = lockDataFolder();
  if (!backups.exists(name)) {
    // Written under a temporary name first, so that a copy cut short is
    // never taken for today's.
    QString partial = backups.filePath(name + ".partial");
    QDir(partial).removeRecursively();
    // Made first: a data folder with nothing to copy yet is backed up as
    // an empty one.
    ok = QDir().mkpath(partial) &&
         copyTree(dataDir, partial, error, {canonicalOrEmpty(settingsFile)}) &&
         copySettings(settingsFile, partial, error) &&
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
  // Not while the data folder is being copied: the copy would fail on a
  // file it had listed, or (on Windows) the move on a file the copy has open.
  auto copying = lockDataFolder();
  QDir().mkpath(folder);
  QString base =
      info.completeBaseName() + "." + when.toString("yyyyMMdd-hhmmss");
  QString suffix = info.suffix().isEmpty() ? QString() : "." + info.suffix();
  QString dest = QDir(folder).filePath(base + suffix);
  for (int n = 2; QFile::exists(dest); ++n) {
    dest = QDir(folder).filePath(base + "-" + QString::number(n) + suffix);
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
  return copyTree(dataDir, QDir(target).filePath("data"), error,
                  {to, canonicalOrEmpty(settingsFile)}) &&
         copySettings(settingsFile, target, error);
}

} // namespace vcube
