#pragma once

#include "app/app_settings.hpp"

#include <QWidget>

class QKeyEvent;

namespace vcube {

// A page of the main window, one per mode (Learn, Drill, Library, Timed,
// Untimed, Move Count, Inspection, Blind, Today). The main window forwards
// the keyboard to the active page (see KeyRouter) and tells pages when
// they are shown and hidden, when the settings change and when focus mode
// is turned on or off.
class ModePage : public QWidget {
  Q_OBJECT
public:
  using QWidget::QWidget;

  // A key press or release meant for the page; true if it was used. A key
  // held down reaches the page once: its repeats go to the page only if
  // acceptsAutoRepeat() says so, and are swallowed if its press was used.
  virtual bool handleKey(QKeyEvent *e, bool press) {
    (void)e;
    (void)press;
    return false;
  }
  // The page acts on every repeat of this key held down (e.g. Backspace
  // deleting typed digits one after another).
  virtual bool acceptsAutoRepeat(const QKeyEvent *e) const {
    (void)e;
    return false;
  }

  // The parts of the settings the page uses: applySettings() is called
  // only when one of them (or the theme) changed in the settings dialog.
  virtual SettingsParts settingsUsed() const { return SettingsPart::All; }
  virtual void applySettings() {}

  // Busy: an attempt is under way that leaving the page would cancel and
  // that must keep the keyboard: Space held to start a timer, inspection,
  // memo or a solve running. While a page is busy the mode can't be
  // switched, the settings and the command list don't open, and quitting
  // asks first. A timer only armed (waiting for the first turn) is not
  // busy: leaving disarms it.
  virtual bool isBusy() const { return false; }

  // The page was shown or hidden, e.g. to refresh shared state, start or
  // stop timers, and finish animations.
  virtual void activated() {}
  virtual void deactivated() {}
  // The window lost the keyboard: a key held down now won't be released.
  virtual void windowDeactivated() {}

  // Focus mode: hide everything but the page's central display (the
  // scramble, timer, cube and algs), or show it all again. Every page is
  // told, shown or not; a page asks focusMode() rather than whether its
  // controls are visible.
  void setFocusMode(bool on) {
    focusMode_ = on;
    focusModeChanged();
  }
  bool focusMode() const { return focusMode_; }

signals:
  void message(const QString &text);

protected:
  // Shows or hides the page's controls as focusMode() now says.
  virtual void focusModeChanged() {}

private:
  bool focusMode_ = false;
};

} // namespace vcube
