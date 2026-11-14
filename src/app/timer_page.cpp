#include "app/timer_page.hpp"
#include "app/page_parts.hpp"

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

QString statText(StatValue v, int decimals) {
  return QString::fromStdString(formatTime(v, decimals));
}

// The table's columns. Those after ao12 are for virtual sessions.
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
  // Backspace toggles the DNF, as in Drill, Blind and Alg Lab.
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

void TimerPage::refreshSessionMenu() {
  std::vector<SessionPanel::MenuSession> list;
  for (const Session &s : store_.sessions()) {
    list.push_back({qs(s.name), s.solves.size()});
  }
  panel_->fillSessionMenu(list, store_.currentIndex());
  QMenu *menu = panel_->menu();
  menu->addSeparator();
  menu->addAction(tr("Import from cstimer…"), this, &TimerPage::importCstimer);
  menu->addAction(tr("Export to cstimer…"), this, &TimerPage::exportCstimer);
  // No session changes in the middle of a solve.
  panel_->setMenuEnabled(!isBusy());
}

void TimerPage::selectSession(std::size_t index) {
  if (isBusy()) {
    return;
  }
  bool changed = index != store_.currentIndex();
  store_.setCurrentIndex(index);
  if (changed) {
    deleted_.forget(); // undo is for the session it was deleted from
  }
  bool wasFinished = flow_.finished();
  flow_.nextScramble();
  panel_->setSessionName(qs(session().name));
  configureFlow(); // the flow is for the session's cube
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
  std::vector<AttemptRow> rows;
  rows.reserve(solves.size());
  for (std::size_t i = solves.size(); i-- > 0;) {
    const Solve &s = solves[i];
    AttemptRow row;
    row.index = static_cast<int>(i);
    row.cells.resize(kColumns);
    auto set = [&](int col, const QString &text) {
      AttemptCell &cell = row.cells[static_cast<std::size_t>(col)];
      cell.text = text;
      cell.alignRight = col > 0;
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
    for (AttemptCell &cell : row.cells) {
      cell.tip = tip.join("\n");
    }
    rows.push_back(std::move(row));
  }
  panel_->setRows(std::move(rows));
  if (missing) {
    requestSplits();
  }
  static constexpr std::string_view kStats[] = {"single", "mo3", "ao5", "ao12",
                                                "ao100"};
  SessionStats stats = sessionStats(solves, kStats, unit);
  const SessionSummary &sum = stats.summary;
  QString colour = palette().color(QPalette::WindowText).name();
  QString html = statsTableHtml(
      stats,
      [](const std::string &id) {
        return id == "single" ? tr("time") : qs(id);
      },
      [d](const std::string &, StatValue v) { return statText(v, d); }, colour);
  html += tr("<p>Solves: %1%2 &nbsp; Mean: %3</p>")
              .arg(sum.count)
              .arg(sum.dnfs ? QString(" (%1 DNF)").arg(sum.dnfs) : QString())
              .arg(statLinkHtml("mean:all", sum.mean, statText(sum.mean, d),
                                colour));
  panel_->setStatsHtml(html);
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
  int d = settings_.decimals;
  StatLinkView view;
  view.unit = statUnit(d);
  view.format = [d](StatValue v) { return statText(v, d); };
  view.result = [&solves, d](std::size_t i) {
    return qs(formatSolve(solves[i], d));
  };
  view.number = [](std::size_t i) { return i + 1; };
  view.details = [this](std::size_t i) { showDetails(static_cast<int>(i)); };
  openStatLink(this, link, solves, view);
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
  if (isBusy() || index < 0 ||
      !deleted_.erase(solves, static_cast<std::size_t>(index))) {
    return;
  }
  save();
  refreshSolves();
  showLastResult();
  emit message(tr("Deleted solve #%1 (Ctrl+Z puts it back)").arg(index + 1));
}

void TimerPage::undoDelete() {
  if (!deleted_.has() || isBusy()) {
    return;
  }
  std::optional<std::size_t> at = deleted_.restore(session().solves);
  emit message(tr("Put back solve #%1").arg(*at + 1));
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
  if (std::optional<QString> text = askComment(
          this, tr("Comment on solve #%1:").arg(index + 1), qs(s.comment))) {
    s.comment = ss(*text);
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

void TimerPage::showSolveMenu(int index, const QPoint &globalPos) {
  const auto &solves = session().solves;
  if (isBusy() || index < 0 || index >= static_cast<int>(solves.size())) {
    return; // nothing changes in the middle of a solve
  }
  const Solve &s = solves[static_cast<std::size_t>(index)];
  ActionMenu menu(this);
  auto penalty = [this, index](Penalty p) {
    return [this, index, p] {
      auto &list = session().solves;
      if (index >= static_cast<int>(list.size())) {
        return;
      }
      list[static_cast<std::size_t>(index)].penalty = p;
      if (index + 1 == static_cast<int>(list.size())) {
        showLastResult();
      }
      save();
      refreshSolves();
    };
  };
  for (auto [text, p] : {std::pair{tr("No penalty"), Penalty::None},
                         std::pair{QString("+2"), Penalty::Plus2},
                         std::pair{QString("DNF"), Penalty::Dnf}}) {
    QAction *a = menu.add(text, penalty(p));
    a->setCheckable(true);
    a->setChecked(s.penalty == p);
  }
  menu.addSeparator();
  if (!s.moves.empty()) {
    menu.add(tr("Review…"), [this, index] { review(index); });
  }
  AttemptMenuActions common;
  common.details = [this, index] { showDetails(index); };
  common.comment = [this, index] { commentSolve(index); };
  common.scramble = qs(s.scramble);
  common.solution = qs(s.moves);
  common.remove = [this, index] { deleteSolve(index); };
  addAttemptActions(menu, common);
  menu.run(globalPos, [this] { return !isBusy(); });
}

void TimerPage::addSession() {
  std::optional<QString> name =
      askSessionName(this, tr("New session"), tr("Name:"),
                     tr("Session %1").arg(store_.sessions().size() + 1));
  if (!name) {
    return;
  }
  CubeKind cube = session().cube;
  Session &s = store_.addSession(ss(*name));
  s.scrambleType = ss(typeBox_->currentType());
  s.cube = cube;
  selectSession(store_.currentIndex());
}

void TimerPage::renameSession() {
  if (!renameSessionAsked(this, session())) {
    return;
  }
  panel_->setSessionName(qs(session().name));
  save();
}

void TimerPage::deleteSession() {
  if (!deleteSessionAsked(this, store_, session().solves.size(),
                          tr("solves"))) {
    return;
  }
  deleted_.forget();
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
  saveInBackground(store_, this, [this](const QString &error) {
    emit message(tr("Could not save sessions: %1").arg(error));
  });
}

} // namespace vcube
