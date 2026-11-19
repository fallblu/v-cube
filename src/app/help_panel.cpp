#include "app/help_panel.hpp"

#include "app/help_text.hpp"

#include <QScrollBar>
#include <QTextBrowser>

namespace vcube {

HelpPanel::HelpPanel(QWidget *parent)
    : QDockWidget(tr("Keyboard controls"), parent) {
  setObjectName("helpPanel");
  setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
              QDockWidget::DockWidgetFloatable);
  text_ = new QTextBrowser(this);
  text_->setFocusPolicy(Qt::NoFocus);
  text_->setOpenLinks(false);
  text_->setMinimumWidth(220);
  setWidget(text_);
  connect(this, &QDockWidget::visibilityChanged, this, [this](bool on) {
    if (on) {
      refresh();
    }
  });
}

void HelpPanel::setContext(std::optional<AppMode::Id> mode,
                           const Keymap &keymap) {
  mode_ = mode;
  keymap_ = keymap;
  refresh();
}

void HelpPanel::refresh() {
  if (isVisible()) {
    int scroll = text_->verticalScrollBar()->value();
    text_->setHtml(controlsHtml(mode_, keymap_));
    text_->verticalScrollBar()->setValue(scroll);
  }
}

} // namespace vcube
