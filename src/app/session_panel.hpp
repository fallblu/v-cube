#pragma once

#include "train/stats.hpp"

#include <QAbstractTableModel>
#include <QColor>
#include <QMenu>
#include <QString>
#include <QWidget>

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

class QLabel;
class QPushButton;
class QTableView;
class QToolButton;

namespace vcube {

class TrendChart;

// A column of a session's attempt table.
struct AttemptColumn {
  QString id = {}; // names it in the hidden-columns setting, if any
  QString title;   // the header
  // Sized to its contents; otherwise the columns share the width.
  bool fit = false;
  // Offered in the header's menu, to hide or show.
  bool hideable = false;
  // Shown at all (e.g. only for virtual-cube sessions); offered but
  // disabled in the header's menu when it is hideable.
  bool available = true;
};

// A cell of the attempt table.
struct AttemptCell {
  QString text;
  QColor colour; // the text's, when valid
  bool alignRight = true;
};

// A row of the attempt table: one attempt of the session, by its index.
struct AttemptRow {
  int index = 0;
  std::vector<AttemptCell> cells;
};

// The attempt table's contents: rows of cells, each row for an attempt
// (Qt::UserRole gives its index).
class AttemptTableModel : public QAbstractTableModel {
  Q_OBJECT
public:
  using QAbstractTableModel::QAbstractTableModel;

  void setColumns(std::vector<AttemptColumn> columns);
  void setRows(std::vector<AttemptRow> rows);
  // Adds a row above the others, which stay as they are.
  void prependRow(AttemptRow row);
  const std::vector<AttemptColumn> &columns() const { return columns_; }
  // The attempt in a row, or -1.
  int attemptAt(int row) const;

  int rowCount(const QModelIndex &parent = {}) const override;
  int columnCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role) const override;

private:
  std::vector<AttemptColumn> columns_;
  std::vector<AttemptRow> rows_;
};

// The left column every session page has: the session button and its
// menu, the statistics, the attempt table and (optionally) the delete and
// undo buttons, with the trend chart of the session for the page to place.
class SessionPanel : public QWidget {
  Q_OBJECT
public:
  explicit SessionPanel(QWidget *parent = nullptr);

  // --- the session menu ---
  void setSessionName(const QString &name); // "Session: <name>"
  QToolButton *sessionButton() const { return sessionButton_; }
  // The menu is filled when it opens: the page answers menuAboutToShow()
  // with fillSessionMenu() and may add its own actions to menu() after.
  struct MenuSession {
    QString name;
    std::size_t count = 0;
  };
  void fillSessionMenu(const std::vector<MenuSession> &sessions,
                       std::size_t current);
  QMenu *menu() const { return menu_; }
  // Enables or disables every action of the menu as it is.
  void setMenuEnabled(bool on);

  // --- the statistics ---
  // Clickable statistics (a table of values linked to their solves, see
  // statActivated()) don't wrap; the others do.
  void setStatsClickable(bool on);
  void setStatsHtml(const QString &html);

  // --- the attempt table ---
  void setColumns(std::vector<AttemptColumn> columns);
  void setRows(std::vector<AttemptRow> rows);
  // Adds a row above the others (the newest attempt), which stay as they
  // are.
  void prependRow(AttemptRow row);
  // Lets the header's context menu hide the hideable columns, kept under
  // this QSettings key.
  void setHiddenColumnsKey(const QString &key);

  // For the page's menu in the menu bar: the column chooser of the
  // header's context menu, and each clickable statistic's solves, so that
  // the command list finds them too.
  void fillMenu(QMenu &menu);

  // --- the delete and undo buttons (hidden until shown) ---
  void setDeleteRowVisible(bool on);
  void setDeleteEnabled(bool on);
  void setUndoVisible(bool on);
  void setUndoEnabled(bool on);

  // The session's trend chart, for the page to lay out.
  TrendChart *chart() const { return chart_; }

signals:
  void menuAboutToShow();
  void sessionChosen(std::size_t index);
  void newSessionRequested();
  void renameSessionRequested();
  void deleteSessionRequested();
  void statActivated(const QString &link);
  // An attempt double-clicked, or its context menu asked for.
  void attemptActivated(int index);
  void attemptMenuRequested(int index, const QPoint &globalPos);
  void deleteLastClicked();
  void undoClicked();

private:
  QToolButton *sessionButton_ = nullptr;
  QMenu *menu_ = nullptr;
  QLabel *stats_ = nullptr;
  QTableView *table_ = nullptr;
  AttemptTableModel *model_ = nullptr;
  QWidget *deleteRow_ = nullptr;
  QPushButton *deleteButton_ = nullptr;
  QPushButton *undoButton_ = nullptr;
  TrendChart *chart_ = nullptr;
  QString hiddenKey_;
  bool statsClickable_ = false;

  void applyColumns();
  void showColumnMenu(const QPoint &pos);
  void fillColumnMenu(QMenu &menu);
};

// --- shared pieces of the session pages ------------------------------------

// The statistics table: a row per line of `stats`, named by `label(id)`,
// with the current and the best value as `format(id, value)`. With a
// `linkColour`, each value links to "cur:<id>" or "best:<id>" (coloured as
// the text, so it reads as before but can be clicked).
QString statsTableHtml(
    const SessionStats &stats,
    const std::function<QString(const std::string &id)> &label,
    const std::function<QString(const std::string &id, StatValue v)> &format,
    const QString &linkColour = {});
// A value linking to `href`, when there is a value and a colour.
QString statLinkHtml(const QString &href, StatValue v, const QString &text,
                     const QString &colour);

// Opens what a statistics link stands for ("cur:ao5", "best:mo3",
// "cur:single", "mean:all") over `solves`: a single solve with
// `details(i)`, the others listed (as statListing() does) with their
// results as `result(i)`, the values as `format(v)`, numbered from
// `number(i)` (the number of solve i).
struct StatLinkView {
  std::int64_t unit = kWcaUnit;
  std::function<QString(StatValue)> format;
  std::function<QString(std::size_t)> result;
  std::function<std::size_t(std::size_t)> number;
  std::function<void(std::size_t)> details;
};
void openStatLink(QWidget *parent, const QString &link,
                  std::span<const Solve> solves, const StatLinkView &view);

// Asks for a session name: nullopt when cancelled or left empty, else the
// name trimmed.
std::optional<QString> askSessionName(QWidget *parent, const QString &title,
                                      const QString &label,
                                      const QString &suggested);
// Asks whether to delete a session and its `count` attempts, which the
// question calls `noun` ("solves", "attempts").
bool confirmDeleteSession(QWidget *parent, const QString &name,
                          std::size_t count, const QString &noun);

// What every mode with sessions does to rename and delete the current
// one. `store` has sessions(), currentIndex() and removeSession(); each
// session has a name. The page shows the session again afterwards.

// Asks for a new name for `session`; true if it was renamed.
template <typename Session>
bool renameSessionAsked(QWidget *parent, Session &session) {
  std::optional<QString> name = askSessionName(
      parent, QObject::tr("Rename session"), QObject::tr("Name:"),
      QString::fromStdString(session.name));
  if (!name) {
    return false;
  }
  session.name = name->toStdString();
  return true;
}

// Asks first (naming how many `noun`, e.g. "solves", go with it), then
// removes the store's current session; true if it was removed.
template <typename Store>
bool deleteSessionAsked(QWidget *parent, Store &store, std::size_t count,
                        const QString &noun) {
  const auto &session = store.sessions()[store.currentIndex()];
  if (!confirmDeleteSession(parent, QString::fromStdString(session.name), count,
                            noun)) {
    return false;
  }
  store.removeSession(store.currentIndex());
  return true;
}
// Asks for a comment: nullopt when cancelled.
std::optional<QString> askComment(QWidget *parent, const QString &label,
                                  const QString &comment);

// A context menu whose actions run a function once the menu has closed,
// as an action chosen from QMenu::exec() would be handled.
class ActionMenu : public QMenu {
  Q_OBJECT
public:
  using QMenu::QMenu;
  QAction *add(const QString &text, std::function<void()> run);
  // Shows the menu at `globalPos` and runs the chosen action's function,
  // if `stillOk` (when given) still allows it then.
  void run(const QPoint &globalPos, const std::function<bool()> &stillOk = {});

private:
  std::vector<std::pair<QAction *, std::function<void()>>> runs_;
};

// The common end of an attempt's context menu: Details, Comment, Copy
// scramble, Copy solution (with a solution) and Delete; a missing
// function leaves its action out.
struct AttemptMenuActions {
  std::function<void()> details;
  std::function<void()> comment;
  QString scramble;
  QString solution;
  std::function<void()> remove;
  bool copyApart = false; // a separator before the copy actions
};
void addAttemptActions(ActionMenu &menu, const AttemptMenuActions &actions);

} // namespace vcube
