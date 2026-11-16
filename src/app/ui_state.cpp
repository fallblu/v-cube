#include "app/ui_state.hpp"

#include "app/app_settings.hpp"

#include <QDir>

namespace vcube::saved {

namespace {

// The settings group that holds a set's own settings (ticked cases, Drill
// and Learn choices).
QString setGroup(const std::string &setId) {
  return setSettingsKey(setId, QString()).chopped(1);
}

} // namespace

Stored<QByteArray> windowGeometry() { return {"window/geometry"}; }
Stored<QByteArray> windowState() { return {"window/state"}; }
Stored<bool> sidebarShown() { return {"window/sidebar", true}; }
Stored<QString> windowMode() { return {"window/mode"}; }
Stored<int> legacyTab() { return {"window/tab", -1}; }

Stored<QByteArray> splitter(const QString &modeId, int index) {
  return {QString("window/splitters/%1/%2").arg(modeId).arg(index)};
}
Stored<QByteArray> splitter(const QString &modeId, const QString &name) {
  return {QString("window/splitters/%1/%2").arg(modeId, name)};
}

Stored<QString> exportFolder() { return {"export/folder", QDir::homePath()}; }

QString settingsFile() { return QSettings().fileName(); }

void removeObsolete() {
  QSettings q;
  for (const char *key :
       {"window/tab", "window/focus", "trainer/virtual", "trainer/showScramble",
        "timer/virtual", "timer/cubeMode", "scramble/type", "scramble/showAlgs",
        "virtual/scrambleType", "virtual/showAlgs", "ui/view/virtual"}) {
    q.remove(key);
  }
}

QString caseSet(const QString &mode) {
  QSettings q;
  return q.value(mode + "/set", q.value("trainer/set")).toString();
}

void setCaseSet(const QString &mode, const QString &setId) {
  QSettings().setValue(mode + "/set", setId);
}

Stored<QString> shownSet(const QString &mode) { return {mode + "/shownSet"}; }
Stored<int> sortColumn(const QString &mode) {
  return {mode + "/sortColumn", -1};
}
Stored<bool> sortAscending(const QString &mode) {
  return {mode + "/sortAscending", false};
}
Stored<QStringList> hiddenColumns(const QString &mode,
                                  const QStringList &fallback) {
  return {mode + "/hiddenColumns", fallback};
}

Stored<QByteArray> learnSplitter() { return {"learn/splitter"}; }
Stored<QByteArray> learnRightSplitter() { return {"learn/rightSplitter"}; }
Stored<bool> learnListHidden() { return {"learn/listHidden", false}; }
Stored<bool> sideHidden(const QString &modeId) {
  // Learn's list was the first that could be hidden: its key is kept.
  return {modeId == "learn" ? QStringLiteral("learn/listHidden")
                            : "window/sideHidden/" + modeId,
          false};
}
Stored<QString> learnPractice() { return {"learn/practice", "solve"}; }
Stored<QString> learnOrder(const std::string &setId) {
  return {setSettingsKey(setId, "learnOrder"), "list"};
}
Stored<QString> learnView(const std::string &setId) {
  return {setSettingsKey(setId, "learnView")};
}
Stored<QString> learnSeed(const std::string &setId) {
  return {setSettingsKey(setId, "learnSeed")};
}

Stored<QString> drillCube(const std::string &setId) {
  return {setSettingsKey(setId, "drillCube"), "virtual"};
}
Stored<QString> drillScope(const std::string &setId) {
  return {setSettingsKey(setId, "drillScope"), "ticked"};
}
Stored<QString> drillPool(const std::string &setId) {
  return {setSettingsKey(setId, "drillPool"), "learned"};
}
Stored<QString> drillOrder(const std::string &setId) {
  return {setSettingsKey(setId, "drillOrder"), "random"};
}

QVariantMap setSettings(const std::string &setId) {
  QSettings q;
  q.beginGroup(setGroup(setId));
  QVariantMap out;
  for (const QString &key : q.allKeys()) {
    out[key] = q.value(key);
  }
  return out;
}

void setSetSettings(const std::string &setId, const QVariantMap &values) {
  QSettings q;
  q.remove(setGroup(setId));
  q.beginGroup(setGroup(setId));
  for (auto it = values.begin(); it != values.end(); ++it) {
    q.setValue(it.key(), it.value());
  }
}

void removeSetSettings(const std::string &setId) {
  QSettings().remove(setGroup(setId));
}

Stored<QString> cubeViewMode(const QString &page) {
  return {"ui/view/" + page};
}
Stored<int> chartLast(const QString &key) { return {key + "/last", 0}; }
Stored<bool> chartSeries(const QString &key) {
  return {key + "/series", false};
}
Stored<bool> showAlgs(const QString &key) { return {key, false}; }

Stored<QString> reviewPairSet() { return {"review/pairSet", "zz-f2l"}; }

Stored<QStringList> timedHiddenColumns() { return {"timed/hiddenColumns"}; }
Stored<QStringList> hiddenSessionColumns(const QString &key) { return {key}; }
Stored<int> inspectionChart() { return {"inspection/chart", 0}; }
Stored<int> blindDrill() { return {"blind/drill", 0}; }
Stored<int> blindQuizPieces() { return {"blind/quizPieces", 0}; }
Stored<int> blindQuizDirection() { return {"blind/quizDirection", 0}; }
Stored<int> blindSetupPieces() { return {"blind/setupPieces", 0}; }
Stored<int> blindSetupMode() { return {"blind/setupMode", 0}; }
Stored<QStringList> blindList(const QString &name) { return {"blind/" + name}; }
Stored<int> blindHoldTop(int fallback) { return {"blind/top", fallback}; }
Stored<int> blindHoldFront(int fallback) { return {"blind/front", fallback}; }

Stored<QVariant> lab(const char *name, const QVariant &fallback) {
  return {QStringLiteral("alglab/") + name, fallback};
}

Stored<QVariant> f2l(const char *name, const QVariant &fallback) {
  return {QStringLiteral("f2l/") + name, fallback};
}

Stored<QVariant> tsle(const char *name, const QVariant &fallback) {
  return {QStringLiteral("tsle/") + name, fallback};
}

} // namespace vcube::saved
