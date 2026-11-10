#pragma once

#include <QLockFile>
#include <QObject>
#include <QString>

class QLocalServer;

namespace vcube {

// Keeps v-cube to one running copy per data folder: every save rewrites a
// whole file from what was loaded at start-up, so two copies would undo
// each other's changes. The first copy holds a lock file in the data
// folder and listens for later ones, which ask it to come to the front and
// then quit.
class SingleInstance : public QObject {
  Q_OBJECT
public:
  explicit SingleInstance(const QString &dataDir, QObject *parent = nullptr);

  enum class Claim {
    First,     // this is the only copy: go on
    Signalled, // another copy runs and was asked to show itself
    Running,   // another copy holds the lock but didn't answer
  };
  Claim claim();

signals:
  // A later copy was started: show the window.
  void activationRequested();

private:
  QString serverName_;
  QLockFile lock_;
  QLocalServer *server_ = nullptr;

  void listen();
};

} // namespace vcube
