#pragma once

#include <QKeySequence>
#include <QString>

#include <array>

namespace vcube {

// The main window's modes, one page each.
struct AppMode {
  // The order of the pages and of the Today page's activity. The sidebar
  // has its own order (sidebarOrder), which Ctrl+0 to Ctrl+9 and Ctrl+-
  // follow.
  enum Id : int {
    Learn,
    Drill,
    Library,
    Timed,
    Untimed,
    MoveCount,
    EoCross,
    AlgLab,
    F2L,
    Tsle,
    Today,
    Count
  };

  // The order of the sidebar and the Mode menu: Today first.
  static const std::array<Id, Count> &sidebarOrder();
  // The heading above the mode in the sidebar ("ALGS", "SOLVES",
  // "PRACTICE"), or empty.
  static QString sidebarHeader(int mode);
  // A separator follows the mode in the Mode menu.
  static bool menuSeparatorAfter(int mode);
  // The mode's name, translated.
  static QString label(int mode);
  // Its id in settings, e.g. "learn".
  static QString settingsId(int mode);
  static QKeySequence shortcut(int mode);
  // The mode with that settings id, also those of earlier versions
  // ("timer", "scramble", "virtual", "inspection"); -1 for none.
  static int fromSettingsId(const QString &id);
  // Earlier versions saved a tab instead: Timer, Trainer or Library.
  static int fromSavedTab(int tab);
};

} // namespace vcube
