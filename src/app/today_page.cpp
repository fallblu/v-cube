#include "app/today_page.hpp"
#include "app/case_page.hpp"

#include "app/mode_table.hpp"
#include "app/page_parts.hpp"
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
#include <QKeyEvent>
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
  t->setUniformRowHeights(true);
  // As tall as its rows, no taller: the page doesn't stretch it.
  t->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
  t->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  t->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
  t->header()->setStretchLastSection(false);
  t->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
  t->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  for (int c = 1; c < headers.size(); ++c) {
    t->headerItem()->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
  }
  return t;
}

QString count(int n) { return n > 0 ? QString::number(n) : QString("-"); }

// The height of every row of both tables: that of the row buttons, so
// that the two tables read alike.
int rowHeight(QWidget *page) {
  QPushButton probe(QStringLiteral("Review"), page);
  return probe.sizeHint().height() + 2;
}

void setRowHeight(QTreeWidgetItem *item, int h) {
  item->setSizeHint(0, QSize(0, h));
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

  // What to do next, as the one prominent button.
  next_ = new QPushButton(this);
  next_->setObjectName("todayNext");
  next_->setFocusPolicy(Qt::NoFocus);
  QFont nf = next_->font();
  nf.setBold(true);
  nf.setPointSizeF(nf.pointSizeF() * 1.15);
  next_->setFont(nf);
  next_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  nextNote_ = new QLabel(this);
  nextNote_->setWordWrap(true);
  connect(next_, &QPushButton::clicked, this, [this] {
    if (nextAction_) {
      nextAction_();
    }
  });
  auto *nextRow = new QHBoxLayout;
  nextRow->addWidget(next_);
  nextRow->addWidget(nextNote_, 1);
  layout->addSpacing(6);
  layout->addLayout(nextRow);
  layout->addSpacing(10);

  auto *setsTitle = new QLabel(tr("<b>Learn: what's waiting</b>"), this);
  sets_ = table({tr("Cases"), tr("Due"), tr("New today"), tr("In queue"),
                 tr("Learned"), QString()},
                this);
  sets_->setAccessibleName(tr("What's waiting in Learn"));
  connect(sets_, &QTreeWidget::itemDoubleClicked, this, &TodayPage::open);

  auto *modesTitle = new QLabel(tr("<b>Activity</b>"), this);
  modes_ = table({tr("Mode"), tr("Today"), tr("Time today"), tr("Last 7 days"),
                  tr("Time, 7 days")},
                 this);
  modes_->setSelectionMode(QAbstractItemView::NoSelection);
  modes_->setAccessibleName(tr("Activity"));
  auto *setsHelp = helpLabel(
      tr("Reviews due now or later today, and the new cases Learn can "
         "still bring in today from the learn queue. A row's button (or "
         "Enter) opens it: Review due, Learn new, or Open when nothing "
         "waits."),
      this);
  auto *modesHelp = helpLabel(tr("Solves, cases and answers recorded in each "
                                 "mode, and the time they took where it is "
                                 "timed."),
                              this);

  // What to do, and what was done, side by side and no wider than they
  // need to be; each table only as tall as its rows.
  auto *columns = new QHBoxLayout;
  auto *learnColumn = new QVBoxLayout;
  learnColumn->addWidget(setsTitle);
  learnColumn->addWidget(sets_);
  learnColumn->addWidget(setsHelp);
  learnColumn->addStretch(1);
  auto *activityColumn = new QVBoxLayout;
  activityColumn->addWidget(modesTitle);
  activityColumn->addWidget(modes_);
  activityColumn->addWidget(modesHelp);
  activityColumn->addStretch(1);
  sets_->setMaximumWidth(720);
  modes_->setMaximumWidth(720);
  columns->addLayout(learnColumn, 1);
  columns->addSpacing(16);
  columns->addLayout(activityColumn, 1);
  columns->addStretch(0);
  layout->addLayout(columns, 1);

  hints_ = new HintBar(this);
  hints_->setHint({}, {{tr("↑ ↓"), tr("pick a row")},
                       {tr("Enter"), tr("open it")},
                       {tr("R"), tr("review all sets")},
                       {tr("Space"), tr("the button above")}});
  layout->addWidget(hints_);

  refresh_.setInterval(60 * 1000);
  connect(&refresh_, &QTimer::timeout, this, &TodayPage::refresh);
}

void TodayPage::activated() {
  refresh();
  refresh_.start();
}

void TodayPage::open(QTreeWidgetItem *item) {
  if (!item) {
    return;
  }
  if (int mode = item->data(0, kModeRole).toInt(); mode >= 0) {
    emit practiceRequested(mode);
  } else {
    emit learnSetRequested(item->data(0, kSetIdRole).toString());
  }
}

void TodayPage::reviewAll() {
  if (dueTotal_ > 0) {
    emit learnSetRequested(QString(CasePage::kAcrossSetsId));
  } else {
    emit message(tr("No reviews are due in any set."));
  }
}

bool TodayPage::handleKey(QKeyEvent *e, bool press) {
  if (e->modifiers() & ~(Qt::KeypadModifier | Qt::ShiftModifier)) {
    return false; // Ctrl and Alt keys are the window's
  }
  const int key = e->key();
  switch (key) {
  case Qt::Key_Up:
  case Qt::Key_Down:
  case Qt::Key_Home:
  case Qt::Key_End: {
    if (press && sets_->topLevelItemCount() > 0) {
      int last = sets_->topLevelItemCount() - 1;
      int row = sets_->currentItem()
                    ? sets_->indexOfTopLevelItem(sets_->currentItem())
                    : -1;
      if (key == Qt::Key_Home) {
        row = 0;
      } else if (key == Qt::Key_End) {
        row = last;
      } else if (row < 0) {
        row = key == Qt::Key_Down ? 0 : last;
      } else {
        row = std::clamp(row + (key == Qt::Key_Down ? 1 : -1), 0, last);
      }
      QTreeWidgetItem *item = sets_->topLevelItem(row);
      sets_->setCurrentItem(item);
      item->setSelected(true);
      sets_->scrollToItem(item);
    }
    return true;
  }
  case Qt::Key_Return:
  case Qt::Key_Enter:
    if (press) {
      QTreeWidgetItem *item = sets_->currentItem();
      if (item && item->isSelected()) {
        open(item);
      } else if (nextAction_) {
        nextAction_();
      }
    }
    return true;
  case Qt::Key_R:
    if (press) {
      reviewAll();
    }
    return true;
  case Qt::Key_Space:
    if (press && nextAction_) {
      nextAction_();
    }
    return true;
  default:
    return false;
  }
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
  const int rowH = rowHeight(this);
  // The row picked stays picked.
  QString pickedSet;
  int pickedMode = -2;
  if (QTreeWidgetItem *item = sets_->currentItem();
      item && item->isSelected()) {
    pickedSet = item->data(0, kSetIdRole).toString();
    pickedMode = item->data(0, kModeRole).toInt();
  }
  // What to do next: the first set or practice mode with new cases, and
  // the practice mode with the most due.
  QString firstNewSet, firstNewName;
  int practiceDue = 0, practiceDueMode = -1, firstNewMode = -1;
  QString practiceDueName, firstNewModeName;
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
    setRowHeight(item, rowH);
    // What the set has waiting, as a button.
    QString go = d.due > 0                       ? tr("Review due")
                 : d.newLeft > 0 || d.queued > 0 ? tr("Learn new")
                                                 : tr("Open");
    auto *button = new QPushButton(go, sets_);
    button->setFocusPolicy(Qt::NoFocus);
    button->setAccessibleName(tr("%1: %2").arg(go, qs(set.name)));
    QString id = qs(set.id);
    connect(button, &QPushButton::clicked, this,
            [this, id] { emit learnSetRequested(id); });
    sets_->setItemWidget(item, 5, button);
    dueTotal += d.due;
    if (firstNewSet.isEmpty() && d.newLeft > 0) {
      firstNewSet = id;
      firstNewName = qs(set.name);
    }
    if (pickedMode == -1 && pickedSet == id) {
      sets_->setCurrentItem(item);
    }
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
    setRowHeight(item, rowH);
    QString go = w.due > 0       ? tr("Review due")
                 : w.newLeft > 0 ? tr("Learn new")
                                 : tr("Open");
    auto *button = new QPushButton(go, sets_);
    button->setFocusPolicy(Qt::NoFocus);
    button->setAccessibleName(tr("%1: %2").arg(go, p.name));
    connect(button, &QPushButton::clicked, this,
            [this, mode = p.mode] { emit practiceRequested(mode); });
    sets_->setItemWidget(item, 5, button);
    if (w.due > practiceDue) {
      practiceDue = w.due;
      practiceDueMode = p.mode;
      practiceDueName = p.name;
    }
    if (firstNewMode < 0 && w.newLeft > 0) {
      firstNewMode = p.mode;
      firstNewModeName = p.name;
    }
    if (pickedMode == p.mode) {
      sets_->setCurrentItem(item);
    }
  }
  if (QTreeWidgetItem *item = sets_->currentItem(); item && pickedMode > -2) {
    item->setSelected(true);
  } else {
    sets_->setCurrentItem(nullptr);
    sets_->clearSelection();
  }
  dueTotal_ = dueTotal;

  // The next thing to do: reviews first, then new cases.
  QString note;
  if (dueTotal > 0) {
    next_->setText(dueTotal == 1 ? tr("Review 1 due case")
                                 : tr("Review %1 due cases").arg(dueTotal));
    nextAction_ = [this] { reviewAll(); };
    note = tr("Every set's due cases, together.");
  } else if (practiceDue > 0) {
    next_->setText(practiceDue == 1
                       ? tr("Review 1 due card")
                       : tr("Review %1 due cards").arg(practiceDue));
    nextAction_ = [this, mode = practiceDueMode] {
      emit practiceRequested(mode);
    };
    note = tr("Due in %1.").arg(practiceDueName);
  } else if (!firstNewSet.isEmpty()) {
    next_->setText(tr("Learn new cases"));
    nextAction_ = [this, id = firstNewSet] { emit learnSetRequested(id); };
    note = tr("Nothing is due. Next in line: %1.").arg(firstNewName);
  } else if (firstNewMode >= 0) {
    next_->setText(tr("Learn new cards"));
    nextAction_ = [this, mode = firstNewMode] { emit practiceRequested(mode); };
    note = tr("Nothing is due. Next in line: %1.").arg(firstNewModeName);
  } else {
    next_->setText(tr("Nothing due"));
    nextAction_ = {};
    note = tr("No reviews are due and no new cases are left for today.");
  }
  next_->setEnabled(static_cast<bool>(nextAction_));
  // Set again on each refresh, so that the colours follow the theme.
  next_->setStyleSheet(
      nextAction_ ? "#todayNext { background: palette(highlight); color: "
                    "palette(highlighted-text); border: none; border-radius: "
                    "4px; padding: 7px 18px; }"
                  : "#todayNext { padding: 7px 18px; }");
  next_->setAccessibleDescription(note);
  nextNote_->setText(note);
  hints_->setHint({}, {{tr("↑ ↓"), tr("pick a row")},
                       {tr("Enter"), tr("open it")},
                       {tr("R"), dueTotal > 0 ? tr("review all sets")
                                              : tr("review all (none due)")},
                       {tr("Space"), next_->text()}});

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
    setRowHeight(item, rowH);
    dayTotal.count += a.today.count;
    dayTotal.ms += a.today.ms;
    weekTotal.count += a.week.count;
    weekTotal.ms += a.week.ms;
  }
  auto *total = new QTreeWidgetItem(
      modes_, {tr("All modes"), count(dayTotal.count), duration(dayTotal.ms),
               count(weekTotal.count), duration(weekTotal.ms)});
  setRowHeight(total, rowH);
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
