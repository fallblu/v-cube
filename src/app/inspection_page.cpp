#include "app/inspection_page.hpp"

#include <QLabel>
#include <QVBoxLayout>

namespace vcube {

InspectionPage::InspectionPage(AppSettings &settings, const QString &dataDir,
                               QWidget *parent)
    : ModePage(parent), settings_(settings) {
  (void)dataDir;
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(new QLabel(tr("Inspection"), this));
}

} // namespace vcube
