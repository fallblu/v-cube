#pragma once

#include "app/app_settings.hpp"
#include "app/mode_page.hpp"
#include "io/library.hpp"
#include "io/move_counts.hpp"
#include "io/sessions.hpp"
#include "train/today.hpp"

#include <QTimer>

#include <map>
#include <string>
#include <vector>

class QLabel;
class QTreeWidget;

namespace vcube {

// What is waiting and what was done, across the modes: each alg set's
// reviews due and new cases left in Learn, the practice streak, and the
// solves and time per mode today and over the last 7 days.
class TodayPage : public ModePage {
  Q_OBJECT
public:
  TodayPage(AppSettings &settings, AlgLibrary &library,
            const SessionStore &timed, const MoveCountStore &untimed,
            const MoveCountStore &moveCounts, QString dataDir,
            QWidget *parent = nullptr);

  // Learn's due reviews (and learning steps) and the new cases left today,
  // over every set, for the sidebar.
  struct Totals {
    int due = 0;
    int newLeft = 0;
  };
  Totals totals();

  void activated() override;
  void deactivated() override;
  void applySettings() override;

signals:
  // A set was double-clicked: learn it.
  void learnSetRequested(const QString &setId);

private:
  AppSettings &settings_;
  AlgLibrary &library_;
  const SessionStore &timed_;
  const MoveCountStore &untimed_;
  const MoveCountStore &moveCounts_;
  QString dataDir_;
  QLabel *streakLabel_;
  QTreeWidget *sets_;
  QTreeWidget *modes_;
  QTimer refresh_;

  std::map<std::string, SetWaiting> dueBySet();
  std::vector<ActivityEvent> events();
  void refresh();
};

// The cases Learn can offer in a set: ticked ones with an alg.
std::vector<std::string> learnPool(const AlgSet &set);

} // namespace vcube
