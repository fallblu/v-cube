#pragma once

#include "io/alglab_store.hpp"
#include "io/library.hpp"
#include "io/move_counts.hpp"
#include "io/sessions.hpp"

#include <QString>
#include <QStringList>

namespace vcube {

// The data files the modes share, in the data folder: the alg sets and
// their progress, the timer sessions, the untimed solves and the move
// counts, and Alg Lab's file. (EO Cross, F2L and TSLE keep their own
// files.)
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

  AlgLibrary library;
  SessionStore sessions;
  MoveCountStore untimed;
  MoveCountStore moveCounts;
  AlgLabStore lab;

private:
  QString dataDir_;
};

} // namespace vcube
