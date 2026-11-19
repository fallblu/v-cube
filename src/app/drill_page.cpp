#include "app/drill_page.hpp"
#include "app/drill_private.hpp"
#include "app/key_router.hpp"
#include "app/page_parts.hpp"

#include "app/drill_history_dialog.hpp"
#include "app/ui_state.hpp"
#include "train/alg_follow.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>

namespace vcube {

namespace {

QString poolName(int i) {
  static const char *names[] = {"learned", "ticked", "learning", "queue",
                                "weakest"};
  return names[std::clamp(i, 0, 4)];
}

QString cubeName(bool virt) { return virt ? "virtual" : "physical"; }

QString orderName(PickMode m) {
  switch (m) {
  case PickMode::Random:
    return "random";
  case PickMode::Weighted:
    return "weighted";
  case PickMode::InOrder:
    return "inorder";
  case PickMode::Recap:
    return "recap";
  }
  return "random";
}

} // namespace

DrillPage::DrillPage(AppSettings &settings, AlgLibrary &library,
                     QWidget *parent)
    : CasePage(settings, library, "drill", parent) {
  cubeBox_ = new QComboBox(left_);
  cubeBox_->setFocusPolicy(Qt::NoFocus);
  cubeBox_->addItem(tr("Virtual cube"), cubeName(true));
  cubeBox_->addItem(tr("Physical cube"), cubeName(false));
  cubeBox_->setToolTip(
      tr("The cube the cases are drilled on. Each keeps its own times."));
  scopeBox_ = new QComboBox(left_);
  scopeBox_->setFocusPolicy(Qt::NoFocus);
  scopeBox_->setToolTip(
      tr("The cases to drill from, apart from the ticks (which Learn "
         "shares): the ticked ones, those the list shows (searched or "
         "filtered), or one group"));
  poolBox_ = new QComboBox(left_);
  poolBox_->setFocusPolicy(Qt::NoFocus);
  poolBox_->addItem(tr("Learned in Learn"), poolName(0));
  poolBox_->addItem(tr("All"), poolName(1));
  poolBox_->addItem(tr("Being learned"), poolName(2));
  poolBox_->addItem(tr("In the learn queue"), poolName(3));
  poolBox_->addItem(QString(), poolName(4)); // named in applySettings()
  poolBox_->setToolTip(
      tr("Which of those to drill: the ones learned in Learn mode, all of "
         "them, those in their learning steps, those waiting in Learn's "
         "queue, or the weakest drilled ones (as the Weak column has it)"));
  orderBox_ = new QComboBox(left_);
  orderBox_->setFocusPolicy(Qt::NoFocus);
  orderBox_->addItem(tr("Random"), orderName(PickMode::Random));
  orderBox_->addItem(tr("Weighted (slow and failed cases more often)"),
                     orderName(PickMode::Weighted));
  orderBox_->addItem(tr("In list order"), orderName(PickMode::InOrder));
  orderBox_->addItem(tr("Recap (each case once, then a summary)"),
                     orderName(PickMode::Recap));
  // What is drilled is one choice on the page: the scope and the pool
  // are picked from one menu (the two boxes only hold the choices).
  scopeBox_->hide();
  poolBox_->hide();
  casesButton_ = new QToolButton(left_);
  casesButton_->setFocusPolicy(Qt::NoFocus);
  casesButton_->setPopupMode(QToolButton::InstantPopup);
  casesButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  auto *casesMenu = new QMenu(casesButton_);
  casesButton_->setMenu(casesMenu);
  connect(casesMenu, &QMenu::aboutToShow, this, [this, casesMenu] {
    casesMenu->clear();
    auto fill = [&](QComboBox *box, const QString &title) {
      casesMenu->addSection(title);
      auto *group = new QActionGroup(casesMenu);
      for (int i = 0; i < box->count(); ++i) {
        if (box->itemText(i).isEmpty()) {
          continue; // a separator
        }
        QAction *a = casesMenu->addAction(box->itemText(i));
        a->setCheckable(true);
        a->setChecked(i == box->currentIndex());
        group->addAction(a);
        connect(a, &QAction::triggered, box, [box, i] {
          box->setCurrentIndex(i);
          emit box->activated(i);
        });
      }
    };
    fill(scopeBox_, tr("From"));
    fill(poolBox_, tr("Which of them"));
  });

  auto *sessionRow = new QHBoxLayout;
  sessionLabel_ = new QLabel(left_);
  sessionLabel_->setWordWrap(true);
  sessionLabel_->setTextFormat(Qt::RichText);
  auto *sessionReset = new QPushButton(tr("Reset tally"), left_);
  sessionReset->setFocusPolicy(Qt::NoFocus);
  sessionReset->setToolTip(tr("Start counting attempts afresh (nothing "
                              "recorded is lost)"));
  sessionRow->addWidget(sessionLabel_, 1);
  sessionRow->addWidget(sessionReset, 0, Qt::AlignTop);
  leftLayout_->addLayout(sessionRow);

  // --- right: the drill ---
  auto *right = new QWidget(this);
  auto *rightLayout = new QVBoxLayout(right);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  cubeView_ = new CubeView(right);
  cubeView_->setFillsArea(true);
  cubeView_->setMinimumSize(200, 160);
  cubeView_->setToolTip(
      tr("Turn the cube as a whole to look at the case, tap Space and solve: "
         "the first face turn starts the timer and it stops when the step "
         "is done"));
  options_ = new OptionBar(right);
  options_->addPicker(tr("Alg set:"), panel_->takeSetBox(), 1);
  options_->addPicker(tr("Cube:"), cubeBox_);
  options_->addPicker(tr("Cases:"), casesButton_);
  options_->addPicker(tr("Order:"), orderBox_);
  viewRow_ = options_->addViewPicker(cubeView_, "drill");
  rightLayout->addWidget(options_);
  scrambleLabel_ = new QLabel(right);
  scrambleLabel_->setWordWrap(true);
  scrambleLabel_->setAlignment(Qt::AlignCenter);
  scrambleLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  QFont sf = scrambleLabel_->font();
  sf.setPointSize(17);
  scrambleLabel_->setFont(sf);
  markScrambleLabel(scrambleLabel_);
  scrambleLabel_->setMinimumHeight(56);

  display_ = new TimeDisplay(right);
  statusLabel_ = new QLabel(right);
  statusLabel_->setAlignment(Qt::AlignCenter);
  statusLabel_->setWordWrap(true);
  // The message line and the result keep their room while hidden, so
  // that the lines under them don't move from one state to the next.
  statusLabel_->setMinimumHeight(statusLabel_->fontMetrics().height());
  QSizePolicy keep = statusLabel_->sizePolicy();
  keep.setRetainSizeWhenHidden(true);
  statusLabel_->setSizePolicy(keep);
  // The reason on a line of its own; wrong alg (or DNF) and deleting the
  // attempt are clicked on their keys' chips (see showStatus()).
  resultRow_ = new QWidget(right);
  auto *resultRows = new QVBoxLayout(resultRow_);
  resultRows->setContentsMargins(0, 0, 0, 0);
  resultLabel_ = new QLabel(resultRow_);
  resultLabel_->setWordWrap(true); // a long DNF reason mustn't widen the page
  resultLabel_->setAlignment(Qt::AlignCenter);
  resultLabel_->setMinimumHeight(resultLabel_->fontMetrics().height());
  resultRows->addWidget(resultLabel_);
  keep = resultRow_->sizePolicy();
  keep.setRetainSizeWhenHidden(true);
  resultRow_->setSizePolicy(keep);
  keysLabel_ = new KeyStrip(right);
  // One block centred in the height: the scramble, the cube, and right
  // under it the time, the result, the message, the keys and the algs.
  rightLayout->addStretch(1);
  rightLayout->addWidget(scrambleLabel_);
  rightLayout->addWidget(cubeView_, 4);
  rightLayout->addWidget(display_);
  rightLayout->addWidget(resultRow_);
  rightLayout->addWidget(statusLabel_);
  rightLayout->addWidget(keysLabel_);

  // After a case: its diagram and algs side by side under the time, kept
  // together rather than spread across the page.
  auto *revealBox = new QWidget(right);
  revealBox->setMinimumHeight(kDiagramSize);
  auto *revealLayout = new QHBoxLayout(revealBox);
  revealLayout->setContentsMargins(0, 0, 0, 0);
  diagram_ = new QLabel(revealBox);
  diagram_->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
  diagram_->setFixedSize(kDiagramSize, kDiagramSize);
  reveal_ = new QLabel(revealBox);
  reveal_->setTextFormat(Qt::RichText);
  reveal_->setWordWrap(true);
  reveal_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  reveal_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  reveal_->setMaximumWidth(560);
  reveal_->setMinimumWidth(0);
  QFont rf = reveal_->font();
  rf.setPointSize(rf.pointSize() + 3);
  reveal_->setFont(rf);
  revealLayout->addStretch(1);
  revealLayout->addWidget(diagram_, 0, Qt::AlignTop);
  revealLayout->addWidget(reveal_, 3);
  revealLayout->addStretch(1);
  setDiagram({});
  rightLayout->addWidget(revealBox);
  rightLayout->addStretch(1);

  auto *split = new QSplitter(this);
  split->setObjectName("main");
  split->addWidget(left_);
  split->addWidget(right);
  split->setStretchFactor(1, 1);
  left_->setMinimumWidth(240);
  split->setSizes({360, 840});
  auto *layout = new QHBoxLayout(this);
  layout->addWidget(split);

  connect(&tick_, &QTimer::timeout, this, &DrillPage::onTick);
  connect(&watcher_, &QFutureWatcher<GeneratedScramble>::finished, this,
          &DrillPage::scrambleReady);
  connect(cubeBox_, &QComboBox::activated, this, [this] {
    saved::drillCube(setId_).set(cubeBox_->currentData().toString());
    picker_.setAttemptMode(attemptMode());
    showCubeKind();
    session_.clear(); // the other cube's times
    recapFrom_ = 0;
    refreshSession();
    updateAllRows(); // the other cube's statistics
    if (current_) {
      retryCase();
    } else {
      nextCase();
    }
  });
  connect(scopeBox_, &QComboBox::activated, this, [this] {
    saved::drillScope(setId_).set(scopeBox_->currentData().toString());
    picker_.restartRecap();
    recapFrom_ = session_.size();
    updateSummary();
    nextCase();
  });
  connect(poolBox_, &QComboBox::activated, this, [this] {
    saved::drillPool(setId_).set(poolBox_->currentData().toString());
    picker_.restartRecap();
    recapFrom_ = session_.size();
    updateSummary();
    nextCase();
  });
  connect(orderBox_, &QComboBox::activated, this, [this] {
    saved::drillOrder(setId_).set(orderBox_->currentData().toString());
    picker_.setMode(static_cast<PickMode>(orderBox_->currentIndex()));
    picker_.restartRecap();
    recapFrom_ = session_.size();
    updateSummary();
    nextCase();
  });
  // "Cases shown in the list" follows the search and filter.
  connect(filter_, &CaseFilter::changed, this, [this] { updateSummary(); });
  connect(sessionReset, &QPushButton::clicked, this, [this] {
    session_.clear();
    sessionIndex_.reset();
    recapFrom_ = 0;
    refreshSession();
  });
  init();
  refreshSession();
}

bool DrillPage::onVirtualCube() const { return cubeBox_->currentIndex() == 0; }

AttemptMode DrillPage::attemptMode() const {
  return onVirtualCube() ? AttemptMode::VirtualDrill
                         : AttemptMode::PhysicalDrill;
}

void DrillPage::showCubeKind() {
  bool virt = onVirtualCube();
  flow_.setConfig(flowConfig());
  cubeView_->setVisible(virt);
  viewRow_->setVisible(virt);
  scrambleLabel_->setVisible(!virt);
}

DrillFlow::Config DrillPage::flowConfig() const {
  const DrillSettings &d = settings_.drill;
  return {onVirtualCube(),     d.tapToArm,         d.autoStop,
          d.shiftSpaceGivesUp, d.practiseAfterDnf, d.holdMs};
}

void DrillPage::focusModeChanged() {
  CasePage::focusModeChanged();
  options_->setVisible(!focusMode());
}

bool DrillPage::isBusy() const {
  // Armed by a tap of Space, nothing is under way yet: the page can still
  // be left (which disarms it).
  return flow_.busy();
}

void DrillPage::applySettings() {
  flow_.setConfig(flowConfig());
  display_->setDecimals(settings_.decimals);
  CubeLook look = cubeView_->look();
  look.scheme = settings_.scheme;
  cubeView_->setLook(look);
  cubeView_->setAnimationMs(settings_.animationMs);
  poolBox_->setItemText(
      4, tr("Weakest %1 (drilled)").arg(settings_.drill.weakestCount));
  CasePage::applySettings();
  if (current_ && flow_.resultShown()) {
    if (flow_.phase() != Phase::Unaided) {
      showReveal();
    }
    showResult();
  }
  if (flow_.phase() != Phase::NoCase && !isBusy()) {
    showStatus(); // the keys may have changed
  }
}

void DrillPage::activated() {
  CasePage::activated();
  if (!current_) {
    nextCase();
  }
}

void DrillPage::deactivated() {
  CasePage::deactivated();
  cubeView_->finishAnimations();
  tick_.stop();
  if (flow_.leave()) {
    cancelSolve(tr("The solve was left and cancelled; nothing was "
                   "recorded."));
  } else if (flow_.phase() == Phase::Waiting) {
    display_->showTime(0);
    showStatus();
  }
  updateControls();
}

void DrillPage::windowDeactivated() {
  if (flow_.loseKeyboard()) { // Space's release would never arrive
    display_->showTimer(flow_.timer(), monotonicMs()); // as shown before
    updateControls();
  }
}

void DrillPage::updateControls() {
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
  if (busy == controlsLocked_) {
    return;
  }
  controlsLocked_ = busy;
  for (QWidget *w : std::initializer_list<QWidget *>{setBox_, cubeBox_,
                                                     casesButton_, orderBox_}) {
    w->setEnabled(!busy);
  }
}

// --- drilling
// ------------------------------------------------------------------

void DrillPage::startSet() {
  bool virt = onVirtualCube();
  loadChoices();
  if (onVirtualCube() != virt) {
    updateAllRows(); // the rows were filled for the last set's cube
  }
  current_.reset();
  session_.clear(); // its slowest cases are named from the set
  sessionIndex_.reset();
  recapFrom_ = 0;
  refreshSession();
  updateSummary();
  nextCase();
}

void DrillPage::setReloaded() {
  QString scope = scopeBox_->currentData().toString();
  fillScopes(); // the groups may have changed
  if (scopeBox_->currentData().toString() != scope) {
    scopeBox_->setCurrentIndex(0);
  }
  updateSummary();
  if (current_ && !drillable(current_->caseId)) {
    current_.reset();
    nextCase();
  } else if (current_ && flow_.phase() == Phase::Waiting && !isBusy() &&
             infoOf(current_->caseId)->state != current_->caseState) {
    retryCase(); // the case was edited: set it up afresh
  } else if (current_ && (flow_.phase() == Phase::Revealed ||
                          flow_.phase() == Phase::Aided)) {
    showReveal();
  } else if (!current_) {
    nextCase();
  }
  refreshSession();
}

void DrillPage::ticksChanged() {
  if (flow_.phase() == Phase::NoCase) {
    nextCase();
  }
}

void DrillPage::neutralityChanged() {
  // A new scramble for the case (or new colours on the virtual cube),
  // meant for a hold the new choice allows.
  if (current_ && flow_.phase() == Phase::Waiting && !isBusy()) {
    current_->hold = pickHold();
    retryCase();
  }
}

void DrillPage::startCase(const std::string &caseId) {
  if (drillable(caseId) && !isBusy()) {
    current_ = Current{caseId, {}, pickHold(), {}, pickWorld(caseId), {}};
    picker_.setLast(caseId); // so that it doesn't come straight again
    retryCase();
  }
}

QString DrillPage::giveUpKeyText() const {
  return settings_.drill.shiftSpaceGivesUp ? tr("Shift+Space")
                                           : tr("Backspace");
}

void DrillPage::say(const QString &message, const QList<KeyHint> &keys) {
  statusLabel_->setText(message);
  statusLabel_->setVisible(!message.isEmpty());
  keysLabel_->setKeys(keys);
}

void DrillPage::showStatus() {
  const bool virt = onVirtualCube();
  const KeyHint giveUp{giveUpKeyText(), tr("give up")};
  const bool recorded = flow_.recorded();
  const bool failed = flow_.failed();
  const bool gaveUp = flow_.gaveUp();
  QString message;
  QList<KeyHint> keys;
  switch (flow_.phase()) {
  case Phase::NoCase:
    return;
  case Phase::Waiting:
    if (!virt) {
      message = tr("Scramble, then start the timer.");
      keys = {{tr("hold Space"), tr("start on letting go")}, giveUp};
    } else if (flow_.armed()) {
      message = tr("Ready: the first face turn starts the timer.");
      keys = {{tr("Esc"), tr("not yet")}, giveUp};
    } else if (settings_.drill.tapToArm) {
      message = tr("Turn the cube as a whole to look at the case.");
      keys = {{tr("Space"), tr("ready: the first face turn starts the timer")},
              giveUp};
    } else {
      message = tr("Turn the cube as a whole to look at the case.");
      keys = {{tr("hold Space"), tr("start on letting go or turning")}, giveUp};
    }
    break;
  case Phase::Running:
    if (!virt) {
      keys = {{tr("any key"), tr("stop")}, giveUp, {tr("Esc"), tr("cancel")}};
    } else if (settings_.drill.autoStop) {
      message = tr("The timer stops when the step is done.");
      keys = {{tr("Space"), tr("stop now (a DNF unless done)")},
              giveUp,
              {tr("Esc"), tr("cancel")}};
    } else {
      keys = {{tr("Space"), tr("stop")}, giveUp, {tr("Esc"), tr("cancel")}};
    }
    break;
  case Phase::Revealed: {
    if (!recorded) {
      message = tr("Deleted; nothing is recorded.");
    }
    if (failed && settings_.drill.practiseAfterDnf) {
      keys << KeyHint{tr("Space"), tr("practise it with the algs")}
           << KeyHint{tr("Enter"), tr("next case")};
    } else {
      keys << KeyHint{tr("Space"), tr("next case")}
           << KeyHint{tr("Tab"), tr("practise it")};
    }
    if (recorded && !gaveUp) {
      keys << KeyHint{tr("Backspace"),
                      virt ? (failed ? tr("not a DNF") : tr("DNF"))
                           : (failed ? tr("not a wrong alg") : tr("wrong alg")),
                      [this] { setWrong(!flow_.failed()); }};
    }
    if (recorded) {
      keys << KeyHint{tr("Delete"),
                      gaveUp && flow_.gaveUpBeforeStart() ? tr("take it back")
                                                          : tr("delete it"),
                      [this] { deleteAttempt(); }};
    }
    keys << KeyHint{tr("Esc"), tr("try it again")};
    break;
  }
  case Phase::Aided:
    if (!virt) {
      message = tr("Practice, not timed or recorded: scramble again and solve "
                   "the case with the algs.");
      keys = {{tr("Tab"), tr("now without them")},
              {tr("Space"), tr("next case")}};
    } else if (flow_.practiceDone()) {
      if (settings_.drill.autoAdvance) {
        message = tr("Solved. Now without the algs…");
      } else {
        message = tr("Solved.");
        keys = {{tr("Tab"), tr("now without the algs")},
                {tr("Esc"), tr("once more with them")},
                {tr("Space"), tr("next case")}};
      }
    } else {
      message = tr("Practice, not timed or recorded: solve the case with the "
                   "algs shown.");
      keys = {{tr("Tab"), tr("without them")},
              {tr("Esc"), tr("reset")},
              {tr("Space"), tr("next case")}};
    }
    break;
  case Phase::Unaided:
    if (!virt) {
      message = tr("Scramble again and solve the case without the algs.");
      keys = {{tr("Tab"), tr("show the algs again")},
              {tr("Space"), tr("next case")}};
    } else if (flow_.practiceDone()) {
      if (settings_.drill.autoAdvance) {
        message = tr("Solved without help. Next case…");
      } else {
        message = tr("Solved without help.");
        keys = {{tr("Space"), tr("next case")}, {tr("Esc"), tr("once more")}};
      }
    } else {
      message = tr("Now solve the case without the algs.");
      keys = {{tr("Tab"), tr("show them again")},
              {tr("Esc"), tr("reset")},
              {tr("Space"), tr("next case")}};
    }
    break;
  }
  if (flow_.inPractice() && recorded && !gaveUp) {
    keys << KeyHint{tr("Backspace"),
                    virt ? tr("toggle the DNF") : tr("toggle the wrong alg"),
                    [this] { setWrong(!flow_.failed()); }};
  }
  say(message, keys);
}

QString DrillPage::sessionText(std::size_t from) {
  DrillSummary s = session_.summary(from);
  if (s.count == 0) {
    return {};
  }
  QStringList parts;
  parts << (s.count == 1 ? tr("1 attempt") : tr("%1 attempts").arg(s.count));
  if (s.meanMs) {
    parts << tr("mean %1 s").arg(secondsText(s.meanMs));
  }
  parts << tr("%1 DNF").arg(s.dnfs);
  if (s.newBests > 0) {
    parts << (s.newBests == 1 ? tr("1 new best")
                              : tr("%1 new bests").arg(s.newBests));
  }
  QString text = parts.join(", ");
  text += ".";
  if (!s.slowest.empty()) {
    QStringList slow;
    for (const SessionAttempt &a : s.slowest) {
      slow << QString("%1 %2").arg(caseName(a.caseId),
                                   secondsText(static_cast<double>(a.timeMs)));
    }
    text += " " + tr("Slowest: %1.").arg(slow.join(", "));
  }
  return text;
}

void DrillPage::refreshSession() {
  QString text = sessionText(0);
  sessionLabel_->setText(
      text.isEmpty() ? tr("<b>Tally:</b> no attempts yet.")
                     : tr("<b>Tally:</b> %1").arg(text.toHtmlEscaped()));
}

void DrillPage::nextCase() {
  flow_.leaveCase();
  reveal_->clear();
  setDiagram({});
  resultRow_->setVisible(false);
  const AlgSet *s = set();
  std::vector<std::string> cases = s ? pool() : std::vector<std::string>{};
  picker_.setPool(cases);
  picker_.setTargetMs(s ? s->targetSeconds * 1000.0 : 0.0);
  std::optional<std::string> id = picker_.next(progress(), rng_);
  updateSummary();
  if (!id) {
    current_.reset();
    scrambleLabel_->clear();
    attempt_.load(FaceletCube());
    cubeView_->setState(attempt_.cube());
    display_->showTime(0);
    QString text;
    if (!s) {
      text = tr("No alg set selected.");
    } else if (!cases.empty() && picker_.recapDone()) {
      text = tr("Recap done: every case has come once.") + " " +
             sessionText(recapFrom_) + " " + tr("Space: recap again.");
    } else if (tickedEligible().empty() && tickedCount() > 0 &&
               excludedCount() > 0 &&
               scopeBox_->currentData().toString() == kScopeTicked) {
      text = tr("None of the ticked cases has an alg that solves it yet. "
                "Add algs in Library mode, e.g. with Paste algs.");
    } else if (scopeCases().empty()) {
      text = scopeBox_->currentData().toString() == kScopeTicked
                 ? tr("Tick some cases that have algs to drill them.")
                 : tr("None of these cases has an alg that solves it yet.");
    } else {
      switch (poolChoice()) {
      case Pool::Learned:
        text = tr("None of these cases is learned yet. Learn some in Learn "
                  "mode, or drill \"All\".");
        break;
      case Pool::Learning:
        text = tr("None of these cases is in its learning steps. Choose "
                  "other cases.");
        break;
      case Pool::Queue:
        text = tr("None of these cases is in the learn queue. Choose other "
                  "cases.");
        break;
      case Pool::Weakest:
        text = tr("None of these cases was drilled on this cube yet. Drill "
                  "\"All\" first.");
        break;
      case Pool::All:
        break;
      }
    }
    say(text);
    flow_.noCase();
    return;
  }
  current_ = Current{*id, {}, pickHold(), {}, pickWorld(*id), {}};
  retryCase();
}

void DrillPage::retryCase() {
  flow_.leaveCase();
  const AlgSet *s = set();
  if (!current_ || !drillable(current_->caseId)) {
    // E.g. deleted in the Library: on to another case, which nextCase()
    // picks among those that can be drilled.
    current_.reset();
    if (retryDepth_ == 0) {
      ++retryDepth_;
      nextCase();
      --retryDepth_;
    }
    return;
  }
  const CaseGroup *g = s->groupOf(current_->caseId);
  const CaseInfo *info = infoOf(current_->caseId);
  // The case as it is drawn, in its world, with the step's free pieces
  // scrambled and random U turns around it.
  current_->caseState = info->state;
  current_->state = withRandomAufs(
      *s,
      setUpCase(info->state, s->specFor(*g).free, current_->world, true, rng_),
      rng_);
  current_->scramble.clear();
  missText_.clear();
  sessionIndex_.reset();
  reveal_->clear();
  setDiagram({});
  resultRow_->setVisible(false);
  display_->showTime(0);
  // The list names the case only once it is revealed.
  clearCaseSelection();
  flow_.setUp(monotonicMs());
  if (onVirtualCube()) {
    ++scrambleRequest_; // a physical scramble still on its way is dropped
    scrambleLabel_->clear();
    cubeView_->setColourMap(colourMapOf(current_->hold));
    resetCube();
  } else {
    scrambleLabel_->setText(tr("Generating scramble…"));
    requestScramble();
  }
  showStatus();
}

void DrillPage::followView() {
  const AlgSet *s = set();
  if (!s || !current_ || !s->reduction || current_->world.none()) {
    return;
  }
  // A mirrored case is looked at from its slot's side. Once a step
  // leaves a case whose algs are done as written, such as an OCLL at the
  // end, the cube is seen from the front right again, as the algs are.
  auto c = cubeState();
  auto seen = c ? seenCase(*s, *c, current_->world) : std::nullopt;
  if (!seen) {
    return;
  }
  double yaw =
      yawFor(seen->caseId, seen->mirrored ? current_->world : Mirror{});
  CubeLook look = cubeView_->look();
  if (look.yaw != yaw) {
    look.yaw = yaw;
    cubeView_->setLook(look);
  }
}

void DrillPage::resetCube() {
  if (current_) {
    // In 3D, look from the corner the group's diagrams are drawn from,
    // so that e.g. a back-left F2L slot can be seen, on the case's side.
    CubeLook look = cubeView_->look();
    look.yaw = yawFor(current_->caseId, current_->world);
    cubeView_->setLook(look);
  }
  attempt_.load(current_ ? current_->state.toFacelets() : FaceletCube());
  cubeView_->setState(attempt_.cube());
}

void DrillPage::requestScramble() {
  if (!current_) {
    return;
  }
  int request = ++scrambleRequest_;
  CubieCube state = current_->state;
  // Results of superseded requests are dropped in scrambleReady().
  watcher_.setProperty("request", request);
  ColourNeutrality hold;
  hold.main = current_->hold;
  hold.holds = {current_->hold};
  watcher_.setFuture(generateScramble(
      [state](Rng &) { return ScrambleSetup{state, {}, {}}; }, hold));
}

void DrillPage::scrambleReady() {
  if (onVirtualCube()) {
    return; // the case is set up on the cube instead
  }
  if (watcher_.property("request").toInt() != scrambleRequest_ || !current_) {
    if (current_ && current_->scramble.isEmpty()) {
      requestScramble();
    }
    return;
  }
  current_->scramble = watcher_.result().text;
  scrambleLabel_->setText(current_->scramble);
}

void DrillPage::onTick() {
  std::int64_t now = monotonicMs();
  // A popup or dialog took the keyboard: Space's release won't come.
  if (flow_.holding() && (QApplication::activePopupWidget() ||
                          QApplication::activeModalWidget())) {
    flow_.loseKeyboard();
    display_->showTimer(flow_.timer(), now);
    updateControls();
    return;
  }
  flow_.tick(now);
  if (isBusy()) {
    display_->showTimer(flow_.timer(), now);
  }
}

} // namespace vcube
