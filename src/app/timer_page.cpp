#include "app/timer_page.hpp"

#include "app/review_dialog.hpp"
#include "app/ui_state.hpp"
#include "io/data_file.hpp"
#include "train/step_solver.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

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

QString dateText(std::int64_t unixSeconds) {
  if (unixSeconds <= 0) {
    return {};
  }
  return QLocale().toString(QDateTime::fromSecsSinceEpoch(unixSeconds),
                            QLocale::ShortFormat);
}

// The table's columns. Those after Ao12 are for virtual sessions.
enum Column {
  kNumber,
  kTime,
  kAo5,
  kAo12,
  kMoves,
  kTps,
  kCross,
  kF2l,
  kLl,
  kColumns
};
QStringList hiddenColumns() { return saved::timedHiddenColumns().get(); }

SolveSplits computeSplits(const Solve &s, bool simplified) {
  ParseResult scramble = parseAlg(s.scramble);
  ParseResult moves = parseAlg(s.moves);
  if (!scramble || !moves) {
    return {};
  }
  FaceletCube start;
  start.apply(scramble.alg);
  return solveSplits(start, moves.alg, s.moveTimes, s.timeMs, simplified);
}

} // namespace

TimerPage::TimerPage(AppSettings &settings, SessionStore &store,
                     AlgLibrary &library, QWidget *parent)
    : ModePage(parent), settings_(settings), store_(store), library_(library),
      feed_(library) {
  // --- left column: sessions, stats, times ---
  auto *left = leftPanel_ = new QWidget(this);
  auto *leftLayout = new QVBoxLayout(left);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  sessionButton_ = new QToolButton(left);
  sessionButton_->setFocusPolicy(Qt::NoFocus);
  sessionButton_->setPopupMode(QToolButton::InstantPopup);
  sessionButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  sessionButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  sessionButton_->setToolTip(
      tr("Switch, add, rename or delete sessions, or import and export "
         "cstimer sessions"));
  sessionMenu_ = new QMenu(sessionButton_);
  sessionButton_->setMenu(sessionMenu_);
  leftLayout->addWidget(sessionButton_);

  stats_ = new QLabel(left);
  stats_->setTextFormat(Qt::RichText);
  stats_->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                  Qt::LinksAccessibleByMouse);
  stats_->setFocusPolicy(Qt::NoFocus);
  stats_->setToolTip(tr("Click a statistic to see its solves"));
  leftLayout->addWidget(stats_);

  table_ = new QTableWidget(0, kColumns, left);
  table_->setHorizontalHeaderLabels({"#", tr("Time"), "ao5", "ao12",
                                     tr("Moves"), tr("TPS"), tr("Cross"),
                                     tr("F2L"), tr("LL")});
  table_->horizontalHeaderItem(kMoves)->setToolTip(
      tr("The solve's move count (HTM)"));
  table_->horizontalHeaderItem(kTps)->setToolTip(
      tr("Moves per second over the whole solve"));
  table_->horizontalHeaderItem(kCross)->setToolTip(
      tr("Time to the cross (EO Cross in a ZZ solve)"));
  table_->horizontalHeaderItem(kF2l)->setToolTip(
      tr("Time from the cross to the end of the F2L"));
  table_->horizontalHeaderItem(kLl)->setToolTip(
      tr("Time from the F2L to the end of the solve"));
  table_->verticalHeader()->setVisible(false);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setFocusPolicy(Qt::NoFocus);
  table_->setContextMenuPolicy(Qt::CustomContextMenu);
  table_->horizontalHeader()->setSectionResizeMode(
      QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
  table_->horizontalHeader()->setToolTip(
      tr("Right-click to choose the columns"));
  leftLayout->addWidget(table_, 1);

  auto *listRow = new QHBoxLayout;
  deleteButton_ = flatButton(tr("Delete last"), left);
  deleteButton_->setToolTip(tr("Delete the last solve (Delete)"));
  undoButton_ = flatButton(tr("Undo delete"), left);
  undoButton_->setToolTip(tr("Put back the solve last deleted (Ctrl+Z)"));
  undoButton_->setVisible(false);
  listRow->addWidget(deleteButton_);
  listRow->addWidget(undoButton_);
  listRow->addStretch();
  leftLayout->addLayout(listRow);

  // --- right column: scramble, timer, chart ---
  auto *right = new QWidget(this);
  auto *rightLayout = new QVBoxLayout(right);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  typeRow_ = new QWidget(right);
  auto *typeRow = new QHBoxLayout(typeRow_);
  typeRow->setContentsMargins(0, 0, 0, 0);
  typeBox_ = new ScrambleTypeBox(library_, typeRow_);
  nextButton_ = flatButton(tr("Skip scramble"), typeRow_);
  nextButton_->setToolTip(
      tr("Go on to the next scramble without solving this one (Ctrl+→)"));
  cubeBox_ = new QComboBox(typeRow_);
  cubeBox_->setFocusPolicy(Qt::NoFocus);
  cubeBox_->addItem(tr("Virtual cube"));
  cubeBox_->addItem(tr("Physical cube"));
  cubeBox_->setToolTip(tr("The cube this session's solves are done on (fixed "
                          "once the session has solves)"));
  cubeView_ = new CubeView(right);
  cubeView_->setToolTip(
      tr("Turn the cube as a whole to look at it, hold Space to start the "
         "timer, and solve"));
  viewPicker_ = new QWidget(typeRow_);
  auto *viewLayout = new QHBoxLayout(viewPicker_);
  viewLayout->setContentsMargins(0, 0, 0, 0);
  viewLayout->addWidget(new QLabel(tr("View:"), viewPicker_));
  viewLayout->addWidget(createViewSelector(cubeView_, "timed", viewPicker_));
  typeRow->addWidget(new QLabel(tr("Scramble:"), typeRow_));
  algs_ = new ScrambleAlgs(library_, "timer/showAlgs", right);
  typeRow->addWidget(typeBox_, 1);
  typeRow->addWidget(algs_->button());
  typeRow->addSpacing(12);
  typeRow->addWidget(cubeBox_);
  typeRow->addWidget(viewPicker_);
  typeRow->addWidget(nextButton_);
  rightLayout->addWidget(typeRow_);

  auto *scrambleRow = new QHBoxLayout;
  auto *scrambleColumn = new QVBoxLayout;
  scrambleLabel_ = new QLabel(right);
  scrambleLabel_->setWordWrap(true);
  scrambleLabel_->setAlignment(Qt::AlignCenter);
  scrambleLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  QFont sf = scrambleLabel_->font();
  sf.setPointSize(17);
  scrambleLabel_->setFont(sf);
  markScrambleLabel(scrambleLabel_);
  scrambleLabel_->setMinimumHeight(60);
  holdLabel_ = new QLabel(right);
  holdLabel_->setAlignment(Qt::AlignCenter);
  holdLabel_->setTextFormat(Qt::RichText);
  holdLabel_->setToolTip(
      tr("The hold your colour neutrality picked for this scramble"));
  scrambleColumn->addWidget(scrambleLabel_);
  scrambleColumn->addWidget(holdLabel_);
  net_ = new NetPreview(right);
  scrambleRow->addLayout(scrambleColumn, 1);
  scrambleRow->addWidget(net_);
  rightLayout->addLayout(scrambleRow);

  auto *timeColumn = new QVBoxLayout;
  display_ = new TimeDisplay(right);
  statusLabel_ = new QLabel(right);
  statusLabel_->setAlignment(Qt::AlignCenter);
  statusLabel_->setWordWrap(true);
  focusStats_ = new QLabel(right);
  focusStats_->setAlignment(Qt::AlignCenter);
  focusStats_->setForegroundRole(QPalette::PlaceholderText);
  focusStats_->setVisible(false);
  auto *penaltyRow = new QHBoxLayout;
  plus2Button_ = flatButton("+2", right);
  dnfButton_ = flatButton("DNF", right);
  reviewButton_ = flatButton(tr("Review last"), right);
  plus2Button_->setToolTip(tr("Toggle +2 on the last solve (Alt+2)"));
  dnfButton_->setToolTip(tr("Toggle DNF on the last solve (Alt+3)"));
  reviewButton_->setToolTip(
      tr("Replay the last solve with its steps and splits (Alt+R; "
         "double-click any solve to review it)"));
  penaltyRow->addStretch();
  penaltyRow->addWidget(plus2Button_);
  penaltyRow->addWidget(dnfButton_);
  penaltyRow->addWidget(reviewButton_);
  penaltyRow->addStretch();
  timeColumn->addStretch(1);
  timeColumn->addWidget(cubeView_, 8);
  timeColumn->addWidget(display_);
  timeColumn->addWidget(statusLabel_);
  timeColumn->addWidget(focusStats_);
  timeColumn->addLayout(penaltyRow);
  timeColumn->addWidget(algs_->label());
  timeColumn->addStretch(1);
  rightLayout->addLayout(timeColumn, 3);

  chart_ = new TrendChart(right);
  chart_->setSettingsKey("timed/chart");
  rightLayout->addWidget(chart_, 1);

  auto *split = new QSplitter(this);
  split->addWidget(left);
  split->addWidget(right);
  split->setStretchFactor(0, 0);
  split->setStretchFactor(1, 1);
  split->setSizes({340, 760});
  auto *layout = new QHBoxLayout(this);
  layout->addWidget(split);

  // --- wiring ---
  connect(&tick_, &QTimer::timeout, this, &TimerPage::onTick);
  connect(&feed_, &ScrambleFeed::currentChanged, this,
          &TimerPage::showScramble);
  connect(nextButton_, &QPushButton::clicked, this, &TimerPage::nextScramble);
  connect(typeBox_, &ScrambleTypeBox::typeChanged, this,
          [this](const QString &type) {
            if (session().scrambleType != ss(type)) {
              session().scrambleType = ss(type);
              save();
            }
            finished_ = false;
            algs_->setType(type);
            feed_.setType(type);
          });
  connect(cubeBox_, &QComboBox::activated, this, [this](int index) {
    CubeKind kind = index == 0 ? CubeKind::Virtual : CubeKind::Physical;
    if (isBusy() || session().cube == kind) {
      return;
    }
    if (!session().solves.empty()) {
      // Its solves were done on the other cube.
      cubeBox_->setCurrentIndex(onVirtualCube() ? 0 : 1);
      emit message(tr("This session has solves on the other cube: start a "
                      "new session for this one"));
      return;
    }
    session().cube = kind;
    finished_ = false;
    save();
    showCubeKind();
    showScramble();
    refreshSolves();
  });
  connect(sessionMenu_, &QMenu::aboutToShow, this,
          &TimerPage::refreshSessionMenu);
  connect(plus2Button_, &QPushButton::clicked, this,
          [this] { setLastPenalty(Penalty::Plus2); });
  connect(dnfButton_, &QPushButton::clicked, this,
          [this] { setLastPenalty(Penalty::Dnf); });
  connect(deleteButton_, &QPushButton::clicked, this, [this] {
    if (!session().solves.empty()) {
      deleteSolve(static_cast<int>(session().solves.size()) - 1);
    }
  });
  connect(undoButton_, &QPushButton::clicked, this, &TimerPage::undoDelete);
  connect(reviewButton_, &QPushButton::clicked, this,
          [this] { review(static_cast<int>(session().solves.size()) - 1); });
  connect(table_, &QTableWidget::customContextMenuRequested, this,
          &TimerPage::showSolveMenu);
  connect(table_->horizontalHeader(), &QWidget::customContextMenuRequested,
          this, &TimerPage::showColumnMenu);
  connect(table_, &QTableWidget::itemDoubleClicked, this,
          [this](QTableWidgetItem *item) {
            int index = item->data(Qt::UserRole).toInt();
            const auto &solves = session().solves;
            if (index >= 0 && index < static_cast<int>(solves.size()) &&
                !solves[static_cast<std::size_t>(index)].moves.empty()) {
              review(index);
            } else {
              showDetails(index);
            }
          });
  connect(stats_, &QLabel::linkActivated, this, &TimerPage::showStat);
  connect(&splitsWatcher_, &QFutureWatcher<SplitResults>::finished, this,
          [this] {
            for (auto &[key, s] : splitsWatcher_.result()) {
              splits_[key] = std::move(s);
            }
            refreshSolves();
          });
  connect(
      &eoWatcher_, &QFutureWatcher<std::optional<int>>::finished, this, [this] {
        std::optional<int> best = eoWatcher_.result();
        if (!best || isBusy()) {
          return;
        }
        QString yours = eoYours_ ? QString::number(*eoYours_) : tr("not done");
        QString line =
            tr("EO Cross: yours %1, optimal %2").arg(yours).arg(*best);
        QString was = statusLabel_->text();
        statusLabel_->setText(was.isEmpty() ? line : was + "\n" + line);
      });

  applySettings();
  selectSession(store_.currentIndex());
  algs_->setType(typeBox_->currentType());
  feed_.setType(typeBox_->currentType()); // if the type didn't change
}

TimerPage::~TimerPage() {
  splitsWatcher_.waitForFinished();
  eoWatcher_.waitForFinished();
}

bool TimerPage::isBusy() const {
  auto s = timer_.state();
  return s == SolveTimer::State::Running ||
         s == SolveTimer::State::Inspecting ||
         s == SolveTimer::State::Holding || s == SolveTimer::State::Ready;
}

void TimerPage::activated() { updateControls(); }

bool TimerPage::onVirtualCube() const {
  return store_.sessions()[store_.currentIndex()].cube == CubeKind::Virtual;
}

bool TimerPage::cubeHidden() const {
  if (!onVirtualCube() || !settings_.inspection ||
      !settings_.hideBeforeInspection || finished_) {
    return false;
  }
  auto s = timer_.state();
  return s == SolveTimer::State::Idle || s == SolveTimer::State::Stopped;
}

void TimerPage::showCube() {
  if (cubeHidden()) {
    FaceletCube::Stickers blank{};
    blank.fill(kMaskedColour);
    cubeView_->setState(FaceletCube(blank));
    cubeView_->setToolTip(
        tr("The cube is hidden until inspection starts: press Space"));
  } else {
    cubeView_->setState(attempt_.cube());
    cubeView_->setToolTip(
        tr("Turn the cube as a whole to look at it, hold Space to start the "
           "timer, and solve"));
  }
}

void TimerPage::showCubeKind() {
  bool virt = onVirtualCube();
  cubeBox_->setCurrentIndex(virt ? 0 : 1);
  cubeView_->setVisible(virt);
  viewPicker_->setVisible(virt && !focusMode());
  net_->setVisible(!virt);
  QStringList hidden = hiddenColumns();
  const QStringList names = {"#",   "time",  "ao5", "ao12", "moves",
                             "tps", "cross", "f2l", "ll"};
  for (int c = 0; c < kColumns; ++c) {
    bool virtualOnly = c >= kMoves;
    table_->setColumnHidden(c, (virtualOnly && !virt) ||
                                   hidden.contains(names[c]));
  }
}

void TimerPage::showLastResult() {
  const auto &solves = session().solves;
  if (solves.empty()) {
    display_->showTime(0);
  } else {
    display_->showTime(solves.back().timeMs, solves.back().penalty);
  }
}

void TimerPage::deactivated() {
  cubeView_->finishAnimations();
  tick_.stop();
  if (isBusy()) {
    timer_.cancel();
    if (onVirtualCube()) {
      attempt_.restart();
    }
    statusLabel_->setText(tr("Solve cancelled; nothing was recorded."));
    showLastResult();
    showCube();
  }
  updateControls();
}

void TimerPage::windowDeactivated() {
  auto s = timer_.state();
  if (s == SolveTimer::State::Holding || s == SolveTimer::State::Ready) {
    timer_.cancel(); // Space's release would never arrive
    display_->showTimer(timer_, monotonicMs()); // what was shown before
    showCube();
    updateControls();
  }
}

void TimerPage::updateControls() {
  bool busy = isBusy();
  // The time only changes during a hold, inspection or solve, so the
  // display is only redrawn then.
  if (busy != tick_.isActive()) {
    if (busy) {
      tick_.start(16);
    } else {
      tick_.stop();
    }
  }
  bool any = !session().solves.empty();
  for (QWidget *w : std::initializer_list<QWidget *>{
           typeBox_, nextButton_, sessionButton_, cubeBox_}) {
    w->setEnabled(!busy);
  }
  plus2Button_->setEnabled(!busy && any);
  dnfButton_->setEnabled(!busy && any);
  deleteButton_->setEnabled(!busy && any);
  undoButton_->setEnabled(!busy);
  undoButton_->setVisible(deleted_.has_value());
  bool reviewable = any && !session().solves.back().moves.empty();
  reviewButton_->setVisible(reviewable);
  reviewButton_->setEnabled(!busy);
  controlsLocked_ = busy;
}

void TimerPage::focusModeChanged() {
  const bool on = focusMode();
  leftPanel_->setVisible(!on);
  typeRow_->setVisible(!on);
  chart_->setVisible(!on);
  focusStats_->setVisible(on);
  showCubeKind();
}

void TimerPage::applySettings() {
  timer_.setConfig({settings_.holdMs, settings_.inspection, 15000});
  display_->setDecimals(settings_.decimals);
  net_->setScheme(settings_.scheme);
  CubeLook look = cubeView_->look();
  look.scheme = settings_.scheme;
  cubeView_->setLook(look);
  cubeView_->setAnimationMs(settings_.animationMs);
  feed_.setNeutrality(settings_.neutrality);
  algs_->refresh();
  splits_.clear(); // the move counts may be counted another way
  if (!isBusy()) {
    showCube();
  }
  refreshSolves();
}

void TimerPage::refreshScrambleTypes(const QString &changedSet) {
  QString before = typeBox_->currentType();
  typeBox_->refresh(); // emits typeChanged if the type went away
  if (typeBox_->currentType() == before && !changedSet.isEmpty() &&
      before == "set:" + changedSet) {
    finished_ = false;
    feed_.regenerate(); // the set's cases or ticks may have changed
  }
  algs_->refresh();
}

void TimerPage::showScramble() {
  if (finished_) {
    return; // the finished solve stays until Space
  }
  const auto &current = feed_.current();
  algs_->setScramble(current ? &*current : nullptr);
  if (!current) {
    scrambleLabel_->setText(tr("Generating scramble…"));
    holdLabel_->clear();
    net_->setCube(std::nullopt);
    attempt_.load(Alg{});
    showCube();
    return;
  }
  scrambleLabel_->setText(current->text);
  holdLabel_->setText(
      settings_.neutrality.isFixed()
          ? QString()
          : tr("Hold: %1").arg(holdHtml(current->hold, settings_.scheme)));
  net_->setScramble(current->text);
  // Shown as scrambled holding white on top and green in front.
  attempt_.load(parseAlgOrEmpty(ss(current->text)));
  showCube();
}

void TimerPage::onTick() {
  std::int64_t now = monotonicMs();
  bool wasHidden = cubeHidden();
  timer_.tick(now);
  auto s = timer_.state();
  // A popup or dialog took the Space release: the hold can't be let go of.
  if ((s == SolveTimer::State::Holding || s == SolveTimer::State::Ready) &&
      (QApplication::activePopupWidget() ||
       QApplication::activeModalWidget())) {
    timer_.cancel();
  }
  // Also after a cancelled hold, to bring back what it replaced.
  display_->showTimer(timer_, now);
  if (wasHidden != cubeHidden()) {
    showCube();
  }
  updateControls();
}

void TimerPage::nextScramble() {
  if (isBusy()) {
    return;
  }
  finished_ = false;
  statusLabel_->clear();
  feed_.advance();
}

bool TimerPage::handleActionKey(QKeyEvent *e) {
  Qt::KeyboardModifiers mods = e->modifiers() & ~Qt::KeypadModifier;
  int key = e->key();
  int last = static_cast<int>(session().solves.size()) - 1;
  if (mods == Qt::AltModifier) {
    switch (key) {
    case Qt::Key_1:
      if (last >= 0) {
        session().solves.back().penalty = Penalty::None;
        showLastResult();
        save();
        refreshSolves();
      }
      return true;
    case Qt::Key_2:
      setLastPenalty(Penalty::Plus2);
      return true;
    case Qt::Key_3:
      setLastPenalty(Penalty::Dnf);
      return true;
    case Qt::Key_C:
      commentSolve(last);
      return true;
    case Qt::Key_R:
      review(last);
      return true;
    default:
      return false;
    }
  }
  if (mods == Qt::ControlModifier && key == Qt::Key_Z) {
    undoDelete();
    return true;
  }
  if (mods == Qt::ControlModifier && key == Qt::Key_Right) {
    nextScramble();
    return true;
  }
  if (mods == Qt::NoModifier && key == Qt::Key_Delete) {
    if (last >= 0) {
      deleteSolve(last);
    }
    return true;
  }
  return false;
}

bool TimerPage::handleKey(QKeyEvent *e, bool press) {
  struct Update {
    TimerPage *page;
    ~Update() { page->updateControls(); }
  } update{this};
  bool virt = onVirtualCube();
  std::optional<Move> move =
      virt ? settings_.keymap.lookup(e) : std::optional<Move>();
  std::int64_t now = monotonicMs();
  using S = SolveTimer::State;
  // Back to the scramble as it was dealt, e.g. after a cancelled solve.
  auto restartCube = [this] {
    attempt_.restart();
    showCube();
  };

  if (timer_.state() == S::Running) {
    if (press && e->key() == Qt::Key_Escape) {
      timer_.cancel();
      showLastResult();
      statusLabel_->setText(tr("Solve cancelled; nothing was recorded."));
      if (virt) {
        restartCube();
      }
      return true;
    }
    if (virt) {
      // Space stops the timer; the turning keys turn the cube.
      if (press && e->key() == Qt::Key_Space) {
        stopVirtual(now);
      } else if (press && move && attempt_.turn(*move, now)) {
        cubeView_->animateMove(*move);
        if (settings_.autoStop && attempt_.cube().isSolved()) {
          stopVirtual(now);
        }
      }
      return e->key() == Qt::Key_Space || move.has_value();
    }
    if (press) {
      timer_.pressStop(now);
      if (auto r = timer_.takeResult()) {
        recordSolve(r->timeMs, r->penalty);
      }
      return true;
    }
    return false;
  }
  bool idle = timer_.state() == S::Idle || timer_.state() == S::Stopped;
  if (press && idle && handleActionKey(e)) {
    return true;
  }
  if (e->key() == Qt::Key_Escape && press) {
    bool was = isBusy();
    timer_.cancel();
    if (was) {
      showLastResult();
    }
    statusLabel_->clear();
    if (finished_) {
      nextScramble();
    } else if (virt) {
      restartCube();
    }
    return true;
  }
  if (e->key() == Qt::Key_Tab && press && !move && algs_->toggle()) {
    return true;
  }
  if (move) {
    // Before the solve the cube can only be turned as a whole.
    if (press && timer_.state() != S::Holding && timer_.state() != S::Ready) {
      if (cubeHidden()) {
        statusLabel_->setText(tr("Press Space to start inspection"));
      } else if ((finished_ && !isRotation(move->type)) ||
                 !attempt_.turn(*move, now)) {
        statusLabel_->setText(
            finished_ ? tr("Press Space for the next scramble")
                      : tr("Only rotations until the timer starts (hold "
                           "Space)"));
      } else {
        cubeView_->animateMove(*move);
      }
    }
    return true;
  }
  if (e->key() != Qt::Key_Space &&
      !(finished_ &&
        (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter))) {
    return false;
  }
  if (finished_) {
    // Space (or Enter) after a DNF: on to the next scramble; the release
    // that follows does nothing.
    if (press) {
      nextScramble();
    }
    return true;
  }
  if (press) {
    if (!feed_.current() && timer_.state() != S::Inspecting) {
      return true; // wait for a scramble
    }
    timer_.pressStart(now);
  } else {
    timer_.releaseStart(now);
    if (timer_.state() == S::Inspecting) {
      statusLabel_->clear();
    }
    if (timer_.state() == S::Running) {
      statusLabel_->clear();
      if (virt) {
        attempt_.setClock(now);
        attempt_.start();
      }
    }
  }
  display_->showTimer(timer_, now);
  showCube();
  return true;
}

void TimerPage::stopVirtual(std::int64_t now) {
  timer_.pressStop(now);
  auto r = timer_.takeResult();
  if (!r) {
    return;
  }
  bool solved = attempt_.cube().isSolved();
  int moves = attempt_.count(settings_.simplifiedCounts);
  recordSolve(r->timeMs, solved ? r->penalty : Penalty::Dnf,
              toString(attempt_.solution()), attempt_.solutionTimes());
  QString text =
      solved ? (moves == 1 ? tr("Solved in 1 move (HTM)")
                           : tr("Solved in %1 moves (HTM)").arg(moves))
             : tr("DNF: the cube wasn't solved when you stopped the timer. "
                  "Look at it, then press Space for the next scramble.");
  if (solved && r->timeMs > 0) {
    text += tr(", %1 TPS.").arg(tps(moves, r->timeMs), 0, 'f', 2);
  }
  QString was = statusLabel_->text(); // a new best, if any
  statusLabel_->setText(was.isEmpty() ? text : text + "\n" + was);
  if (!solved) {
    // Keep the cube as it was left, to look at.
    finished_ = true;
  }
  analyseEoCross(session().solves.back());
}

void TimerPage::analyseEoCross(const Solve &s) {
  ParseResult scramble = parseAlg(s.scramble);
  ParseResult moves = parseAlg(s.moves);
  if (!scramble || !moves || eoWatcher_.isRunning()) {
    return;
  }
  FaceletCube start;
  start.apply(scramble.alg);
  eoYours_.reset();
  for (const StepMark &m : stepMarks(start, moves.alg, {SolveStep::EOCross},
                                     settings_.simplifiedCounts)) {
    eoYours_ = m.htm;
  }
  std::vector<Hold> holds =
      settings_.optimalAnyHold
          ? std::vector<Hold>(allHolds().begin(), allHolds().end())
          : withBothEoAxes(settings_.neutrality.holds);
  Alg alg = scramble.alg;
  eoWatcher_.setFuture(QtConcurrent::run(
      [alg, holds] { return heldDistance(alg, SolveStep::EOCross, holds); }));
}

void TimerPage::recordSolve(std::int64_t timeMs, Penalty penalty,
                            const std::string &solution,
                            std::vector<std::int64_t> times) {
  Solve s;
  s.timeMs = timeMs;
  s.penalty = penalty;
  s.moves = solution;
  s.moveTimes =
      solution.empty() ? std::vector<std::int64_t>{} : std::move(times);
  s.scramble = feed_.current() ? ss(feed_.current()->text) : std::string();
  s.timestamp = unixNow();
  session().solves.push_back(s);
  deleted_.reset();
  display_->showTime(timeMs, penalty);
  statusLabel_->clear();
  // A new best, as cstimer marks it.
  std::int64_t unit = statUnit(settings_.decimals);
  QStringList bests;
  for (const NewBest &b : newBests(session().solves, unit)) {
    bests << tr("%1 %2")
                 .arg(qs(b.name))
                 .arg(statText(b.value, settings_.decimals));
  }
  if (!bests.isEmpty()) {
    QString text = tr("New best: %1!").arg(bests.join(", "));
    statusLabel_->setText(text);
    emit message(text);
  }
  save();
  refreshSolves();
  bool keep = onVirtualCube() && penalty == Penalty::Dnf && !solution.empty();
  if (!keep) {
    feed_.advance(); // on to the next scramble
  }
}

void TimerPage::refreshSessionMenu() {
  sessionMenu_->clear();
  auto *group = new QActionGroup(sessionMenu_);
  for (std::size_t i = 0; i < store_.sessions().size(); ++i) {
    const Session &s = store_.sessions()[i];
    QAction *a = sessionMenu_->addAction(
        tr("%1 (%2)").arg(qs(s.name)).arg(s.solves.size()), this,
        [this, i] { selectSession(i); });
    a->setCheckable(true);
    a->setChecked(i == store_.currentIndex());
    group->addAction(a);
  }
  sessionMenu_->addSeparator();
  sessionMenu_->addAction(tr("New session…"), this, &TimerPage::addSession);
  sessionMenu_->addAction(tr("Rename…"), this, &TimerPage::renameSession);
  sessionMenu_->addAction(tr("Delete…"), this, &TimerPage::deleteSession);
  sessionMenu_->addSeparator();
  sessionMenu_->addAction(tr("Import from cstimer…"), this,
                          &TimerPage::importCstimer);
  sessionMenu_->addAction(tr("Export to cstimer…"), this,
                          &TimerPage::exportCstimer);
  // No session changes in the middle of a solve.
  for (QAction *a : sessionMenu_->actions()) {
    a->setEnabled(!isBusy());
  }
}

void TimerPage::selectSession(std::size_t index) {
  if (isBusy()) {
    return;
  }
  bool changed = index != store_.currentIndex();
  store_.setCurrentIndex(index);
  if (changed) {
    deleted_.reset(); // undo is for the session it was deleted from
  }
  bool wasFinished = finished_;
  finished_ = false;
  sessionButton_->setText(tr("Session: %1").arg(qs(session().name)));
  showCubeKind();
  typeBox_->setCurrentType(qs(session().scrambleType));
  if (wasFinished) {
    feed_.advance(); // the scramble shown was solved (as a DNF) already
  } else {
    showScramble(); // onto the session's cube
  }
  showLastResult();
  statusLabel_->clear();
  refreshSolves();
  save();
}

std::string TimerPage::splitKey(const Solve &s) {
  return s.scramble + '\x1f' + s.moves + '\x1f' + std::to_string(s.timeMs) +
         '\x1f' + std::to_string(s.moveTimes.size());
}

const SolveSplits *TimerPage::splitsOf(const Solve &s) const {
  auto it = splits_.find(splitKey(s));
  return it == splits_.end() ? nullptr : &it->second;
}

void TimerPage::requestSplits() {
  if (splitsWatcher_.isRunning()) {
    return; // asked again when it finishes
  }
  std::vector<Solve> missing;
  for (const Solve &s : session().solves) {
    if (!s.moves.empty() && !splits_.contains(splitKey(s))) {
      missing.push_back(s);
    }
  }
  if (missing.empty()) {
    return;
  }
  bool simplified = settings_.simplifiedCounts;
  splitsWatcher_.setFuture(QtConcurrent::run([missing, simplified] {
    SplitResults out;
    for (const Solve &s : missing) {
      out.emplace_back(splitKey(s), computeSplits(s, simplified));
    }
    return out;
  }));
}

void TimerPage::refreshSolves() {
  const auto &solves = session().solves;
  int d = settings_.decimals;
  std::int64_t unit = statUnit(d);
  auto ao5 = rollingAverages(solves, 5, unit);
  auto ao12 = rollingAverages(solves, 12, unit);
  bool virt = onVirtualCube();
  bool missing = false;
  std::vector<StatValue> crossSeries(solves.size()), f2lSeries(solves.size()),
      llSeries(solves.size());
  table_->setRowCount(static_cast<int>(solves.size()));
  for (std::size_t i = 0; i < solves.size(); ++i) {
    const Solve &s = solves[i];
    int row = static_cast<int>(solves.size() - 1 - i);
    auto set = [&](int col, const QString &text) {
      auto *item = new QTableWidgetItem(text);
      item->setData(Qt::UserRole, static_cast<int>(i));
      if (col > 0) {
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      }
      table_->setItem(row, col, item);
    };
    // A solve with a comment is marked.
    set(kNumber, s.comment.empty() ? QString::number(i + 1)
                                   : QString("%1 *").arg(i + 1));
    set(kTime, qs(formatSolve(s, d)));
    set(kAo5, statText(ao5[i], d));
    set(kAo12, statText(ao12[i], d));
    QString moves, rate, cross, f2l, ll;
    if (virt && !s.moves.empty()) {
      int htm =
          solutionHtm(parseAlgOrEmpty(s.moves), settings_.simplifiedCounts);
      moves = QString::number(htm);
      if (s.penalty != Penalty::Dnf && s.timeMs > 0) {
        rate = QString::number(tps(htm, s.timeMs), 'f', 2);
      }
      if (const SolveSplits *sp = splitsOf(s)) {
        auto text = [&](const std::optional<std::int64_t> &ms) {
          return ms ? statText(*ms, d) : QString();
        };
        cross = text(sp->crossMs);
        f2l = text(sp->f2lMs);
        ll = text(sp->llMs);
        if (sp->crossMs) {
          crossSeries[i] = *sp->crossMs;
        }
        if (sp->f2lMs) {
          f2lSeries[i] = *sp->f2lMs;
        }
        if (sp->llMs) {
          llSeries[i] = *sp->llMs;
        }
      } else {
        missing = true;
        cross = "…";
      }
    }
    set(kMoves, moves);
    set(kTps, rate);
    set(kCross, cross);
    set(kF2l, f2l);
    set(kLl, ll);
    QStringList tip;
    if (!s.comment.empty()) {
      tip << tr("Comment: %1").arg(qs(s.comment));
    }
    tip << dateText(s.timestamp);
    if (!s.scramble.empty()) {
      tip << tr("Scramble: %1").arg(qs(s.scramble));
    }
    if (!s.moves.empty()) {
      tip << tr("Solution: %1").arg(qs(s.moves));
    }
    for (int c = 0; c < kColumns; ++c) {
      table_->item(row, c)->setToolTip(tip.join("\n"));
    }
  }
  if (missing) {
    requestSplits();
  }
  SessionSummary sum = summarize(solves, unit);
  QString html =
      "<table cellspacing='4' width='100%'><tr><th></th><th align='right'>" +
      tr("current") + "</th><th align='right'>" + tr("best") + "</th></tr>";
  // Each value links to the solves behind it.
  auto link = [&](const QString &href, StatValue v, bool bold) {
    QString text = statText(v, d);
    if (bold) {
      text = "<b>" + text + "</b>";
    }
    // Coloured as the text: the statistics read as before, clickable.
    return v ? QString("<a href='%1' style='text-decoration:none; color:%3'>"
                       "%2</a>")
                   .arg(href, text,
                        palette().color(QPalette::WindowText).name())
             : text;
  };
  auto row = [&](const QString &name, const QString &id, StatValue cur,
                 StatValue best) {
    html += QString("<tr><td>%1</td><td align='right'>%2</td><td "
                    "align='right'>%3</td></tr>")
                .arg(name, link("cur:" + id, cur, false),
                     link("best:" + id, best, true));
  };
  row(tr("time"), "single",
      solves.empty() ? StatValue{} : StatValue{effectiveTime(solves.back())},
      sum.best);
  row("mo3", "mo3", currentMean(solves, 3, unit), bestMean(solves, 3, unit));
  for (std::size_t n : {5u, 12u, 100u}) {
    QString id = QString("ao%1").arg(n);
    row(id, id, currentAverage(solves, n, unit), bestAverage(solves, n, unit));
  }
  html += "</table>";
  html += tr("<p>Solves: %1%2 &nbsp; Mean: %3</p>")
              .arg(sum.count)
              .arg(sum.dnfs ? QString(" (%1 DNF)").arg(sum.dnfs) : QString())
              .arg(link("mean:all", sum.mean, false));
  stats_->setText(html);
  focusStats_->setText(tr("ao5 %1 · ao12 %2 · best %3 · %4 solves")
                           .arg(statText(currentAverage(solves, 5, unit), d),
                                statText(currentAverage(solves, 12, unit), d),
                                statText(sum.best, d))
                           .arg(sum.count));
  chart_->setSolves(solves);
  if (virt) {
    chart_->setSeries({{tr("cross"), QColor(0x3d, 0xd6, 0x6b), crossSeries},
                       {tr("F2L"), QColor(0x3d, 0x8e, 0xf0), f2lSeries},
                       {tr("LL"), QColor(0xff, 0x8a, 0x3d), llSeries}});
  } else {
    chart_->setSeries({});
  }
  updateControls();
}

void TimerPage::showStat(const QString &link) {
  const auto &solves = session().solves;
  QStringList parts = link.split(':');
  if (parts.size() != 2 || solves.empty()) {
    return;
  }
  int d = settings_.decimals;
  std::int64_t unit = statUnit(d);
  bool best = parts[0] == "best";
  QString id = parts[1];
  auto result = [&](std::size_t from) {
    return [&, from](std::size_t i) {
      return qs(formatSolve(solves[from + i], d));
    };
  };
  if (id == "single") {
    std::size_t at = solves.size() - 1;
    if (best) {
      for (std::size_t i = 0; i < solves.size(); ++i) {
        if (effectiveTime(solves[i]) < effectiveTime(solves[at])) {
          at = i;
        }
      }
    }
    showDetails(static_cast<int>(at));
    return;
  }
  if (parts[0] == "mean") {
    showTextDialog(
        this, tr("Session mean"),
        statListing(tr("Mean of %1: %2")
                        .arg(solves.size())
                        .arg(statText(summarize(solves, unit).mean, d)),
                    solves, 1, false, result(0)));
    return;
  }
  bool mean = id == "mo3";
  std::size_t n = mean ? 3 : id.mid(2).toULongLong();
  if (n == 0 || solves.size() < n) {
    return;
  }
  std::size_t from = solves.size() - n;
  if (best) {
    std::optional<std::size_t> at = bestAverageStart(solves, n, mean, unit);
    if (!at) {
      return;
    }
    from = *at;
  }
  auto span = std::span(solves).subspan(from, n);
  StatValue value = mean ? meanOf(span, unit) : averageOf(span, unit);
  QString heading = (mean ? tr("Mean of %1: %2") : tr("Average of %1: %2"))
                        .arg(n)
                        .arg(statText(value, d));
  showTextDialog(this, best ? tr("Best %1").arg(id) : tr("Current %1").arg(id),
                 statListing(heading, span, from + 1, !mean, result(from)));
}

void TimerPage::setLastPenalty(Penalty p) {
  if (session().solves.empty() || isBusy()) {
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
  if (isBusy() || index < 0 || index >= static_cast<int>(solves.size())) {
    return;
  }
  deleted_ = {static_cast<std::size_t>(index),
              solves[static_cast<std::size_t>(index)]};
  solves.erase(solves.begin() + index);
  save();
  refreshSolves();
  showLastResult();
  emit message(tr("Deleted solve #%1 (Ctrl+Z puts it back)").arg(index + 1));
}

void TimerPage::undoDelete() {
  if (!deleted_ || isBusy()) {
    return;
  }
  auto &solves = session().solves;
  auto at =
      static_cast<std::ptrdiff_t>(std::min(deleted_->first, solves.size()));
  solves.insert(solves.begin() + at, deleted_->second);
  emit message(tr("Put back solve #%1").arg(at + 1));
  deleted_.reset();
  save();
  refreshSolves();
  showLastResult();
}

void TimerPage::commentSolve(int index) {
  auto &solves = session().solves;
  if (isBusy() || index < 0 || index >= static_cast<int>(solves.size())) {
    return;
  }
  Solve &s = solves[static_cast<std::size_t>(index)];
  bool accepted = false;
  QString text = QInputDialog::getText(
      this, tr("Comment"), tr("Comment on solve #%1:").arg(index + 1),
      QLineEdit::Normal, qs(s.comment), &accepted);
  if (accepted) {
    s.comment = ss(text);
    save();
    refreshSolves();
  }
}

void TimerPage::review(int index) {
  const auto &solves = session().solves;
  if (isBusy() || index < 0 || index >= static_cast<int>(solves.size())) {
    return;
  }
  const Solve &s = solves[static_cast<std::size_t>(index)];
  if (s.moves.empty()) {
    return;
  }
  ReviewedSolve solve;
  solve.title = tr("Solve #%1").arg(index + 1);
  solve.scramble = parseAlgOrEmpty(s.scramble);
  solve.solution = parseAlgOrEmpty(s.moves);
  solve.dnf = s.penalty == Penalty::Dnf;
  solve.times = s.moveTimes;
  solve.totalMs = s.timeMs;
  ReviewDialog dialog(solve, settings_, &library_, this);
  dialog.exec();
}

void TimerPage::showDetails(int index) {
  const auto &solves = session().solves;
  if (index < 0 || index >= static_cast<int>(solves.size())) {
    return;
  }
  const Solve &s = solves[static_cast<std::size_t>(index)];
  int d = settings_.decimals;
  QList<QPair<QString, QString>> rows;
  rows.append({tr("Result"), qs(formatSolve(s, d))});
  rows.append({tr("Date"), dateText(s.timestamp)});
  rows.append({"Scramble", qs(s.scramble)});
  if (!s.moves.empty()) {
    rows.append({"Solution", qs(s.moves)});
    int htm = solutionHtm(parseAlgOrEmpty(s.moves), settings_.simplifiedCounts);
    rows.append({tr("Moves"), QString::number(htm)});
    if (s.timeMs > 0) {
      rows.append({tr("TPS"), QString::number(tps(htm, s.timeMs), 'f', 2)});
    }
    SolveSplits splits = computeSplits(s, settings_.simplifiedCounts);
    QStringList phases;
    for (const SolvePhase &p : splits.phases) {
      QString line = p.htm == 1 ? tr("%1: 1 move").arg(qs(p.name))
                                : tr("%1: %2 moves").arg(qs(p.name)).arg(p.htm);
      if (p.ms > 0) {
        line += tr(" in %1").arg(statText(p.ms, d));
      }
      phases << line;
    }
    if (!phases.isEmpty()) {
      rows.append({tr("Splits"), phases.join("\n")});
    }
  }
  rows.append({tr("Comment"), qs(s.comment)});
  showSolveDetails(
      this, tr("Solve #%1").arg(index + 1), rows,
      s.moves.empty() ? std::function<void()>{}
                      : [this, index] { review(index); });
}

void TimerPage::showColumnMenu(const QPoint &pos) {
  QMenu menu(this);
  const QStringList names = {"#",   "time",  "ao5", "ao12", "moves",
                             "tps", "cross", "f2l", "ll"};
  QStringList hidden = hiddenColumns();
  for (int c = kAo5; c < kColumns; ++c) {
    QAction *a = menu.addAction(table_->horizontalHeaderItem(c)->text());
    a->setCheckable(true);
    a->setChecked(!hidden.contains(names[c]));
    if (c >= kMoves && !onVirtualCube()) {
      a->setEnabled(false); // only virtual solves have them
    }
    connect(a, &QAction::toggled, this, [this, name = names[c]](bool on) {
      QStringList h = hiddenColumns();
      h.removeAll(name);
      if (!on) {
        h << name;
      }
      saved::timedHiddenColumns().set(h);
      showCubeKind();
    });
  }
  menu.exec(table_->horizontalHeader()->mapToGlobal(pos));
}

void TimerPage::showSolveMenu(const QPoint &pos) {
  auto *item = table_->itemAt(pos);
  if (!item || isBusy()) {
    return; // nothing changes in the middle of a solve
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
  QAction *reviewAction = nullptr;
  if (!s.moves.empty()) {
    reviewAction = menu.addAction(tr("Review…"));
  }
  auto *details = menu.addAction(tr("Details…"));
  auto *comment = menu.addAction(tr("Comment…"));
  auto *copy = menu.addAction(tr("Copy scramble"));
  QAction *copyMoves = nullptr;
  if (!s.moves.empty()) {
    copyMoves = menu.addAction(tr("Copy solution"));
  }
  menu.addSeparator();
  auto *del = menu.addAction(tr("Delete"));
  QAction *chosen = menu.exec(table_->viewport()->mapToGlobal(pos));
  if (!chosen || isBusy()) {
    return;
  }
  if (chosen == ok) {
    s.penalty = Penalty::None;
  } else if (chosen == plus2) {
    s.penalty = Penalty::Plus2;
  } else if (chosen == dnf) {
    s.penalty = Penalty::Dnf;
  } else if (chosen == reviewAction) {
    review(index);
    return;
  } else if (chosen == details) {
    showDetails(index);
    return;
  } else if (chosen == comment) {
    commentSolve(index);
    return;
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
  if (index + 1 == static_cast<int>(solves.size())) {
    showLastResult();
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
  CubeKind cube = session().cube;
  Session &s = store_.addSession(ss(name.trimmed()));
  s.scrambleType = ss(typeBox_->currentType());
  s.cube = cube;
  selectSession(store_.currentIndex());
}

void TimerPage::renameSession() {
  bool ok = false;
  QString name =
      QInputDialog::getText(this, tr("Rename session"), tr("Name:"),
                            QLineEdit::Normal, qs(session().name), &ok);
  if (!ok || name.trimmed().isEmpty()) {
    return;
  }
  session().name = ss(name.trimmed());
  sessionButton_->setText(tr("Session: %1").arg(qs(session().name)));
  save();
}

void TimerPage::deleteSession() {
  auto answer =
      QMessageBox::question(this, tr("Delete session"),
                            tr("Delete session \"%1\" and its %2 solves?")
                                .arg(qs(session().name))
                                .arg(session().solves.size()));
  if (answer != QMessageBox::Yes) {
    return;
  }
  store_.removeSession(store_.currentIndex());
  deleted_.reset();
  selectSession(store_.currentIndex());
}

void TimerPage::importCstimer() {
  QString path = QFileDialog::getOpenFileName(
      this, tr("Import cstimer export"), QString(),
      tr("cstimer export (*.txt *.json);;All files (*)"));
  if (path.isEmpty()) {
    return;
  }
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    QMessageBox::warning(this, tr("Import"), f.errorString());
    return;
  }
  QString error;
  auto imported = sessionsFromCstimerFile(f.readAll(), &error);
  if (!imported) {
    QMessageBox::warning(this, tr("Import"),
                         tr("Couldn't import %1: %2").arg(path, error));
    return;
  }
  std::size_t solves = 0;
  std::size_t count = imported->sessions.size();
  for (auto &s : imported->sessions) {
    solves += s.solves.size();
    s.name += " (cstimer)";
    store_.sessions().push_back(std::move(s));
  }
  QStringList notes;
  if (imported->emptySkipped > 0) {
    notes << tr("%n empty session(s) left out", nullptr,
                imported->emptySkipped);
  }
  if (!imported->others.empty()) {
    QStringList names;
    for (const auto &n : imported->others) {
      names << qs(n);
    }
    notes
        << tr("sessions for other puzzles left out: %1").arg(names.join(", "));
  }
  if (count > 0) {
    selectSession(store_.sessions().size() - 1);
  }
  QString text =
      tr("Imported %1 sessions with %2 solves").arg(count).arg(solves);
  if (!notes.isEmpty()) {
    text += " (" + notes.join("; ") + ")";
  }
  emit message(text);
  if (count == 0) {
    QMessageBox::information(this, tr("Import"), text);
  }
}

void TimerPage::exportCstimer() {
  QString path = QFileDialog::getSaveFileName(
      this, tr("Export for cstimer"), "v-cube-sessions.txt",
      tr("cstimer import file (*.txt)"));
  if (path.isEmpty()) {
    return;
  }
  QString error;
  if (!writeFileAtomically(path, sessionsToCstimer(store_.sessions()),
                           &error)) {
    QMessageBox::warning(this, tr("Export"), error);
    return;
  }
  QMessageBox::information(
      this, tr("Export"),
      tr("Exported %1 sessions to %2.\n\nIn cstimer, import it with Data "
         "Import/Export > \"Import session(s) from other timers\", which adds "
         "the sessions to yours. (\"Import from file\" would replace all your "
         "cstimer data with them.)")
          .arg(store_.sessions().size())
          .arg(path));
}

void TimerPage::save() {
  // Written in the background from a copy, so that saving never holds up
  // the next attempt.
  writeInBackground(
      store_.path(),
      [copy = store_](QString *error) { return copy.save(error); }, this,
      [this](const QString &error) {
        emit message(tr("Could not save sessions: %1").arg(error));
      });
}

} // namespace vcube
