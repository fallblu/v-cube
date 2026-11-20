#pragma once

#include <QByteArray>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <string>

namespace vcube {

// One value the app remembers in QSettings, with its key and the value it
// has when nothing was saved yet. The preferences of the Settings dialog
// are AppSettings; this is everything else: the window's layout, the last
// choices in each mode, and so on. Most keys are named below, so that what
// is stored where can be seen in one place.
template <typename T> class Stored {
public:
  Stored(QString key, T fallback = T())
      : key_(std::move(key)), fallback_(std::move(fallback)) {}

  T get() const {
    QVariant v = QSettings().value(key_);
    return v.isValid() ? v.value<T>() : fallback_;
  }
  // A value already there isn't written again: a QSettings left with a
  // change rewrites the whole file.
  void set(const T &value) const {
    QSettings q;
    if (QVariant v = q.value(key_); !v.isValid() || v.value<T>() != value) {
      q.setValue(key_, QVariant::fromValue(value));
    }
  }
  bool exists() const { return QSettings().contains(key_); }
  const QString &key() const { return key_; }

private:
  QString key_;
  T fallback_;
};

// A value kept as it is stored, whatever its type: converting a QVariant
// to a QVariant is what MSVC calls unreachable code in Qt's header.
template <> inline QVariant Stored<QVariant>::get() const {
  QVariant v = QSettings().value(key_);
  return v.isValid() ? v : fallback_;
}
template <> inline void Stored<QVariant>::set(const QVariant &value) const {
  QSettings().setValue(key_, value);
}

// Settings key for a per-alg-set choice shared by Learn, Drill and the
// Library, e.g. "trainer/sets/pll/unticked".
inline QString setSettingsKey(const std::string &setId, const QString &name) {
  return QString("trainer/sets/%1/%2").arg(QString::fromStdString(setId), name);
}

namespace saved {

// --- the window
inline Stored<QByteArray> windowGeometry() { return {"window/geometry"}; }
inline Stored<QByteArray> windowState() { return {"window/state"}; }
inline Stored<bool> sidebarShown() { return {"window/sidebar", true}; }
// The mode last shown (a settings id, see AppMode).
inline Stored<QString> windowMode() { return {"window/mode"}; }
// The tab earlier versions saved instead of a mode (-1: none).
inline Stored<int> legacyTab() { return {"window/tab", -1}; }
// A page's splitter, by mode id and the splitter's index on the page.
inline Stored<QByteArray> splitter(const QString &modeId, int index) {
  return {QString("window/splitters/%1/%2").arg(modeId).arg(index)};
}
// A splitter's sizes by its name (its objectName), which stays the same
// when a page gains or loses a splitter; the index is how they were kept
// before.
inline Stored<QByteArray> splitter(const QString &modeId, const QString &name) {
  return {QString("window/splitters/%1/%2").arg(modeId, name)};
}
Stored<QString> exportFolder();
// The settings file, e.g. for Export all data.
QString settingsFile();
// Writes the settings changed since they were last written, and says why
// when they can't be (a folder that can't be written to, a full disk);
// empty when they are in the file. They are written without this as well,
// but with nothing said when that fails.
QString settingsWriteError();
// Drops the keys earlier versions used that nothing reads any more.
void removeObsolete();

// --- Learn and Drill (`mode` is "learn" or "drill")
// The set drop-down's entry for reviewing every set at once.
inline constexpr const char *kAcrossSetsId = "*across*";
// The set last chosen, or kAcrossSetsId; earlier versions kept one set for
// both modes.
QString caseSet(const QString &mode);
void setCaseSet(const QString &mode, const QString &setId);
// The set shown while reviewing across sets.
inline Stored<QString> shownSet(const QString &mode) {
  return {mode + "/shownSet"};
}
inline Stored<int> sortColumn(const QString &mode) {
  return {mode + "/sortColumn", -1};
}
inline Stored<bool> sortAscending(const QString &mode) {
  return {mode + "/sortAscending", false};
}
inline Stored<QStringList> hiddenColumns(const QString &mode,
                                         const QStringList &fallback) {
  return {mode + "/hiddenColumns", fallback};
}

// --- Learn
inline Stored<QByteArray> learnSplitter() { return {"learn/splitter"}; }
inline Stored<QByteArray> learnRightSplitter() {
  return {"learn/rightSplitter"};
}
// A mode's side panel (its list or session) is hidden.
inline Stored<bool> sideHidden(const QString &modeId) {
  // Learn's list was the first that could be hidden: its key is kept.
  return {modeId == "learn" ? QStringLiteral("learn/listHidden")
                            : "window/sideHidden/" + modeId,
          false};
}
// How reviews are done: "solve", "recognise" or "quiz".
inline Stored<QString> learnPractice() { return {"learn/practice", "solve"}; }
// Per set: the order new cases come in, the cube's view, and the seed of
// a shuffled order.
inline Stored<QString> learnOrder(const std::string &setId) {
  return {setSettingsKey(setId, "learnOrder"), "list"};
}
inline Stored<QString> learnView(const std::string &setId) {
  return {setSettingsKey(setId, "learnView")};
}
inline Stored<QString> learnSeed(const std::string &setId) {
  return {setSettingsKey(setId, "learnSeed")};
}

// --- Drill, per set: the cube, the cases and their order.
inline Stored<QString> drillCube(const std::string &setId) {
  return {setSettingsKey(setId, "drillCube"), "virtual"};
}
inline Stored<QString> drillScope(const std::string &setId) {
  return {setSettingsKey(setId, "drillScope"), "ticked"};
}
inline Stored<QString> drillPool(const std::string &setId) {
  return {setSettingsKey(setId, "drillPool"), "learned"};
}
inline Stored<QString> drillOrder(const std::string &setId) {
  return {setSettingsKey(setId, "drillOrder"), "random"};
}

// --- a set's own settings (ticks, Learn and Drill choices) as a whole,
// e.g. to bring them back with a deleted set.
QVariantMap setSettings(const std::string &setId);
void setSetSettings(const std::string &setId, const QVariantMap &values);
void removeSetSettings(const std::string &setId);

// --- shared widgets, under a key their page gives
// A cube view's mode (see CubeView::modeName()), by page.
inline Stored<QString> cubeViewMode(const QString &page) {
  return {"ui/view/" + page};
}
// A trend chart: the solves it covers (0: all) and whether it shows them.
inline Stored<int> chartLast(const QString &key) { return {key + "/last", 0}; }
inline Stored<bool> chartSeries(const QString &key) {
  return {key + "/series", false};
}
// A scramble source's "Show algs".
inline Stored<bool> showAlgs(const QString &key) { return {key, false}; }

// --- the solve review
inline Stored<QString> reviewPairSet() { return {"review/pairSet", "zz-f2l"}; }

// --- the command list: the commands last run, newest first
inline Stored<QStringList> recentCommands() { return {"commandList/recent"}; }

// --- Timed
inline Stored<QStringList> timedHiddenColumns() {
  return {"timed/hiddenColumns"};
}
// A session table's hidden columns, under the key its page gives.
inline Stored<QStringList> hiddenSessionColumns(const QString &key) {
  return {key};
}

// Alg Lab's choices, kept as "alglab/<name>": source, case, tab, filter,
// noRotations, maxHtm, statsCube, statsWindow (the Search tab), moveSet,
// lengthMode, extra, maxLength, maxResults, generatorShown (the generator)
// and scoreProfile (the profile the Score column uses).
inline Stored<QVariant> lab(const char *name, const QVariant &fallback = {}) {
  return {QStringLiteral("alglab/") + name, fallback};
}

// The choices of EO Cross, F2L and TSLE mode, kept as "<mode>/<name>" (mode
// "eocross", "f2l" or "tsle"). Every one has tab (0 Learn, 1 Timed), scope
// (Learn's: 0 one unit, 1 the whole step) and physical (Timed's cube);
// each mode adds its own (EO Cross: step, blind; F2L: method, leave,
// dMoves, halfFB, wide; TSLE: slots).
inline Stored<QVariant> practice(const QString &mode, const char *name,
                                 const QVariant &fallback = {}) {
  return {mode + "/" + name, fallback};
}

} // namespace saved

} // namespace vcube
