#pragma once

#include <QString>

namespace vcube {

// True when running under the Windows Subsystem for Linux: WSL_DISTRO_NAME
// is set or the kernel release names Microsoft.
bool runningUnderWsl();

// Call before constructing QApplication. Under WSL, picks the X11 (xcb)
// platform plugin, falling back to Wayland when xcb can't be loaded, unless
// QT_QPA_PLATFORM is already set: WSLg forwards each
// Wayland surface, popups included, as a separate Windows window, which
// leaves stale popups behind and loses the window position.
void selectQtPlatform();

// Opens a folder in the desktop's file manager (Windows Explorer under WSL).
// Returns false if no file manager could be started.
bool openFolder(const QString &path);

} // namespace vcube
