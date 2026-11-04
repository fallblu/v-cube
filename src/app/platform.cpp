#include "app/platform.hpp"

#include <QFile>

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
    qputenv("QT_QPA_PLATFORM", "xcb");
  }
}

} // namespace vcube
