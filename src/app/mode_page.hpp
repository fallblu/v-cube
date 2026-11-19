#pragma once

#include "app/app_settings.hpp"

#include <QResizeEvent>
#include <QWidget>

class QKeyEvent;
class QMenu;

namespace vcube {

// A page of the main window, one per mode (Learn, Drill, Library, Timed,
// Untimed, Move Count, EO Cross, F2L, TSLE, Alg Lab, Today). The main window
// forwards the keyboard to the active page (see KeyRouter) and tells pages when
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
  // that must keep the keyboard: Space held to start a timer, inspection
  // or a solve running. While a page is busy the mode can't be
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
    showSidePanel();
  }
  bool focusMode() const { return focusMode_; }

  // The side panel (the case list or the session with its attempts) can
  // be hidden to give the stage the whole page (F9, View > Side panel):
  // the page names the panel, the window remembers the choice per mode.
  // In a narrow page (narrower than kNarrowWidth) the panel is hidden by
  // itself, and comes back when the page is wide again; F9 shows it all
  // the same until then.
  virtual QWidget *sidePanel() const { return nullptr; }
  static constexpr int kNarrowWidth = 760;

  // The page's own menu in the menu bar, named after the mode: what its
  // lists otherwise offer only on a right-click, for what is selected.
  virtual bool hasModeMenu() const { return false; }
  virtual void fillModeMenu(QMenu &menu) { (void)menu; }
  // The choice the user made (and the window remembers), whatever the
  // page's width.
  bool sideHidden() const { return sideHidden_; }
  void setSideHidden(bool hidden) {
    if (hidden == sideHidden_) {
      return;
    }
    sideHidden_ = hidden;
    showSidePanel();
    emit sideHiddenToggled(hidden);
  }
  // The panel is shown now: not hidden by the user, nor by a narrow page
  // (unless F9 brought it back).
  bool sideShown() const {
    return !sideHidden_ && (!narrow_ || shownWhileNarrow_);
  }
  // F9 and View > Side panel: shows or hides the panel. Shown in a narrow
  // page, it stays until the page is wide again; hidden after that, the
  // page is left as narrowness had it.
  void setSideShown(bool show) {
    if (show) {
      shownWhileNarrow_ = narrow_;
      setSideHidden(false);
    } else if (narrow_ && shownWhileNarrow_ && !sideHidden_) {
      shownWhileNarrow_ = false;
    } else {
      setSideHidden(true);
    }
    showSidePanel();
  }
  // Hides the panel while the page is narrow (the window does this as the
  // page is resized, and before showing it).
  void setNarrow(bool narrow) {
    if (narrow == narrow_) {
      return;
    }
    narrow_ = narrow;
    shownWhileNarrow_ = false;
    showSidePanel();
  }

signals:
  void message(const QString &text);
  // The user hid or showed the panel (the choice to remember).
  void sideHiddenToggled(bool hidden);
  // The panel was hidden or shown, by the user or by the page's width.
  void sideShownChanged(bool shown);

protected:
  // Shows or hides the page's controls as focusMode() now says.
  virtual void focusModeChanged() {}
  // The side panel was hidden or shown (e.g. to word a button).
  virtual void sideHiddenChanged() {}
  void resizeEvent(QResizeEvent *e) override {
    QWidget::resizeEvent(e);
    setNarrow(e->size().width() < kNarrowWidth);
  }

private:
  bool focusMode_ = false;
  bool sideHidden_ = false;
  bool narrow_ = false;
  bool shownWhileNarrow_ = false;
  bool shown_ = true; // as sideShown() was last
  void showSidePanel() {
    if (QWidget *side = sidePanel()) {
      side->setVisible(sideShown() && !focusMode_);
    }
    if (sideShown() != shown_) {
      shown_ = sideShown();
      sideHiddenChanged();
      emit sideShownChanged(shown_);
    }
  }
};

} // namespace vcube
