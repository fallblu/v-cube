#include "app/today_page.hpp"

#include "app/mode_table.hpp"
#include "app/page_parts.hpp"
#include "app/text_style.hpp"
#include "app/ui_state.hpp"
#include "app/ui_text.hpp"
#include "app/widgets.hpp"

#include <QDate>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace vcube {

namespace {

constexpr int kSetIdRole = Qt::UserRole;
// A row of a practice mode's cards (F2L, TSLE): the mode, else -1.
// A practice mode's row keeps its mode here; a set's row has none.
constexpr int kModeRole = Qt::UserRole + 1;

std::optional<AppMode::Id> rowMode(const QTreeWidgetItem *item) {
  const QVariant v = item->data(0, kModeRole);
  return v.isValid() ? AppMode::fromIndex(v.toInt()) : std::nullopt;
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

// The height of every row of both tables: that of the row buttons, so
// that the two tables read alike.
int rowHeight(QWidget *page) {
  QPushButton probe(QStringLiteral("Review"), page);
  // With the gap above and below the button (see spacedButton).
  return probe.sizeHint().height() + 8;
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

TodayPage::TodayPage(AppSettings &settings, const AppData &appData,
                     CardPool f2lPool, CardPool tslePool, QWidget *parent)
    : ModePage(parent), settings_(settings), data_(appData),
      f2lPool_(std::move(f2lPool)), tslePool_(std::move(tslePool)) {
  auto *layout = new QVBoxLayout(this);
  auto *title = new QLabel(tr("Today"), this);
  setTextRole(title, TextRole::Display, true);
  streakLabel_ = new QLabel(this);
  layout->addWidget(title);
  layout->addWidget(streakLabel_);

  // What to do next, as the one prominent button.
  next_ = new QPushButton(this);
  next_->setObjectName("todayNext");
  next_->setFocusPolicy(Qt::NoFocus);
  setTextRole(next_, TextRole::Heading, true);
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
  connect(sets_, &QTreeWidget::itemDoubleClicked, this,
          qOverload<QTreeWidgetItem *>(&TodayPage::open));

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
  if (item) {
    open(item->data(0, kSetIdRole).toString(), rowMode(item));
  }
}

void TodayPage::open(const QString &setId, std::optional<AppMode::Id> mode) {
  if (mode) {
    emit practiceRequested(*mode);
  } else {
    emit learnSetRequested(setId);
  }
}

void TodayPage::reviewAll() {
  if (dueTotal_ > 0) {
    emit learnSetRequested(QString(saved::kAcrossSetsId));
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

std::map<std::string, SetWaiting> TodayPage::dueBySet() const {
  std::map<std::string, SetWaiting> out;
  std::int64_t now = unixNow();
  for (const AlgSet &set : data_.library.sets()) {
    out[set.id] =
        setWaiting(learnPool(set), data_.library.progress(set.id), now,
                   localDayStart(), localDayEnd(), settings_.srs);
  }
  return out;
}

TodayPage::Totals TodayPage::totals() const {
  Totals t;
  for (const auto &[id, d] : dueBySet()) {
    t.due += d.due;
    t.newLeft += d.newLeft;
  }
  return t;
}

void TodayPage::refresh() {
  // Learn.
  auto due = dueBySet();
  int dueTotal = 0;
  // The rows of the sets table: each set's, then the cards of F2L and TSLE
  // mode, which come in by themselves.
  struct Row {
    QString setId;
    std::optional<AppMode::Id> mode; // the practice mode's; none for a set
    QString name;
    int due = 0, newLeft = 0, queued = 0, learned = 0;
    QString go; // what its button says
  };
  std::vector<Row> rows;
  for (const AlgSet &set : data_.library.sets()) {
    const SetWaiting &d = due[set.id];
    rows.push_back({qs(set.id), std::nullopt, qs(set.name), d.due, d.newLeft,
                    d.queued, d.learned,
                    d.due > 0                       ? tr("Review due")
                    : d.newLeft > 0 || d.queued > 0 ? tr("Learn new")
                                                    : tr("Open")});
    dueTotal += d.due;
  }
  struct Practice {
    AppMode::Id mode;
    QString name;
    const PracticeData &data;
    const CardPool &pool;
  };
  for (const Practice &p :
       {Practice{AppMode::F2L, tr("F2L pairs"), data_.f2l.data, f2lPool_},
        Practice{AppMode::Tsle, tr("TSLE cases"), data_.tsle.data,
                 tslePool_}}) {
    const PracticeWaiting w = practiceWaiting(
        p.pool ? p.pool() : std::vector<std::string>{}, p.data.cards, unixNow(),
        localDayStart(), localDayEnd(), settings_.srs);
    rows.push_back({{},
                    p.mode,
                    p.name,
                    w.due,
                    w.newLeft,
                    w.total - w.started,
                    w.learned,
                    w.due > 0       ? tr("Review due")
                    : w.newLeft > 0 ? tr("Learn new")
                                    : tr("Open")});
  }
  // What to do next: the practice mode with the most due, and the first
  // set or practice mode with new cases.
  const Row *mostDue = nullptr;
  for (const Row &r : rows) {
    if (r.mode && r.due > (mostDue ? mostDue->due : 0)) {
      mostDue = &r;
    }
  }
  auto firstNew =
      std::ranges::find_if(rows, [](const Row &r) { return r.newLeft > 0; });

  // The table is made again only when its rows changed (a set added,
  // removed or moved): a refresh otherwise writes the counts in place, so
  // that the list keeps its scroll position and the buttons stay under
  // the mouse.
  bool same = sets_->topLevelItemCount() == static_cast<int>(rows.size());
  for (int i = 0; same && i < sets_->topLevelItemCount(); ++i) {
    const QTreeWidgetItem *item = sets_->topLevelItem(i);
    const Row &r = rows[static_cast<std::size_t>(i)];
    same = rowMode(item) == r.mode &&
           item->data(0, kSetIdRole).toString() == r.setId;
  }
  if (!same) {
    // The row picked stays picked.
    QString pickedSet;
    bool picked = false;
    std::optional<AppMode::Id> pickedMode;
    if (QTreeWidgetItem *item = sets_->currentItem();
        item && item->isSelected()) {
      picked = true;
      pickedSet = item->data(0, kSetIdRole).toString();
      pickedMode = rowMode(item);
    }
    const int rowH = rowHeight(this);
    sets_->clear();
    for (const Row &r : rows) {
      auto *item = new QTreeWidgetItem(sets_);
      item->setData(0, kSetIdRole, r.setId);
      if (r.mode) {
        item->setData(0, kModeRole, static_cast<int>(*r.mode));
      }
      for (int c = 1; c < 5; ++c) {
        item->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
      }
      setRowHeight(item, rowH);
      // What the row has waiting, as a button.
      auto *button = new QPushButton(sets_);
      button->setFocusPolicy(Qt::NoFocus);
      connect(button, &QPushButton::clicked, this,
              [this, id = r.setId, mode = r.mode] { open(id, mode); });
      sets_->setItemWidget(item, 5, spacedButton(button, sets_));
      if (picked && pickedMode == r.mode && (r.mode || pickedSet == r.setId)) {
        sets_->setCurrentItem(item);
      }
    }
    if (QTreeWidgetItem *item = sets_->currentItem(); item && picked) {
      item->setSelected(true);
    } else {
      sets_->setCurrentItem(nullptr);
      sets_->clearSelection();
    }
  }
  for (int i = 0; i < sets_->topLevelItemCount(); ++i) {
    QTreeWidgetItem *item = sets_->topLevelItem(i);
    const Row &r = rows[static_cast<std::size_t>(i)];
    item->setText(0, r.name);
    item->setText(1, count(r.due));
    item->setText(2, count(r.newLeft));
    item->setText(3, count(r.queued));
    item->setText(4, count(r.learned));
    QFont f = item->font(1);
    f.setBold(r.due > 0);
    item->setFont(1, f);
    if (QWidget *cell = sets_->itemWidget(item, 5)) {
      if (auto *button = cell->findChild<QPushButton *>()) {
        button->setText(r.go);
        button->setAccessibleName(tr("%1: %2").arg(r.go, r.name));
      }
    }
  }
  dueTotal_ = dueTotal;

  // The next thing to do: reviews first, then new cases.
  QString note;
  if (dueTotal > 0) {
    next_->setText(dueTotal == 1 ? tr("Review 1 due case")
                                 : tr("Review %1 due cases").arg(dueTotal));
    nextAction_ = [this] { reviewAll(); };
    note = tr("Every set's due cases, together.");
  } else if (mostDue) {
    next_->setText(mostDue->due == 1
                       ? tr("Review 1 due card")
                       : tr("Review %1 due cards").arg(mostDue->due));
    nextAction_ = [this, mode = mostDue->mode] { open({}, mode); };
    note = tr("Due in %1.").arg(mostDue->name);
  } else if (firstNew != rows.end()) {
    next_->setText(firstNew->mode ? tr("Learn new cards")
                                  : tr("Learn new cases"));
    nextAction_ = [this, id = firstNew->setId, mode = firstNew->mode] {
      open(id, mode);
    };
    note = tr("Nothing is due. Next in line: %1.").arg(firstNew->name);
  } else {
    next_->setText(tr("Nothing due"));
    nextAction_ = {};
    note = tr("No reviews are due and no new cases are left for today.");
  }
  next_->setEnabled(static_cast<bool>(nextAction_));
  // Set again on each refresh, so that the colours follow the theme.
  if (nextAction_) {
    // A stylesheet button keeps no hover or pressed look of its own.
    const QColor accent = palette().color(QPalette::Highlight);
    next_->setStyleSheet(
        QString("#todayNext { background: %1; color: "
                "palette(highlighted-text); border: none; border-radius: "
                "4px; padding: 7px 18px; }"
                "#todayNext:hover { background: %2; }"
                "#todayNext:pressed { background: %3; }")
            .arg(accent.name(), accent.lighter(110).name(),
                 accent.darker(110).name()));
  } else {
    next_->setStyleSheet("#todayNext { padding: 7px 18px; }");
  }
  next_->setAccessibleDescription(note);
  nextNote_->setText(note);
  hints_->setHint({}, {{tr("↑ ↓"), tr("pick a row")},
                       {tr("Enter"), tr("open it")},
                       {tr("R"), dueTotal > 0 ? tr("review all sets")
                                              : tr("review all (none due)")},
                       {tr("Space"), next_->text()}});

  // Activity.
  // A year and more back, for the streak.
  std::vector<ActivityEvent> all =
      activityEvents(data_, localDayStart() - 400 * kSecondsPerDay);
  std::int64_t today = localDayStart();
  QDate date = QDate::currentDate();
  std::int64_t week =
      QDateTime(date.addDays(-6), QTime(0, 0)).toSecsSinceEpoch();
  auto activity = summarizeActivity(all, ActivityMode::Count, today, week);
  static const std::pair<int, const char *> modeRows[] = {
      {ActivityMode::Learn, QT_TR_NOOP("Learn (cases)")},
      {ActivityMode::Drill, QT_TR_NOOP("Drill (cases)")},
      {ActivityMode::AlgLab, QT_TR_NOOP("Alg Lab (algs timed)")},
      {ActivityMode::Timed, QT_TR_NOOP("Timed (solves)")},
      {ActivityMode::Untimed, QT_TR_NOOP("Untimed (solves)")},
      {ActivityMode::MoveCount, QT_TR_NOOP("Move Count (solves)")},
      {ActivityMode::EoCross, QT_TR_NOOP("EO Cross (solves)")},
      {ActivityMode::F2L, QT_TR_NOOP("F2L (cards and solves)")},
      {ActivityMode::Tsle, QT_TR_NOOP("TSLE (cards and solves)")},
  };
  constexpr int kModeRows = static_cast<int>(std::size(modeRows));
  // The rows never change: they are made once, then written in place.
  if (modes_->topLevelItemCount() != kModeRows + 1) {
    modes_->clear();
    const int rowH = rowHeight(this);
    for (int i = 0; i <= kModeRows; ++i) {
      auto *item = new QTreeWidgetItem(modes_);
      for (int c = 1; c < 5; ++c) {
        item->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
      }
      setRowHeight(item, rowH);
    }
    QTreeWidgetItem *total = modes_->topLevelItem(kModeRows);
    for (int c = 0; c < 5; ++c) {
      QFont f = total->font(c);
      f.setBold(true);
      total->setFont(c, f);
    }
  }
  auto fill = [](QTreeWidgetItem *item, const QString &label,
                 const ActivityTotals &day, const ActivityTotals &days7) {
    item->setText(0, label);
    item->setText(1, count(day.count));
    item->setText(2, duration(day.ms));
    item->setText(3, count(days7.count));
    item->setText(4, duration(days7.ms));
  };
  ActivityTotals dayTotal, weekTotal;
  for (int i = 0; i < kModeRows; ++i) {
    const auto &[mode, label] = modeRows[i];
    const ModeActivity &a = activity[static_cast<std::size_t>(mode)];
    fill(modes_->topLevelItem(i), tr(label), a.today, a.week);
    dayTotal.count += a.today.count;
    dayTotal.ms += a.today.ms;
    weekTotal.count += a.week.count;
    weekTotal.ms += a.week.ms;
  }
  fill(modes_->topLevelItem(kModeRows), tr("All modes"), dayTotal, weekTotal);

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
        plural(streak.days, tr("Practice streak: %n day, today included."),
               tr("Practice streak: %n days, today included.")));
  } else {
    streakLabel_->setText(plural(streak.days,
                                 tr("Practice streak: %n day. Practise today "
                                    "to keep it going."),
                                 tr("Practice streak: %n days. Practise today "
                                    "to keep it going.")));
  }
}

} // namespace vcube
