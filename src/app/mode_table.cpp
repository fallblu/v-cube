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
    {"eocross", QT_TRANSLATE_NOOP("MainWindow", "EO Cross")},
    {"alglab", QT_TRANSLATE_NOOP("MainWindow", "Alg Lab")},
    {"f2l", QT_TRANSLATE_NOOP("MainWindow", "F2L")},
    {"tsle", QT_TRANSLATE_NOOP("MainWindow", "TSLE")},
    {"today", QT_TRANSLATE_NOOP("MainWindow", "Today")},
}};

const ModeInfo &info(AppMode::Id mode) {
  return kModes[static_cast<std::size_t>(mode)];
}

} // namespace

const std::array<AppMode::Id, AppMode::Count> &AppMode::all() {
  static constexpr std::array<Id, Count> modes = {
      Learn,   Drill,  Library, Timed, Untimed, MoveCount,
      EoCross, AlgLab, F2L,     Tsle,  Today};
  return modes;
}

std::optional<AppMode::Id> AppMode::fromIndex(int index) {
  if (index < 0 || index >= Count) {
    return std::nullopt;
  }
  return all()[static_cast<std::size_t>(index)];
}

const std::array<AppMode::Id, AppMode::Count> &AppMode::sidebarOrder() {
  static constexpr std::array<Id, Count> order = {
      Today,   Learn,     Drill,   Library, AlgLab, Timed,
      Untimed, MoveCount, EoCross, F2L,     Tsle};
  return order;
}

QString AppMode::sidebarHeader(Id mode) {
  switch (mode) {
  case Learn:
    return QCoreApplication::translate("vcube::MainWindow", "ALGS");
  case Timed:
    return QCoreApplication::translate("vcube::MainWindow", "SOLVES");
  case EoCross:
    return QCoreApplication::translate("vcube::MainWindow", "PRACTICE");
  case Drill:
  case Library:
  case Untimed:
  case MoveCount:
  case AlgLab:
  case F2L:
  case Tsle:
  case Today:
    return {};
  }
  return {};
}

bool AppMode::menuSeparatorAfter(Id mode) {
  return mode == Today || mode == AlgLab || mode == MoveCount;
}

QString AppMode::label(Id mode) {
  return QCoreApplication::translate("MainWindow", info(mode).label);
}

QString AppMode::settingsId(Id mode) { return info(mode).id; }

QKeySequence AppMode::shortcut(Id mode) {
  // The digit is the mode's place in the sidebar: Today 0, Learn 1, ...
  // and the key after 9 on the keyboard, -, for the eleventh (= would be
  // a twelfth's).
  const auto &order = sidebarOrder();
  for (std::size_t i = 0; i < order.size(); ++i) {
    if (order[i] == mode) {
      Qt::Key key = i < 10
                        ? static_cast<Qt::Key>(Qt::Key_0 + static_cast<int>(i))
                    : i == 10 ? Qt::Key_Minus
                              : Qt::Key_Equal;
      return QKeySequence(Qt::CTRL | key);
    }
  }
  return {};
}

std::optional<AppMode::Id> AppMode::fromSettingsId(const QString &id) {
  for (Id mode : all()) {
    if (id == info(mode).id) {
      return mode;
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
  // Inspection's drills are EO Cross mode's now.
  if (id == "inspection") {
    return EoCross;
  }
  return std::nullopt;
}

std::optional<AppMode::Id> AppMode::fromSavedTab(int tab) {
  constexpr std::array<Id, 3> fromTab = {Timed, Drill, Library};
  if (tab < 0 || tab >= 3) {
    return std::nullopt;
  }
  return fromTab[static_cast<std::size_t>(tab)];
}

} // namespace vcube
