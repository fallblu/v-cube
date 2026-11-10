#pragma once

#include "app/colours.hpp"
#include "app/keymap.hpp"
#include "train/srs.hpp"

#include <QApplication>

namespace vcube {

struct AlgSet;

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
  Keymap keymap = Keymap::defaultLayout();
  // Which way the solver holds the cube for scrambles and cases in every
  // mode (scrambles are always applied white top, green front).
  ColourNeutrality neutrality;

  // Timer
  int holdMs = 300;
  bool inspection = false;
  int decimals = 2;
  // Timed and Untimed on the virtual cube: the solve ends as soon as the
  // cube is solved, without Space.
  bool autoStop = true;
  // Timed on the virtual cube with inspection: the cube is hidden until
  // inspection starts, so it can't be studied before.
  bool hideBeforeInspection = true;
  // Solves (Timed, Untimed, Move Count)
  // Move counts are taken after simplify(): U D U' counts as D, R x R' as
  // nothing. Off: every turn as done counts.
  bool simplifiedCounts = true;
  // Move Count's Opt column: the optimum in any hold, as the step is
  // checked; off: only in the holds of the colour neutrality.
  bool optimalAnyHold = true;

  // Spaced repetition
  SrsConfig srs;
  // A case whose mean recognition time is over this is slow to recognise.
  int slowRecognitionMs = 1500;

  static AppSettings load();
  void save() const;
};

void applyTheme(QApplication &app, bool dark);

// Settings key for a per-alg-set choice shared by Learn, Drill and the
// Library, e.g. "trainer/sets/pll/unticked".
QString setSettingsKey(const std::string &setId, const QString &name);
// The cases unticked for an alg set in Learn and Drill; every other case is
// ticked, so that cases added to the set later come ticked. Ticks saved by
// earlier versions (the ticked cases) are turned into this the first time.
QStringList untickedCases(const AlgSet &set);
void setUntickedCases(const std::string &setId, const QStringList &caseIds);

// "1m 10m" <-> seconds.
QString stepsToText(const std::vector<std::int64_t> &steps);
std::vector<std::int64_t> stepsFromText(const QString &text);

} // namespace vcube
