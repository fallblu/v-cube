#include "app/timer_page.hpp"

#include "app/key_router.hpp"
#include "app/page_parts.hpp"
#include "app/timer_private.hpp"

#include "app/cube_view.hpp"
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

TimerPage::TimerPage(AppSettings &settings, SessionStore &store,
                     AlgLibrary &library, QWidget *parent)
    : SessionSolvePage(settings, library, "timed", 340, parent), store_(store) {
  panel_->setHiddenColumnsKey(saved::timedHiddenColumns().key());
  auto *types = new ScrambleTypeBox(library_, stage_->options);
  cubeView_->setToolTip(
      tr("Turn the cube as a whole to look at it, hold Space to start the "
         "timer, and solve"));
  algs_ = new ScrambleAlgs(library_, "timer/showAlgs", stage_);
  setTypeBox(types, algs_->button());
  // +2, DNF, deleting and reviewing the last solve are clicked on their
  // keys' chips (see updateControls()).
  stage_->addUnderButtons(algs_->label());
  stage_->setChart(chart_);

  connect(&tick_, &QTimer::timeout, this, &TimerPage::onTick);
  connect(&splitsWatcher_, &QFutureWatcher<SplitResults>::finished, this,
          [this] {
            for (auto &[key, s] : splitsWatcher_.result()) {
              splits_[key] = std::move(s);
            }
            refreshAttempts();
          });
  connect(&eoCheck_, &EoCrossCheck::found, this, [this](const QString &line) {
    if (isBusy()) {
      return;
    }
    QString was = hintLabel_->text();
    hintLabel_->setText(was.isEmpty() ? line : was + "\n" + line);
  });

  start(store_.currentIndex());
}

TimerPage::~TimerPage() { splitsWatcher_.waitForFinished(); }

bool TimerPage::isBusy() const { return flow_.isBusy(); }

void TimerPage::activated() { updateControls(); }

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

void TimerPage::showLastResult() {
  const auto &solves = session().solves;
  if (solves.empty()) {
    display_->showTime(0);
  } else {
    display_->showTime(solves.back().timeMs, solves.back().penalty);
  }
}

void TimerPage::deactivated() {
  justSolved_ = false;
  cubeView_->finishAnimations();
  tick_.stop();
  if (flow_.leave()) {
    if (onVirtualCube()) {
      attempt_.restart();
    }
    hintLabel_->setText(tr("Solve cancelled; nothing was recorded."));
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
      keys << KeyHint{tr("Backspace"), tr("DNF"),
                      [this] { setLastPenalty(Penalty::Dnf); }}
           << KeyHint{tr("Alt+2"), tr("+2"),
                      [this] { setLastPenalty(Penalty::Plus2); }}
           << KeyHint{tr("Delete"), tr("delete last"), [this] {
                        if (!isBusy() && !session().solves.empty()) {
                          deleteAttempt(
                              static_cast<int>(session().solves.size()) - 1);
                        }
                      }};
    }
    if (any && !session().solves.back().moves.empty()) {
      keys << KeyHint{tr("Alt+R"), tr("review"), [this] {
                        if (!isBusy() && !session().solves.empty()) {
                          review(static_cast<int>(session().solves.size()) - 1);
                        }
                      }};
    }
    keys << KeyHint{tr("Ctrl+→"), tr("skip")};
  }
  stage_->setKeys(keys);
  enableControls(deleted_.has());
}

void TimerPage::focusModeChanged() {
  SessionSolvePage::focusModeChanged();
  showCubeKind();
}

std::vector<AttemptColumn> TimerPage::columns() const {
  bool virt = onVirtualCube();
  // The number fits its contents; the others share the width evenly.
  return {
      {.id = "#", .title = "#", .fit = true},
      {.id = "time", .title = tr("Time")},
      {.id = "ao5", .title = "ao5", .hideable = true},
      {.id = "ao12", .title = "ao12", .hideable = true},
      {.id = "moves",
       .title = tr("Moves"),
       .hideable = true,
       .available = virt},
      {.id = "tps", .title = tr("TPS"), .hideable = true, .available = virt},
      {.id = "cross",
       .title = tr("Cross"),
       .hideable = true,
       .available = virt},
      {.id = "f2l", .title = tr("F2L"), .hideable = true, .available = virt},
      {.id = "ll", .title = tr("LL"), .hideable = true, .available = virt},
  };
}

void TimerPage::configureFlow() {
  flow_.setConfig({{settings_.holdMs, settings_.inspection, 15000},
                   onVirtualCube(),
                   settings_.hideBeforeInspection});
}

void TimerPage::settingsApplied() {
  display_->setDecimals(settings_.decimals);
  splits_.clear(); // the move counts may be counted another way
  solveRows_.clear();
  if (!isBusy()) {
    showCube();
  }
}

void TimerPage::typeChosen(const QString &type) {
  if (session().scrambleType != ss(type)) {
    session().scrambleType = ss(type);
    save();
  }
  flow_.nextScramble();
}

void TimerPage::cubeChosen(CubeKind kind) {
  session().cube = kind;
  flow_.nextScramble();
  configureFlow(); // the flow is for the session's cube
  save();
  showCubeKind();
  showScramble();
  refreshAttempts();
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

void TimerPage::skip() {
  if (isBusy()) {
    return;
  }
  flow_.nextScramble();
  eoCheck_.cancel();
  hintLabel_->clear();
  feed_.advance();
}

bool TimerPage::handleActionKey(QKeyEvent *e) {
  Qt::KeyboardModifiers mods = e->modifiers() & ~Qt::KeypadModifier;
  int key = e->key();
  int last = static_cast<int>(session().solves.size()) - 1;
  // Backspace toggles the DNF, as in Drill and Alg Lab: only on the solve
  // just finished, so that it can't mark an older one unseen.
  if (mods == Qt::NoModifier && key == Qt::Key_Backspace && last >= 0) {
    if (justSolved_) {
      setLastPenalty(Penalty::Dnf);
    } else {
      hintLabel_->setText(tr("Backspace marks a solve DNF right after it; "
                             "Alt+3 toggles the DNF on the last solve."));
    }
    return true;
  }
  if (mods == Qt::AltModifier) {
    switch (key) {
    case Qt::Key_1:
      if (last >= 0) {
        session().solves.back().penalty = Penalty::None;
        showLastResult();
        saveAndRefresh();
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
    skip();
    return true;
  }
  if (mods == Qt::NoModifier && key == Qt::Key_Delete) {
    if (last >= 0) {
      deleteAttempt(last);
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
  // Any key but Backspace (and Shift, Ctrl, Alt alone) moves on from the
  // solve just finished.
  if (press && key != Qt::Key_Backspace && !isModifierKey(key)) {
    justSolved_ = false;
  }

  if (flow_.isRunning()) {
    if (press && key == Qt::Key_Escape) {
      apply(flow_.escape(), now);
      return true;
    }
    if (virt) {
      // Space stops the timer; the turning keys turn the cube.
      if (key == Qt::Key_Space) {
        apply(flow_.space(press ? KeyEdge::Down : KeyEdge::Up, now,
                          Scramble::Shown),
              now);
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
        hintLabel_->setText(tr("Press Space to start inspection"));
        break;
      case TimedFlow::MoveAnswer::NextScramble:
        hintLabel_->setText(tr("Press Space for the next scramble"));
        break;
      case TimedFlow::MoveAnswer::Turn:
        if (attempt_.turn(*move, now)) {
          cubeView_->animateMove(*move);
        } else {
          hintLabel_->setText(flow_.finished()
                                  ? tr("Press Space for the next scramble")
                                  : tr("Only rotations until the timer starts "
                                       "(hold Space)"));
        }
        break;
      }
    }
    return true;
  }
  if (key == Qt::Key_Space) {
    apply(flow_.space(press ? KeyEdge::Down : KeyEdge::Up, now,
                      feed_.current() ? Scramble::Shown : Scramble::Awaited),
          now);
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
    hintLabel_->setText(tr("Solve cancelled; nothing was recorded."));
  }
  if (e.clearStatus) {
    hintLabel_->clear();
  }
  if (e.nextScramble) {
    skip();
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
  QString was = hintLabel_->text(); // a new best, if any
  hintLabel_->setText(was.isEmpty() ? text : text + "\n" + was);
  if (!solved) {
    // Keep the cube as it was left, to look at.
    flow_.keepDnf();
  }
  eoCheck_.start(session().solves.back().scramble,
                 session().solves.back().moves);
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
  justSolved_ = true;
  deleted_.forget();
  display_->showTime(timeMs, penalty);
  hintLabel_->clear();
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
    hintLabel_->setText(text);
    emit message(text);
  }
  saveAndRefresh();
  bool keep = onVirtualCube() && penalty == Penalty::Dnf && !solution.empty();
  if (!keep) {
    feed_.advance(); // on to the next scramble
  }
}

} // namespace vcube
