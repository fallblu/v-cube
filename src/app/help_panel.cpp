#include "app/help_panel.hpp"

#include "app/help_text.hpp"

#include <QScrollBar>
#include <QShortcut>
#include <QTextBrowser>

#include <utility>

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
  // The text takes no focus (the keys belong to the page): Alt+Page Up and
  // Alt+Page Down scroll it while it is shown.
  QWidget *window = parent ? parent : this;
  for (auto [key, step] :
       {std::pair{Qt::Key_PageUp, QAbstractSlider::SliderPageStepSub},
        std::pair{Qt::Key_PageDown, QAbstractSlider::SliderPageStepAdd}}) {
    auto *s = new QShortcut(QKeySequence(Qt::ALT | key), window);
    s->setContext(Qt::WindowShortcut);
    s->setEnabled(false);
    connect(s, &QShortcut::activated, this,
            [this, step] { text_->verticalScrollBar()->triggerAction(step); });
    connect(this, &QDockWidget::visibilityChanged, s, &QShortcut::setEnabled);
  }
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
