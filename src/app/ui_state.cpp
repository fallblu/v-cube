#include "app/ui_state.hpp"

#include <QDir>

namespace vcube::saved {

namespace {

// The settings group that holds a set's own settings (ticked cases, Drill
// and Learn choices).
QString setGroup(const std::string &setId) {
  return setSettingsKey(setId, QString()).chopped(1);
}

} // namespace

Stored<QString> exportFolder() { return {"export/folder", QDir::homePath()}; }

QString settingsFile() { return QSettings().fileName(); }

void removeObsolete() {
  QSettings q;
  for (const char *key :
       {"window/tab", "window/focus", "trainer/virtual", "trainer/showScramble",
        "timer/virtual", "timer/cubeMode", "scramble/type", "scramble/showAlgs",
        "virtual/scrambleType", "virtual/showAlgs", "ui/view/virtual",
        // The drills EO Cross, F2L and TSLE had before their Learn and
        // Timed tabs.
        "eocross/drill", "eocross/bad", "eocross/axes", "eocross/limit",
        "f2l/drill", "f2l/stage", "f2l/slot", "f2l/context", "tsle/drill",
        "tsle/allCases", "tsle/slot"}) {
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

} // namespace vcube::saved
