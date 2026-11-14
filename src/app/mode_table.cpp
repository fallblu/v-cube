#include "app/mode_table.hpp"

#include <QCoreApplication>

namespace vcube {

namespace {

struct ModeInfo {
  const char *id; // settings value, e.g. "learn"
  const char *label;
};

constexpr std::array<ModeInfo, AppMode::Count> kModes = {{
    {"learn", QT_TRANSLATE_NOOP("MainWindow", "Learn")},
    {"drill", QT_TRANSLATE_NOOP("MainWindow", "Drill")},
    {"library", QT_TRANSLATE_NOOP("MainWindow", "Library")},
    {"timed", QT_TRANSLATE_NOOP("MainWindow", "Timed")},
    {"untimed", QT_TRANSLATE_NOOP("MainWindow", "Untimed")},
    {"movecount", QT_TRANSLATE_NOOP("MainWindow", "Move Count")},
    {"inspection", QT_TRANSLATE_NOOP("MainWindow", "Inspection")},
    {"blind", QT_TRANSLATE_NOOP("MainWindow", "Blind")},
    {"alglab", QT_TRANSLATE_NOOP("MainWindow", "Alg Lab")},
    {"today", QT_TRANSLATE_NOOP("MainWindow", "Today")},
}};

const ModeInfo &info(int mode) {
  return kModes[static_cast<std::size_t>(mode)];
}

} // namespace

const std::array<AppMode::Id, AppMode::Count> &AppMode::sidebarOrder() {
  static constexpr std::array<Id, Count> order = {
      Today, Learn,   Drill,     Library,    AlgLab,
      Timed, Untimed, MoveCount, Inspection, Blind};
  return order;
}

QString AppMode::sidebarHeader(int mode) {
  switch (mode) {
  case Learn:
    return QCoreApplication::translate("vcube::MainWindow", "ALGS");
  case Timed:
    return QCoreApplication::translate("vcube::MainWindow", "SOLVES");
  case Inspection:
    return QCoreApplication::translate("vcube::MainWindow", "PRACTICE");
  default:
    return {};
  }
}

bool AppMode::menuSeparatorAfter(int mode) {
  return mode == Today || mode == AlgLab || mode == MoveCount;
}

QString AppMode::label(int mode) {
  return QCoreApplication::translate("MainWindow", info(mode).label);
}

QString AppMode::settingsId(int mode) { return info(mode).id; }

QKeySequence AppMode::shortcut(int mode) {
  // The digit is the mode's place in the sidebar: Today 0, Learn 1, ...
  const auto &order = sidebarOrder();
  for (std::size_t i = 0; i < order.size(); ++i) {
    if (order[i] == mode) {
      return QKeySequence(
          Qt::CTRL | static_cast<Qt::Key>(Qt::Key_0 + static_cast<int>(i)));
    }
  }
  return {};
}

int AppMode::fromSettingsId(const QString &id) {
  for (std::size_t i = 0; i < kModes.size(); ++i) {
    if (id == kModes[i].id) {
      return static_cast<int>(i);
    }
  }
  // Modes of earlier versions: Timer became Timed; Scramble and Virtual
  // became Untimed.
  if (id == "timer") {
    return Timed;
  }
  if (id == "scramble" || id == "virtual") {
    return Untimed;
  }
  return -1;
}

int AppMode::fromSavedTab(int tab) {
  constexpr std::array<int, 3> fromTab = {Timed, Drill, Library};
  return tab >= 0 && tab < 3 ? fromTab[static_cast<std::size_t>(tab)] : Learn;
}

} // namespace vcube
