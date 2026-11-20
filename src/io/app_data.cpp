#include "io/app_data.hpp"

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
  return QCoreApplication::translate("vcube::AppData", text);
}

// An unreadable file is kept, under another name if it could be moved.
QString kept(const QString &path) {
  return path.isEmpty()
             ? tr("It is left where it is, and nothing will be saved over it.")
             : tr("It was kept as %1.").arg(QDir::toNativeSeparators(path));
}

// Reads `store`. A file that can't be read is told in `warnings` with
// `message`, whose %1 is the reason, and what became of the file.
void loadStore(FileStore &store, const QString &message,
               QStringList &warnings) {
  QString error;
  if (!store.load(&error)) {
    warnings << message.arg(error) + " " + kept(store.setAsidePath());
  }
}

// Reads a practice mode's file, `name` in the data folder. What the mode
// kept before it had its tabs is dropped first (TSLE's OCLL algs are
// carried over, and saved at once: nothing else may save before the app
// is closed, and the old file is no longer where the next start would
// look for it).
void loadPractice(PracticeStore &store, const QString &name,
                  QStringList &warnings) {
  const QJsonObject old = removeOldPracticeFile(store.path());
  loadStore(store,
            tr("Could not read %1 (%2), so the mode starts afresh.").arg(name),
            warnings);
  bool carried = false;
  for (const auto &[key, value] : oldOcllNotes(old)) {
    carried |= store.data.notes.emplace(key, value).second;
  }
  QString error;
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
  loadStore(sessions,
            tr("Could not read your timer sessions (%1), so the timer starts "
               "with an empty session."),
            report.warnings);
  loadStore(untimed,
            tr("Could not read your untimed solves (%1), so Untimed starts "
               "with an empty session."),
            report.warnings);
  loadStore(moveCounts,
            tr("Could not read your move counts (%1), so Move Count starts "
               "with the default sessions."),
            report.warnings);
  loadStore(lab,
            tr("Could not read your Alg Lab data (%1), so Alg Lab starts "
               "afresh."),
            report.warnings);
  loadPractice(eoCross, "eocross.json", report.warnings);
  loadPractice(f2l, "f2l.json", report.warnings);
  loadPractice(tsle, "tsle.json", report.warnings);
  return report;
}

std::vector<ActivityEvent> activityEvents(const AppData &data,
                                          std::int64_t since) {
  std::vector<ActivityEvent> out;
  auto add = [&](int mode, std::int64_t t, std::int64_t ms) {
    if (t >= since) {
      out.push_back({mode, t, ms});
    }
  };
  for (const AlgSet &set : data.library.sets()) {
    for (const auto &[id, p] : data.library.progress(set.id)) {
      for (const Attempt &a : p.attempts) {
        bool learn = a.mode == AttemptMode::Learn;
        add(learn ? ActivityMode::Learn : ActivityMode::Drill, a.timestamp,
            learn ? 0 : a.timeMs);
      }
    }
  }
  for (const Session &s : data.sessions.sessions()) {
    for (const Solve &solve : s.solves) {
      add(ActivityMode::Timed, solve.timestamp, solve.timeMs);
    }
  }
  for (const auto &[store, mode] :
       {std::pair{&data.untimed, int(ActivityMode::Untimed)},
        std::pair{&data.moveCounts, int(ActivityMode::MoveCount)}}) {
    for (const MoveCountSession &s : store->sessions()) {
      for (const MoveCountAttempt &a : s.attempts) {
        add(mode, a.timestamp, 0);
      }
    }
  }
  for (const LabAttempt &a : data.lab.attempts()) {
    add(ActivityMode::AlgLab, a.timestamp, a.timeMs);
  }
  // The practice modes: every card's try and every solve kept.
  for (const auto &[practised, mode] :
       {std::pair{&data.eoCross.data, int(ActivityMode::EoCross)},
        std::pair{&data.f2l.data, int(ActivityMode::F2L)},
        std::pair{&data.tsle.data, int(ActivityMode::Tsle)}}) {
    const PracticeData &kept = *practised;
    for (const auto &[id, p] : kept.cards) {
      for (const Attempt &a : p.attempts) {
        add(mode, a.timestamp, 0);
      }
    }
    for (const PracticeTries *tries :
         {&kept.learn, &kept.inspection, &kept.timed}) {
      for (const auto &[key, list] : *tries) {
        for (const PracticeTry &t : list) {
          add(mode, t.timestamp, t.ms + t.inspectMs);
        }
      }
    }
  }
  return out;
}

} // namespace vcube
