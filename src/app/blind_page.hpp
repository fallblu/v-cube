#pragma once

#include "app/app_settings.hpp"
#include "app/mode_page.hpp"

namespace vcube {

// Blind mode: Old Pochmann blindfolded practice.
class BlindPage : public ModePage {
  Q_OBJECT
public:
  // Sessions and drill statistics are kept in `dataDir`/blind.json.
  BlindPage(AppSettings &settings, const QString &dataDir,
            QWidget *parent = nullptr);

private:
  AppSettings &settings_;
};

} // namespace vcube
