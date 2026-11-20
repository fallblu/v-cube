#include "app/single_instance.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>

namespace vcube {

namespace {

constexpr int kWaitMs = 1500;

// One name per data folder, short enough for every platform's sockets.
QString serverNameFor(const QString &dataDir) {
  QByteArray hash = QCryptographicHash::hash(
      QDir(dataDir).absolutePath().toUtf8(), QCryptographicHash::Sha1);
  return "v-cube-" + QString::fromLatin1(hash.toHex().left(16));
}

} // namespace

SingleInstance::SingleInstance(const QString &dataDir, QObject *parent)
    : QObject(parent), serverName_(serverNameFor(dataDir)),
      lock_(QDir(dataDir).filePath("v-cube.lock")) {
  // A lock left by a copy that crashed is taken over (its process is gone).
  lock_.setStaleLockTime(0);
}

SingleInstance::Claim SingleInstance::claim() {
  if (lock_.tryLock(200)) {
    listen();
    return Claim::First;
  }
  if (lock_.error() != QLockFile::LockFailedError) {
    // The lock file can't be made (e.g. a read-only folder): don't stand in
    // the way, as before.
    listen();
    return Claim::First;
  }
  QLocalSocket socket;
  socket.connectToServer(serverName_);
  if (!socket.waitForConnected(kWaitMs)) {
    return Claim::Running;
  }
  socket.write("show\n");
  socket.waitForBytesWritten(kWaitMs);
  socket.disconnectFromServer();
  return Claim::Signalled;
}

QString SingleInstance::lockHolder() const {
  qint64 pid = 0;
  QString host, app;
  if (!lock_.getLockInfo(&pid, &host, &app)) {
    return {};
  }
  return host.isEmpty() ? tr("process %1").arg(pid)
                        : tr("process %1 on %2").arg(pid).arg(host);
}

bool SingleInstance::takeOver() {
  if (!lock_.removeStaleLockFile() && QFile::exists(lock_.fileName())) {
    return false;
  }
  if (!lock_.tryLock(200)) {
    return false;
  }
  listen();
  return true;
}

void SingleInstance::listen() {
  server_ = new QLocalServer(this);
  // A socket left by a crashed copy would stop listen(); the lock says no
  // other copy is using it.
  QLocalServer::removeServer(serverName_);
  server_->setSocketOptions(QLocalServer::UserAccessOption);
  connect(server_, &QLocalServer::newConnection, this, [this] {
    while (QLocalSocket *s = server_->nextPendingConnection()) {
      connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
      emit activationRequested();
    }
  });
  server_->listen(serverName_);
}

} // namespace vcube
