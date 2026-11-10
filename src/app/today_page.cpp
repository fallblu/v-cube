#include "app/today_page.hpp"

#include "app/main_window.hpp"
#include "app/widgets.hpp"
#include "io/blind_store.hpp"
#include "io/data_file.hpp"
#include "io/inspection_store.hpp"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace vcube {

namespace {

constexpr int kSetIdRole = Qt::UserRole;

// Read-only copies of the files the Inspection and Blind pages keep.
constexpr FileFormat kInspectionFile{"v-cube.inspection", 1, false,
                                     "inspection file"};
constexpr FileFormat kBlindFile{"v-cube.blind", 1, false, "blind file"};

QString duration(std::int64_t ms) {
  std::int64_t s = ms / 1000;
  if (s <= 0) {
    return "-";
  }
  if (s >= 3600) {
    return QString("%1h %2m").arg(s / 3600).arg(s / 60 % 60, 2, 10, QChar('0'));
  }
  if (s >= 60) {
    return QString("%1m %2s").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
  }
  return QString("%1s").arg(s);
}

std::int64_t localDay(std::int64_t unix) {
  return QDateTime::fromSecsSinceEpoch(unix).date().toJulianDay();
}

QTreeWidget *table(const QStringList &headers, QWidget *parent) {
  auto *t = new QTreeWidget(parent);
  t->setHeaderLabels(headers);
  t->setRootIsDecorated(false);
  t->setFocusPolicy(Qt::NoFocus);
  t->setSelectionMode(QAbstractItemView::SingleSelection);
  t->header()->setStretchLastSection(false);
  t->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
  t->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  for (int c = 1; c < headers.size(); ++c) {
    t->headerItem()->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
  }
  return t;
}

QString count(int n) { return n > 0 ? QString::number(n) : QString("-"); }

} // namespace

std::vector<std::string> learnPool(const AlgSet &set) {
  QStringList unticked = untickedCases(set);
  std::vector<std::string> pool;
  for (const auto &g : set.groups) {
    for (const auto &c : g.cases) {
      if (!c.algs.empty() && !unticked.contains(qs(c.id))) {
        pool.push_back(c.id);
      }
    }
  }
  return pool;
}

TodayPage::TodayPage(AppSettings &settings, AlgLibrary &library,
                     const SessionStore &timed, const MoveCountStore &untimed,
                     const MoveCountStore &moveCounts, QString dataDir,
                     QWidget *parent)
    : ModePage(parent), settings_(settings), library_(library), timed_(timed),
      untimed_(untimed), moveCounts_(moveCounts), dataDir_(std::move(dataDir)) {
  auto *layout = new QVBoxLayout(this);
  auto *title = new QLabel(tr("Today"), this);
  QFont tf = title->font();
  tf.setPointSizeF(tf.pointSizeF() * 1.6);
  tf.setBold(true);
  title->setFont(tf);
  streakLabel_ = new QLabel(this);
  layout->addWidget(title);
  layout->addWidget(streakLabel_);

  auto *setsTitle = new QLabel(tr("<b>Learn: what's waiting</b>"), this);
  sets_ = table({tr("Alg set"), tr("Due"), tr("New today"), tr("In queue"),
                 tr("Learned")},
                this);
  sets_->setToolTip(tr("Reviews due now or later today, and the new cases "
                       "Learn can still bring in today from the learn "
                       "queue. Double-click a set to learn it."));
  connect(sets_, &QTreeWidget::itemDoubleClicked, this,
          [this](QTreeWidgetItem *item) {
            emit learnSetRequested(item->data(0, kSetIdRole).toString());
          });
  layout->addWidget(setsTitle);
  layout->addWidget(sets_, 3);

  auto *modesTitle = new QLabel(tr("<b>Practice</b>"), this);
  modes_ = table({tr("Mode"), tr("Today"), tr("Time today"), tr("Last 7 days"),
                  tr("Time, 7 days")},
                 this);
  modes_->setSelectionMode(QAbstractItemView::NoSelection);
  modes_->setToolTip(tr("Solves, cases and answers recorded in each mode, "
                        "and the time they took where it is timed"));
  layout->addWidget(modesTitle);
  layout->addWidget(modes_, 2);

  refresh_.setInterval(60 * 1000);
  connect(&refresh_, &QTimer::timeout, this, &TodayPage::refresh);
}

void TodayPage::activated() {
  refresh();
  refresh_.start();
}

void TodayPage::deactivated() { refresh_.stop(); }

void TodayPage::applySettings() {
  if (isVisible()) {
    refresh();
  }
}

std::map<std::string, SetDue> TodayPage::dueBySet() {
  std::map<std::string, SetDue> out;
  std::int64_t now = unixNow();
  for (const AlgSet &set : library_.sets()) {
    out[set.id] = setDue(learnPool(set), library_.progress(set.id), now,
                         localDayStart(), localDayEnd(), settings_.srs);
  }
  return out;
}

TodayPage::Totals TodayPage::totals() {
  Totals t;
  for (const auto &[id, d] : dueBySet()) {
    t.due += d.due;
    t.newLeft += d.newLeft;
  }
  return t;
}

std::vector<ActivityEvent> TodayPage::events() {
  std::vector<ActivityEvent> out;
  std::int64_t since = localDayStart() - 400 * kSecondsPerDay; // for streaks
  auto add = [&](int mode, std::int64_t t, std::int64_t ms) {
    if (t >= since) {
      out.push_back({mode, t, ms});
    }
  };
  for (const AlgSet &set : library_.sets()) {
    for (const auto &[id, p] : library_.progress(set.id)) {
      for (const Attempt &a : p.attempts) {
        bool learn = a.mode == AttemptMode::Learn;
        add(learn ? MainWindow::Learn : MainWindow::Drill, a.timestamp,
            learn ? 0 : a.timeMs);
      }
    }
  }
  for (const Session &s : timed_.sessions()) {
    for (const Solve &solve : s.solves) {
      add(MainWindow::Timed, solve.timestamp, solve.timeMs);
    }
  }
  for (const auto &[store, mode] :
       {std::pair{&untimed_, int(MainWindow::Untimed)},
        std::pair{&moveCounts_, int(MainWindow::MoveCount)}}) {
    for (const MoveCountSession &s : store->sessions()) {
      for (const MoveCountAttempt &a : s.attempts) {
        add(mode, a.timestamp, 0);
      }
    }
  }
  FileRead inspection =
      readDataFile(QDir(dataDir_).filePath("inspection.json"), kInspectionFile);
  if (inspection.status == FileStatus::Read) {
    for (const InspectionSession &s : inspectionFromJson(inspection.object)) {
      for (const InspectionAttempt &a : s.attempts) {
        add(MainWindow::Inspection, a.timestamp, a.ms);
      }
    }
  }
  FileRead blind =
      readDataFile(QDir(dataDir_).filePath("blind.json"), kBlindFile);
  if (blind.status == FileStatus::Read) {
    BlindData blindData = blindFromJson(blind.object);
    for (const BlindSession &s : blindData.sessions) {
      for (const BlindAttempt &a : s.attempts) {
        add(MainWindow::Blind, a.timestamp, a.timeMs);
      }
    }
    for (const TraceAttempt &t : blindData.traces) {
      add(MainWindow::Blind, t.timestamp, t.timeMs);
    }
  }
  return out;
}

void TodayPage::refresh() {
  // Learn.
  auto due = dueBySet();
  sets_->clear();
  for (const AlgSet &set : library_.sets()) {
    const SetDue &d = due[set.id];
    auto *item = new QTreeWidgetItem(sets_, {qs(set.name), count(d.due),
                                             count(d.newLeft), count(d.queued),
                                             count(d.learned)});
    item->setData(0, kSetIdRole, qs(set.id));
    for (int c = 1; c < 5; ++c) {
      item->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
    }
    if (d.due > 0) {
      QFont f = item->font(1);
      f.setBold(true);
      item->setFont(1, f);
    }
  }

  // Practice.
  std::vector<ActivityEvent> all = events();
  std::int64_t today = localDayStart();
  QDate date = QDate::currentDate();
  std::int64_t week =
      QDateTime(date.addDays(-6), QTime(0, 0)).toSecsSinceEpoch();
  auto activity = summarizeActivity(all, MainWindow::ModeCount, today, week);
  modes_->clear();
  static const std::pair<int, const char *> rows[] = {
      {MainWindow::Learn, QT_TR_NOOP("Learn (cases)")},
      {MainWindow::Drill, QT_TR_NOOP("Drill (cases)")},
      {MainWindow::Timed, QT_TR_NOOP("Timed (solves)")},
      {MainWindow::Untimed, QT_TR_NOOP("Untimed (solves)")},
      {MainWindow::MoveCount, QT_TR_NOOP("Move Count (solves)")},
      {MainWindow::Inspection, QT_TR_NOOP("Inspection (scrambles)")},
      {MainWindow::Blind, QT_TR_NOOP("Blind (solves and traces)")},
  };
  ActivityTotals dayTotal, weekTotal;
  for (const auto &[mode, label] : rows) {
    const ModeActivity &a = activity[static_cast<std::size_t>(mode)];
    auto *item = new QTreeWidgetItem(
        modes_, {tr(label), count(a.today.count), duration(a.today.ms),
                 count(a.week.count), duration(a.week.ms)});
    for (int c = 1; c < 5; ++c) {
      item->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
    }
    dayTotal.count += a.today.count;
    dayTotal.ms += a.today.ms;
    weekTotal.count += a.week.count;
    weekTotal.ms += a.week.ms;
  }
  auto *total = new QTreeWidgetItem(
      modes_, {tr("All modes"), count(dayTotal.count), duration(dayTotal.ms),
               count(weekTotal.count), duration(weekTotal.ms)});
  for (int c = 0; c < 5; ++c) {
    QFont f = total->font(c);
    f.setBold(true);
    total->setFont(c, f);
    if (c > 0) {
      total->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
    }
  }

  // Streak.
  std::vector<std::int64_t> times;
  times.reserve(all.size());
  for (const ActivityEvent &e : all) {
    times.push_back(e.timestamp);
  }
  Streak streak = practiceStreak(times, date.toJulianDay(), localDay);
  if (streak.days == 0) {
    streakLabel_->setText(tr("No practice streak yet: anything done today "
                             "starts one."));
  } else if (streak.doneToday) {
    streakLabel_->setText(
        tr("Practice streak: %n day(s), today included.", "", streak.days));
  } else {
    streakLabel_->setText(tr("Practice streak: %n day(s). Practise today to "
                             "keep it going.",
                             "", streak.days));
  }
}

} // namespace vcube
