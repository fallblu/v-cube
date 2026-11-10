#include "app/blind_solves_drill.hpp"

#include "app/session_panel.hpp"
#include "app/widgets.hpp"
#include "io/algset_json.hpp"

#include "cube/cubie.hpp"

#include <QComboBox>
#include <QKeyEvent>
#include <QLabel>
#include <QToolButton>

#include <cmath>
#include <string_view>

namespace vcube {

SolvesDrill::SolvesDrill(BlindContext &context, QWidget *parent)
    : BlindDrill(context, parent) {
  panel_ = new SessionPanel(parent);
  panel_->chart()->setEmptyText(tr("Blind solve times will be charted here"));
  panel_->setColumns({blindColumn("number", "#"),
                      blindColumn("time", tr("Time")),
                      blindColumn("memo", tr("Memo"))});
  cubeBox_ = blindComboBox({tr("Virtual cube"), tr("Physical cube")}, parent);
  cubeBox_->setToolTip(tr("The cube this session's solves are done on"));

  connect(&tick_, &QTimer::timeout, this, &SolvesDrill::onTick);
  connect(cubeBox_, &QComboBox::activated, this, [this](int index) {
    CubeKind kind = index == 0 ? CubeKind::Virtual : CubeKind::Physical;
    if (isBusy() || session().cube == kind) {
      return;
    }
    session().cube = kind;
    ctx_.save();
    emit restartRequested();
  });
  connect(panel_, &SessionPanel::menuAboutToShow, this,
          &SolvesDrill::refreshSessionMenu);
  connect(panel_, &SessionPanel::sessionChosen, this,
          &SolvesDrill::selectSession);
  connect(panel_, &SessionPanel::newSessionRequested, this,
          &SolvesDrill::addSession);
  connect(panel_, &SessionPanel::renameSessionRequested, this,
          &SolvesDrill::renameSession);
  connect(panel_, &SessionPanel::deleteSessionRequested, this,
          &SolvesDrill::deleteSession);
  connect(panel_, &SessionPanel::attemptMenuRequested, this,
          &SolvesDrill::showAttemptMenu);
}

TrendChart *SolvesDrill::chart() const { return panel_->chart(); }

QWidget *SolvesDrill::leftPanel() const { return panel_; }

std::vector<QWidget *> SolvesDrill::options() const { return {cubeBox_}; }

bool SolvesDrill::onVirtualCube() const {
  return ctx_.store.current().cube == CubeKind::Virtual;
}

BlindDrill::Layout SolvesDrill::layout() const {
  bool physical = !onVirtualCube();
  Layout l;
  l.cube = !physical;
  l.net = physical;
  l.scramble = true;
  l.viewPicker = !physical;
  l.chart = true;
  return l;
}

BlindDrill::View SolvesDrill::view() const {
  switch (flow_.phase()) {
  case BlindSolveFlow::Phase::Ready:
    return {View::Kind::Hidden, tr("Press Space to start the memo")};
  case BlindSolveFlow::Phase::Executing:
    return {View::Kind::Hidden, tr("Solving blind… press Space when done")};
  default:
    return {};
  }
}

void SolvesDrill::enter(bool changed) {
  (void)changed;
  leave();
  flow_.setVirtual(onVirtualCube());
  cubeBox_->setCurrentIndex(onVirtualCube() ? 0 : 1);
  panel_->setSessionName(qs(session().name));
  ctx_.display->showTime(0);
  showScramble();
  updateControls();
}

void SolvesDrill::leave() {
  flow_.reset();
  tick_.stop();
}

void SolvesDrill::deactivated() {
  ctx_.cube->finishAnimations();
  if (isBusy()) {
    apply(flow_.cancel(), std::nullopt);
  }
}

void SolvesDrill::updateControls() {
  bool busy = isBusy();
  panel_->sessionButton()->setEnabled(!busy);
  cubeBox_->setEnabled(!busy);
  // The time only changes during a solve, so the display is only redrawn
  // then.
  if (busy != tick_.isActive()) {
    if (busy) {
      tick_.start(30);
    } else {
      tick_.stop();
    }
  }
  emit busyChanged();
}

bool SolvesDrill::handleKey(QKeyEvent *e, bool press) {
  int key = e->key();
  std::optional<Move> move = ctx_.settings.keymap.lookup(e);
  using K = BlindSolveFlow::Key;
  K k = key == Qt::Key_Space       ? K::Space
        : key == Qt::Key_Escape    ? K::Escape
        : flow_.isVirtual()        ? (move ? K::Move : K::Other)
        : key == Qt::Key_Backspace ? K::Backspace
                                   : K::Other;
  const auto &attempts = session().attempts;
  BlindSolveFlow::Context c;
  c.scrambleReady = ctx_.feed.current().has_value();
  c.session = ctx_.store.currentIndex();
  if (!attempts.empty()) {
    c.lastTimestamp = attempts.back().timestamp;
  }
  BlindSolveFlow::Effects fx =
      flow_.key(k, press, e->isAutoRepeat(), monotonicMs(), c);
  apply(fx, move);
  return fx.used;
}

void SolvesDrill::apply(const BlindSolveFlow::Effects &e,
                        std::optional<Move> move) {
  if (e.waitForScramble) {
    emit message(tr("Wait for the scramble before starting"));
  }
  if (e.startMemo) {
    startMemo();
  }
  if (e.startExecution) {
    startExecution();
  }
  if (e.stop) {
    stopSolve(*e.stop);
  }
  if (e.cancel) {
    cancelSolve();
  }
  if (e.next) {
    next();
  }
  if (e.toggleDnf) {
    BlindAttempt &a = session().attempts.back();
    a.dnf = !a.dnf;
    ctx_.display->showTime(a.timeMs, a.dnf ? Penalty::Dnf : Penalty::None);
    ctx_.save();
    refreshLeft();
  }
  if (e.turn && move) {
    if (attempt_.turn(*move)) {
      ctx_.cube->animateMove(*move);
    } else {
      emit message(tr("Only rotations during the memo"));
    }
  }
  if (e.look && move) {
    look(*move);
  }
}

void SolvesDrill::scrambleChanged() {
  if (flow_.phase() == BlindSolveFlow::Phase::Ready) {
    showScramble();
  }
}

void SolvesDrill::showScramble() {
  // The solve's own while it lasts and after; the feed's next when ready.
  const std::optional<GeneratedScramble> &current =
      flow_.phase() == BlindSolveFlow::Phase::Ready ? ctx_.feed.current()
                                                    : used_;
  if (!current) {
    ctx_.scramble->setText(tr("Generating scramble…"));
    ctx_.net->setCube(std::nullopt);
    return;
  }
  ctx_.scramble->setText(current->text);
  ctx_.net->setScramble(current->text);
  CubieCube scrambled = cubieOf(parseAlgOrEmpty(ss(current->text)));
  heldCubie_ = solverState(scrambled, ctx_.scheme.hold);
  heldStart_ = heldCubie_.toFacelets();
  attempt_.load(heldStart_);
  ctx_.cube->setState(attempt_.cube());
}

void SolvesDrill::startMemo() {
  used_ = ctx_.feed.current();
  showScramble();
  ctx_.feed.advance(); // seen now: cancelled or not, the next solve is new
  ctx_.result->clear();
  emit viewChanged();
  updateControls();
  showHint();
  onTick();
}

void SolvesDrill::startExecution() {
  if (onVirtualCube()) {
    attempt_.start();
  }
  emit viewChanged();
  updateControls();
  showHint();
}

void SolvesDrill::stopSolve(const BlindSolveFlow::Stop &stop) {
  std::int64_t ms = stop.timeMs;
  std::int64_t memoMs = stop.memoMs;
  updateControls();
  if (!onVirtualCube()) {
    recordSolve(ms, memoMs, false, {});
    emit viewChanged();
    showHint();
    return;
  }
  bool solved = attempt_.cube().isSolved();
  // The solution from the WCA hold, so that it replays from the scramble.
  Alg turn = holdRotation(ctx_.scheme.hold);
  std::string solution = toString(attempt_.solution());
  if (!turn.empty()) {
    solution = toString(turn) + (solution.empty() ? "" : " " + solution);
  }
  recordSolve(ms, memoMs, !solved, solution);
  ctx_.cube->finishAnimations();
  emit viewChanged(); // reveal the cube as it was left
  int d = ctx_.settings.decimals;
  if (solved) {
    ctx_.result->setText(colouredHtml(
        kBlindRight, tr("Solved! %1 (memo %2).")
                         .arg(blindTimeText(ms, d), blindTimeText(memoMs, d))));
  } else {
    FaceletCube end = attempt_.cube();
    end.normalizeOrientation();
    std::optional<CubieCube> left = CubieCube::fromFacelets(end);
    BldMemo memo = traceMemo(heldCubie_);
    const LetterScheme &letters = ctx_.scheme.letters;
    QString off;
    if (left) {
      int edges = unsolvedCount(*left, BldPiece::Edge);
      int corners = unsolvedCount(*left, BldPiece::Corner);
      off = tr("%1 and %2 off.")
                .arg(edges == 1 ? tr("1 edge") : tr("%1 edges").arg(edges),
                     corners == 1 ? tr("1 corner")
                                  : tr("%1 corners").arg(corners));
    }
    ctx_.result->setText(
        QString("<p style='color:%1'>%2</p><p>%3</p>")
            .arg(kBlindWrong.name(), tr("DNF: %1").arg(off),
                 tr("The memo traced here: edges %1, corners %2%3.")
                     .arg(qs(letters.format(BldPiece::Edge, memo.edges))
                              .toHtmlEscaped(),
                          qs(letters.format(BldPiece::Corner, memo.corners))
                              .toHtmlEscaped(),
                          memo.parity() ? tr(", with parity") : QString())));
  }
  showHint();
}

void SolvesDrill::cancelSolve() {
  ctx_.prompt->clear();
  updateControls();
  ctx_.display->showTime(0);
  ctx_.result->setText(tr("Solve cancelled; nothing was recorded. The "
                          "next scramble is a new one."));
  showScramble(); // the memo was seen: on to the feed's next one
  emit viewChanged();
  showHint();
}

void SolvesDrill::recordSolve(std::int64_t timeMs, std::int64_t memoMs,
                              bool dnf, const std::string &solution) {
  BlindAttempt a;
  a.timeMs = timeMs;
  a.memoMs = std::min(memoMs, timeMs);
  a.dnf = dnf;
  a.scramble = used_ ? ss(used_->text) : std::string();
  a.solution = solution;
  a.timestamp = unixNow();
  session().attempts.push_back(a);
  flow_.recorded(ctx_.store.currentIndex(), a.timestamp);
  ctx_.display->showTime(timeMs, dnf ? Penalty::Dnf : Penalty::None);
  ctx_.save();
  refreshLeft();
}

void SolvesDrill::next() {
  ctx_.result->clear();
  ctx_.prompt->clear();
  ctx_.cube->clearMarks();
  showScramble(); // the feed moved on when the memo started
  emit viewChanged();
  showHint();
}

void SolvesDrill::onTick() {
  if (!isBusy()) {
    updateControls();
    return;
  }
  std::int64_t now = monotonicMs();
  ctx_.display->showText(blindTimeText(flow_.elapsed(now), 1));
  if (flow_.phase() == BlindSolveFlow::Phase::Executing) {
    ctx_.prompt->setText(
        tr("Memo: %1")
            .arg(blindTimeText(flow_.memoMs(), ctx_.settings.decimals)));
  } else {
    ctx_.prompt->clear();
  }
}

void SolvesDrill::refreshLeft() {
  const auto &attempts = session().attempts;
  std::vector<Solve> solves = blindSolves(session());
  int d = ctx_.settings.decimals;
  std::vector<AttemptRow> rows(attempts.size());
  int successes = 0;
  std::int64_t memoTotal = 0;
  for (std::size_t i = 0; i < attempts.size(); ++i) {
    const BlindAttempt &a = attempts[i];
    QStringList tip;
    if (!a.comment.empty()) {
      tip << qs(a.comment);
    }
    tip << tr("Scramble: %1").arg(qs(a.scramble));
    if (!a.solution.empty()) {
      tip << tr("Solution: %1").arg(qs(a.solution));
    }
    AttemptRow &row = rows[attempts.size() - 1 - i]; // the newest first
    row.index = static_cast<int>(i);
    row.cells = {{QString::number(i + 1), {}, {}, false},
                 {qs(formatSolve(solves[i], d)), {}, tip.join("\n"), true},
                 {blindTimeText(a.memoMs, d), {}, {}, true}};
    if (!a.dnf) {
      ++successes;
      memoTotal += a.memoMs;
    }
  }
  panel_->setRows(std::move(rows));
  static constexpr std::string_view kStats[] = {"single", "mo3", "ao5", "ao12"};
  SessionStats stats = sessionStats(solves, kStats);
  QString html = statsTableHtml(
      stats,
      [](const std::string &id) {
        return id == "single" ? tr("time") : qs(id);
      },
      [d](const std::string &, StatValue v) { return blindTimeText(v, d); });
  int n = static_cast<int>(attempts.size());
  html += tr("<p>Successes: %1 of %2%3<br>Mean of successes: %4 &nbsp; Mean "
             "memo: %5</p>")
              .arg(successes)
              .arg(n)
              .arg(n ? QString(" (%1%)").arg(std::lround(100.0 * successes / n))
                     : QString())
              .arg(blindTimeText(stats.summary.mean, d),
                   successes ? blindTimeText(memoTotal / successes, d)
                             : QString("-"));
  panel_->setStatsHtml(html);
  panel_->chart()->setSolves(solves);
}

void SolvesDrill::showHint() {
  using P = BlindSolveFlow::Phase;
  P phase = flow_.phase();
  QString text;
  if (!onVirtualCube()) {
    text = phase == P::Ready ? tr("Space starts the timer and the memo.")
           : phase == P::Memo
               ? tr("Space when the memo is done (put the blindfold on).")
           : phase == P::Executing
               ? tr("Any key stops the timer. Esc cancels.")
               : tr("Space: next scramble. Backspace toggles DNF.");
  } else {
    text = phase == P::Ready ? tr("Space starts the memo and the timer.")
           : phase == P::Memo
               ? tr("Memorise: turn the cube as a whole to look at it. "
                    "Space hides it and starts the execution. Esc cancels.")
           : phase == P::Executing
               ? tr("Turn the hidden cube. Space stops the timer. Esc "
                    "cancels.")
               : tr("Space: next scramble. Rotations turn the cube to look "
                    "at it.");
  }
  ctx_.hint->setText(text);
}

void SolvesDrill::refreshSessionMenu() {
  std::vector<SessionPanel::MenuSession> list;
  for (const BlindSession &s : ctx_.store.sessions()) {
    list.push_back({qs(s.name), s.attempts.size()});
  }
  panel_->fillSessionMenu(list, ctx_.store.currentIndex());
}

void SolvesDrill::selectSession(std::size_t index) {
  if (isBusy()) {
    return;
  }
  ctx_.store.setCurrentIndex(index);
  ctx_.save();
  emit restartRequested();
}

void SolvesDrill::showAttemptMenu(int index, const QPoint &globalPos) {
  auto &attempts = session().attempts;
  if (index < 0 || index >= static_cast<int>(attempts.size())) {
    return;
  }
  const BlindAttempt &a = attempts[static_cast<std::size_t>(index)];
  // Each action finds the attempt again: the menu runs them once closed.
  auto edit = [this, index](auto change) {
    return [this, index, change] {
      auto &list = session().attempts;
      if (index >= static_cast<int>(list.size())) {
        return;
      }
      if (change(list[static_cast<std::size_t>(index)])) {
        ctx_.save();
        refreshLeft();
      }
    };
  };
  ActionMenu menu(ctx_.page);
  QAction *dnf = menu.add("DNF", edit([](BlindAttempt &x) {
                            x.dnf = !x.dnf;
                            return true;
                          }));
  dnf->setCheckable(true);
  dnf->setChecked(a.dnf);
  AttemptMenuActions common;
  common.comment = edit([this](BlindAttempt &x) {
    std::optional<QString> text =
        askComment(ctx_.page, tr("Comment:"), qs(x.comment));
    if (!text) {
      return false;
    }
    x.comment = ss(*text);
    return true;
  });
  common.copyApart = true;
  common.scramble = qs(a.scramble);
  common.solution = qs(a.solution);
  common.remove = [this, index] {
    auto &list = session().attempts;
    if (index >= static_cast<int>(list.size())) {
      return;
    }
    list.erase(list.begin() + index);
    ctx_.save();
    refreshLeft();
  };
  addAttemptActions(menu, common);
  menu.run(globalPos);
}

void SolvesDrill::addSession() {
  std::optional<QString> name =
      askSessionName(ctx_.page, tr("New session"), tr("Name:"),
                     tr("Session %1").arg(ctx_.store.sessions().size() + 1));
  if (!name) {
    return;
  }
  CubeKind cube = session().cube;
  ctx_.store.addSession(ss(*name)).cube = cube;
  selectSession(ctx_.store.currentIndex());
}

void SolvesDrill::renameSession() {
  std::optional<QString> name = askSessionName(ctx_.page, tr("Rename session"),
                                               tr("Name:"), qs(session().name));
  if (!name) {
    return;
  }
  session().name = ss(*name);
  selectSession(ctx_.store.currentIndex());
}

void SolvesDrill::deleteSession() {
  if (!confirmDeleteSession(ctx_.page, qs(session().name),
                            session().attempts.size(), tr("solves"))) {
    return;
  }
  ctx_.store.removeSession(ctx_.store.currentIndex());
  selectSession(ctx_.store.currentIndex());
}

} // namespace vcube
