#include "app/timer_page.hpp"

#include "train/case_logic.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace vcube {

namespace {

QPushButton *flatButton(const QString &text, QWidget *parent) {
  auto *b = new QPushButton(text, parent);
  b->setFocusPolicy(Qt::NoFocus);
  return b;
}

QString statText(StatValue v, int decimals) {
  return QString::fromStdString(formatTime(v, decimals));
}

} // namespace

TimerPage::TimerPage(AppSettings &settings, SessionStore &store,
                     AlgLibrary &library, QWidget *parent)
    : QWidget(parent), settings_(settings), store_(store), library_(library) {
  // --- left column: sessions, stats, times ---
  auto *left = new QWidget(this);
  auto *leftLayout = new QVBoxLayout(left);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  auto *sessionRow = new QHBoxLayout;
  sessionBox_ = new QComboBox(left);
  sessionBox_->setFocusPolicy(Qt::NoFocus);
  sessionBox_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  auto *addButton = flatButton("+", left);
  addButton->setToolTip(tr("New session"));
  auto *renameButton = flatButton(tr("Rename"), left);
  auto *removeButton = flatButton(tr("Delete"), left);
  for (auto *b : {addButton}) {
    b->setMaximumWidth(32);
  }
  sessionRow->addWidget(sessionBox_);
  sessionRow->addWidget(addButton);
  sessionRow->addWidget(renameButton);
  sessionRow->addWidget(removeButton);
  leftLayout->addLayout(sessionRow);

  stats_ = new QLabel(left);
  stats_->setTextFormat(Qt::RichText);
  stats_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  leftLayout->addWidget(stats_);

  table_ = new QTableWidget(0, 4, left);
  table_->setHorizontalHeaderLabels({"#", tr("Time"), "ao5", "ao12"});
  table_->verticalHeader()->setVisible(false);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setFocusPolicy(Qt::NoFocus);
  table_->setContextMenuPolicy(Qt::CustomContextMenu);
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  leftLayout->addWidget(table_, 1);

  // --- right column: scramble, timer, cube, chart ---
  auto *right = new QWidget(this);
  auto *rightLayout = new QVBoxLayout(right);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  auto *typeRow = new QHBoxLayout;
  typeBox_ = new QComboBox(right);
  typeBox_->setFocusPolicy(Qt::NoFocus);
  virtualBox_ = new QCheckBox(tr("Virtual cube"), right);
  virtualBox_->setFocusPolicy(Qt::NoFocus);
  virtualBox_->setToolTip(tr("Solve on the keyboard-controlled cube: Space scrambles, "
                             "the first turn starts the timer, solving stops it, Esc aborts"));
  auto *nextButton = flatButton(tr("Next scramble"), right);
  typeRow->addWidget(new QLabel(tr("Scramble:"), right));
  typeRow->addWidget(typeBox_, 1);
  typeRow->addWidget(virtualBox_);
  typeRow->addWidget(nextButton);
  rightLayout->addLayout(typeRow);

  scrambleLabel_ = new QLabel(right);
  scrambleLabel_->setWordWrap(true);
  scrambleLabel_->setAlignment(Qt::AlignCenter);
  scrambleLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  QFont sf = scrambleLabel_->font();
  sf.setPointSize(17);
  scrambleLabel_->setFont(sf);
  scrambleLabel_->setMinimumHeight(60);
  rightLayout->addWidget(scrambleLabel_);

  auto *middle = new QHBoxLayout;
  auto *timeColumn = new QVBoxLayout;
  display_ = new TimeDisplay(right);
  statusLabel_ = new QLabel(right);
  statusLabel_->setAlignment(Qt::AlignCenter);
  auto *penaltyRow = new QHBoxLayout;
  plus2Button_ = flatButton("+2", right);
  dnfButton_ = flatButton("DNF", right);
  deleteButton_ = flatButton(tr("Delete"), right);
  plus2Button_->setToolTip(tr("Toggle +2 on the last solve"));
  dnfButton_->setToolTip(tr("Toggle DNF on the last solve"));
  deleteButton_->setToolTip(tr("Delete the last solve"));
  penaltyRow->addStretch();
  penaltyRow->addWidget(plus2Button_);
  penaltyRow->addWidget(dnfButton_);
  penaltyRow->addWidget(deleteButton_);
  penaltyRow->addStretch();
  timeColumn->addStretch();
  timeColumn->addWidget(display_);
  timeColumn->addWidget(statusLabel_);
  timeColumn->addLayout(penaltyRow);
  timeColumn->addStretch();
  middle->addLayout(timeColumn, 3);
  cubeView_ = new CubeView(right);
  cubeView_->setMinimumWidth(260);
  middle->addWidget(cubeView_, 2);
  rightLayout->addLayout(middle, 3);

  chart_ = new TrendChart(right);
  rightLayout->addWidget(chart_, 1);

  auto *split = new QSplitter(this);
  split->addWidget(left);
  split->addWidget(right);
  split->setStretchFactor(0, 0);
  split->setStretchFactor(1, 1);
  split->setSizes({300, 800});
  auto *layout = new QHBoxLayout(this);
  layout->addWidget(split);

  // --- wiring ---
  connect(&tick_, &QTimer::timeout, this, &TimerPage::onTick);
  tick_.start(16);
  connect(&watcher_, &QFutureWatcher<GeneratedScramble>::finished, this,
          &TimerPage::scrambleReady);
  connect(nextButton, &QPushButton::clicked, this, [this] {
    if (!isBusy()) {
      current_.reset();
      showScramble();
      requestScramble();
    }
  });
  connect(sessionBox_, &QComboBox::activated, this, [this](int i) {
    store_.setCurrentIndex(static_cast<std::size_t>(i));
    int t = typeBox_->findData(qs(session().scrambleType));
    if (t >= 0 && t != typeBox_->currentIndex()) {
      typeBox_->setCurrentIndex(t);
    }
    refreshSolves();
    save();
  });
  connect(typeBox_, &QComboBox::currentIndexChanged, this, [this] {
    session().scrambleType = ss(currentType());
    save();
    current_.reset();
    prefetched_.reset();
    showScramble();
    requestScramble();
  });
  connect(addButton, &QPushButton::clicked, this, &TimerPage::addSession);
  connect(renameButton, &QPushButton::clicked, this, &TimerPage::renameSession);
  connect(removeButton, &QPushButton::clicked, this, &TimerPage::deleteSession);
  connect(virtualBox_, &QCheckBox::toggled, this, &TimerPage::setVirtual);
  connect(plus2Button_, &QPushButton::clicked, this, [this] { setLastPenalty(Penalty::Plus2); });
  connect(dnfButton_, &QPushButton::clicked, this, [this] { setLastPenalty(Penalty::Dnf); });
  connect(deleteButton_, &QPushButton::clicked, this, [this] {
    if (!session().solves.empty()) {
      deleteSolve(static_cast<int>(session().solves.size()) - 1);
    }
  });
  connect(table_, &QTableWidget::customContextMenuRequested, this, &TimerPage::showSolveMenu);

  QSettings q;
  virtualBox_->setChecked(q.value("timer/virtual", false).toBool());
  cubeView_->setMode(static_cast<CubeView::Mode>(q.value("timer/cubeMode", 0).toInt()));
  connect(cubeView_, &CubeView::modeChanged, this, [](CubeView::Mode m) {
    QSettings().setValue("timer/cubeMode", static_cast<int>(m));
  });
  setVirtual(virtualBox_->isChecked());
  applySettings();
  refreshScrambleTypes();
  refreshSessionBox();
  refreshSolves();
}

bool TimerPage::isBusy() const {
  auto s = timer_.state();
  return s == SolveTimer::State::Running || s == SolveTimer::State::Inspecting ||
         s == SolveTimer::State::Holding || s == SolveTimer::State::Ready;
}

void TimerPage::applySettings() {
  timer_.setConfig({settings_.holdMs, settings_.inspection, 15000});
  display_->setDecimals(settings_.decimals);
  CubeLook look = cubeView_->look();
  look.scheme = settings_.scheme;
  cubeView_->setLook(look);
  cubeView_->setAnimationMs(settings_.animationMs);
  refreshSolves();
}

QString TimerPage::currentType() const {
  QString t = typeBox_->currentData().toString();
  return t.isEmpty() ? QString("333") : t;
}

void TimerPage::refreshScrambleTypes() {
  QString previous = typeBox_->count() ? currentType() : qs(session().scrambleType);
  QSignalBlocker block(typeBox_);
  typeBox_->clear();
  for (const auto &t : scrambleTypes()) {
    typeBox_->addItem(qs(t.name), qs(t.id));
  }
  if (!library_.sets().empty()) {
    typeBox_->insertSeparator(typeBox_->count());
    for (const auto &set : library_.sets()) {
      if (set.caseCount() > 0) {
        typeBox_->addItem(tr("Alg set: %1").arg(qs(set.name)), "set:" + qs(set.id));
      }
    }
  }
  int i = typeBox_->findData(previous);
  typeBox_->setCurrentIndex(i >= 0 ? i : 0);
  if (!current_ || previous != currentType()) {
    current_.reset();
    prefetched_.reset();
    showScramble();
    requestScramble();
  }
}

std::function<CubieCube(Rng &)> TimerPage::stateMaker(const QString &type) const {
  if (type.startsWith("set:")) {
    std::string id = ss(type.mid(4));
    const AlgSet *set = const_cast<AlgLibrary &>(library_).find(id);
    if (set && set->caseCount() > 0) {
      // Practise the trainer's current selection for the set (or all).
      QStringList selected =
          QSettings().value(QString("trainer/sets/%1/selection").arg(qs(id))).toStringList();
      struct Entry {
        CubieCube state;
        PieceMask free;
      };
      std::vector<Entry> pool;
      for (const auto &g : set->groups) {
        for (const auto &c : g.cases) {
          if (selected.isEmpty() || selected.contains(qs(c.id))) {
            pool.push_back({c.state, set->specFor(g).free});
          }
        }
      }
      if (pool.empty()) {
        for (const auto &g : set->groups) {
          for (const auto &c : g.cases) {
            pool.push_back({c.state, set->specFor(g).free});
          }
        }
      }
      bool before = set->aufBefore, after = set->aufAfter;
      return [pool, before, after](Rng &rng) {
        std::uniform_int_distribution<std::size_t> pick(0, pool.size() - 1);
        std::uniform_int_distribution<int> quarter(0, 3);
        const Entry &e = pool[pick(rng)];
        CubieCube s = randomState(e.free, rng) * e.state;
        if (before) {
          s = s * uTurn(quarter(rng));
        }
        if (after) {
          s = uTurn(quarter(rng)) * s;
        }
        return s.isSolved() ? randomState(PieceMask::all(), rng) : s;
      };
    }
  }
  const ScrambleType *t = findScrambleType(ss(type));
  ScrambleType st = t ? *t : scrambleTypes().front();
  return [st](Rng &rng) { return randomStateOf(st, rng); };
}

void TimerPage::requestScramble() {
  if (watcher_.isRunning()) {
    wantNext_ = true;
    return;
  }
  wantNext_ = false;
  prefetchedType_ = currentType();
  watcher_.setFuture(generateScramble(stateMaker(prefetchedType_)));
}

void TimerPage::scrambleReady() {
  GeneratedScramble result = watcher_.result();
  bool sameType = prefetchedType_ == currentType();
  if (sameType) {
    if (!current_) {
      current_ = result;
      showScramble();
      requestScramble(); // prefetch the one after
      return;
    }
    if (!prefetched_) {
      prefetched_ = result;
    }
  }
  if (wantNext_ || !current_ || !prefetched_) {
    requestScramble();
  }
}

void TimerPage::showScramble() {
  if (!current_) {
    scrambleLabel_->setText(tr("Generating scramble…"));
    return;
  }
  scrambleLabel_->setText(current_->text);
  if (virtualBox_->isChecked() && !isBusy()) {
    statusLabel_->setText(tr("Press Space to scramble the virtual cube"));
  }
}

void TimerPage::onTick() {
  std::int64_t now = monotonicMs();
  timer_.tick(now);
  if (isBusy()) {
    display_->showTimer(timer_, now);
  }
}

bool TimerPage::handleKey(QKeyEvent *e, bool press) {
  if (e->isAutoRepeat()) {
    return e->key() == Qt::Key_Space;
  }
  std::int64_t now = monotonicMs();
  using S = SolveTimer::State;

  if (virtualBox_->isChecked()) {
    if (!press) {
      return false;
    }
    if (e->key() == Qt::Key_Escape) {
      if (timer_.state() == S::Running) {
        timer_.stopNow(now);
        timer_.takeResult();
        recordSolve(timer_.elapsed(now), Penalty::Dnf);
      } else {
        timer_.cancel();
        display_->showTime(0);
      }
      virtualScrambled_ = false;
      cube_ = FaceletCube();
      cubeView_->setState(cube_);
      statusLabel_->setText(tr("Press Space to scramble the virtual cube"));
      return true;
    }
    if (e->key() == Qt::Key_Space) {
      if (timer_.state() == S::Running || !current_) {
        return true;
      }
      // Scramble the virtual cube with the current scramble.
      cube_ = FaceletCube();
      cube_.apply(parseAlgOrEmpty(ss(current_->text)));
      cubeView_->setState(cube_);
      virtualScrambled_ = true;
      solveMoves_.clear();
      timer_.arm(now);
      display_->showTimer(timer_, now);
      statusLabel_->setText(timer_.isInspecting() ? tr("Inspecting: the first turn starts the timer")
                                                  : tr("The first turn starts the timer"));
      return true;
    }
    if (auto m = settings_.keymap.lookup(e)) {
      virtualMove(*m);
      return true;
    }
    return false;
  }

  // Physical cube with the space bar.
  if (timer_.state() == S::Running) {
    if (press) {
      timer_.pressStop(now);
      if (auto r = timer_.takeResult()) {
        recordSolve(r->timeMs, r->penalty);
      }
      return true;
    }
    return false;
  }
  if (e->key() == Qt::Key_Escape && press) {
    timer_.cancel();
    display_->showTime(0);
    statusLabel_->clear();
    return true;
  }
  if (e->key() != Qt::Key_Space) {
    return false;
  }
  if (press) {
    if (!current_ && timer_.state() != S::Inspecting) {
      return true; // wait for a scramble
    }
    timer_.pressStart(now);
  } else {
    timer_.releaseStart(now);
    if (timer_.state() == S::Running) {
      statusLabel_->clear();
    }
  }
  display_->showTimer(timer_, now);
  return true;
}

void TimerPage::virtualMove(Move m) {
  std::int64_t now = monotonicMs();
  using S = SolveTimer::State;
  cube_.apply(m);
  cubeView_->animateMove(m);
  if (!virtualScrambled_) {
    return; // free play
  }
  if ((timer_.state() == S::Ready || timer_.state() == S::Inspecting) && !isRotation(m.type)) {
    timer_.startNow(now);
    statusLabel_->clear();
  }
  if (timer_.state() == S::Running) {
    solveMoves_ << qs(toString(m));
    if (cube_.isSolved()) {
      timer_.stopNow(now);
      if (auto r = timer_.takeResult()) {
        recordSolve(r->timeMs, r->penalty);
      }
      virtualScrambled_ = false;
    }
  }
}

void TimerPage::recordSolve(std::int64_t timeMs, Penalty penalty) {
  Solve s;
  s.timeMs = timeMs;
  s.penalty = penalty;
  s.scramble = current_ ? ss(current_->text) : std::string();
  s.timestamp = unixNow();
  if (virtualBox_->isChecked()) {
    s.moves = ss(solveMoves_.join(' '));
    solveMoves_.clear();
  }
  session().solves.push_back(s);
  display_->showTime(timeMs, penalty);
  statusLabel_->setText(virtualBox_->isChecked() ? tr("Press Space to scramble the virtual cube")
                                                 : QString());
  save();
  refreshSolves();
  // Move on to the next scramble.
  if (prefetched_ && prefetchedType_ == currentType()) {
    current_ = prefetched_;
    prefetched_.reset();
    showScramble();
    requestScramble();
  } else {
    current_.reset();
    showScramble();
    requestScramble();
  }
}

void TimerPage::refreshSessionBox() {
  QSignalBlocker block(sessionBox_);
  sessionBox_->clear();
  for (const auto &s : store_.sessions()) {
    sessionBox_->addItem(qs(s.name));
  }
  sessionBox_->setCurrentIndex(static_cast<int>(store_.currentIndex()));
}

void TimerPage::refreshSolves() {
  const auto &solves = session().solves;
  int d = settings_.decimals;
  auto ao5 = rollingAverages(solves, 5);
  auto ao12 = rollingAverages(solves, 12);
  table_->setRowCount(static_cast<int>(solves.size()));
  for (std::size_t i = 0; i < solves.size(); ++i) {
    int row = static_cast<int>(solves.size() - 1 - i);
    auto set = [&](int col, const QString &text) {
      auto *item = new QTableWidgetItem(text);
      item->setData(Qt::UserRole, static_cast<int>(i));
      if (col > 0) {
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      }
      table_->setItem(row, col, item);
    };
    set(0, QString::number(i + 1));
    set(1, qs(formatSolve(solves[i], d)));
    set(2, statText(ao5[i], d));
    set(3, statText(ao12[i], d));
    if (!solves[i].comment.empty()) {
      table_->item(row, 1)->setToolTip(qs(solves[i].comment));
    }
  }
  SessionSummary sum = summarize(solves);
  QString html = "<table cellspacing='4' width='100%'><tr><th></th><th align='right'>" +
                 tr("current") + "</th><th align='right'>" + tr("best") + "</th></tr>";
  auto row = [&](const QString &name, StatValue cur, StatValue best) {
    html += QString("<tr><td>%1</td><td align='right'>%2</td><td align='right'><b>%3</b></td></tr>")
                .arg(name, statText(cur, d), statText(best, d));
  };
  row(tr("time"), solves.empty() ? StatValue{} : StatValue{effectiveTime(solves.back())}, sum.best);
  row("mo3", currentMean(solves, 3), bestMean(solves, 3));
  row("ao5", currentAverage(solves, 5), bestAverage(solves, 5));
  row("ao12", currentAverage(solves, 12), bestAverage(solves, 12));
  row("ao100", currentAverage(solves, 100), bestAverage(solves, 100));
  html += "</table>";
  html += tr("<p>Solves: %1%2 &nbsp; Mean: %3</p>")
              .arg(sum.count)
              .arg(sum.dnfs ? QString(" (%1 DNF)").arg(sum.dnfs) : QString())
              .arg(statText(sum.mean, d));
  stats_->setText(html);
  chart_->setSolves(solves);
  bool any = !solves.empty();
  plus2Button_->setEnabled(any);
  dnfButton_->setEnabled(any);
  deleteButton_->setEnabled(any);
}

void TimerPage::setLastPenalty(Penalty p) {
  if (session().solves.empty()) {
    return;
  }
  Solve &s = session().solves.back();
  s.penalty = s.penalty == p ? Penalty::None : p;
  display_->showTime(s.timeMs, s.penalty);
  save();
  refreshSolves();
}

void TimerPage::deleteSolve(int index) {
  auto &solves = session().solves;
  if (index < 0 || index >= static_cast<int>(solves.size())) {
    return;
  }
  solves.erase(solves.begin() + index);
  save();
  refreshSolves();
}

void TimerPage::showSolveMenu(const QPoint &pos) {
  auto *item = table_->itemAt(pos);
  if (!item) {
    return;
  }
  int index = item->data(Qt::UserRole).toInt();
  auto &solves = session().solves;
  if (index < 0 || index >= static_cast<int>(solves.size())) {
    return;
  }
  Solve &s = solves[static_cast<std::size_t>(index)];
  QMenu menu(this);
  auto *ok = menu.addAction(tr("No penalty"));
  auto *plus2 = menu.addAction("+2");
  auto *dnf = menu.addAction("DNF");
  ok->setCheckable(true);
  plus2->setCheckable(true);
  dnf->setCheckable(true);
  ok->setChecked(s.penalty == Penalty::None);
  plus2->setChecked(s.penalty == Penalty::Plus2);
  dnf->setChecked(s.penalty == Penalty::Dnf);
  menu.addSeparator();
  auto *comment = menu.addAction(tr("Comment…"));
  auto *copy = menu.addAction(tr("Copy scramble"));
  QAction *copyMoves = nullptr;
  if (!s.moves.empty()) {
    copyMoves = menu.addAction(tr("Copy solution moves"));
  }
  menu.addSeparator();
  auto *del = menu.addAction(tr("Delete"));
  QAction *chosen = menu.exec(table_->viewport()->mapToGlobal(pos));
  if (!chosen) {
    return;
  }
  if (chosen == ok) {
    s.penalty = Penalty::None;
  } else if (chosen == plus2) {
    s.penalty = Penalty::Plus2;
  } else if (chosen == dnf) {
    s.penalty = Penalty::Dnf;
  } else if (chosen == comment) {
    bool accepted = false;
    QString text = QInputDialog::getText(this, tr("Comment"), tr("Comment:"), QLineEdit::Normal,
                                         qs(s.comment), &accepted);
    if (accepted) {
      s.comment = ss(text);
    }
  } else if (chosen == copy) {
    QApplication::clipboard()->setText(qs(s.scramble));
    return;
  } else if (copyMoves && chosen == copyMoves) {
    QApplication::clipboard()->setText(qs(s.moves));
    return;
  } else if (chosen == del) {
    deleteSolve(index);
    return;
  }
  save();
  refreshSolves();
}

void TimerPage::addSession() {
  bool ok = false;
  QString name = QInputDialog::getText(
      this, tr("New session"), tr("Name:"), QLineEdit::Normal,
      tr("Session %1").arg(store_.sessions().size() + 1), &ok);
  if (!ok || name.trimmed().isEmpty()) {
    return;
  }
  store_.addSession(ss(name.trimmed())).scrambleType = ss(currentType());
  save();
  refreshSessionBox();
  refreshSolves();
}

void TimerPage::renameSession() {
  bool ok = false;
  QString name = QInputDialog::getText(this, tr("Rename session"), tr("Name:"),
                                       QLineEdit::Normal, qs(session().name), &ok);
  if (!ok || name.trimmed().isEmpty()) {
    return;
  }
  session().name = ss(name.trimmed());
  save();
  refreshSessionBox();
}

void TimerPage::deleteSession() {
  auto answer = QMessageBox::question(
      this, tr("Delete session"),
      tr("Delete session \"%1\" and its %2 solves?")
          .arg(qs(session().name))
          .arg(session().solves.size()));
  if (answer != QMessageBox::Yes) {
    return;
  }
  store_.removeSession(store_.currentIndex());
  save();
  refreshSessionBox();
  refreshSolves();
}

void TimerPage::reloadSessions() {
  refreshSessionBox();
  int t = typeBox_->findData(qs(session().scrambleType));
  if (t >= 0) {
    typeBox_->setCurrentIndex(t);
  }
  refreshSolves();
}

void TimerPage::setVirtual(bool on) {
  QSettings().setValue("timer/virtual", on);
  timer_.cancel();
  virtualScrambled_ = false;
  cube_ = FaceletCube();
  cubeView_->setState(cube_);
  cubeView_->setVisible(on);
  statusLabel_->setText(on ? tr("Press Space to scramble the virtual cube") : QString());
}

void TimerPage::save() {
  QString error;
  if (!store_.save(&error)) {
    emit message(tr("Could not save sessions: %1").arg(error));
  }
}

} // namespace vcube
