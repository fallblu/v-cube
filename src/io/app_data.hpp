#pragma once

#include "io/alglab_store.hpp"
#include "io/library.hpp"
#include "io/move_counts.hpp"
#include "io/practice_store.hpp"
#include "io/sessions.hpp"
#include "train/today.hpp"

#include <QDate>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>
#include <vector>

namespace vcube {

// The data files the modes share, in the data folder: the alg sets and
// their progress, the timer sessions, the untimed solves and the move
// counts, Alg Lab's file, and the files EO Cross, F2L and TSLE each keep
// (see PracticeStore).
class AppData {
public:
  explicit AppData(QString dataDir);

  // What went wrong in load(), to tell the user.
  struct LoadReport {
    QString backupError;  // today's backup failed (empty: it didn't)
    QStringList warnings; // a file couldn't be read, and what became of it
  };
  // Makes today's backup of the files, before anything is changed, then
  // reads them all.
  LoadReport load();

  const QString &dataDir() const { return dataDir_; }
  // The backups folder, where a failed backup is reported.
  QString backupsDir() const;
  // Makes today's backup (of the data folder and the settings file) if this
  // copy hasn't made it yet, e.g. when the app is left open past midnight.
  // Returns nullopt when it had been made, else why it failed (empty when
  // it didn't).
  std::optional<QString> backUpIfNewDay();

  AlgLibrary library;
  SessionStore sessions;
  MoveCountStore untimed;
  MoveCountStore moveCounts;
  AlgLabStore lab;
  PracticeStore eoCross;
  PracticeStore f2l;
  PracticeStore tsle;

private:
  QString dataDir_;
  QDate lastBackup_;
};

// The modes an event from activityEvents() is of (its `mode`).
struct ActivityMode {
  enum Id : int {
    Learn,
    Drill,
    AlgLab,
    Timed,
    Untimed,
    MoveCount,
    EoCross,
    F2L,
    Tsle
  };
  static constexpr int Count = Tsle + 1;
};

// Everything done in the modes at or after `since` (Unix seconds), as the
// stores have it in memory: each alg set's case attempts (Learn, else
// Drill), the timer's solves, the untimed solves and move counts, Alg Lab's
// timed algs, and the practice modes' card attempts and solves.
std::vector<ActivityEvent> activityEvents(const AppData &data,
                                          std::int64_t since);

} // namespace vcube
