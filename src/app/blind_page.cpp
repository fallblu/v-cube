#include "app/blind_page.hpp"

#include <QLabel>
#include <QVBoxLayout>

namespace vcube {

BlindPage::BlindPage(AppSettings &settings, const QString &dataDir,
                     QWidget *parent)
    : ModePage(parent), settings_(settings) {
  (void)dataDir;
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(new QLabel(tr("Blind"), this));
}

} // namespace vcube
