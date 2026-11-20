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

// Whether the desktop asks for less motion: Windows' animation effects
// turned off, or GNOME's animations. False where it can't be told.
bool platformPrefersReducedMotion();

// Opens a folder in the desktop's file manager (Windows Explorer under WSL).
// Returns false if no file manager could be started.
bool openFolder(const QString &path);

// On Unix, makes SIGTERM and SIGHUP (logout, `wsl --shutdown`) quit the
// app as closing its window does, so that the saves still being written
// finish and the window's layout is kept. Elsewhere it does nothing. Call
// once, after the application is made.
void quitOnTerminationSignals();

} // namespace vcube
