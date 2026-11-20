#include "app/platform.hpp"

#include <QApplication>
#include <QDesktopServices>
#include <QFile>
#include <QProcess>
#include <QUrl>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifdef Q_OS_UNIX
#include <QSocketNotifier>

#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace vcube {

bool runningUnderWsl() {
#ifdef Q_OS_LINUX
  static const bool wsl = [] {
    if (qEnvironmentVariableIsSet("WSL_DISTRO_NAME")) {
      return true;
    }
    QFile f("/proc/sys/kernel/osrelease");
    return f.open(QIODevice::ReadOnly) &&
           f.readAll().toLower().contains("microsoft");
  }();
  return wsl;
#else
  return false;
#endif
}

void selectQtPlatform() {
  if (runningUnderWsl() && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    // Falls back to Wayland when the xcb plugin (or its libraries) is
    // missing, rather than failing to start.
    qputenv("QT_QPA_PLATFORM", "xcb;wayland");
  }
}

namespace {

// What `program` prints, trimmed, or "" when it fails or takes over `ms`.
QString runOutput(const QString &program, const QStringList &args, int ms) {
  QProcess p;
  p.start(program, args);
  if (!p.waitForFinished(ms) || p.exitStatus() != QProcess::NormalExit ||
      p.exitCode() != 0) {
    return {};
  }
  return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

bool openFolderInExplorer(const QString &path) {
  // Linux path -> Windows path (e.g. \\wsl.localhost\Ubuntu\home\...).
  QString winPath = runOutput("wslpath", {"-w", path}, 5000);
  if (winPath.isEmpty()) {
    return false;
  }
  // explorer.exe exits non-zero even when it succeeds, so a successful start
  // is all we can check. It is not on PATH when appendWindowsPath=false.
  for (const QString &explorer :
       {QStringLiteral("explorer.exe"),
        QStringLiteral("/mnt/c/Windows/explorer.exe")}) {
    if (QProcess::startDetached(explorer, {winPath})) {
      return true;
    }
  }
  return false;
}

} // namespace

bool platformPrefersReducedMotion() {
#if defined(Q_OS_WIN)
  // Settings > Accessibility > Visual effects > Animation effects.
  BOOL animate = TRUE;
  return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0) &&
         !animate;
#elif defined(Q_OS_LINUX)
  // GNOME's Settings > Accessibility > Reduce animation. Asked only on
  // GNOME, where gsettings answers at once.
  if (!qEnvironmentVariable("XDG_CURRENT_DESKTOP")
           .contains("GNOME", Qt::CaseInsensitive)) {
    return false;
  }
  return runOutput("gsettings",
                   {"get", "org.gnome.desktop.interface", "enable-animations"},
                   1000) == "false";
#else
  return false;
#endif
}

bool openFolder(const QString &path) {
  if (runningUnderWsl()) {
    return openFolderInExplorer(path);
  }
  return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

#ifdef Q_OS_UNIX
namespace {

// The handler only writes to one end; the event loop reads the other.
int gSignalFds[2] = {-1, -1};

void onTerminationSignal(int) {
  char c = 1;
  [[maybe_unused]] ssize_t n = ::write(gSignalFds[0], &c, 1);
}

} // namespace
#endif

void quitOnTerminationSignals() {
#ifdef Q_OS_UNIX
  if (gSignalFds[0] != -1 ||
      ::socketpair(AF_UNIX, SOCK_STREAM, 0, gSignalFds) != 0) {
    return;
  }
  auto *notifier = new QSocketNotifier(gSignalFds[1], QSocketNotifier::Read,
                                       QCoreApplication::instance());
  QObject::connect(notifier, &QSocketNotifier::activated, notifier, [] {
    char c = 0;
    [[maybe_unused]] ssize_t n = ::read(gSignalFds[1], &c, 1);
    // As closing the window does: what it saves on closing is saved, and
    // the app quits once the last window has closed.
    QApplication::closeAllWindows();
  });
  struct sigaction action = {};
  action.sa_handler = onTerminationSignal;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_RESTART;
  sigaction(SIGTERM, &action, nullptr);
  sigaction(SIGHUP, &action, nullptr);
#endif
}

} // namespace vcube
