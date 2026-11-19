#pragma once

#include "app/app_settings.hpp"
#include "train/practice.hpp"

#include <QIcon>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace vcube {

// The tabs of a practice mode (see PracticePage).
enum class PracticeTab { Learn, Inspection, Untimed, Timed };

// The tries a tab keeps (Learn's cards aside).
PracticeTries &tabTries(PracticeData &data, PracticeTab tab);
const PracticeTries &tabTries(const PracticeData &data, PracticeTab tab);

// The side panel of a practice mode: a line on how the sitting went, the
// list of the open tab (its cards, or the groups its tries are kept under,
// each with its figures) and the button that clears what the tab keeps.
// The page says what the list is of; the panel reads the figures from the
// mode's data, and clears them when asked to.
class PracticeList : public QWidget {
  Q_OBJECT
public:
  struct Row {
    std::string key;
    QString name;
    QString section; // rows are grouped under these, in order
    QIcon icon;
  };
  // What the list is of.
  struct View {
    PracticeTab tab = PracticeTab::Learn;
    CubeKind cube = CubeKind::Virtual; // Timed's
    bool cards = false;                // the rows are cards, not groups
    QString step;                      // "F2L", as in "Whole F2L"
    QString help;                      // said under the list
  };

  PracticeList(PracticeData &practice, const AppSettings &settings,
               QWidget *parent = nullptr);

  // While the page is busy, rows aren't activated and nothing is cleared.
  void setBusyCheck(std::function<bool()> busy) { busy_ = std::move(busy); }

  // The rows of a tab, with their figures.
  void fill(const View &view, const std::vector<Row> &rows);
  // The figures again, after a try or a change of settings.
  void refresh();
  // The line above the list: what the cards of `pool` have waiting (Learn;
  // none for an empty pool), or how this sitting's tries under `group`
  // went (a timedKey() in Timed; none for an empty one).
  void showSummary(const std::vector<std::string> &pool,
                   const std::string &group);

signals:
  // A row was double-clicked: a card's, or a group's.
  void activated(const std::string &key, bool card);
  // What the tab kept was cleared (after asking): it is to be saved.
  void cleared();

private:
  PracticeData &data_;
  const AppSettings &settings_;
  View view_;
  std::int64_t sittingStart_;
  std::function<bool()> busy_;
  QLabel *summary_;
  QTreeWidget *tree_;
  QLabel *help_;
  QPushButton *clearButton_;

  // The tries of the tab that is up.
  const PracticeTries &tries() const;
  QString clock(std::int64_t ms) const;
  void fillRow(QTreeWidgetItem *row, std::int64_t now, std::int64_t dayEnd);
  void rowActivated(QTreeWidgetItem *row);
  void clear();
};

} // namespace vcube
