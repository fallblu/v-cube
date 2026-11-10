#pragma once

#include "app/keymap.hpp"

#include <QDockWidget>

class QTextBrowser;

namespace vcube {

// The keyboard controls (F1), docked beside the pages: the shown mode's
// keys first (see controlsHtml()). Kept up to date only while it is open.
class HelpPanel : public QDockWidget {
  Q_OBJECT
public:
  explicit HelpPanel(QWidget *parent = nullptr);

  // The mode shown (an AppMode) and the turning keys changed.
  void setContext(int mode, const Keymap &keymap);

private:
  QTextBrowser *text_;
  int mode_ = -1;
  Keymap keymap_;

  void refresh();
};

} // namespace vcube
