#include "app/mode_table.hpp"
#include "app/ui_text.hpp"

namespace vcube {

namespace {

// The window's own context, which MainWindow::tr() uses too.
constexpr char kTrContext[] = "vcube::MainWindow";
constexpr auto tr = trIn<kTrContext>;

struct ModeInfo {
  const char *id; // settings value, e.g. "learn"
  const char *label;
};

constexpr std::array<ModeInfo, AppMode::Count> kModes = {{
    {"learn", QT_TRANSLATE_NOOP("vcube::MainWindow", "Learn")},
    {"drill", QT_TRANSLATE_NOOP("vcube::MainWindow", "Drill")},
    {"library", QT_TRANSLATE_NOOP("vcube::MainWindow", "Library")},
    {"timed", QT_TRANSLATE_NOOP("vcube::MainWindow", "Timed")},
    {"untimed", QT_TRANSLATE_NOOP("vcube::MainWindow", "Untimed")},
    {"movecount", QT_TRANSLATE_NOOP("vcube::MainWindow", "Move Count")},
    {"eocross", QT_TRANSLATE_NOOP("vcube::MainWindow", "EO Cross")},
    {"alglab", QT_TRANSLATE_NOOP("vcube::MainWindow", "Alg Lab")},
    {"f2l", QT_TRANSLATE_NOOP("vcube::MainWindow", "F2L")},
    {"tsle", QT_TRANSLATE_NOOP("vcube::MainWindow", "TSLE")},
    {"today", QT_TRANSLATE_NOOP("vcube::MainWindow", "Today")},
}};

constexpr std::array<AppMode::Id, AppMode::Count> kAll = {
    AppMode::Learn,   AppMode::Drill,     AppMode::Library, AppMode::Timed,
    AppMode::Untimed, AppMode::MoveCount, AppMode::EoCross, AppMode::AlgLab,
    AppMode::F2L,     AppMode::Tsle,      AppMode::Today};

constexpr std::array<AppMode::Id, AppMode::Count> kSidebarOrder = {
    AppMode::Today,   AppMode::Learn, AppMode::Drill,   AppMode::Library,
    AppMode::AlgLab,  AppMode::Timed, AppMode::Untimed, AppMode::MoveCount,
    AppMode::EoCross, AppMode::F2L,   AppMode::Tsle};

// A short initialiser would fill the rest with Learn (or null ids), and a
// mode would be missing without a compiler warning.
constexpr bool isPermutation(const std::array<AppMode::Id, AppMode::Count> &a) {
  std::array<bool, AppMode::Count> seen{};
  for (AppMode::Id mode : a) {
    const auto i = static_cast<std::size_t>(mode);
    if (i >= seen.size() || seen[i]) {
      return false;
    }
    seen[i] = true;
  }
  return true;
}

constexpr bool everyModeNamed() {
  for (const ModeInfo &mode : kModes) {
    if (mode.id == nullptr || mode.label == nullptr) {
      return false;
    }
  }
  return true;
}

static_assert(isPermutation(kAll));
static_assert(isPermutation(kSidebarOrder));
static_assert(everyModeNamed());

constexpr bool inEnumOrder() {
  for (std::size_t i = 0; i < kAll.size(); ++i) {
    if (static_cast<std::size_t>(kAll[i]) != i) {
      return false;
    }
  }
  return true;
}
static_assert(inEnumOrder());

const ModeInfo &info(AppMode::Id mode) {
  return kModes[static_cast<std::size_t>(mode)];
}

} // namespace

const std::array<AppMode::Id, AppMode::Count> &AppMode::all() { return kAll; }

std::optional<AppMode::Id> AppMode::fromIndex(int index) {
  if (index < 0 || index >= Count) {
    return std::nullopt;
  }
  return all()[static_cast<std::size_t>(index)];
}

const std::array<AppMode::Id, AppMode::Count> &AppMode::sidebarOrder() {
  return kSidebarOrder;
}

QString AppMode::sidebarHeader(Id mode) {
  switch (mode) {
  case Learn:
    return tr("ALGS");
  case Timed:
    return tr("SOLVES");
  case EoCross:
    return tr("PRACTICE");
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

QString AppMode::label(Id mode) { return tr(info(mode).label); }

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
