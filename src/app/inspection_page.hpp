#pragma once

#include "app/app_settings.hpp"
#include "app/mode_page.hpp"

namespace vcube {

// Inspection mode: planning practice for the inspection steps of ZZ on the
// virtual cube.
class InspectionPage : public ModePage {
  Q_OBJECT
public:
  // Sessions are kept in `dataDir`/inspection.json.
  InspectionPage(AppSettings &settings, const QString &dataDir,
                 QWidget *parent = nullptr);

private:
  AppSettings &settings_;
};

} // namespace vcube
