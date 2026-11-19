#pragma once

#include <QDate>
#include <QDateTime>
#include <QString>

namespace vcube {

// A safety net for the data folder: dated copies of every data file, files
// moved aside rather than deleted, and a copy of everything to take
// elsewhere.

// Folders inside the data folder that hold copies, not data: skipped by
// backups and exports.
inline constexpr const char *kBackupsFolder = "backups";
inline constexpr const char *kDeletedFolder = "deleted";

// Copies every file in `dataDir` (and its subfolders, but not the backup
// and deleted folders, lock files or save temporaries) to
// <dataDir>/backups/<yyyy-MM-dd>/, keeping the folder layout, and
// `settingsFile` (if given and there) to its settings/ subfolder, unless
// today's copy exists. Only the newest `keep` dated copies are kept. Returns
// false, with the reason in `error`, if a file couldn't be copied.
bool makeDailyBackup(const QString &dataDir, const QString &settingsFile,
                     const QDate &today, int keep, QString *error = nullptr);

// Whether `name` is a temporary file left by a save that was cut short
// (<file>.json.XXXXXX, or .ini), which never became the file itself.
bool isSaveTemporary(const QString &name);

// Moves the save temporaries in `dataDir` (outside the backup and deleted
// folders) into <dataDir>/deleted/unsaved/, so that they don't pile up and
// get copied into every backup, while what they hold is still there to look
// at. Returns how many were moved. Call it before anything is saved.
int moveSaveTemporaries(const QString &dataDir, const QDateTime &when);

// Moves `file` into `folder` (made if needed) as <name>.<yyyyMMdd-hhmmss>
// .<suffix>, so that a deleted file can still be brought back by hand.
// Returns the new path, or an empty string if the file couldn't be moved
// (it is then left where it is). A missing file gives an empty string too.
QString moveToDeleted(const QString &file, const QString &folder,
                      const QDateTime &when, QString *error = nullptr);

// Copies the data folder (as a backup does) and `settingsFile` into a new
// folder `target`, which can't be inside the data folder. Returns false, with
// the reason in `error`, if something couldn't be copied.
bool exportAllData(const QString &dataDir, const QString &settingsFile,
                   const QString &target, QString *error = nullptr);

} // namespace vcube
