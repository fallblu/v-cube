#pragma once

#include "io/app_data.hpp"

#include "app/app_settings.hpp"
#include "app/mode_page.hpp"
#include "app/mode_table.hpp"
#include "train/today.hpp"

#include <QTimer>

#include <functional>
#include <map>
#include <string>
#include <vector>

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace vcube {

class HintBar;

// What is waiting and what was done, across the modes: each alg set's
// reviews due and new cases left in Learn, the same for the cards of F2L
// and TSLE mode, the practice streak, and the
// solves and time per mode today and over the last 7 days. One button says
// what to do next (review what is due, else learn new cases); Up and Down
// pick a row, Enter opens it, R reviews every set and Space does the next
// thing.
class TodayPage : public ModePage {
  Q_OBJECT
public:
  // A practice mode's cards, as its page works them out.
  using CardPool = std::function<std::vector<std::string>()>;

  TodayPage(AppSettings &settings, const AppData &appData, CardPool f2lPool,
            CardPool tslePool, QWidget *parent = nullptr);

  // Learn's due reviews (and learning steps) and the new cases left today,
  // over every set, for the sidebar.
  struct Totals {
    int due = 0;
    int newLeft = 0;
  };
  Totals totals() const;

  void activated() override;
  void deactivated() override;
  SettingsParts settingsUsed() const override { return SettingsPart::Srs; }
  void applySettings() override;

signals:
  // A set was double-clicked: learn it.
  void learnSetRequested(const QString &setId);
  // The cards of a practice mode (F2L, TSLE) were asked for: its Learn
  // tab, one unit at a time.
  void practiceRequested(AppMode::Id mode);

protected:
  bool handleKey(QKeyEvent *e, bool press) override;

private:
  AppSettings &settings_;
  // What the modes keep, as they have it in memory: read from there, a
  // refresh opens no file and sees what was just recorded.
  const AppData &data_;
  CardPool f2lPool_;
  CardPool tslePool_;
  QLabel *streakLabel_ = nullptr;
  QPushButton *next_ = nullptr;
  QLabel *nextNote_ = nullptr;
  QTreeWidget *sets_ = nullptr;
  QTreeWidget *modes_ = nullptr;
  HintBar *hints_ = nullptr;
  QTimer refresh_;
  // What next_ does, and Review all when anything is due.
  std::function<void()> nextAction_;
  int dueTotal_ = 0;

  std::map<std::string, SetWaiting> dueBySet() const;
  void refresh();
  // Opens a row of the sets table: its set in Learn, or its practice
  // mode's cards.
  void open(QTreeWidgetItem *item);
  void open(const QString &setId, std::optional<AppMode::Id> mode);
  void reviewAll();
};

// The cases Learn can offer in a set: ticked ones with an alg.
std::vector<std::string> learnPool(const AlgSet &set);

} // namespace vcube
