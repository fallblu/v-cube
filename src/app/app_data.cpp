#include "app/app_data.hpp"

#include "io/backup.hpp"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QSettings>

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

} // namespace

AppData::AppData(QString dataDir)
    : library(QDir(dataDir).filePath("algsets"), ":/algsets"),
      sessions(QDir(dataDir).filePath("sessions.json")),
      untimed(QDir(dataDir).filePath("untimed.json"), SolveLog::Untimed),
      moveCounts(QDir(dataDir).filePath("movecounts.json")),
      lab(QDir(dataDir).filePath("alglab.json")), dataDir_(std::move(dataDir)) {
}

QString AppData::backupsDir() const {
  return QDir(dataDir_).filePath(kBackupsFolder);
}

std::optional<QString> AppData::backUpIfNewDay() {
  QDate today = QDate::currentDate();
  if (today == lastBackup_) {
    return std::nullopt;
  }
  lastBackup_ = today;
  QString error;
  if (!makeDailyBackup(dataDir_, QSettings().fileName(), today, kBackupDays,
                       &error)) {
    return tr("Today's backup failed: %1").arg(error);
  }
  return QString();
}

AppData::LoadReport AppData::load() {
  LoadReport report;
  // Left by saves cut short (a crash): out of the way before the backup.
  moveSaveTemporaries(dataDir_, QDateTime::currentDateTime());
  report.backupError = backUpIfNewDay().value_or(QString());
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
  return report;
}

} // namespace vcube
