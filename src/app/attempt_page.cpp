#include "app/attempt_page.hpp"

#include "app/colours.hpp"
#include "app/flow_layout.hpp"
#include "app/review_dialog.hpp"
#include "io/data_file.hpp"
#include "train/optimal_cache.hpp"

#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace vcube {

namespace {

QString movesText(StatValue v, int decimals = 2) {
  return QString::fromStdString(formatMoves(v, decimals));
}

// The page's keys as the flow reads them.
AttemptFlow::KeyEvent flowKey(const QKeyEvent *e, bool press, bool turn) {
  using K = AttemptFlow::Key;
  AttemptFlow::KeyEvent k;
  k.press = press;
  k.autoRepeat = e->isAutoRepeat();
  k.modified = (e->modifiers() & (Qt::ControlModifier | Qt::AltModifier |
                                  Qt::MetaModifier)) != 0;
  Qt::KeyboardModifiers mods = e->modifiers() & ~Qt::KeypadModifier;
  int key = e->key();
  if (mods == Qt::AltModifier &&
      (key == Qt::Key_3 || key == Qt::Key_C || key == Qt::Key_R)) {
    k.key = key == Qt::Key_3   ? K::Dnf
            : key == Qt::Key_C ? K::Comment
                               : K::Review;
  } else if (mods == Qt::ControlModifier &&
             (key == Qt::Key_Z || key == Qt::Key_Right)) {
    k.key = key == Qt::Key_Z ? K::Undo : K::Skip;
  } else if (mods == Qt::NoModifier && key == Qt::Key_Delete) {
    k.key = K::Delete;
  } else if (turn) {
    k.key = K::Turn;
  } else if (key >= Qt::Key_0 && key <= Qt::Key_9) {
    k.key = K::Digit;
    k.digit = key - Qt::Key_0;
  } else {
    switch (key) {
    case Qt::Key_Space:
      k.key = K::Space;
      break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      k.key = K::Enter;
      break;
    case Qt::Key_Escape:
      k.key = K::Escape;
      break;
    case Qt::Key_Tab:
      k.key = K::Tab;
      break;
    case Qt::Key_Backspace:
      k.key = K::Backspace;
      break;
    case Qt::Key_Left:
      k.key = K::Left;
      break;
    case Qt::Key_Right:
      k.key = K::Right;
      break;
    default:
      k.key = K::Other;
      break;
    }
  }
  return k;
}

} // namespace

AttemptPage::AttemptPage(AppSettings &settings, MoveCountStore &store,
                         AlgLibrary &library, const QString &mode,
                         bool moveCount, QWidget *parent)
    : ModePage(parent), settings_(settings), store_(store), library_(library),
      feed_(library), moveCountFlow_(moveCount) {
  // --- left column: sessions, stats, attempts ---
  panel_ = new SessionPanel(this);
  panel_->setMenuTip(
      tr("Switch, add, rename or delete sessions, or retry the worst solves"));
  panel_->setStatsClickable(true);
  panel_->setDeleteRowVisible(true);

  // --- right column: scramble, cube or entry, chart ---
  auto *right = new QWidget(this);
  rightLayout_ = new QVBoxLayout(right);
  rightLayout_->setContentsMargins(0, 0, 0, 0);
  typeRow_ = new QWidget(right);
  typeLayout_ = new FlowLayout(typeRow_);
  typeLayout_->setContentsMargins(0, 0, 0, 0);
  cubeBox_ = new QComboBox(typeRow_);
  cubeBox_->setFocusPolicy(Qt::NoFocus);
  cubeBox_->addItem(tr("Virtual cube"));
  cubeBox_->addItem(tr("Physical cube"));
  cubeBox_->setToolTip(tr("The cube this session's solves are done on (fixed "
                          "once the session has solves)"));
  cubeView_ = new CubeView(right);
  viewPicker_ = new QWidget(typeRow_);
  auto *viewLayout = new QHBoxLayout(viewPicker_);
  viewLayout->setContentsMargins(0, 0, 0, 0);
  viewLayout->addWidget(new QLabel(tr("View:"), viewPicker_));
  viewLayout->addWidget(createViewSelector(cubeView_, mode, viewPicker_));
  skipButton_ = flatButton(tr("Skip scramble"), typeRow_);
  skipButton_->setToolTip(
      tr("Go on to the next scramble without solving this one (Ctrl+→)"));
  rightLayout_->addWidget(typeRow_);

  auto *scrambleRow = new QHBoxLayout;
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
  auto *scrambleColumn = new QVBoxLayout;
  scrambleColumn->addWidget(scrambleLabel_);
  scrambleColumn->addWidget(holdLabel_);
  net_ = new NetPreview(right);
  scrambleRow->addLayout(scrambleColumn, 1);
  scrambleRow->addWidget(net_);
  rightLayout_->addLayout(scrambleRow);

  centre_ = new QVBoxLayout;
  display_ = new TimeDisplay(right);
  hintLabel_ = new QLabel(right);
  hintLabel_->setAlignment(Qt::AlignCenter);
  hintLabel_->setWordWrap(true);
  hintLabel_->setForegroundRole(QPalette::PlaceholderText);
  buttonRow_ = new QHBoxLayout;
  reviewButton_ = flatButton(tr("Review last"), right);
  reviewButton_->setToolTip(
      tr("Replay the last solve with its steps marked, see the optimal "
         "solutions and how the F2L pairs went (Alt+R; double-click any "
         "solve to review it)"));
  buttonRow_->addStretch();
  buttonRow_->addWidget(reviewButton_);
  buttonRow_->addStretch();
  lastLabel_ = new QLabel(right);
  lastLabel_->setAlignment(Qt::AlignCenter);
  lastLabel_->setWordWrap(true);
  focusStats_ = new QLabel(right);
  focusStats_->setAlignment(Qt::AlignCenter);
  focusStats_->setForegroundRole(QPalette::PlaceholderText);
  focusStats_->setVisible(false);
  centre_->addStretch(1);
  centre_->addWidget(cubeView_, 8);
  centre_->addWidget(display_);
  centre_->addWidget(hintLabel_);
  centre_->addLayout(buttonRow_);
  centre_->addWidget(lastLabel_);
  centre_->addWidget(focusStats_);
  centre_->addStretch(1);
  rightLayout_->addLayout(centre_, 3);

  chart_ = panel_->chart();
  chart_->setParent(right);
  chart_->setEmptyText(tr("Move counts will be charted here"));
  chart_->setMoveCounts(true);
  chart_->setSettingsKey(mode + "/chart");
  rightLayout_->addWidget(chart_, 1);

  auto *split = new QSplitter(this);
  split->addWidget(panel_);
  split->addWidget(right);
  split->setStretchFactor(0, 0);
  split->setStretchFactor(1, 1);
  split->setSizes({300, 800});
  auto *layout = new QHBoxLayout(this);
  layout->addWidget(split);

  // --- wiring ---
  holdTimer_.setSingleShot(true);
  holdTimer_.setTimerType(Qt::PreciseTimer);
  connect(&holdTimer_, &QTimer::timeout, this, &AttemptPage::onHoldTimer);
  holdWatch_.setInterval(50);
  connect(&holdWatch_, &QTimer::timeout, this, &AttemptPage::onHoldTimer);
  connect(&feed_, &ScrambleFeed::currentChanged, this,
          &AttemptPage::showScramble);
  connect(skipButton_, &QPushButton::clicked, this, &AttemptPage::skip);
  connect(cubeBox_, &QComboBox::activated, this, [this](int index) {
    CubeKind kind = index == 0 ? CubeKind::Virtual : CubeKind::Physical;
    if (isBusy() || session().cube == kind) {
      return;
    }
    if (!session().attempts.empty()) {
      cubeBox_->setCurrentIndex(onVirtualCube() ? 0 : 1);
      emit message(tr("This session has solves on the other cube: start a "
                      "new session for this one"));
      return;
    }
    session().cube = kind;
    flow_.resetRetry();
    save();
    showCubeKind();
    apply(flow_.freshScramble());
    refreshAttempts();
  });
  connect(panel_, &SessionPanel::menuAboutToShow, this,
          &AttemptPage::refreshSessionMenu);
  connect(panel_, &SessionPanel::sessionChosen, this,
          &AttemptPage::selectSession);
  connect(panel_, &SessionPanel::newSessionRequested, this,
          &AttemptPage::addSession);
  connect(panel_, &SessionPanel::renameSessionRequested, this,
          &AttemptPage::renameSession);
  connect(panel_, &SessionPanel::deleteSessionRequested, this,
          &AttemptPage::deleteSession);
  connect(panel_, &SessionPanel::deleteLastClicked, this, [this] {
    if (!session().attempts.empty()) {
      deleteAttempt(static_cast<int>(session().attempts.size()) - 1);
    }
  });
  connect(panel_, &SessionPanel::undoClicked, this, &AttemptPage::undoDelete);
  connect(panel_, &SessionPanel::attemptMenuRequested, this,
          &AttemptPage::showAttemptMenu);
  connect(panel_, &SessionPanel::attemptActivated, this, [this](int index) {
    const auto &attempts = session().attempts;
    if (index >= 0 && index < static_cast<int>(attempts.size()) &&
        reviewable(attempts[static_cast<std::size_t>(index)])) {
      review(index);
    } else {
      showDetails(index);
    }
  });
  connect(reviewButton_, &QPushButton::clicked, this,
          [this] { review(static_cast<int>(session().attempts.size()) - 1); });
  connect(panel_, &SessionPanel::statActivated, this, &AttemptPage::showStat);
}

AttemptPage::~AttemptPage() = default;

void AttemptPage::setTypeBox(ScrambleTypeBox *box, QWidget *extra) {
  typeBox_ = box;
  typeLayout_->addWidget(new QLabel(tr("Scramble:"), typeRow_));
  typeLayout_->addWidget(typeBox_, 1);
  if (extra) {
    typeLayout_->addWidget(extra);
  }
  typeLayout_->addSpacing(12);
  typeLayout_->addWidget(cubeBox_);
  typeLayout_->addWidget(viewPicker_);
  typeLayout_->addWidget(skipButton_);
  connect(typeBox_, &ScrambleTypeBox::typeChanged, this,
          [this](const QString &type) {
            if (session().scrambleType != ss(type)) {
              session().scrambleType = ss(type);
              save();
            }
            bool fresh = feed_.type() != type;
            if (fresh) {
              flow_.clearEntry(); // the count was for the old scramble
              showEntry();
              historyCleared();
            }
            scrambleTypeChanged(type, fresh);
            feed_.setType(type);
          });
}

void AttemptPage::start() {
  applySettings();
  selectSession(store_.currentIndex());
  scrambleTypeChanged(typeBox_->currentType(), false);
  feed_.setType(typeBox_->currentType()); // if the type didn't change
}

void AttemptPage::addBelowCentre(QWidget *w) {
  rightLayout_->insertWidget(rightLayout_->indexOf(chart_), w);
}

bool AttemptPage::onVirtualCube() const {
  return store_.sessions()[store_.currentIndex()].cube == CubeKind::Virtual;
}

const GeneratedScramble *AttemptPage::shownScramble() const {
  if (flow_.retry()) {
    return &retryScramble_;
  }
  if (const GeneratedScramble *back = historyScramble()) {
    return back;
  }
  const auto &current = feed_.current();
  return current ? &*current : nullptr;
}

void AttemptPage::configureFlow() {
  flow_.setConfig({moveCountFlow_, onVirtualCube(),
                   static_cast<std::int64_t>(settings_.holdMs),
                   settings_.autoStop});
}

void AttemptPage::deactivated() {
  apply(flow_.dropHold());
  cubeView_->finishAnimations();
  returnFocus();
}

void AttemptPage::windowDeactivated() { apply(flow_.dropHold()); }

void AttemptPage::setFocusMode(bool on) {
  focusMode_ = on;
  if (on) {
    returnFocus();
  }
  panel_->setVisible(!on);
  typeRow_->setVisible(!on);
  // The hint shows the hold's progress in focus mode too.
  hintLabel_->setVisible(!on || flow_.isHolding());
  chart_->setVisible(!on && !scramblesOnly());
  focusStats_->setVisible(on && !scramblesOnly());
  focusModeChanged(on);
}

void AttemptPage::applySettings() {
  configureFlow();
  net_->setScheme(settings_.scheme);
  CubeLook look = cubeView_->look();
  look.scheme = settings_.scheme;
  cubeView_->setLook(look);
  cubeView_->setAnimationMs(settings_.animationMs);
  feed_.setNeutrality(settings_.neutrality);
  settingsApplied();
  refreshAttempts();
}

void AttemptPage::refreshScrambleTypes(const QString &changedSet) {
  QString before = typeBox_->currentType();
  typeBox_->refresh(); // emits typeChanged if the type went away
  if (typeBox_->currentType() == before && !changedSet.isEmpty() &&
      before == "set:" + changedSet) {
    historyCleared();
    feed_.regenerate(); // the set's cases or ticks may have changed
  }
  typesRefreshed();
}

void AttemptPage::showCubeKind() {
  configureFlow(); // the flow is for the session's cube
  bool virt = onVirtualCube();
  cubeBox_->setCurrentIndex(virt ? 0 : 1);
  cubeView_->setVisible(virt);
  viewPicker_->setVisible(virt);
  net_->setVisible(!virt);
  showModeWidgets(virt);
  chart_->setVisible(!scramblesOnly() && !focusMode_);
  focusStats_->setVisible(focusMode_ && !scramblesOnly());
  historyCleared();
  flow_.clearEntry();
}

void AttemptPage::showScramble() {
  const GeneratedScramble *shown = shownScramble();
  scrambleShown(shown);
  // A retry's scramble has no hold of its own.
  holdLabel_->setText(
      !shown || flow_.retry() || settings_.neutrality.isFixed()
          ? QString()
          : tr("Hold: %1").arg(holdHtml(shown->hold, settings_.scheme)));
  if (!shown) {
    scrambleLabel_->setText(tr("Generating scramble…"));
    net_->setCube(std::nullopt);
  } else {
    scrambleLabel_->setText(scrambleText(*shown));
    net_->setScramble(shown->text);
  }
  if (onVirtualCube()) {
    // Shown as scrambled holding white on top and green in front.
    attempt_.load(shown ? parseAlgOrEmpty(ss(shown->text)) : Alg{});
    cubeView_->setState(attempt_.cube());
  }
  apply(flow_.scrambleShown());
  showEntry();
  showHint();
}

void AttemptPage::showHint() {
  using Phase = AttemptFlow::Phase;
  QString text;
  const auto &retry = flow_.retry();
  if (!onVirtualCube()) {
    text = physicalHint();
  } else if (flow_.phase() == Phase::Inspect) {
    text = tr("Turn the cube as a whole to look at it, then hold Space and "
              "release it to start.");
    if (retry) {
      QString which = tr("Retry of #%1 (not recorded). ").arg(retry->index + 1);
      if (flow_.retryCount() > 1) {
        which = tr("Retry %1 of %2, of #%3 (not recorded). ")
                    .arg(flow_.retryNumber())
                    .arg(flow_.retryCount())
                    .arg(retry->index + 1);
      }
      text =
          which + text + " " + tr("Skip scramble goes back to new scrambles.");
    }
  } else if (flow_.phase() == Phase::Solving) {
    text = solvingHint();
  } else if (retry) {
    text = !flow_.retriesQueued()
               ? tr("The cube turns freely. Space: back to new scrambles. Esc: "
                    "reset the cube to solved.")
               : tr("The cube turns freely. Space: the next retry. Esc: reset "
                    "the cube to solved.");
  } else {
    text = tr("The cube turns freely. Space or Enter: next scramble. Esc: "
              "reset the cube to solved.");
  }
  hintLabel_->setText(text);
}

void AttemptPage::showEntry() {
  using Phase = AttemptFlow::Phase;
  if (onVirtualCube()) {
    QColor muted = palette().color(QPalette::PlaceholderText);
    const auto &retry = flow_.retry();
    if (flow_.phase() == Phase::Solving) {
      display_->showText(
          QString::number(attempt_.count(settings_.simplifiedCounts)));
    } else if (retry && retry->done) {
      // The retry's own result, not the last recorded one.
      display_->showText(qs(formatAttempt(retry->result)),
                         retry->result.dnf ? muted : QColor());
    } else if (retry) {
      display_->showText("–", muted);
    } else if (flow_.phase() == Phase::Done || !session().attempts.empty()) {
      // The last result, until the next solve starts.
      const auto &attempts = session().attempts;
      display_->showText(
          attempts.empty() ? QString("–") : qs(formatAttempt(attempts.back())),
          attempts.empty() || attempts.back().dnf ? muted : QColor());
    } else {
      display_->showText("–", muted);
    }
    return;
  }
  const TypedCount &entry = flow_.entry();
  if (entry.empty()) {
    display_->showText("–", palette().color(QPalette::PlaceholderText));
  } else {
    display_->showText(qs(entry.text()));
  }
  entryShown();
}

void AttemptPage::showPhase() {
  bool done = flow_.phase() == AttemptFlow::Phase::Done;
  attempt_.setFree(done);
  phaseShown(done);
  updateControls();
  showHint();
}

void AttemptPage::updateControls() {
  bool busy = isBusy();
  for (QWidget *w : std::initializer_list<QWidget *>{
           panel_->sessionButton(), typeBox_, cubeBox_, skipButton_}) {
    w->setEnabled(!busy);
  }
  controlsEnabled(!busy);
  panel_->setDeleteEnabled(!busy && !session().attempts.empty());
  panel_->setUndo(deleted_.has(), !busy);
  bool canReview =
      !session().attempts.empty() && reviewable(session().attempts.back());
  reviewButton_->setVisible(canReview);
  reviewButton_->setEnabled(!busy);
}

bool AttemptPage::handleKey(QKeyEvent *e, bool press) {
  std::optional<Move> move;
  if (onVirtualCube()) {
    move = settings_.keymap.lookup(e);
  }
  AttemptFlow::Effects fx =
      flow_.key(flowKey(e, press, move.has_value()), monotonicMs(),
                feed_.current().has_value());
  if (fx.toggleAlgs) {
    return toggleAlgs();
  }
  if (fx.turn) {
    turn(*move);
  }
  apply(fx);
  return fx.used;
}

void AttemptPage::apply(const AttemptFlow::Effects &e) {
  using Notice = AttemptFlow::Notice;
  using Action = AttemptFlow::Action;
  switch (e.notice) {
  case Notice::WaitToStart:
    emit message(tr("Wait for the scramble before starting"));
    break;
  case Notice::WaitToRecord:
    emit message(tr("Wait for the scramble before recording"));
    break;
  case Notice::TypeFirst:
    emit message(tr("Type the move count first"));
    break;
  case Notice::None:
    break;
  }
  if (e.holdStarted) {
    holdTimer_.start(static_cast<int>(settings_.holdMs));
    holdWatch_.start();
  }
  if (e.showHold) {
    showHold();
  }
  int last = static_cast<int>(session().attempts.size()) - 1;
  switch (e.action) {
  case Action::Comment:
    commentAttempt(last);
    break;
  case Action::Review:
    review(last);
    break;
  case Action::Undo:
    undoDelete();
    break;
  case Action::Delete:
    if (last >= 0) {
      deleteAttempt(last);
    }
    break;
  case Action::None:
    break;
  }
  if (e.restartCube) {
    attempt_.restart();
    cubeView_->setState(attempt_.cube());
  }
  if (e.solvedCube) {
    attempt_.load(Alg{});
    attempt_.setFree(true);
    cubeView_->setState(attempt_.cube());
  }
  if (e.startSolve) {
    attempt_.start();
  }
  if (e.endSolve) {
    endSolve();
  }
  if (e.record) {
    record(e.record->count, e.record->dnf, e.record->solution);
  }
  if (e.retryResult) {
    showRetryResult();
  }
  if (e.phase) {
    showPhase();
  }
  if (e.entryChanged) {
    showEntry();
  }
  if (e.historyToEnd) {
    historyToEnd();
  }
  if (e.forward) {
    forward();
  }
  if (e.back) {
    back();
  }
  if (e.nextScramble) {
    feed_.advance(); // shows the next scramble
  }
  if (e.showScramble) {
    showScramble();
  }
  if (e.retryNext) {
    startRetry(*e.retryNext);
  }
  if (e.retryStarted && flow_.retry()) {
    const AttemptFlow::Retry &r = *flow_.retry();
    retryScramble_ = GeneratedScramble{};
    retryScramble_.text = qs(r.original.scramble);
    showScramble();
    lastLabel_->setText(tr("Retry of #%1, which was %2")
                            .arg(r.index + 1)
                            .arg(qs(formatAttempt(r.original))));
  }
  if (e.retryEnded) {
    refreshAttempts();
  }
  if (e.analyse && !session().attempts.empty()) {
    solveRecorded(session().attempts.back());
  }
  if (e.notReached) {
    emit message(notReachedText());
  }
}

void AttemptPage::onHoldTimer() {
  std::int64_t now = monotonicMs();
  apply(flow_.tick(now, QApplication::activePopupWidget() ||
                            QApplication::activeModalWidget()));
  if (flow_.hold() == AttemptFlow::Hold::Holding && !holdTimer_.isActive()) {
    holdTimer_.start(static_cast<int>(flow_.holdLeft(now)));
  }
}

void AttemptPage::showHold() {
  using H = AttemptFlow::Hold;
  H hold = flow_.hold();
  if (hold == H::None) {
    holdTimer_.stop();
    holdWatch_.stop();
  }
  if (focusMode_) {
    hintLabel_->setVisible(hold != H::None);
  }
  if (hold == H::None) {
    hintLabel_->setStyleSheet({});
    showHint();
    showEntry();
    return;
  }
  QColor colour =
      hold == H::Ready ? statusColour(Status::Good) : statusColour(Status::Bad);
  hintLabel_->setStyleSheet(QString("color: %1").arg(colour.name()));
  hintLabel_->setText(hold == H::Ready ? tr("Release Space to start")
                                       : tr("Keep holding Space…"));
  holdShown(colour);
}

void AttemptPage::turn(Move m) {
  if (!feed_.current() && !flow_.retry()) {
    return; // no scramble on the cube yet
  }
  if (!attempt_.turn(m)) {
    emit message(tr("Only rotations until you press Space to start"));
    return;
  }
  cubeView_->animateMove(m);
  if (flow_.phase() == AttemptFlow::Phase::Solving) {
    showEntry();
    apply(flow_.turned(attempt_.cube().isSolved()));
  }
}

void AttemptPage::endSolve() {
  apply(flow_.solveEnded(reached(attempt_.cube()),
                         attempt_.count(settings_.simplifiedCounts),
                         toString(attempt_.solution())));
}

void AttemptPage::showRetryResult() {
  const AttemptFlow::Retry &r = *flow_.retry();
  // Compared with the attempt it repeats, and not recorded.
  int count = r.result.moves;
  QString result = r.result.dnf ? tr("DNF (%1)").arg(count)
                   : count == 1 ? tr("1 move")
                                : tr("%1 moves").arg(count);
  QString text = tr("Retry of #%1: %2, was %3")
                     .arg(r.index + 1)
                     .arg(result, qs(formatAttempt(r.original)));
  text += retryNote(r.original);
  lastLabel_->setText(text);
  emit message(text);
}

void AttemptPage::record(std::optional<int> count, bool dnf,
                         const std::string &solution) {
  const GeneratedScramble *shown = shownScramble();
  MoveCountAttempt a;
  a.counted = count.has_value();
  a.moves = std::clamp(count.value_or(0), 0, kMaxMoveCount);
  a.dnf = dnf;
  a.scramble = shown ? ss(shown->text) : std::string();
  a.solution = solution;
  a.timestamp = unixNow();
  session().attempts.push_back(a);
  deleted_.forget();
  save();
  refreshAttempts();
  // A new best, as Timed marks it.
  if (a.counted) {
    QStringList bests;
    for (const NewBest &b :
         newBests(attemptsAsSolves(session().attempts), kWcaUnit)) {
      bests << QString("%1 %2").arg(
          qs(b.name), movesText(b.value, b.name == "single" ? 0 : 2));
    }
    if (!bests.isEmpty()) {
      QString text = tr("New best: %1!").arg(bests.join(", "));
      lastLabel_->setText(lastLabel_->text() + " · " + text);
      emit message(text);
    }
  }
}

void AttemptPage::skip() { apply(flow_.skip()); }

void AttemptPage::review(int index) {
  const auto &attempts = session().attempts;
  if (isBusy() || index < 0 || index >= static_cast<int>(attempts.size())) {
    return;
  }
  const MoveCountAttempt &a = attempts[static_cast<std::size_t>(index)];
  if (!reviewable(a)) {
    return;
  }
  ReviewedSolve solve;
  describeReview(solve, index);
  solve.scramble = parseAlgOrEmpty(a.scramble);
  solve.solution = parseAlgOrEmpty(a.solution);
  solve.dnf = a.dnf;
  if (a.counted && !a.solution.empty()) {
    solve.recorded = a.moves;
  }
  ReviewDialog dialog(solve, settings_, &library_, this);
  dialog.setRetryEnabled(onVirtualCube());
  connect(&dialog, &ReviewDialog::retryRequested, this, [this, index] {
    apply(flow_.retryOne(static_cast<std::size_t>(index)));
  });
  dialog.exec();
}

void AttemptPage::showDetails(int index) {
  const auto &attempts = session().attempts;
  if (index < 0 || index >= static_cast<int>(attempts.size())) {
    return;
  }
  const MoveCountAttempt &a = attempts[static_cast<std::size_t>(index)];
  QList<QPair<QString, QString>> rows;
  if (a.counted) {
    rows.append({tr("Moves"), qs(formatAttempt(a))});
  }
  addDetails(rows, a);
  rows.append({tr("Date"), dateText(a.timestamp)});
  rows.append({"Scramble", qs(a.scramble)});
  if (!a.solution.empty()) {
    rows.append({"Solution", qs(a.solution)});
  }
  rows.append({tr("Comment"), qs(a.comment)});
  showSolveDetails(this, tr("Solve #%1").arg(index + 1), rows,
                   reviewable(a)
                       ? std::function<void()>([this, index] { review(index); })
                       : std::function<void()>{});
}

void AttemptPage::commentAttempt(int index) {
  auto &attempts = session().attempts;
  if (isBusy() || index < 0 || index >= static_cast<int>(attempts.size())) {
    return;
  }
  MoveCountAttempt &a = attempts[static_cast<std::size_t>(index)];
  if (std::optional<QString> text = askComment(
          this, tr("Comment on solve #%1:").arg(index + 1), qs(a.comment))) {
    a.comment = ss(*text);
    save();
    refreshAttempts();
  }
}

void AttemptPage::startRetry(std::size_t index) {
  const auto &attempts = session().attempts;
  if (isBusy() || index >= attempts.size() || !onVirtualCube()) {
    return;
  }
  apply(flow_.beginRetry(index, attempts[index]));
}

void AttemptPage::retryWorst() {
  if (isBusy() || !onVirtualCube()) {
    return;
  }
  // DNFs first, then the most moves over the optimal (when known), then
  // the most moves.
  std::vector<std::size_t> worst =
      worstAttempts(session().attempts, kWorst, optimalLookup());
  if (worst.empty()) {
    emit message(tr("No solves to retry yet"));
    return;
  }
  apply(flow_.retryQueue(worst));
}

void AttemptPage::refreshSessionMenu() {
  std::vector<SessionPanel::MenuSession> list;
  for (const MoveCountSession &s : store_.sessions()) {
    list.push_back({qs(s.name), s.attempts.size()});
  }
  panel_->fillSessionMenu(list, store_.currentIndex());
  if (onVirtualCube() && !scramblesOnly()) {
    QMenu *menu = panel_->menu();
    menu->addSeparator();
    QAction *worst = menu->addAction(tr("Retry the worst %1").arg(kWorst), this,
                                     &AttemptPage::retryWorst);
    worst->setToolTip(retryWorstTip());
    worst->setEnabled(!session().attempts.empty());
  }
}

void AttemptPage::selectSession(std::size_t index) {
  if (isBusy()) {
    return;
  }
  if (index != store_.currentIndex()) {
    deleted_.forget(); // undo is for the session it was deleted from
  }
  flow_.resetRetry();
  store_.setCurrentIndex(index);
  panel_->setSessionName(qs(session().name));
  sessionShown();
  showCubeKind();
  typeBox_->setCurrentType(qs(session().scrambleType));
  apply(flow_.freshScramble()); // onto the session's cube
  refreshAttempts();
  save();
}

QString AttemptPage::scrambleStatsHtml() const {
  // No counts: how many scrambles were done, and when.
  const auto &attempts = session().attempts;
  std::int64_t today = localDayStart();
  std::size_t todayCount = static_cast<std::size_t>(std::count_if(
      attempts.begin(), attempts.end(),
      [today](const MoveCountAttempt &a) { return a.timestamp >= today; }));
  QString html = tr("<p>Solves: %1 &nbsp; Today: %2</p>")
                     .arg(attempts.size())
                     .arg(todayCount);
  if (!attempts.empty()) {
    html += tr("<p>First: %1<br>Last: %2</p>")
                .arg(dateText(attempts.front().timestamp),
                     dateText(attempts.back().timestamp));
  }
  return html;
}

void AttemptPage::refreshAttempts() {
  const auto &attempts = session().attempts;
  std::vector<Solve> solves = attemptsAsSolves(attempts);
  auto ao5 = rollingAverages(solves, 5);
  auto ao12 = rollingAverages(solves, 12);
  panel_->setColumns(columns());
  // Each counted attempt's index in `solves`.
  std::vector<std::optional<std::size_t>> counted(attempts.size());
  for (std::size_t i = 0, c = 0; i < attempts.size(); ++i) {
    if (attempts[i].counted) {
      counted[i] = c++;
    }
  }
  std::vector<AttemptRow> rows;
  rows.reserve(attempts.size());
  for (std::size_t i = attempts.size(); i-- > 0;) {
    const MoveCountAttempt &a = attempts[i];
    AttemptRow row;
    row.index = static_cast<int>(i);
    // A solve with a comment is marked.
    AttemptCell number;
    number.text =
        a.comment.empty() ? QString::number(i + 1) : QString("%1 *").arg(i + 1);
    number.alignRight = false;
    row.cells.push_back(number);
    addCells(row.cells, a, counted[i] ? movesText(ao5[*counted[i]]) : "-",
             counted[i] ? movesText(ao12[*counted[i]]) : "-");
    QStringList tip;
    if (!a.comment.empty()) {
      tip << tr("Comment: %1").arg(qs(a.comment));
    }
    tip << dateText(a.timestamp);
    tip << tr("Scramble: %1").arg(qs(a.scramble));
    if (!a.solution.empty()) {
      tip << tr("Solution: %1").arg(qs(a.solution));
    }
    for (AttemptCell &cell : row.cells) {
      cell.tip = tip.join("\n");
    }
    rows.push_back(std::move(row));
  }
  panel_->setRows(std::move(rows));
  if (scramblesOnly()) {
    panel_->setStatsHtml(scrambleStatsHtml());
    chart_->setSolves({});
    lastLabel_->clear();
    updateControls();
    return;
  }
  static constexpr std::string_view kStats[] = {"single", "mo3", "ao5", "ao12",
                                                "ao100"};
  SessionStats stats = sessionStats(solves, kStats);
  const SessionSummary &sum = stats.summary;
  QString colour = palette().color(QPalette::WindowText).name();
  QString html = statsTableHtml(
      stats,
      [this](const std::string &id) {
        return id == "single" ? tr("single") : qs(id);
      },
      [](const std::string &id, StatValue v) {
        return movesText(v, id == "single" ? 0 : 2);
      },
      colour);
  html +=
      tr("<p>Solves: %1%2 &nbsp; Mean: %3</p>")
          .arg(attempts.size())
          .arg(sum.dnfs ? QString(" (%1 DNF)").arg(sum.dnfs) : QString())
          .arg(statLinkHtml("mean:all", sum.mean, movesText(sum.mean), colour));
  addStats(html);
  panel_->setStatsHtml(html);
  focusStats_->setText(tr("ao5 %1 · ao12 %2 · best %3 · %4 solves")
                           .arg(movesText(currentAverage(solves, 5)),
                                movesText(currentAverage(solves, 12)),
                                movesText(sum.best, 0))
                           .arg(attempts.size()));
  chart_->setSolves(solves);
  updateControls();
  if (flow_.retry()) {
    return; // the label is the retry's
  }
  if (attempts.empty() || !attempts.back().counted) {
    lastLabel_->clear();
  } else {
    const MoveCountAttempt &last = attempts.back();
    lastLabel_->setText(last.dnf ? tr("Last: %1").arg(qs(formatAttempt(last)))
                        : last.moves == 1
                            ? tr("Last: 1 move")
                            : tr("Last: %1 moves").arg(last.moves));
  }
}

void AttemptPage::showStat(const QString &link) {
  const auto &attempts = session().attempts;
  std::vector<Solve> solves = attemptsAsSolves(attempts);
  // The counted attempts, by their numbers in the whole list.
  std::vector<std::size_t> numbers;
  for (std::size_t i = 0; i < attempts.size(); ++i) {
    if (attempts[i].counted) {
      numbers.push_back(i);
    }
  }
  for (std::size_t i = 0; i < solves.size(); ++i) {
    solves[i].scramble = attempts[numbers[i]].scramble;
    solves[i].comment = attempts[numbers[i]].comment;
  }
  StatLinkView view;
  view.format = [](StatValue v) { return movesText(v); };
  view.result = [&attempts, &numbers](std::size_t i) {
    return qs(formatAttempt(attempts[numbers[i]]));
  };
  view.number = [&numbers](std::size_t i) { return numbers[i] + 1; };
  view.details = [this, &numbers](std::size_t i) {
    showDetails(static_cast<int>(numbers[i]));
  };
  openStatLink(this, link, solves, view);
}

void AttemptPage::deleteAttempt(int index) {
  auto &attempts = session().attempts;
  if (isBusy() || index < 0 ||
      !deleted_.erase(attempts, static_cast<std::size_t>(index))) {
    return;
  }
  flow_.attemptDeleted();
  save();
  refreshAttempts();
  showEntry();
  emit message(tr("Deleted solve #%1 (Ctrl+Z puts it back)").arg(index + 1));
}

void AttemptPage::undoDelete() {
  if (!deleted_.has() || isBusy()) {
    return;
  }
  std::optional<std::size_t> at = deleted_.restore(session().attempts);
  emit message(tr("Put back solve #%1").arg(*at + 1));
  save();
  refreshAttempts();
  showEntry();
}

void AttemptPage::showAttemptMenu(int index, const QPoint &globalPos) {
  const auto &attempts = session().attempts;
  if (isBusy() || index < 0 || index >= static_cast<int>(attempts.size())) {
    return; // nothing changes in the middle of a solve
  }
  const MoveCountAttempt &a = attempts[static_cast<std::size_t>(index)];
  // Changes the attempt (if it is still there) and shows it.
  auto change = [this,
                 index](const std::function<void(MoveCountAttempt &)> &f) {
    auto &list = session().attempts;
    if (index >= static_cast<int>(list.size())) {
      return;
    }
    f(list[static_cast<std::size_t>(index)]);
    save();
    refreshAttempts();
    showEntry();
  };
  ActionMenu menu(this);
  if (a.counted) {
    // A virtual solve's count is its solution's.
    if (a.solution.empty()) {
      int moves = a.moves;
      menu.add(tr("Edit move count…"), [this, change, moves] {
        bool ok = false;
        int count =
            QInputDialog::getInt(this, tr("Edit move count"), tr("Moves:"),
                                 moves, 0, kMaxMoveCount, 1, &ok);
        if (ok) {
          change([count](MoveCountAttempt &x) { x.moves = count; });
        }
      });
    }
    QAction *dnf = menu.add("DNF", [change] {
      change([](MoveCountAttempt &x) { x.dnf = !x.dnf; });
    });
    dnf->setCheckable(true);
    dnf->setChecked(a.dnf);
    menu.addSeparator();
  }
  if (reviewable(a)) {
    QAction *reviewAction =
        menu.add(tr("Review…"), [this, index] { review(index); });
    menu.setDefaultAction(reviewAction);
  }
  AttemptMenuActions common;
  common.details = [this, index] { showDetails(index); };
  common.comment = [this, index] { commentAttempt(index); };
  common.scramble = qs(a.scramble);
  common.solution = qs(a.solution);
  common.remove = [this, index] { deleteAttempt(index); };
  addAttemptActions(menu, common);
  menu.run(globalPos, [this] { return !isBusy(); });
}

void AttemptPage::addSession() {
  std::optional<QString> name = askSessionName(
      this, tr("New session"), newSessionLabel(), suggestedName());
  if (!name) {
    return;
  }
  // What the new session takes from the current one (which addSession()
  // may move).
  MoveCountSession was;
  was.name = session().name;
  was.scrambleType = session().scrambleType;
  was.cube = session().cube;
  was.step = session().step;
  MoveCountSession &s = store_.addSession(ss(*name));
  setUpSession(s, *name, was);
  selectSession(store_.currentIndex());
}

void AttemptPage::renameSession() {
  std::optional<QString> name = askSessionName(this, tr("Rename session"),
                                               tr("Name:"), qs(session().name));
  if (!name) {
    return;
  }
  // Only the name changes: the scramble, a typed count and a retry stay.
  session().name = ss(*name);
  panel_->setSessionName(qs(session().name));
  sessionShown();
  save();
}

void AttemptPage::deleteSession() {
  if (!confirmDeleteSession(this, qs(session().name), session().attempts.size(),
                            tr("solves"))) {
    return;
  }
  store_.removeSession(store_.currentIndex());
  deleted_.forget();
  selectSession(store_.currentIndex());
}

void AttemptPage::save() {
  saveInBackground(store_, this, [this](const QString &error) {
    emit message(tr("Could not save sessions: %1").arg(error));
  });
}

} // namespace vcube
