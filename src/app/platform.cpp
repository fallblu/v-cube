#include "app/platform.hpp"

#include <QDesktopServices>
#include <QFile>
#include <QProcess>
#include <QUrl>

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

// Linux path -> Windows path (e.g. \\wsl.localhost\Ubuntu\home\...).
QString windowsPath(const QString &path) {
  QProcess p;
  p.start("wslpath", {"-w", path});
  if (!p.waitForFinished(5000) || p.exitStatus() != QProcess::NormalExit ||
      p.exitCode() != 0) {
    return {};
  }
  return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

bool openFolderInExplorer(const QString &path) {
  QString winPath = windowsPath(path);
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

bool openFolder(const QString &path) {
  if (runningUnderWsl()) {
    return openFolderInExplorer(path);
  }
  return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

} // namespace vcube
