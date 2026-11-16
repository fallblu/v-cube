#include "app/timer_page.hpp"
#include "app/page_parts.hpp"
#include "app/timer_private.hpp"

#include "app/review_dialog.hpp"
#include "app/ui_state.hpp"
#include "io/data_file.hpp"
#include "train/step_solver.hpp"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace vcube {

namespace {

std::vector<AttemptColumn> columns(bool virt) {
  auto col = [](const char *id, const QString &title, const QString &tip,
                bool hideable, bool available) {
    AttemptColumn c;
    c.id = id;
    c.title = title;
    c.tip = tip;
    c.fit = true;
    c.hideable = hideable;
    c.available = available;
    return c;
  };
  return {
      col("#", "#", {}, false, true),
      col("time", QObject::tr("Time"), {}, false, true),
      col("ao5", "ao5", {}, true, true),
      col("ao12", "ao12", {}, true, true),
      col("moves", QObject::tr("Moves"),
          QObject::tr("The solve's move count (HTM)"), true, virt),
      col("tps", QObject::tr("TPS"),
          QObject::tr("Moves per second over the whole solve"), true, virt),
      col("cross", QObject::tr("Cross"),
          QObject::tr("Time to the cross (EO Cross in a ZZ solve)"), true,
          virt),
      col("f2l", QObject::tr("F2L"),
          QObject::tr("Time from the cross to the end of the F2L"), true, virt),
      col("ll", QObject::tr("LL"),
          QObject::tr("Time from the F2L to the end of the solve"), true, virt),
  };
}

} // namespace

TimerPage::TimerPage(AppSettings &settings, SessionStore &store,
                     AlgLibrary &library, QWidget *parent)
    : ModePage(parent), settings_(settings), store_(store), library_(library),
      feed_(library) {
  // --- left column: sessions, stats, times ---
  panel_ = new SessionPanel(this);
  panel_->setMenuTip(
      tr("Switch, add, rename or delete sessions, or import and export "
         "cstimer sessions"));
  panel_->setStatsClickable(true);
  panel_->setDeleteRowVisible(true);
  panel_->setStretchLast(true);
  panel_->setColumns(columns(true));
  panel_->setHiddenColumnsKey(saved::timedHiddenColumns().key());

  // --- right column: scramble, timer, chart ---
  stage_ = new SolveStage("timed", this);
  QWidget *right = stage_;
  typeBox_ = new ScrambleTypeBox(library_, stage_->options);
  nextButton_ = stage_->skipButton;
  cubeBox_ = stage_->cubeBox;
  cubeView_ = stage_->cube;
  cubeView_->setToolTip(
      tr("Turn the cube as a whole to look at it, hold Space to start the "
         "timer, and solve"));
  algs_ = new ScrambleAlgs(library_, "timer/showAlgs", right);
  stage_->options->addPicker(tr("Scramble:"), typeBox_, 1);
  stage_->options->addWidget(algs_->button());
  stage_->addStandardOptions();
  scrambleLabel_ = stage_->scramble;
  net_ = stage_->net;
  display_ = stage_->display;
  statusLabel_ = stage_->hint;
  focusStats_ = stage_->focusStats;
  plus2Button_ = flatButton("+2", right);
  dnfButton_ = flatButton("DNF", right);
  reviewButton_ = flatButton(tr("Review last"), right);
  plus2Button_->setToolTip(tr("Toggle +2 on the last solve (Alt+2)"));
  dnfButton_->setToolTip(tr("Toggle DNF on the last solve (Alt+3)"));
  reviewButton_->setToolTip(
      tr("Replay the last solve with its steps and splits (Alt+R; "
         "double-click any solve to review it)"));
  stage_->addButton(plus2Button_);
  stage_->addButton(dnfButton_);
  stage_->addButton(reviewButton_);
  stage_->addUnderButtons(algs_->label());

  chart_ = panel_->chart();
  chart_->setVisible(true);
  chart_->setSettingsKey("timed/chart");
  stage_->setChart(chart_);

  auto *split = new QSplitter(this);
  split->setObjectName("main");
  split->addWidget(panel_);
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
            flow_.nextScramble();
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
    flow_.nextScramble();
    configureFlow(); // the flow is for the session's cube
    save();
    showCubeKind();
    showScramble();
    refreshSolves();
  });
  connect(panel_, &SessionPanel::menuAboutToShow, this,
          &TimerPage::refreshSessionMenu);
  connect(panel_, &SessionPanel::sessionChosen, this,
          &TimerPage::selectSession);
  connect(panel_, &SessionPanel::newSessionRequested, this,
          &TimerPage::addSession);
  connect(panel_, &SessionPanel::renameSessionRequested, this,
          &TimerPage::renameSession);
  connect(panel_, &SessionPanel::deleteSessionRequested, this,
          &TimerPage::deleteSession);
  connect(plus2Button_, &QPushButton::clicked, this,
          [this] { setLastPenalty(Penalty::Plus2); });
  connect(dnfButton_, &QPushButton::clicked, this,
          [this] { setLastPenalty(Penalty::Dnf); });
  connect(panel_, &SessionPanel::deleteLastClicked, this, [this] {
    if (!session().solves.empty()) {
      deleteSolve(static_cast<int>(session().solves.size()) - 1);
    }
  });
  connect(panel_, &SessionPanel::undoClicked, this, &TimerPage::undoDelete);
  connect(reviewButton_, &QPushButton::clicked, this,
          [this] { review(static_cast<int>(session().solves.size()) - 1); });
  connect(panel_, &SessionPanel::attemptMenuRequested, this,
          &TimerPage::showSolveMenu);
  connect(panel_, &SessionPanel::attemptActivated, this, [this](int index) {
    const auto &solves = session().solves;
    if (index >= 0 && index < static_cast<int>(solves.size()) &&
        !solves[static_cast<std::size_t>(index)].moves.empty()) {
      review(index);
    } else {
      showDetails(index);
    }
  });
  connect(panel_, &SessionPanel::statActivated, this, &TimerPage::showStat);
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

bool TimerPage::isBusy() const { return flow_.isBusy(); }

void TimerPage::activated() { updateControls(); }

bool TimerPage::onVirtualCube() const {
  return store_.sessions()[store_.currentIndex()].cube == CubeKind::Virtual;
}

void TimerPage::showCube() {
  if (flow_.cubeHidden()) {
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
  stage_->viewPicker->setVisible(virt);
  net_->setVisible(!virt);
  showColumns();
}

void TimerPage::showColumns() { panel_->setColumns(columns(onVirtualCube())); }

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
  if (flow_.leave()) {
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
  if (flow_.dropHold()) { // Space's release would never arrive
    display_->showTimer(flow_.timer(), monotonicMs()); // what was shown before
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
  // The keys that do something between solves; none while one runs.
  QList<KeyHint> keys;
  if (!busy && !focusMode()) {
    if (settings_.inspection) {
      keys << KeyHint{tr("Space"), tr("inspect")};
    }
    keys << KeyHint{tr("hold Space"), tr("start")};
    if (any) {
      keys << KeyHint{tr("Backspace"), tr("DNF")}
           << KeyHint{tr("Alt+2"), tr("+2")}
           << KeyHint{tr("Delete"), tr("delete last")}
           << KeyHint{tr("Alt+R"), tr("review")};
    }
    keys << KeyHint{tr("Ctrl+→"), tr("skip")};
  }
  stage_->setKeys(keys);
  for (QWidget *w : std::initializer_list<QWidget *>{
           typeBox_, nextButton_, panel_->sessionButton(), cubeBox_}) {
    w->setEnabled(!busy);
  }
  plus2Button_->setEnabled(!busy && any);
  dnfButton_->setEnabled(!busy && any);
  panel_->setDeleteEnabled(!busy && any);
  panel_->setUndo(deleted_.has(), !busy);
  bool reviewable = any && !session().solves.back().moves.empty();
  reviewButton_->setVisible(reviewable);
  reviewButton_->setEnabled(!busy);
}

QWidget *TimerPage::sidePanel() const { return panel_; }

void TimerPage::focusModeChanged() {
  const bool on = focusMode();
  panel_->setVisible(!on);
  stage_->options->setVisible(!on);
  chart_->setVisible(!on);
  focusStats_->setVisible(on);
  showCubeKind();
}

void TimerPage::configureFlow() {
  flow_.setConfig({{settings_.holdMs, settings_.inspection, 15000},
                   onVirtualCube(),
                   settings_.hideBeforeInspection});
}

void TimerPage::applySettings() {
  configureFlow();
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
    flow_.nextScramble();
    feed_.regenerate(); // the set's cases or ticks may have changed
  }
  algs_->refresh();
}

void TimerPage::showScramble() {
  if (flow_.finished()) {
    return; // the finished solve stays until Space
  }
  const auto &current = feed_.current();
  algs_->setScramble(current ? &*current : nullptr);
  if (!current) {
    scrambleLabel_->setText(tr("Generating scramble…"));
    net_->setCube(std::nullopt);
    attempt_.load(Alg{});
    showCube();
    return;
  }
  scrambleLabel_->setText(current->text);
  net_->setScramble(current->text);
  // Shown as scrambled holding white on top and green in front.
  attempt_.load(parseAlgOrEmpty(ss(current->text)));
  showCube();
}

void TimerPage::onTick() {
  std::int64_t now = monotonicMs();
  bool wasHidden = flow_.cubeHidden();
  // A popup or dialog took the Space release: the hold can't be let go of.
  flow_.tick(now, QApplication::activePopupWidget() ||
                      QApplication::activeModalWidget());
  // Also after a cancelled hold, to bring back what it replaced.
  display_->showTimer(flow_.timer(), now);
  if (wasHidden != flow_.cubeHidden()) {
    showCube();
  }
  updateControls();
}

void TimerPage::nextScramble() {
  if (isBusy()) {
    return;
  }
  flow_.nextScramble();
  statusLabel_->clear();
  feed_.advance();
}

bool TimerPage::handleActionKey(QKeyEvent *e) {
  Qt::KeyboardModifiers mods = e->modifiers() & ~Qt::KeypadModifier;
  int key = e->key();
  int last = static_cast<int>(session().solves.size()) - 1;
  // Backspace toggles the DNF, as in Drill and Alg Lab.
  if (mods == Qt::NoModifier && key == Qt::Key_Backspace && last >= 0) {
    setLastPenalty(Penalty::Dnf);
    return true;
  }
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
  int key = e->key();
  std::int64_t now = monotonicMs();

  if (flow_.isRunning()) {
    if (press && key == Qt::Key_Escape) {
      apply(flow_.escape(), now);
      return true;
    }
    if (virt) {
      // Space stops the timer; the turning keys turn the cube.
      if (key == Qt::Key_Space) {
        apply(flow_.space(press, now, true), now);
      } else if (press && move) {
        turnDuringSolve(*move, now);
      }
      return key == Qt::Key_Space || move.has_value();
    }
    if (press) {
      apply(flow_.stopKey(now), now);
      return true;
    }
    return false;
  }
  if (press && flow_.isIdle() && handleActionKey(e)) {
    return true;
  }
  if (key == Qt::Key_Escape && press) {
    apply(flow_.escape(), now);
    return true;
  }
  if (key == Qt::Key_Tab && press && !move && algs_->toggle()) {
    return true;
  }
  if (move) {
    if (press) {
      // Before the solve the cube can only be turned as a whole.
      switch (flow_.move(isRotation(move->type))) {
      case TimedFlow::MoveAnswer::Ignore:
        break;
      case TimedFlow::MoveAnswer::Hidden:
        statusLabel_->setText(tr("Press Space to start inspection"));
        break;
      case TimedFlow::MoveAnswer::NextScramble:
        statusLabel_->setText(tr("Press Space for the next scramble"));
        break;
      case TimedFlow::MoveAnswer::Turn:
        if (attempt_.turn(*move, now)) {
          cubeView_->animateMove(*move);
        } else {
          statusLabel_->setText(
              flow_.finished() ? tr("Press Space for the next scramble")
                               : tr("Only rotations until the timer starts "
                                    "(hold Space)"));
        }
        break;
      }
    }
    return true;
  }
  if (key == Qt::Key_Space) {
    apply(flow_.space(press, now, feed_.current().has_value()), now);
    return true;
  }
  if ((key == Qt::Key_Return || key == Qt::Key_Enter) && flow_.finished()) {
    // Enter after a DNF, as Space: the next scramble.
    if (press) {
      apply(flow_.enter(), now);
    }
    return true;
  }
  return false;
}

void TimerPage::apply(const TimedFlow::Effects &e, std::int64_t now) {
  if (e.showLastResult) {
    showLastResult();
  }
  if (e.cancelled) {
    statusLabel_->setText(tr("Solve cancelled; nothing was recorded."));
  }
  if (e.clearStatus) {
    statusLabel_->clear();
  }
  if (e.nextScramble) {
    nextScramble();
  }
  if (e.restartCube) {
    // Back to the scramble as it was dealt, e.g. after a cancelled solve.
    attempt_.restart();
    showCube();
  }
  if (e.startAttempt) {
    attempt_.setClock(now);
    attempt_.start();
  }
  if (e.stopVirtual) {
    stopVirtual(now);
  }
  if (e.record) {
    recordSolve(e.record->timeMs, e.record->penalty);
  }
  if (e.showTimer) {
    display_->showTimer(flow_.timer(), now);
    showCube();
  }
}

void TimerPage::turnDuringSolve(Move m, std::int64_t now) {
  if (attempt_.turn(m, now)) {
    cubeView_->animateMove(m);
    if (settings_.autoStop && attempt_.cube().isSolved()) {
      stopVirtual(now);
    }
  }
}

void TimerPage::stopVirtual(std::int64_t now) {
  bool solved = attempt_.cube().isSolved();
  std::optional<SolveTimer::Result> r = flow_.stopVirtual(now, solved);
  if (!r) {
    return;
  }
  int moves = attempt_.count(settings_.simplifiedCounts);
  recordSolve(r->timeMs, r->penalty, toString(attempt_.solution()),
              attempt_.solutionTimes());
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
    flow_.keepDnf();
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
  deleted_.forget();
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

} // namespace vcube
