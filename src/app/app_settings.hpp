#pragma once

#include "app/colours.hpp"
#include "app/drill_settings.hpp"
#include "app/keymap.hpp"
#include "train/srs.hpp"

#include <QFlags>
#include <QStringList>

#include <cstdint>
#include <string>
#include <vector>

class QApplication;

namespace vcube {

struct AlgSet;

// Makes QSettings use an INI file on every platform
// (~/.config/v-cube/v-cube.ini, %APPDATA%\v-cube\v-cube.ini on Windows)
// and copies settings over from the native store used by earlier versions
// when `migrate` is set (not for a separate --data-dir profile). Call once,
// after the organization and application names are set.
void initSettingsStorage(bool migrate = true);

// Application-wide preferences, kept in QSettings.
struct AppSettings {
  bool darkTheme = true;
  ColourScheme scheme = ColourScheme::standard();
  int animationMs = 110;
  Keymap keymap = Keymap::defaultLayout();
  // Key bindings in the settings file that couldn't be read when loaded
  // (not saved), to say so once.
  int droppedKeyBindings = 0;
  // Which way the solver holds the cube for scrambles and cases in every
  // mode (scrambles are always applied white top, green front).
  ColourNeutrality neutrality;

  // Text sizes: all the interface's text (per cent of the system font),
  // the scramble (points), the hints and status messages (per cent of the
  // interface text) and the case thumbnails in Learn and Drill (pixels).
  int interfaceTextPercent = 100;
  int scrambleFontPt = 17;
  int hintTextPercent = 120;
  int thumbnailSize = 38;

  // Timer
  int holdMs = 300;
  bool inspection = false;
  // A beep at 8 s and two at 12 s of inspection, like a judge's calls.
  bool inspectionSounds = false;
  int decimals = 2;
  int timerFontPt = 54;
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

  // Drill mode
  DrillSettings drill;

  // Allowed ranges; values outside them are brought into range on load.
  static constexpr int kMaxAnimationMs = 500;
  static constexpr int kMaxHoldMs = 2000;
  static constexpr int kMinScrambleFontPt = 9, kMaxScrambleFontPt = 40;
  static constexpr int kMinTimerFontPt = 20, kMaxTimerFontPt = 150;
  static constexpr int kMinHintPercent = 80, kMaxHintPercent = 200;
  static constexpr int kMinInterfacePercent = 80, kMaxInterfacePercent = 200;
  static constexpr int kMinThumbnail = 32, kMaxThumbnail = 96;

  static AppSettings load();
  void save() const;
};

// The parts of AppSettings, so that a change tells only the pages that
// use the part changed (see ModePage::settingsUsed()).
enum class SettingsPart : unsigned {
  Theme = 1 << 0,      // darkTheme
  Cube = 1 << 1,       // scheme, animationMs
  Keys = 1 << 2,       // keymap
  Neutrality = 1 << 3, // neutrality
  // interfaceTextPercent, scrambleFontPt, hintTextPercent, timerFontPt
  Text = 1 << 4,
  Thumbnails = 1 << 5, // thumbnailSize
  // holdMs, inspection, inspectionSounds, decimals, autoStop,
  // hideBeforeInspection
  Timer = 1 << 6,
  Counts = 1 << 7, // simplifiedCounts, optimalAnyHold
  Srs = 1 << 8,    // srs, slowRecognitionMs
  Drill = 1 << 9,  // drill
  All = (1 << 10) - 1,
};
Q_DECLARE_FLAGS(SettingsParts, SettingsPart)
Q_DECLARE_OPERATORS_FOR_FLAGS(SettingsParts)

// The parts that differ between two settings.
SettingsParts changedParts(const AppSettings &a, const AppSettings &b);

// Sets the application style and a dark or a light palette (an explicit
// one either way, so that "Light" stays light on a dark desktop).
void applyTheme(QApplication &app, bool dark);

// The cases unticked for an alg set in Learn and Drill; every other case is
// ticked, so that cases added to the set later come ticked. Ticks saved by
// earlier versions (the ticked cases) are turned into this the first time.
QStringList untickedCases(const AlgSet &set);
void setUntickedCases(const std::string &setId, const QStringList &caseIds);

// "1m 10m" <-> seconds.
QString stepsToText(const std::vector<std::int64_t> &steps);
std::vector<std::int64_t> stepsFromText(const QString &text);

} // namespace vcube
