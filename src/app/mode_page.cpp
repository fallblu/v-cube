#include "app/mode_page.hpp"

#include <QApplication>

namespace vcube {

bool ModePage::keyboardTaken() {
  return QApplication::activePopupWidget() || QApplication::activeModalWidget();
}

} // namespace vcube
