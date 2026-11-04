#pragma once

#include "app/colours.hpp"
#include "app/keymap.hpp"
#include "train/srs.hpp"

#include <QApplication>

namespace vcube {

// Application-wide preferences, kept in QSettings (~/.config/v-cube).
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
