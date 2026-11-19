#include "app/today_page.hpp"
#include "app/case_page.hpp"

#include "app/mode_table.hpp"
#include "app/ui_state.hpp"
#include "app/widgets.hpp"
#include "io/data_file.hpp"
#include "io/practice_store.hpp"
#include "train/f2l.hpp"
#include "train/tsle.hpp"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace vcube {

namespace {

constexpr int kSetIdRole = Qt::UserRole;
// A row of a practice mode's cards (F2L, TSLE): the mode, else -1.
constexpr int kModeRole = Qt::UserRole + 1;

// A practice mode's file as it is on disk (empty when there is none).
PracticeData practiceData(const QString &dataDir, const char *file) {
  FileRead read = readDataFile(QDir(dataDir).filePath(file), kPracticeFile);
  return read.status == FileStatus::Read ? practiceFromJson(read.object)
                                         : PracticeData{};
}

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

// A row's button with a gap above and below it, so that the buttons of
// rows one under another don't touch.
QWidget *spacedButton(QPushButton *button, QWidget *parent) {
  auto *cell = new QWidget(parent);
  auto *layout = new QHBoxLayout(cell);
  layout->setContentsMargins(2, 3, 2, 3);
  button->setParent(cell);
  layout->addWidget(button);
  return cell;
}

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
                     const MoveCountStore &moveCounts, const AlgLabStore &lab,
                     QString dataDir, QWidget *parent)
    : ModePage(parent), settings_(settings), library_(library), timed_(timed),
      untimed_(untimed), moveCounts_(moveCounts), lab_(lab),
      dataDir_(std::move(dataDir)) {
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
  sets_ = table({tr("Cases"), tr("Due"), tr("New today"), tr("In queue"),
                 tr("Learned"), QString()},
                this);
  sets_->setToolTip(tr("Reviews due now or later today, and the new cases "
                       "Learn can still bring in today from the learn "
                       "queue. Double-click a set to learn it."));
  connect(sets_, &QTreeWidget::itemDoubleClicked, this,
          [this](QTreeWidgetItem *item) {
            if (int mode = item->data(0, kModeRole).toInt(); mode >= 0) {
              emit practiceRequested(mode);
            } else {
              emit learnSetRequested(item->data(0, kSetIdRole).toString());
            }
          });
  reviewAll_ = new QPushButton(this);
  reviewAll_->setFocusPolicy(Qt::NoFocus);
  connect(reviewAll_, &QPushButton::clicked, this,
          [this] { emit learnSetRequested(QString(CasePage::kAcrossSetsId)); });

  auto *modesTitle = new QLabel(tr("<b>Activity</b>"), this);
  modes_ = table({tr("Mode"), tr("Today"), tr("Time today"), tr("Last 7 days"),
                  tr("Time, 7 days")},
                 this);
  modes_->setSelectionMode(QAbstractItemView::NoSelection);
  modes_->setToolTip(tr("Solves, cases and answers recorded in each mode, "
                        "and the time they took where it is timed"));

  // What to do, and what was done, side by side and no wider than they
  // need to be.
  auto *columns = new QHBoxLayout;
  auto *learnColumn = new QVBoxLayout;
  auto *learnHead = new QHBoxLayout;
  learnHead->addWidget(setsTitle);
  learnHead->addStretch();
  learnHead->addWidget(reviewAll_);
  learnColumn->addLayout(learnHead);
  learnColumn->addWidget(sets_, 1);
  auto *activityColumn = new QVBoxLayout;
  // As tall as the other column's head, so the tables line up.
  modesTitle->setMinimumHeight(reviewAll_->sizeHint().height());
  activityColumn->addWidget(modesTitle);
  activityColumn->addWidget(modes_, 1);
  sets_->setMaximumWidth(720);
  modes_->setMaximumWidth(720);
  columns->addLayout(learnColumn, 1);
  columns->addSpacing(16);
  columns->addLayout(activityColumn, 1);
  columns->addStretch(0);
  layout->addLayout(columns, 1);

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

std::map<std::string, SetWaiting> TodayPage::dueBySet() {
  std::map<std::string, SetWaiting> out;
  std::int64_t now = unixNow();
  for (const AlgSet &set : library_.sets()) {
    out[set.id] = setWaiting(learnPool(set), library_.progress(set.id), now,
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
        add(learn ? AppMode::Learn : AppMode::Drill, a.timestamp,
            learn ? 0 : a.timeMs);
      }
    }
  }
  for (const Session &s : timed_.sessions()) {
    for (const Solve &solve : s.solves) {
      add(AppMode::Timed, solve.timestamp, solve.timeMs);
    }
  }
  for (const auto &[store, mode] :
       {std::pair{&untimed_, int(AppMode::Untimed)},
        std::pair{&moveCounts_, int(AppMode::MoveCount)}}) {
    for (const MoveCountSession &s : store->sessions()) {
      for (const MoveCountAttempt &a : s.attempts) {
        add(mode, a.timestamp, 0);
      }
    }
  }
  for (const LabAttempt &a : lab_.attempts()) {
    add(AppMode::AlgLab, a.timestamp, a.timeMs);
  }
  // The practice modes: every card's try and every solve kept.
  for (const auto &[file, mode] :
       {std::pair{"eocross.json", int(AppMode::EoCross)},
        std::pair{"f2l.json", int(AppMode::F2L)},
        std::pair{"tsle.json", int(AppMode::Tsle)}}) {
    const PracticeData kept = practiceData(dataDir_, file);
    for (const auto &[id, p] : kept.cards) {
      for (const Attempt &a : p.attempts) {
        add(mode, a.timestamp, 0);
      }
    }
    for (const PracticeTries *tries :
         {&kept.learn, &kept.inspection, &kept.timed}) {
      for (const auto &[key, list] : *tries) {
        for (const PracticeTry &t : list) {
          add(mode, t.timestamp, t.ms + t.inspectMs);
        }
      }
    }
  }
  return out;
}

void TodayPage::refresh() {
  // Learn.
  auto due = dueBySet();
  int dueTotal = 0;
  sets_->clear();
  for (const AlgSet &set : library_.sets()) {
    const SetWaiting &d = due[set.id];
    auto *item = new QTreeWidgetItem(sets_, {qs(set.name), count(d.due),
                                             count(d.newLeft), count(d.queued),
                                             count(d.learned)});
    item->setData(0, kSetIdRole, qs(set.id));
    item->setData(0, kModeRole, -1);
    for (int c = 1; c < 5; ++c) {
      item->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
    }
    if (d.due > 0) {
      QFont f = item->font(1);
      f.setBold(true);
      item->setFont(1, f);
    }
    // What the set has waiting, as a button.
    QString go = d.due > 0                       ? tr("Review")
                 : d.newLeft > 0 || d.queued > 0 ? tr("Learn")
                                                 : tr("Open");
    auto *button = new QPushButton(go, sets_);
    button->setFocusPolicy(Qt::NoFocus);
    QString id = qs(set.id);
    connect(button, &QPushButton::clicked, this,
            [this, id] { emit learnSetRequested(id); });
    sets_->setItemWidget(item, 5, spacedButton(button, sets_));
    dueTotal += d.due;
  }
  // The cards of F2L and TSLE mode, which come in by themselves.
  const bool eo = methodFromId(saved::practice("f2l", "method", "zz-1")
                                   .get()
                                   .toString()
                                   .toStdString())
                      .transform([](F2LMethod m) {
                        return m == F2LMethod::ZZMinus1 || m == F2LMethod::ZZ;
                      })
                      .value_or(true);
  struct Practice {
    int mode;
    QString name;
    const char *file;
    std::vector<std::string> pool;
  };
  for (const Practice &p :
       {Practice{AppMode::F2L, tr("F2L pairs"), "f2l.json", pairCardPool(eo)},
        Practice{AppMode::Tsle, tr("TSLE cases"), "tsle.json",
                 tsleCardPool()}}) {
    const PracticeWaiting w =
        practiceWaiting(p.pool, practiceData(dataDir_, p.file).cards, unixNow(),
                        localDayStart(), localDayEnd(), settings_.srs);
    auto *item = new QTreeWidgetItem(
        sets_, {p.name, count(w.due), count(w.newLeft),
                count(w.total - w.started), count(w.learned)});
    item->setData(0, kModeRole, p.mode);
    for (int c = 1; c < 5; ++c) {
      item->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
    }
    if (w.due > 0) {
      QFont f = item->font(1);
      f.setBold(true);
      item->setFont(1, f);
    }
    auto *button = new QPushButton(w.due > 0       ? tr("Review")
                                   : w.newLeft > 0 ? tr("Learn")
                                                   : tr("Open"),
                                   sets_);
    button->setFocusPolicy(Qt::NoFocus);
    connect(button, &QPushButton::clicked, this,
            [this, mode = p.mode] { emit practiceRequested(mode); });
    sets_->setItemWidget(item, 5, spacedButton(button, sets_));
  }
  reviewAll_->setText(tr("Review all sets (%1 due)").arg(dueTotal));
  reviewAll_->setEnabled(dueTotal > 0);

  // Activity.
  std::vector<ActivityEvent> all = events();
  std::int64_t today = localDayStart();
  QDate date = QDate::currentDate();
  std::int64_t week =
      QDateTime(date.addDays(-6), QTime(0, 0)).toSecsSinceEpoch();
  auto activity = summarizeActivity(all, AppMode::Count, today, week);
  modes_->clear();
  static const std::pair<int, const char *> rows[] = {
      {AppMode::Learn, QT_TR_NOOP("Learn (cases)")},
      {AppMode::Drill, QT_TR_NOOP("Drill (cases)")},
      {AppMode::AlgLab, QT_TR_NOOP("Alg Lab (algs timed)")},
      {AppMode::Timed, QT_TR_NOOP("Timed (solves)")},
      {AppMode::Untimed, QT_TR_NOOP("Untimed (solves)")},
      {AppMode::MoveCount, QT_TR_NOOP("Move Count (solves)")},
      {AppMode::EoCross, QT_TR_NOOP("EO Cross (solves)")},
      {AppMode::F2L, QT_TR_NOOP("F2L (cards and solves)")},
      {AppMode::Tsle, QT_TR_NOOP("TSLE (cards and solves)")},
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
