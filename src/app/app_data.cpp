#include "app/app_data.hpp"

#include "io/backup.hpp"

#include <QCoreApplication>
#include <QDate>
#include <QDir>

namespace vcube {

namespace {

// Dated copies of the data files kept in the backups folder.
constexpr int kBackupDays = 14;

QString tr(const char *text) {
  return QCoreApplication::translate("vcube::MainWindow", text);
}

// An unreadable file is kept, under another name if it could be moved.
QString kept(const QString &path) {
  return path.isEmpty()
             ? tr("It is left where it is, and nothing will be saved over it.")
             : tr("It was kept as %1.").arg(QDir::toNativeSeparators(path));
}

// Reads a practice mode's file, `name` in the data folder. What the mode
// kept before it had its tabs is dropped first (TSLE's OCLL algs are
// carried over, and saved at once: nothing else may save before the app
// is closed, and the old file is no longer where the next start would
// look for it).
void loadPractice(PracticeStore &store, const QString &name,
                  QStringList &warnings) {
  const QJsonObject old = removeOldPracticeFile(store.path());
  QString error;
  if (!store.load(&error)) {
    warnings << tr("Could not read %1 (%2), so the mode starts afresh.")
                        .arg(name, error) +
                    " " + kept(store.setAsidePath());
  }
  bool carried = false;
  for (const auto &[key, value] : oldOcllNotes(old)) {
    carried |= store.data.notes.emplace(key, value).second;
  }
  if (carried && !store.save(&error)) {
    warnings << tr("Could not save the OCLL algs carried over to %1: %2")
                    .arg(name, error);
  }
}

} // namespace

AppData::AppData(QString dataDir)
    : library(QDir(dataDir).filePath("algsets"), ":/algsets"),
      sessions(QDir(dataDir).filePath("sessions.json")),
      untimed(QDir(dataDir).filePath("untimed.json"), SolveLog::Untimed),
      moveCounts(QDir(dataDir).filePath("movecounts.json")),
      lab(QDir(dataDir).filePath("alglab.json")),
      eoCross(QDir(dataDir).filePath("eocross.json")),
      f2l(QDir(dataDir).filePath("f2l.json")),
      tsle(QDir(dataDir).filePath("tsle.json")), dataDir_(std::move(dataDir)) {}

QString AppData::backupsDir() const {
  return QDir(dataDir_).filePath(kBackupsFolder);
}

AppData::LoadReport AppData::load() {
  LoadReport report;
  QString backupError;
  if (!makeDailyBackup(dataDir_, QDate::currentDate(), kBackupDays,
                       &backupError)) {
    report.backupError = tr("Today's backup failed: %1").arg(backupError);
  }
  library.load();
  QString error;
  if (!sessions.load(&error)) {
    report.warnings << tr("Could not read your timer sessions (%1), so the "
                          "timer starts with an empty session.")
                               .arg(error) +
                           " " + kept(sessions.setAsidePath());
  }
  if (!untimed.load(&error)) {
    report.warnings << tr("Could not read your untimed solves (%1), so "
                          "Untimed starts with an empty session.")
                               .arg(error) +
                           " " + kept(untimed.setAsidePath());
  }
  if (!moveCounts.load(&error)) {
    report.warnings << tr("Could not read your move counts (%1), so Move "
                          "Count starts with the default sessions.")
                               .arg(error) +
                           " " + kept(moveCounts.setAsidePath());
  }
  if (!lab.load(&error)) {
    report.warnings << tr("Could not read your Alg Lab data (%1), so Alg Lab "
                          "starts afresh.")
                               .arg(error) +
                           " " + kept(lab.setAsidePath());
  }
  loadPractice(eoCross, "eocross.json", report.warnings);
  loadPractice(f2l, "f2l.json", report.warnings);
  loadPractice(tsle, "tsle.json", report.warnings);
  return report;
}

} // namespace vcube
