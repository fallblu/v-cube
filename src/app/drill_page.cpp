#include "app/drill_page.hpp"
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

const StatusColour kGood{Status::Good};
const StatusColour kSlow{Status::Warning};

// A practice stage solved is shown this long before the next one.
constexpr int kAdvanceMs = 700;
constexpr int kDiagramSize = 170;

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
  cubeView_->setMinimumSize(260, 220);
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
  rightLayout->addWidget(scrambleLabel_);

  display_ = new TimeDisplay(right);
  statusLabel_ = new QLabel(right);
  statusLabel_->setAlignment(Qt::AlignCenter);
  statusLabel_->setWordWrap(true);
  resultRow_ = new QWidget(right);
  // The reason on a line of its own, the buttons under it.
  auto *resultRows = new QVBoxLayout(resultRow_);
  resultRows->setContentsMargins(0, 0, 0, 0);
  resultLabel_ = new QLabel(resultRow_);
  resultLabel_->setWordWrap(true); // a long DNF reason mustn't widen the page
  resultLabel_->setAlignment(Qt::AlignCenter);
  resultRows->addWidget(resultLabel_);
  auto *resultLayout = new QHBoxLayout;
  resultRows->addLayout(resultLayout);
  wrongButton_ = new QPushButton(tr("Wrong alg (Backspace)"), resultRow_);
  wrongButton_->setFocusPolicy(Qt::NoFocus);
  wrongButton_->setCheckable(true);
  deleteButton_ = new QPushButton(tr("Delete (Del)"), resultRow_);
  deleteButton_->setFocusPolicy(Qt::NoFocus);
  deleteButton_->setToolTip(
      tr("Delete the attempt, e.g. one stopped by mistake. A case given up "
         "before the timer started is set up again."));
  resultLayout->addStretch();
  resultLayout->addWidget(wrongButton_);
  resultLayout->addWidget(deleteButton_);
  resultLayout->addStretch();
  rightLayout->addWidget(cubeView_, 8);
  rightLayout->addWidget(display_);
  keysLabel_ = new QLabel(right);
  keysLabel_->setAlignment(Qt::AlignCenter);
  keysLabel_->setWordWrap(true);
  keysLabel_->setTextFormat(Qt::RichText);
  keysLabel_->setForegroundRole(QPalette::PlaceholderText);
  keysLabel_->setProperty("vcubeHint", true);
  rightLayout->addWidget(statusLabel_);
  rightLayout->addWidget(resultRow_);
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
  rightLayout->addWidget(revealBox, 1);

  auto *split = new QSplitter(this);
  split->setObjectName("main");
  split->addWidget(left_);
  split->addWidget(right);
  split->setStretchFactor(1, 1);
  left_->setMinimumWidth(300);
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
  connect(wrongButton_, &QPushButton::clicked, this,
          [this](bool on) { setWrong(on); });
  connect(deleteButton_, &QPushButton::clicked, this,
          [this] { deleteAttempt(); });
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
  wrongButton_->setText(virt ? tr("DNF (Backspace)")
                             : tr("Wrong alg (Backspace)"));
  wrongButton_->setToolTip(
      virt ? tr("Toggle DNF on the attempt")
           : tr("Mark the attempt as failed: the wrong alg was done"));
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
  keysLabel_->setText(keyHintsHtml(keys, palette()));
  keysLabel_->setVisible(!keys.isEmpty());
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
                      virt
                          ? (failed ? tr("not a DNF") : tr("DNF"))
                          : (failed ? tr("not a wrong alg") : tr("wrong alg"))};
    }
    if (recorded) {
      keys << KeyHint{tr("Delete"), gaveUp && flow_.gaveUpBeforeStart()
                                        ? tr("take it back")
                                        : tr("delete it")};
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
                    virt ? tr("toggle the DNF") : tr("toggle the wrong alg")};
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

bool DrillPage::handleKey(QKeyEvent *e, bool press) {
  struct Update {
    DrillPage *page;
    ~Update() { page->updateControls(); }
  } update{this};
  std::int64_t now = monotonicMs();
  bool scrambleShown =
      current_ && (onVirtualCube() || !current_->scramble.isEmpty());
  DrillFlow::Step step = flow_.key(flowKey(e, press), now, scrambleShown);
  perform(step, now);
  return step.handled;
}

DrillFlow::KeyEvent DrillPage::flowKey(const QKeyEvent *e, bool press) const {
  using Key = DrillFlow::Key;
  DrillFlow::KeyEvent k;
  k.press = press;
  k.shift = e->modifiers().testFlag(Qt::ShiftModifier);
  const bool ctrl = e->modifiers().testFlag(Qt::ControlModifier);
  switch (e->key()) {
  case Qt::Key_Space:
    k.key = Key::Space;
    break;
  case Qt::Key_Return:
  case Qt::Key_Enter:
    k.key = Key::Enter;
    break;
  case Qt::Key_Tab:
    k.key = Key::Tab;
    break;
  case Qt::Key_Backspace:
    k.key = Key::Backspace;
    break;
  case Qt::Key_Delete:
    k.key = Key::Delete;
    break;
  case Qt::Key_Escape:
    k.key = Key::Escape;
    break;
  case Qt::Key_Z:
    k.key = ctrl ? Key::Undo : Key::Other;
    break;
  default:
    k.key = isModifierKey(e->key()) ? Key::Modifier : Key::Other;
    break;
  }
  if (onVirtualCube()) {
    k.move = settings_.keymap.lookup(e);
  }
  return k;
}

QString DrillPage::hintText(DrillFlow::Hint hint) const {
  switch (hint) {
  case DrillFlow::Hint::GiveUpKey:
    return tr("%1 gives the case up.").arg(giveUpKeyText());
  case DrillFlow::Hint::KeepHolding:
    return tr("Keep holding Space until the time turns green, then let go or "
              "start turning.");
  case DrillFlow::Hint::OnlyRotations:
    return settings_.drill.tapToArm
               ? tr("Only rotations until the timer starts: tap Space first.")
               : tr("Only rotations until the timer starts (hold Space).");
  case DrillFlow::Hint::None:
    break;
  }
  return {};
}

void DrillPage::perform(const DrillFlow::Step &step, std::int64_t now) {
  using A = DrillFlow::Action;
  switch (step.action) {
  case A::None:
    break;
  case A::NextFromIdle:
    if (picker_.recapDone()) {
      picker_.restartRecap();
      recapFrom_ = session_.size();
    }
    nextCase();
    break;
  case A::NextCase:
    nextCase();
    break;
  case A::Retry:
    retryCase();
    break;
  case A::ResetWaiting:
    display_->showTime(0);
    if (onVirtualCube()) {
      resetCube(); // back to the case as it was set up
    }
    showStatus();
    break;
  case A::Rotate:
    if (attempt_.turn(*step.move)) {
      cubeView_->animateMove(*step.move);
    }
    break;
  case A::Start:
    if (onVirtualCube()) {
      attempt_.start();
    }
    showStatus();
    if (step.move) {
      runningTurn(*step.move, now); // the turn that started it counts
    } else {
      display_->showTimer(flow_.timer(), now);
    }
    break;
  case A::Armed:
    display_->showTimer(flow_.timer(), now);
    showStatus();
    break;
  case A::TimerShown:
    display_->showTimer(flow_.timer(), now);
    break;
  case A::Turn:
    runningTurn(*step.move, now);
    break;
  case A::Stopped:
    if (onVirtualCube()) {
      // A DNF unless the cube shows the step done.
      cubeView_->finishAnimations();
      finish(step.timeMs, !stepDone());
    } else {
      finish(step.timeMs, false);
    }
    break;
  case A::CancelNoTurn:
    cancelSolve(tr("Stopped before any face turn: cancelled, nothing was "
                   "recorded."));
    break;
  case A::Cancel:
    cancelSolve(tr("Solve cancelled; nothing was recorded."));
    break;
  case A::GiveUp:
    giveUp(now);
    break;
  case A::Practise:
  case A::PracticeReset:
    startPractice(step.aided);
    break;
  case A::PracticeTurn:
    practiceTurn(*step.move);
    break;
  case A::ToggleWrong:
    setWrong(!flow_.failed());
    break;
  case A::Delete:
    deleteAttempt();
    break;
  case A::Hint:
    statusLabel_->setText(hintText(step.hint));
    statusLabel_->show();
    break;
  }
}

void DrillPage::cancelSolve(const QString &why) {
  flow_.cancelSolve();
  cubeView_->finishAnimations();
  display_->showTime(0);
  if (onVirtualCube()) {
    resetCube();
  }
  showStatus();
  statusLabel_->setText((why + " " + statusLabel_->text()).trimmed());
  statusLabel_->show();
}

void DrillPage::runningTurn(Move m, std::int64_t now) {
  const bool face = !isRotation(m.type);
  const bool offUd = face && !udTurn(attempt_.cube(), m);
  if (!attempt_.turn(m)) {
    return;
  }
  // The first turn of a layer off the U-D axis ends recognition.
  std::optional<std::int64_t> stopped =
      flow_.turned(face, offUd, now, [this] { return stepDone(); });
  cubeView_->animateMove(m);
  followView();
  if (stopped) {
    finish(*stopped, false);
  }
}

std::optional<CubieCube> DrillPage::cubeState() const {
  FaceletCube f = attempt_.cube();
  f.normalizeOrientation();
  return CubieCube::fromFacelets(f);
}

bool DrillPage::stepDone() {
  const AlgSet *s = set();
  const CaseGroup *g = s && current_ ? s->groupOf(current_->caseId) : nullptr;
  if (!g) {
    return false;
  }
  auto c = cubeState();
  // As in Learn: the cube itself must reach the step's end state, so a
  // case that leaves the last layer to be lined up (PLL, say) needs its
  // last U turn too, while one that leaves the last-layer permutation free
  // (OLL) doesn't.
  return c && withinMask(*c, mirrorMask(s->specFor(*g).free, current_->world));
}

void DrillPage::giveUp(std::int64_t now) {
  std::int64_t timeMs = flow_.giveUp(now);
  cubeView_->finishAnimations();
  finish(timeMs, true, true);
}

void DrillPage::startPractice(bool aided) {
  if (!current_) {
    return;
  }
  flow_.startPractice(aided);
  if (onVirtualCube()) {
    cubeView_->finishAnimations();
    resetCube();
    attempt_.start(); // every layer turns
  }
  if (aided) {
    showReveal();
  } else {
    reveal_->clear();
    setDiagram({});
  }
  showStatus();
  showResult();
}

void DrillPage::practiceTurn(Move m) {
  if (!attempt_.turn(m)) {
    return;
  }
  cubeView_->animateMove(m);
  followView();
  if (!flow_.practiceTurned(!isRotation(m.type),
                            [this] { return stepDone(); })) {
    return;
  }
  showStatus();
  if (settings_.drill.autoAdvance) {
    const bool aided = flow_.phase() == Phase::Aided;
    QTimer::singleShot(
        kAdvanceMs, this, [this, token = flow_.practiceToken(), aided] {
          if (token != flow_.practiceToken() || !flow_.practiceDone()) {
            return;
          }
          if (aided && flow_.phase() == Phase::Aided) {
            startPractice(false);
          } else if (!aided && flow_.phase() == Phase::Unaided) {
            nextCase();
          }
        });
  }
}

void DrillPage::finish(std::int64_t timeMs, bool failed, bool gaveUp) {
  if (!current_ || !set()) {
    return;
  }
  const AttemptMode mode = attemptMode();
  const CaseProgress *before = progressOf(current_->caseId);
  flow_.finished(timeMs, failed, gaveUp,
                 before ? before->best(mode) : std::nullopt, monotonicMs());
  Attempt a{unixNow(), timeMs, failed, mode};
  // On the virtual cube: recognition from the case being set up to the
  // first turn off U and D, execution from there to the stop; only for a
  // case solved, which was recognised rightly.
  if (auto t = flow_.timing(timeMs, failed)) {
    a.recognitionMs = t->recognitionMs;
    a.executionMs = t->executionMs;
  }
  progress()[current_->caseId].record(a);
  sessionIndex_ =
      session_.add({current_->caseId, timeMs, failed, flow_.newBest()});
  picker_.requeue(current_->caseId, failed ? settings_.drill.requeueAfter : 0);
  missText_.clear();
  if (flow_.unfinished() && onVirtualCube()) {
    const AlgSet *s = set();
    const CaseGroup *g = s->groupOf(current_->caseId);
    auto c = cubeState();
    if (g && c) {
      StepMiss miss =
          stepMiss(*c, mirrorMask(s->specFor(*g).free, current_->world));
      missText_ =
          miss.auf != 0 ? tr("Off by an AUF: %1 would have finished it.")
                              .arg(qs(aufText(miss.auf)))
          : miss.pieces == 1
              ? tr("1 piece wasn't where the step leaves it when you stopped "
                   "the timer.")
              : tr("%1 pieces weren't where the step leaves them when you "
                   "stopped the timer.")
                    .arg(miss.pieces);
    }
  }
  saveProgress();
  selectInTree(current_->caseId);
  showStatus();
  showReveal();
  showResult();
  updateRow(current_->caseId);
  refreshSession();
}

void DrillPage::setWrong(bool wrong) {
  if (!current_ || !flow_.setWrong(wrong)) {
    showResult();
    return;
  }
  const bool failed = flow_.failed();
  missText_.clear();
  auto &attempts = progress()[current_->caseId].attempts;
  // The attempt just recorded is the last one in this drill mode.
  for (auto it = attempts.rbegin(); it != attempts.rend(); ++it) {
    if (it->mode == attemptMode()) {
      it->failed = failed;
      if (failed) {
        it->recognitionMs = 0; // a case failed wasn't recognised rightly
        it->executionMs = 0;
      }
      break;
    }
  }
  saveProgress();
  if (sessionIndex_) {
    session_.setFailed(*sessionIndex_, failed, flow_.newBest());
  }
  picker_.requeue(current_->caseId, failed ? settings_.drill.requeueAfter : 0);
  showStatus(); // what Space does next depends on the DNF
  showResult();
  updateRow(current_->caseId);
  refreshSession();
}

void DrillPage::deleteAttempt() {
  if (!current_ || !flow_.recorded()) {
    return;
  }
  auto &p = progress();
  if (auto it = p.find(current_->caseId); it != p.end()) {
    it->second.removeLast(attemptMode());
  }
  saveProgress();
  if (sessionIndex_ && *sessionIndex_ + 1 == session_.size()) {
    session_.removeLast();
  }
  sessionIndex_.reset();
  picker_.requeue(current_->caseId, 0);
  const bool takenBack = flow_.deleted();
  updateRow(current_->caseId);
  refreshSession();
  if (takenBack) {
    // Given up by mistake: the case is set up as it was, to try.
    missText_.clear();
    reveal_->clear();
    setDiagram({});
    resultRow_->setVisible(false);
    display_->showTime(0);
    clearCaseSelection();
    if (onVirtualCube()) {
      resetCube();
    }
    showStatus();
    statusLabel_->setText((tr("Give-up taken back; nothing was recorded.") +
                           " " + statusLabel_->text())
                              .trimmed());
    statusLabel_->show();
    return;
  }
  showStatus();
  showResult();
}

void DrillPage::showResult() {
  if (!flow_.resultShown() || !current_) {
    resultRow_->setVisible(false);
    return;
  }
  const bool failed = flow_.failed();
  const bool gaveUp = flow_.gaveUp();
  const bool recorded = flow_.recorded();
  resultRow_->setVisible(true);
  wrongButton_->setChecked(failed);
  wrongButton_->setVisible(!gaveUp && recorded);
  deleteButton_->setVisible(recorded);
  deleteButton_->setText(gaveUp && flow_.gaveUpBeforeStart()
                             ? tr("Take back (Del)")
                             : tr("Delete (Del)"));
  const AlgSet *s = set();
  double target = s ? s->targetSeconds : 0.0;
  Solve shown;
  shown.timeMs = flow_.timeMs();
  shown.penalty = failed ? Penalty::Dnf : Penalty::None;
  QColor colour;
  if (!failed && target > 0) {
    colour =
        static_cast<double>(flow_.timeMs()) <= target * 1000.0 ? kGood : kSlow;
  }
  display_->showText(
      gaveUp ? tr("DNF") : qs(formatSolve(shown, settings_.decimals)), colour);
  QString text;
  if (!recorded) {
    text = tr("Deleted");
  } else if (!failed) {
    text = flow_.newBest() ? tr("New best! Target %1 s").arg(target, 0, 'f', 2)
                           : tr("Target %1 s").arg(target, 0, 'f', 2);
  } else if (gaveUp) {
    text = tr("Given up");
  } else if (!onVirtualCube()) {
    text = tr("Wrong alg");
  } else if (flow_.unfinished()) {
    text = missText_.isEmpty()
               ? tr("The case wasn't solved when you stopped the timer.")
               : missText_;
  } else {
    text = tr("Marked DNF");
  }
  resultLabel_->setText(text);
}

void DrillPage::showReveal() {
  if (!current_) {
    return;
  }
  QString html = algsHtml(current_->caseId, current_->state, current_->world);
  // A virtual DNF shows what was done.
  if (onVirtualCube() && flow_.failed() && !attempt_.moves().empty() &&
      flow_.phase() == Phase::Revealed) {
    html += "<p style='color:gray'>" +
            tr("Your moves: %1")
                .arg(qs(toString(attempt_.solution())))
                .toHtmlEscaped() +
            "</p>";
  }
  reveal_->setText(html);
  setDiagram(caseDiagram(current_->caseId, current_->state, kDiagramSize,
                         colourMapOf(current_->hold), current_->world));
}

void DrillPage::setDiagram(const QPixmap &pixmap) {
  diagram_->setPixmap(pixmap);
  diagram_->setVisible(!pixmap.isNull());
}

} // namespace vcube
