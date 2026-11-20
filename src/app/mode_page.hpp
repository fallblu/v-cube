#pragma once

#include "app/app_settings.hpp"

#include <QResizeEvent>
#include <QWidget>

#include <initializer_list>
#include <vector>

class QAction;
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
  // Esc in `field`, a text field of the page, leaves it, so that the keys
  // are the page's again (KeyRouter does it).
  static void escapeLeaves(QWidget *field) {
    field->setProperty(kEscapeLeaves, true);
  }
  static constexpr const char *kEscapeLeaves = "vcubeEscapeLeaves";

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

  // The window is closing: finish background work and queue what is
  // still to be saved, before the last writes are waited for.
  virtual void aboutToClose() {}
  // Background work aboutToClose() would stop, in a sentence ("Alg Lab's
  // search"), or empty: if quitting is then cancelled (a save failed),
  // the user is told that it was stopped.
  virtual QString workStoppedOnClose() const { return {}; }

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
    for (QWidget *w : focusHidden_) {
      w->setVisible(!on);
    }
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
  // Menus and Edit menu items of the page's own, in the menu bar only
  // while the page is shown.
  virtual QList<QMenu *> pageMenus() const { return {}; }
  virtual QList<QAction *> editActions() const { return {}; }
  // The alg sets changed (or `setId`'s did), wherever the change was made:
  // a page that shows them reads them again.
  virtual void librarySetsChanged(const QString &setId) { (void)setId; }
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
  // Shows or hides the page's controls as focusMode() now says, after
  // those given to hideInFocusMode() and before the side panel.
  virtual void focusModeChanged() {}
  // Widgets hidden in focus mode, such as the option bar.
  void hideInFocusMode(std::initializer_list<QWidget *> widgets) {
    focusHidden_.insert(focusHidden_.end(), widgets);
  }
  // The side panel was hidden or shown (e.g. to word a button).
  virtual void sideHiddenChanged() {}
  void resizeEvent(QResizeEvent *e) override {
    QWidget::resizeEvent(e);
    setNarrow(e->size().width() < kNarrowWidth);
  }

private:
  bool focusMode_ = false;
  std::vector<QWidget *> focusHidden_;
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
