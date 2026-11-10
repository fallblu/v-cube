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
// choices in each mode, and so on. Every key is named in ui_state.cpp, so
// that what is stored where can be seen in one place.
template <typename T> class Stored {
public:
  Stored(QString key, T fallback = T())
      : key_(std::move(key)), fallback_(std::move(fallback)) {}

  T get() const {
    QVariant v = QSettings().value(key_);
    return v.isValid() ? v.value<T>() : fallback_;
  }
  void set(const T &value) const {
    QSettings().setValue(key_, QVariant::fromValue(value));
  }
  bool exists() const { return QSettings().contains(key_); }
  const QString &key() const { return key_; }

private:
  QString key_;
  T fallback_;
};

namespace saved {

// --- the window
Stored<QByteArray> windowGeometry();
Stored<QByteArray> windowState();
Stored<bool> sidebarShown();
// The mode last shown (a settings id, see AppMode).
Stored<QString> windowMode();
// The tab earlier versions saved instead of a mode (-1: none).
Stored<int> legacyTab();
// A page's splitter, by mode id and the splitter's index on the page.
Stored<QByteArray> splitter(const QString &modeId, int index);
Stored<QString> exportFolder();
// The settings file, e.g. for Export all data.
QString settingsFile();
// Drops the keys earlier versions used that nothing reads any more.
void removeObsolete();

// --- Learn and Drill (`mode` is "learn" or "drill")
// The set last chosen, or CasePage::kAcrossSetsId; earlier versions kept
// one set for both modes.
QString caseSet(const QString &mode);
void setCaseSet(const QString &mode, const QString &setId);
// The set shown while reviewing across sets.
Stored<QString> shownSet(const QString &mode);
Stored<int> sortColumn(const QString &mode);
Stored<bool> sortAscending(const QString &mode);
Stored<QStringList> hiddenColumns(const QString &mode,
                                  const QStringList &fallback);

// --- Learn
Stored<QByteArray> learnSplitter();
Stored<QByteArray> learnRightSplitter();
Stored<bool> learnListHidden();
// How reviews are done: "solve", "recognise" or "quiz".
Stored<QString> learnPractice();
// Per set: the order new cases come in, the cube's view, and the seed of
// a shuffled order.
Stored<QString> learnOrder(const std::string &setId);
Stored<QString> learnView(const std::string &setId);
Stored<QString> learnSeed(const std::string &setId);

// --- Drill, per set: the cube, the cases and their order.
Stored<QString> drillCube(const std::string &setId);
Stored<QString> drillScope(const std::string &setId);
Stored<QString> drillPool(const std::string &setId);
Stored<QString> drillOrder(const std::string &setId);

// --- a set's own settings (ticks, Learn and Drill choices) as a whole,
// e.g. to bring them back with a deleted set.
QVariantMap setSettings(const std::string &setId);
void setSetSettings(const std::string &setId, const QVariantMap &values);
void removeSetSettings(const std::string &setId);

// --- shared widgets, under a key their page gives
// A cube view's mode (see CubeView::modeName()), by page.
Stored<QString> cubeViewMode(const QString &page);
// A trend chart: the solves it covers (0: all) and whether it shows them.
Stored<int> chartLast(const QString &key);
Stored<bool> chartSeries(const QString &key);
// A scramble source's "Show algs".
Stored<bool> showAlgs(const QString &key);

// --- the solve review
Stored<QString> reviewPairSet();

// --- Timed, Inspection and Blind
Stored<QStringList> timedHiddenColumns();
// A session table's hidden columns, under the key its page gives.
Stored<QStringList> hiddenSessionColumns(const QString &key);
Stored<int> inspectionChart();
Stored<int> blindDrill();
Stored<int> blindQuizPieces();
Stored<int> blindQuizDirection();
Stored<int> blindSetupPieces();
Stored<int> blindSetupMode();
// The letter scheme and setups (lists of 24) and the blind hold.
Stored<QStringList> blindList(const QString &name);
Stored<int> blindHoldTop(int fallback);
Stored<int> blindHoldFront(int fallback);

} // namespace saved

} // namespace vcube
