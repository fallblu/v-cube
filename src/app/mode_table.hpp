#pragma once

#include <QKeySequence>
#include <QString>

#include <array>
#include <optional>

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
    Today
  };
  // Not an enumerator, so that a switch over the modes has no case for it.
  static constexpr int Count = Today + 1;

  // Every mode, in the order of the enum.
  static const std::array<Id, Count> &all();
  // The order of the sidebar and the Mode menu: Today first.
  static const std::array<Id, Count> &sidebarOrder();
  // The mode a widget or a signal carries as a number, if it is one.
  static std::optional<Id> fromIndex(int index);
  // The heading above the mode in the sidebar ("ALGS", "SOLVES",
  // "PRACTICE"), or empty.
  static QString sidebarHeader(Id mode);
  // A separator follows the mode in the Mode menu.
  static bool menuSeparatorAfter(Id mode);
  // The mode's name, translated.
  static QString label(Id mode);
  // Its id in settings, e.g. "learn".
  static QString settingsId(Id mode);
  static QKeySequence shortcut(Id mode);
  // The mode with that settings id, also those of earlier versions
  // ("timer", "scramble", "virtual", "inspection").
  static std::optional<Id> fromSettingsId(const QString &id);
  // Earlier versions saved a tab instead: Timer, Trainer or Library.
  static std::optional<Id> fromSavedTab(int tab);
};

} // namespace vcube
