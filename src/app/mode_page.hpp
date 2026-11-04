#pragma once

#include <QWidget>

class QKeyEvent;

namespace vcube {

// A page of the main window, one per mode (Learn, Drill, Library, Timer,
// Scramble, Virtual). The main window forwards the keyboard to the active
// page and tells pages when they are shown and hidden.
class ModePage : public QWidget {
  Q_OBJECT
public:
  using QWidget::QWidget;

  // A key press or release meant for the page; true if it was used.
  virtual bool handleKey(QKeyEvent *e, bool press) {
    (void)e;
    (void)press;
    return false;
  }
  // The settings changed (after the settings dialog).
  virtual void applySettings() {}
  // A timed solve or inspection is running: switching modes is blocked.
  virtual bool isBusy() const { return false; }
  // The page was shown or hidden, e.g. to refresh shared state, start or
  // stop timers, and finish animations.
  virtual void activated() {}
  virtual void deactivated() {}

signals:
  void message(const QString &text);
};

} // namespace vcube
