#pragma once

#include "app/colours.hpp"
#include "app/keymap.hpp"
#include "train/srs.hpp"

#include <QApplication>

namespace vcube {

// Makes QSettings use an INI file on every platform
// (~/.config/v-cube/v-cube.ini, %APPDATA%\v-cube\v-cube.ini on Windows)
// and copies settings over from the native store used by earlier versions.
// Call once, after the organization and application names are set.
void initSettingsStorage();

// Application-wide preferences, kept in QSettings.
struct AppSettings {
  bool darkTheme = true;
  ColourScheme scheme = ColourScheme::standard();
  int animationMs = 110;
  Keymap keymap = Keymap::cstimerDefault();

  // Timer
  int holdMs = 300;
  bool inspection = false;
  int decimals = 2;

  // Spaced repetition
  SrsConfig srs;

  static AppSettings load();
  void save() const;
};

void applyTheme(QApplication &app, bool dark);

// "1m 10m" <-> seconds.
QString stepsToText(const std::vector<std::int64_t> &steps);
std::vector<std::int64_t> stepsFromText(const QString &text);

} // namespace vcube
