#pragma once

#include "io/alglab_store.hpp"

#include "app/app_settings.hpp"
#include "app/mode_page.hpp"
#include "io/library.hpp"
#include "io/move_counts.hpp"
#include "io/sessions.hpp"
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
  TodayPage(AppSettings &settings, AlgLibrary &library,
            const SessionStore &timed, const MoveCountStore &untimed,
            const MoveCountStore &moveCounts, const AlgLabStore &lab,
            QString dataDir, QWidget *parent = nullptr);

  // Learn's due reviews (and learning steps) and the new cases left today,
  // over every set, for the sidebar.
  struct Totals {
    int due = 0;
    int newLeft = 0;
  };
  Totals totals();

  bool handleKey(QKeyEvent *e, bool press) override;
  void activated() override;
  void deactivated() override;
  SettingsParts settingsUsed() const override { return SettingsPart::Srs; }
  void applySettings() override;

signals:
  // A set was double-clicked: learn it.
  void learnSetRequested(const QString &setId);
  // The cards of a practice mode (F2L, TSLE) were asked for: its Learn
  // tab, one unit at a time.
  void practiceRequested(int mode);

private:
  AppSettings &settings_;
  AlgLibrary &library_;
  const SessionStore &timed_;
  const MoveCountStore &untimed_;
  const MoveCountStore &moveCounts_;
  const AlgLabStore &lab_;
  QString dataDir_;
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

  std::map<std::string, SetWaiting> dueBySet();
  std::vector<ActivityEvent> events();
  void refresh();
  // Opens a row of the sets table: its set in Learn, or its practice
  // mode's cards.
  void open(QTreeWidgetItem *item);
  void reviewAll();
};

// The cases Learn can offer in a set: ticked ones with an alg.
std::vector<std::string> learnPool(const AlgSet &set);

} // namespace vcube
