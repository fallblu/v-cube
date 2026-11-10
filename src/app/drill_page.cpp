#include "app/drill_page.hpp"

#include "app/drill_history_dialog.hpp"
#include "train/alg_follow.hpp"

#include <QApplication>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>

namespace vcube {

namespace {

const StatusColour kGood{Status::Good};
const StatusColour kSlow{Status::Warning};

// Keys are ignored this long after the timer stops, so that a second key
// meant for the stop doesn't skip the result.
constexpr std::int64_t kGuardMs = 300;
// On a physical cube a stop this soon after the start is taken for a key
// pressed in letting go of Space and ignored.
constexpr std::int64_t kMinPhysicalMs = 150;
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

const QString kScopeTicked = "ticked";
const QString kScopeShown = "shown";
const QString kScopeGroup = "group:";

bool isModifierKey(int key) {
  switch (key) {
  case Qt::Key_Shift:
  case Qt::Key_Control:
  case Qt::Key_Alt:
  case Qt::Key_AltGr:
  case Qt::Key_Meta:
  case Qt::Key_Super_L:
  case Qt::Key_Super_R:
  case Qt::Key_Hyper_L:
  case Qt::Key_Hyper_R:
  case Qt::Key_CapsLock:
  case Qt::Key_NumLock:
    return true;
  default:
    return false;
  }
}

bool isEnter(int key) { return key == Qt::Key_Return || key == Qt::Key_Enter; }

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
  form_->addRow(tr("Cube:"), cubeBox_);
  form_->addRow(tr("Drill:"), scopeBox_);
  form_->addRow(tr("Cases:"), poolBox_);
  form_->addRow(tr("Order:"), orderBox_);

  auto *sessionRow = new QHBoxLayout;
  sessionLabel_ = new QLabel(left_);
  sessionLabel_->setWordWrap(true);
  sessionLabel_->setTextFormat(Qt::RichText);
  auto *sessionReset = new QPushButton(tr("New session"), left_);
  sessionReset->setFocusPolicy(Qt::NoFocus);
  sessionReset->setToolTip(tr("Start counting this session's attempts "
                              "afresh (nothing recorded is lost)"));
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
  viewRow_ = new QWidget(right);
  auto *viewLayout = new QHBoxLayout(viewRow_);
  viewLayout->setContentsMargins(0, 0, 0, 0);
  viewLayout->addWidget(new QLabel(tr("View:"), viewRow_));
  viewLayout->addWidget(createViewSelector(cubeView_, "drill", viewRow_));
  viewLayout->addStretch();
  rightLayout->addWidget(viewRow_);
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
  auto *resultLayout = new QHBoxLayout(resultRow_);
  resultLayout->setContentsMargins(0, 0, 0, 0);
  resultLabel_ = new QLabel(resultRow_);
  resultLabel_->setWordWrap(true); // a long DNF reason mustn't widen the page
  wrongButton_ = new QPushButton(tr("Wrong alg (Backspace)"), resultRow_);
  wrongButton_->setFocusPolicy(Qt::NoFocus);
  wrongButton_->setCheckable(true);
  deleteButton_ = new QPushButton(tr("Delete (Del)"), resultRow_);
  deleteButton_->setFocusPolicy(Qt::NoFocus);
  deleteButton_->setToolTip(
      tr("Delete the attempt, e.g. one stopped by mistake. A case given up "
         "before the timer started is set up again."));
  resultLayout->addStretch();
  resultLayout->addWidget(resultLabel_);
  resultLayout->addWidget(wrongButton_);
  resultLayout->addWidget(deleteButton_);
  resultLayout->addStretch();
  rightLayout->addWidget(cubeView_, 8);
  rightLayout->addWidget(display_);
  rightLayout->addWidget(statusLabel_);
  rightLayout->addWidget(resultRow_);

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
  split->addWidget(left_);
  split->addWidget(right);
  split->setStretchFactor(1, 1);
  left_->setMinimumWidth(380);
  split->setSizes({470, 730});
  auto *layout = new QHBoxLayout(this);
  layout->addWidget(split);

  connect(&tick_, &QTimer::timeout, this, &DrillPage::onTick);
  connect(&watcher_, &QFutureWatcher<GeneratedScramble>::finished, this,
          &DrillPage::scrambleReady);
  connect(cubeBox_, &QComboBox::activated, this, [this] {
    QSettings().setValue(setSettingsKey(setId_, "drillCube"),
                         cubeBox_->currentData());
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
    QSettings().setValue(setSettingsKey(setId_, "drillScope"),
                         scopeBox_->currentData());
    picker_.restartRecap();
    recapFrom_ = session_.size();
    updateSummary();
    nextCase();
  });
  connect(poolBox_, &QComboBox::activated, this, [this] {
    QSettings().setValue(setSettingsKey(setId_, "drillPool"),
                         poolBox_->currentData());
    picker_.restartRecap();
    recapFrom_ = session_.size();
    updateSummary();
    nextCase();
  });
  connect(orderBox_, &QComboBox::activated, this, [this] {
    QSettings().setValue(setSettingsKey(setId_, "drillOrder"),
                         orderBox_->currentData());
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
  cubeView_->setVisible(virt);
  viewRow_->setVisible(virt && left_->isVisibleTo(this));
  scrambleLabel_->setVisible(!virt);
  wrongButton_->setText(virt ? tr("DNF (Backspace)")
                             : tr("Wrong alg (Backspace)"));
  wrongButton_->setToolTip(
      virt ? tr("Toggle DNF on the attempt")
           : tr("Mark the attempt as failed: the wrong alg was done"));
}

void DrillPage::setFocusMode(bool on) {
  CasePage::setFocusMode(on);
  viewRow_->setVisible(!on && onVirtualCube());
}

QStringList DrillPage::columns() const {
  return {tr("Avg"), tr("Best"), tr("Fail"), tr("Weak")};
}

bool DrillPage::isBusy() const {
  // Armed by a tap of Space, nothing is under way yet: the page can still
  // be left (which disarms it).
  auto s = timer_.state();
  return s == SolveTimer::State::Holding ||
         (s == SolveTimer::State::Ready && !armed_) ||
         s == SolveTimer::State::Running;
}

void DrillPage::applySettings() {
  timer_.setConfig({settings_.drill.holdMs, false, 15000});
  display_->setDecimals(settings_.decimals);
  CubeLook look = cubeView_->look();
  look.scheme = settings_.scheme;
  cubeView_->setLook(look);
  cubeView_->setAnimationMs(settings_.animationMs);
  poolBox_->setItemText(
      4, tr("Weakest %1 (drilled)").arg(settings_.drill.weakestCount));
  CasePage::applySettings();
  if (current_ && (phase_ == Phase::Revealed || inPractice())) {
    if (phase_ != Phase::Unaided) {
      showReveal();
    }
    showResult();
  }
  if (phase_ != Phase::NoCase && !isBusy()) {
    setPhase(phase_); // the keys may have changed
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
  ++practiceToken_;
  if (phase_ == Phase::Running) {
    cancelSolve(tr("The solve was left and cancelled; nothing was "
                   "recorded."));
  } else {
    cancelTimer();
    if (phase_ == Phase::Waiting) {
      display_->showTime(0);
      setPhase(Phase::Waiting);
    }
  }
  updateControls();
}

void DrillPage::windowDeactivated() {
  auto s = timer_.state();
  if (s == SolveTimer::State::Holding ||
      (s == SolveTimer::State::Ready && !armed_)) {
    timer_.cancel(); // Space's release would never arrive
    display_->showTimer(timer_, monotonicMs()); // what was shown before
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
  for (QWidget *w : std::initializer_list<QWidget *>{
           setBox_, cubeBox_, scopeBox_, poolBox_, orderBox_}) {
    w->setEnabled(!busy);
  }
}

std::vector<CasePage::CaseStatusFilter> DrillPage::statusFilters() {
  return {
      {tr("Learned"),
       [this](const std::string &id) {
         return eligible(id) && statusOf(cardOf(id)) == CaseStatus::Learned;
       }},
      {tr("Never drilled"),
       [this](const std::string &id) {
         const CaseProgress *cp = progressOf(id);
         return eligible(id) && (!cp || cp->drillCount(attemptMode()) == 0);
       }},
      {tr("Slower than the target"),
       [this](const std::string &id) {
         const CaseProgress *cp = progressOf(id);
         std::optional<double> mean =
             cp ? cp->recentMean(attemptMode()) : std::nullopt;
         const AlgSet *s = set();
         return mean && s && s->targetSeconds > 0 &&
                *mean > s->targetSeconds * 1000.0;
       }},
      {tr("Slow to recognise"),
       [this](const std::string &id) {
         const CaseProgress *cp = progressOf(id);
         std::optional<double> mean = cp ? cp->recognitionMean() : std::nullopt;
         return mean && *mean > settings_.slowRecognitionMs;
       }},
  };
}

void DrillPage::fillRow(QTreeWidgetItem *item, const std::string &caseId) {
  const AttemptMode mode = attemptMode();
  const CaseProgress *cp = progressOf(caseId);
  const AlgSet *s = set();
  double target = s ? s->targetSeconds * 1000.0 : 0.0;
  std::optional<double> mean = cp ? cp->recentMean(mode) : std::nullopt;
  std::optional<double> best;
  if (cp && cp->best(mode)) {
    best = static_cast<double>(*cp->best(mode));
  }
  std::size_t count = cp ? cp->drillCount(mode) : 0;
  std::optional<double> fail;
  if (count) {
    fail = cp->failRate(mode);
  }
  std::optional<double> weak = drillWeakness(cp, mode, target);
  item->setText(1, secondsText(mean));
  item->setText(2, secondsText(best));
  item->setText(3, fail ? QString("%1%").arg(std::lround(*fail * 100))
                        : QString("-"));
  item->setText(4, weak ? QString::number(*weak, 'f', 1) : QString("-"));
  item->setData(1, kSortRole, mean ? QVariant(*mean) : QVariant());
  item->setData(2, kSortRole, best ? QVariant(*best) : QVariant());
  item->setData(3, kSortRole, fail ? QVariant(*fail) : QVariant());
  item->setData(4, kSortRole, weak ? QVariant(*weak) : QVariant());
  if (mean && target > 0) {
    item->setForeground(1, *mean <= target ? kGood : kSlow);
  } else {
    item->setForeground(1, palette().color(QPalette::Text));
  }
  QString tip =
      (onVirtualCube()
           ? tr("Mean of the last 5 good drill attempts on the virtual cube; "
                "%1 virtual drill attempts kept")
           : tr("Mean of the last 5 good drill attempts on a physical cube; "
                "%1 physical drill attempts kept"))
          .arg(count);
  for (int c = 1; c <= 3; ++c) {
    item->setToolTip(c, tip);
  }
  item->setToolTip(
      4, tr("How weak the case is, as Weighted order weighs it: (the median "
            "of its last 5 good times over the target)², held between 0.25 "
            "and 9, times 1 + 3 × its fail rate. 1 is a case at the target "
            "that never fails; higher comes up more. Click the header to "
            "sort by it."));
}

void DrillPage::fillGroupRow(QTreeWidgetItem *item,
                             const std::vector<std::string> &caseIds) {
  // The group's cases taken together: the mean of their means, fail rates
  // and weaknesses, and the best of their bests, over the drilled ones.
  const AttemptMode mode = attemptMode();
  const AlgSet *s = set();
  double target = s ? s->targetSeconds * 1000.0 : 0.0;
  double meanSum = 0, failSum = 0, weakSum = 0;
  int means = 0, drilled = 0;
  std::optional<double> best;
  for (const auto &id : caseIds) {
    const CaseProgress *cp = progressOf(id);
    if (!cp || cp->drillCount(mode) == 0) {
      continue;
    }
    ++drilled;
    failSum += cp->failRate(mode);
    weakSum += drillWeakness(cp, mode, target).value_or(0.0);
    if (auto m = cp->recentMean(mode)) {
      meanSum += *m;
      ++means;
    }
    if (auto b = cp->best(mode); b && (!best || *b < *best)) {
      best = static_cast<double>(*b);
    }
  }
  std::optional<double> mean, fail, weak;
  if (means) {
    mean = meanSum / means;
  }
  if (drilled) {
    fail = failSum / drilled;
    weak = weakSum / drilled;
  }
  item->setText(1, secondsText(mean));
  item->setText(2, secondsText(best));
  item->setText(3, fail ? QString("%1%").arg(std::lround(*fail * 100))
                        : QString("-"));
  item->setText(4, weak ? QString::number(*weak, 'f', 1) : QString("-"));
  item->setData(1, kSortRole, mean ? QVariant(*mean) : QVariant());
  item->setData(2, kSortRole, best ? QVariant(*best) : QVariant());
  item->setData(3, kSortRole, fail ? QVariant(*fail) : QVariant());
  item->setData(4, kSortRole, weak ? QVariant(*weak) : QVariant());
  QColor muted = palette().color(QPalette::PlaceholderText);
  for (int c = 1; c <= 4; ++c) {
    item->setForeground(c, muted);
    item->setToolTip(c, tr("The group's %1 drilled cases of %2 taken "
                           "together: the mean of their means, fail rates "
                           "and weaknesses, and the best of their bests")
                            .arg(drilled)
                            .arg(caseIds.size()));
  }
}

void DrillPage::updateSummary() {
  AlgSet *s = set();
  if (!s) {
    summary_->setText(tr("No alg sets. Create one in Library mode."));
    return;
  }
  QString text = tr("%1 of %2 ticked, %3 to drill. Target time %4 s.")
                     .arg(tickedCount())
                     .arg(s->caseCount())
                     .arg(pool().size())
                     .arg(s->targetSeconds, 0, 'f', 2);
  if (picker_.mode() == PickMode::Recap && picker_.recapLeft() > 0) {
    text += " " + tr("Recap: %1 still to come.").arg(picker_.recapLeft());
  }
  if (int excluded = excludedCount()) {
    text += " " + skippedText(excluded);
  }
  summary_->setText(text);
}

DrillPage::Pool DrillPage::poolChoice() const {
  return static_cast<Pool>(std::max(0, poolBox_->currentIndex()));
}

bool DrillPage::drillable(const std::string &caseId) {
  const AlgSet *s = set();
  return s && s->findCase(caseId) && s->groupOf(caseId) && eligible(caseId);
}

std::vector<std::string> DrillPage::scopeCases() {
  QString scope = scopeBox_->currentData().toString();
  std::vector<std::string> ids;
  if (scope == kScopeShown) {
    ids = shownEligible();
  } else if (scope.startsWith(kScopeGroup)) {
    std::string group = ss(scope.mid(kScopeGroup.size()));
    ids = groupEligible(group);
  } else {
    ids = tickedEligible();
  }
  std::erase_if(ids, [this](const std::string &id) { return !drillable(id); });
  return ids;
}

std::vector<std::string> DrillPage::pool() {
  Pool choice = poolChoice();
  std::vector<std::string> scope = scopeCases();
  if (choice == Pool::All) {
    return scope;
  }
  if (choice == Pool::Weakest) {
    const AlgSet *s = set();
    std::vector<std::string> weak = weakestCases(
        scope, progress(), attemptMode(), s ? s->targetSeconds * 1000.0 : 0.0,
        static_cast<std::size_t>(settings_.drill.weakestCount));
    // In list order, as the others are.
    std::vector<std::string> out;
    for (const auto &id : scope) {
      if (std::find(weak.begin(), weak.end(), id) != weak.end()) {
        out.push_back(id);
      }
    }
    return out;
  }
  std::vector<std::string> out;
  for (const auto &id : scope) {
    const CaseProgress *card = cardOf(id);
    CaseStatus st = statusOf(card);
    bool queued = card && isQueued(card->card);
    if ((choice == Pool::Learned && st == CaseStatus::Learned) ||
        (choice == Pool::Learning && st == CaseStatus::Learning && !queued) ||
        (choice == Pool::Queue && queued)) {
      out.push_back(id);
    }
  }
  return out;
}

void DrillPage::fillScopes() {
  QSignalBlocker block(scopeBox_);
  scopeBox_->clear();
  scopeBox_->addItem(tr("Ticked cases"), kScopeTicked);
  scopeBox_->addItem(tr("Cases shown in the list"), kScopeShown);
  if (const AlgSet *s = set()) {
    if (s->groups.size() > 1) {
      scopeBox_->insertSeparator(scopeBox_->count());
      for (const auto &g : s->groups) {
        scopeBox_->addItem(tr("Group: %1").arg(qs(g.name)),
                           kScopeGroup + qs(g.name));
      }
    }
  }
  int i = scopeBox_->findData(
      QSettings().value(setSettingsKey(setId_, "drillScope"), kScopeTicked));
  scopeBox_->setCurrentIndex(std::max(0, i));
}

void DrillPage::loadChoices() {
  QSettings q;
  int c = cubeBox_->findData(
      q.value(setSettingsKey(setId_, "drillCube"), cubeName(true)));
  cubeBox_->setCurrentIndex(std::max(0, c));
  picker_.setAttemptMode(attemptMode());
  showCubeKind();
  fillScopes();
  int p = poolBox_->findData(
      q.value(setSettingsKey(setId_, "drillPool"), poolName(0)));
  poolBox_->setCurrentIndex(std::max(0, p));
  int o = orderBox_->findData(q.value(setSettingsKey(setId_, "drillOrder"),
                                      orderName(PickMode::Random)));
  orderBox_->setCurrentIndex(std::max(0, o));
  picker_.setMode(static_cast<PickMode>(orderBox_->currentIndex()));
  picker_.restartRecap();
}

// --- drilling
// ------------------------------------------------------------------

QString DrillPage::startCaseLabel(const std::string &caseId) {
  (void)caseId;
  return tr("Drill this case now");
}

void DrillPage::extendCaseMenu(QMenu &menu,
                               const std::vector<std::string> &caseIds) {
  const bool virt = onVirtualCube();
  if (caseIds.size() == 1) {
    std::string id = caseIds.front();
    menu.addAction(tr("Drill history…"), this, [this, id] {
      showDrillHistory(this, caseName(id), progressOf(id), attemptMode(),
                       settings_.decimals);
    });
  }
  QAction *clear =
      menu.addAction(virt ? tr("Clear virtual drill times…")
                          : tr("Clear physical drill "
                               "times…"),
                     this, [this, caseIds] { clearTimes(caseIds); });
  clear->setEnabled(!isBusy());
}

void DrillPage::clearTimes(const std::vector<std::string> &caseIds) {
  if (isBusy()) {
    return;
  }
  QString what = caseIds.size() == 1 ? caseName(caseIds.front())
                                     : tr("%1 cases").arg(caseIds.size());
  auto answer = QMessageBox::question(
      this, tr("Clear drill times"),
      (onVirtualCube()
           ? tr("Forget every virtual-cube drill time of %1, best and daily "
                "history included? Learn progress stays.")
           : tr("Forget every physical-cube drill time of %1, best and daily "
                "history included? Learn progress stays."))
          .arg(what));
  if (answer != QMessageBox::Yes) {
    return;
  }
  for (const auto &id : caseIds) {
    auto &p = progress();
    if (auto it = p.find(id); it != p.end()) {
      it->second.clearDrill(attemptMode());
    }
    picker_.requeue(id, 0);
    if (current_ && current_->caseId == id && recorded_) {
      recorded_ = false; // nothing left to toggle or delete
      showResult();
      setPhase(phase_);
    }
  }
  saveProgress();
  updateAllRows();
}

QString DrillPage::caseName(const std::string &caseId) {
  const AlgSet *s = set();
  const AlgCase *c = s ? s->findCase(caseId) : nullptr;
  return c ? qs(c->name) : qs(caseId);
}

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
  } else if (current_ && phase_ == Phase::Waiting && !isBusy() &&
             infoOf(current_->caseId)->state != current_->caseState) {
    retryCase(); // the case was edited: set it up afresh
  } else if (current_ &&
             (phase_ == Phase::Revealed || phase_ == Phase::Aided)) {
    showReveal();
  } else if (!current_) {
    nextCase();
  }
  refreshSession();
}

void DrillPage::ticksChanged() {
  if (phase_ == Phase::NoCase) {
    nextCase();
  }
}

void DrillPage::neutralityChanged() {
  // A new scramble for the case (or new colours on the virtual cube),
  // meant for a hold the new choice allows.
  if (current_ && phase_ == Phase::Waiting && !isBusy()) {
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

bool DrillPage::isGiveUpKey(const QKeyEvent *e) const {
  return settings_.drill.shiftSpaceGivesUp
             ? e->key() == Qt::Key_Space &&
                   e->modifiers().testFlag(Qt::ShiftModifier)
             : e->key() == Qt::Key_Backspace;
}

void DrillPage::setPhase(Phase p) {
  phase_ = p;
  const bool virt = onVirtualCube();
  const QString giveUp = giveUpKeyText();
  QString status;
  switch (p) {
  case Phase::NoCase:
    return;
  case Phase::Waiting:
    if (!virt) {
      status = tr("Scramble, then hold Space and let go to start. %1 gives "
                  "the case up.")
                   .arg(giveUp);
    } else if (armed_) {
      status = tr("Ready: the first face turn starts the timer. Space: not "
                  "yet. %1 gives the case up.")
                   .arg(giveUp);
    } else if (settings_.drill.tapToArm) {
      status = tr("Turn the cube as a whole to look at the case, then tap "
                  "Space: the first face turn starts the timer. %1 gives the "
                  "case up.")
                   .arg(giveUp);
    } else {
      status = tr("Turn the cube as a whole to look at the case, then hold "
                  "Space and let go (or start turning) to start. %1 gives "
                  "the case up.")
                   .arg(giveUp);
    }
    break;
  case Phase::Running:
    if (!virt) {
      status = tr("Any key stops the timer; %1 gives the case up; Esc "
                  "cancels the solve.")
                   .arg(giveUp);
    } else if (settings_.drill.autoStop) {
      status = tr("The timer stops when the step is done (Space stops it "
                  "before: a DNF unless done); %1 gives the case up; Esc "
                  "cancels the solve.")
                   .arg(giveUp);
    } else {
      status = tr("Space stops the timer; %1 gives the case up; Esc cancels "
                  "the solve.")
                   .arg(giveUp);
    }
    break;
  case Phase::Revealed: {
    QStringList keys;
    if (!recorded_) {
      keys << tr("Deleted; nothing is recorded.");
    }
    if (failed_ && settings_.drill.practiseAfterDnf) {
      keys << tr("Space: practise the case with the algs. Enter: next "
                 "case.");
    } else {
      keys << tr("Space: next case. Tab: practise the case.");
    }
    if (recorded_ && !gaveUp_) {
      keys << (virt ? (failed_ ? tr("Backspace: not a DNF.")
                               : tr("Backspace: DNF."))
                    : (failed_ ? tr("Backspace: not a wrong alg.")
                               : tr("Backspace: wrong alg.")));
    }
    if (recorded_) {
      keys << (gaveUp_ && gaveUpBeforeStart_ ? tr("Delete: take it back.")
                                             : tr("Delete: delete it."));
    }
    keys << tr("Esc: try the case again.");
    status = keys.join(' ');
    break;
  }
  case Phase::Aided:
    if (!virt) {
      status = tr("Practice, not timed or recorded: scramble again and solve "
                  "the case with the algs. Tab: now without them. Space: "
                  "next case.");
    } else if (practiceDone_) {
      status = settings_.drill.autoAdvance
                   ? tr("Solved. Now without the algs…")
                   : tr("Solved. Tab: now without the algs. Esc: once more "
                        "with them. Space: next case.");
    } else {
      status = tr("Practice, not timed or recorded: solve the case with the "
                  "algs shown. Tab: without them. Esc: reset. Space: next "
                  "case.");
    }
    break;
  case Phase::Unaided:
    if (!virt) {
      status = tr("Scramble again and solve the case without the algs. "
                  "Tab: show the algs again. Space: next case.");
    } else if (practiceDone_) {
      status = settings_.drill.autoAdvance
                   ? tr("Solved without help. Next case…")
                   : tr("Solved without help. Space: next case. Esc: once "
                        "more.");
    } else {
      status = tr("Now solve the case without the algs. Tab: show them "
                  "again. Esc: reset. Space: next case.");
    }
    break;
  }
  if (inPractice() && recorded_ && !gaveUp_) {
    status += " " + (virt ? tr("Backspace toggles the DNF.")
                          : tr("Backspace toggles the wrong alg."));
  }
  statusLabel_->setText(status);
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
      text.isEmpty() ? tr("<b>This session:</b> no attempts yet.")
                     : tr("<b>This session:</b> %1").arg(text.toHtmlEscaped()));
}

void DrillPage::cancelTimer() {
  timer_.cancel();
  armed_ = false;
}

void DrillPage::nextCase() {
  ++practiceToken_;
  cancelTimer();
  reveal_->clear();
  setDiagram({});
  resultRow_->setVisible(false);
  recorded_ = false;
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
    statusLabel_->setText(text);
    setPhase(Phase::NoCase);
    return;
  }
  current_ = Current{*id, {}, pickHold(), {}, pickWorld(*id), {}};
  retryCase();
}

void DrillPage::retryCase() {
  ++practiceToken_;
  cancelTimer();
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
  failed_ = false;
  unfinished_ = false;
  gaveUp_ = false;
  gaveUpBeforeStart_ = false;
  recorded_ = false;
  newBest_ = false;
  practiceDone_ = false;
  missText_.clear();
  sessionIndex_.reset();
  reveal_->clear();
  setDiagram({});
  resultRow_->setVisible(false);
  display_->showTime(0);
  // The list names the case only once it is revealed.
  clearCaseSelection();
  shownAtMs_ = monotonicMs();
  firstTurnMs_ = 0;
  timingValid_ = true;
  if (onVirtualCube()) {
    ++scrambleRequest_; // a physical scramble still on its way is dropped
    scrambleLabel_->clear();
    cubeView_->setColourMap(colourMapOf(current_->hold));
    resetCube();
  } else {
    scrambleLabel_->setText(tr("Generating scramble…"));
    requestScramble();
  }
  setPhase(Phase::Waiting);
}

void DrillPage::followView() {
  const AlgSet *s = set();
  if (!s || !current_ || !s->reduction || current_->world.none()) {
    return;
  }
  // A mirrored case is looked at from its slot's corner. Once a step
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
    // mirrored with the case, so that e.g. a back-left F2L slot can be seen.
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
  auto s = timer_.state();
  // A popup or dialog took the keyboard: Space's release won't come.
  if ((s == SolveTimer::State::Holding ||
       (s == SolveTimer::State::Ready && !armed_)) &&
      (QApplication::activePopupWidget() ||
       QApplication::activeModalWidget())) {
    timer_.cancel();
    display_->showTimer(timer_, now);
    updateControls();
    return;
  }
  timer_.tick(now);
  if (isBusy()) {
    display_->showTimer(timer_, now);
  }
}

bool DrillPage::handleKey(QKeyEvent *e, bool press) {
  struct Update {
    DrillPage *page;
    ~Update() { page->updateControls(); }
  } update{this};
  const bool virt = onVirtualCube();
  std::optional<Move> move =
      virt ? settings_.keymap.lookup(e) : std::optional<Move>();
  int key = e->key();
  if (e->isAutoRepeat()) {
    return key == Qt::Key_Space || move.has_value();
  }
  // A modifier alone does nothing, and never stops the timer.
  if (isModifierKey(key)) {
    return phase_ == Phase::Running;
  }
  std::int64_t now = monotonicMs();
  // Just after the stop, a second key meant for it mustn't skip the
  // result, mark it or retry the case.
  if (press && now < guardUntil_ && phase_ == Phase::Revealed) {
    return true;
  }

  switch (phase_) {
  case Phase::NoCase:
    if (press && (key == Qt::Key_Space || isEnter(key))) {
      if (picker_.recapDone()) {
        picker_.restartRecap();
        recapFrom_ = session_.size();
      }
      nextCase();
      return true;
    }
    return move.has_value();
  case Phase::Revealed:
    return handleRevealedKey(e, press, move);
  case Phase::Aided:
  case Phase::Unaided:
    return handlePracticeKey(e, press, move);
  case Phase::Running:
    if (press && key == Qt::Key_Escape) {
      cancelSolve(tr("Solve cancelled; nothing was recorded."));
      return true;
    }
    if (press && isGiveUpKey(e)) {
      giveUp(now);
      return true;
    }
    if (virt) {
      // Space stops the timer; the turning keys turn the cube.
      if (press && key == Qt::Key_Space) {
        stopVirtual(now);
      } else if (press && move) {
        runningTurn(*move, now);
      }
      return key == Qt::Key_Space || move.has_value();
    }
    if (press) {
      if (now - startMs_ < kMinPhysicalMs) {
        return true; // still letting go of Space
      }
      timer_.pressStop(now);
      if (auto r = timer_.takeResult()) {
        finish(r->timeMs, false);
      }
      return true;
    }
    return false;
  case Phase::Waiting:
    return handleWaitingKey(e, press, move, now);
  }
  return false;
}

bool DrillPage::handleWaitingKey(QKeyEvent *e, bool press,
                                 std::optional<Move> move, std::int64_t now) {
  using S = SolveTimer::State;
  const bool virt = onVirtualCube();
  const int key = e->key();
  if (key == Qt::Key_Escape) {
    if (press) {
      cancelTimer();
      display_->showTime(0);
      if (virt) {
        resetCube(); // back to the case as it was set up
      }
      setPhase(Phase::Waiting);
    }
    return true;
  }
  if (isGiveUpKey(e)) {
    if (press && current_) {
      giveUp(now);
    }
    return true;
  }
  if (key == Qt::Key_Backspace) {
    if (press) {
      statusLabel_->setText(tr("%1 gives the case up.").arg(giveUpKeyText()));
    }
    return true;
  }
  if (move) {
    if (!press) {
      return true;
    }
    if (isRotation(move->type)) {
      // Before the solve the cube can only be turned as a whole.
      if (attempt_.turn(*move)) {
        cubeView_->animateMove(*move);
      }
    } else if (virt && current_ && (armed_ || timer_.state() == S::Ready)) {
      // The first face turn starts the timer (also while Space is held
      // once it is ready), and counts.
      startVirtual(now);
      runningTurn(*move, now);
    } else if (timer_.state() == S::Holding) {
      statusLabel_->setText(tr("Keep holding Space until the time turns "
                               "green, then let go or start turning."));
    } else {
      statusLabel_->setText(
          settings_.drill.tapToArm
              ? tr("Only rotations until the timer starts: tap Space first.")
              : tr("Only rotations until the timer starts (hold Space)."));
    }
    return true;
  }
  if (key != Qt::Key_Space) {
    return false;
  }
  if (!current_ || (!virt && current_->scramble.isEmpty())) {
    return true; // wait for the scramble
  }
  if (virt && settings_.drill.tapToArm) {
    if (press) {
      if (armed_) {
        cancelTimer();
      } else {
        timer_.arm(now);
        armed_ = true;
      }
      display_->showTimer(timer_, now);
      setPhase(Phase::Waiting);
    }
    return true;
  }
  if (press) {
    timer_.pressStart(now);
  } else {
    timer_.releaseStart(now);
    if (timer_.state() == S::Running) {
      if (virt) {
        startVirtual(now);
      } else {
        startMs_ = now;
        setPhase(Phase::Running);
      }
    }
  }
  display_->showTimer(timer_, now);
  return true;
}

bool DrillPage::handleRevealedKey(QKeyEvent *e, bool press,
                                  std::optional<Move> move) {
  const int key = e->key();
  if (!press) {
    return key == Qt::Key_Space || move.has_value();
  }
  const bool shift = e->modifiers().testFlag(Qt::ShiftModifier);
  const bool ctrl = e->modifiers().testFlag(Qt::ControlModifier);
  if (key == Qt::Key_Delete || (key == Qt::Key_Z && ctrl)) {
    deleteAttempt();
    return true;
  }
  if (key == Qt::Key_Space && shift) {
    if (!settings_.drill.shiftSpaceGivesUp) {
      nextCase(); // the practice after a DNF is skipped
    }
    // Else Shift is still held from giving up: the reveal stays up.
    return true;
  }
  if (key == Qt::Key_Space || isEnter(key)) {
    if (key == Qt::Key_Space && failed_ && settings_.drill.practiseAfterDnf) {
      startPractice(true);
    } else {
      nextCase();
    }
    return true;
  }
  if (key == Qt::Key_Tab) {
    startPractice(true);
    return true;
  }
  if (key == Qt::Key_Backspace) {
    if (!gaveUp_) { // a case given up has no time to go back to
      setWrong(!failed_);
    }
    return true;
  }
  if (key == Qt::Key_Escape) {
    retryCase();
    return true;
  }
  return move.has_value(); // the cube stays as the solve left it
}

void DrillPage::cancelSolve(const QString &why) {
  cancelTimer();
  cubeView_->finishAnimations();
  display_->showTime(0);
  if (onVirtualCube()) {
    resetCube();
  }
  timingValid_ = false; // the case was seen before this try
  setPhase(Phase::Waiting);
  statusLabel_->setText(why + " " + statusLabel_->text());
}

void DrillPage::startVirtual(std::int64_t now) {
  if (timer_.state() != SolveTimer::State::Running) {
    timer_.startNow(now);
  }
  armed_ = false;
  attempt_.start();
  startMs_ = now;
  faceTurns_ = 0;
  setPhase(Phase::Running);
}

void DrillPage::runningTurn(Move m, std::int64_t now) {
  const bool face = !isRotation(m.type);
  const bool offUd = face && !udTurn(attempt_.cube(), m);
  if (!attempt_.turn(m)) {
    return;
  }
  if (face) {
    ++faceTurns_;
  }
  if (offUd && firstTurnMs_ == 0) {
    // The first turn of a layer off the U-D axis ends recognition.
    firstTurnMs_ = now;
  }
  cubeView_->animateMove(m);
  followView();
  if (face && settings_.drill.autoStop && stepDone()) {
    timer_.stopNow(now);
    if (auto r = timer_.takeResult()) {
      finish(r->timeMs, false);
    }
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

void DrillPage::stopVirtual(std::int64_t now) {
  if (faceTurns_ == 0) {
    // Nothing was done: a stray Space, not an attempt.
    cancelSolve(tr("Stopped before any face turn: cancelled, nothing was "
                   "recorded."));
    return;
  }
  timer_.pressStop(now);
  auto r = timer_.takeResult();
  if (!r) {
    return;
  }
  cubeView_->finishAnimations();
  finish(r->timeMs, !stepDone());
}

void DrillPage::giveUp(std::int64_t now) {
  // The time of a failed attempt counts for nothing, so a case given up
  // before the timer started is recorded with none.
  std::int64_t timeMs = 0;
  bool running = timer_.state() == SolveTimer::State::Running;
  if (running) {
    timer_.pressStop(now);
    if (auto r = timer_.takeResult()) {
      timeMs = r->timeMs;
    }
  }
  cancelTimer();
  cubeView_->finishAnimations();
  gaveUpBeforeStart_ = !running;
  finish(timeMs, true, true);
}

void DrillPage::startPractice(bool aided) {
  if (!current_) {
    return;
  }
  ++practiceToken_;
  practiceDone_ = false;
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
  setPhase(aided ? Phase::Aided : Phase::Unaided);
  showResult();
}

bool DrillPage::handlePracticeKey(QKeyEvent *e, bool press,
                                  std::optional<Move> move) {
  const bool virt = onVirtualCube();
  const bool aided = phase_ == Phase::Aided;
  const int key = e->key();
  const bool ctrl = e->modifiers().testFlag(Qt::ControlModifier);
  const bool handled = key == Qt::Key_Space || isEnter(key) ||
                       key == Qt::Key_Tab || key == Qt::Key_Backspace ||
                       key == Qt::Key_Escape || key == Qt::Key_Delete ||
                       move.has_value();
  if (!press) {
    return handled;
  }
  if (key == Qt::Key_Space || isEnter(key)) {
    nextCase();
    return true;
  }
  if (key == Qt::Key_Tab) {
    startPractice(!aided);
    return true;
  }
  if (key == Qt::Key_Backspace) {
    if (!gaveUp_) {
      setWrong(!failed_); // the DNF stays open to change
    }
    return true;
  }
  if (key == Qt::Key_Delete || (key == Qt::Key_Z && ctrl)) {
    deleteAttempt();
    return true;
  }
  if (key == Qt::Key_Escape) {
    if (virt) {
      startPractice(aided);
    }
    return true;
  }
  if (move && virt && attempt_.turn(*move)) {
    cubeView_->animateMove(*move);
    followView();
    // Once solved it stays solved, whatever turns come after.
    if (!practiceDone_ && !isRotation(move->type) && stepDone()) {
      practiceDone_ = true;
      setPhase(phase_);
      if (settings_.drill.autoAdvance) {
        QTimer::singleShot(kAdvanceMs, this,
                           [this, token = practiceToken_, aided] {
                             if (token != practiceToken_ || !practiceDone_) {
                               return;
                             }
                             if (aided && phase_ == Phase::Aided) {
                               startPractice(false);
                             } else if (!aided && phase_ == Phase::Unaided) {
                               nextCase();
                             }
                           });
      }
    }
  }
  return handled;
}

void DrillPage::finish(std::int64_t timeMs, bool failed, bool gaveUp) {
  if (!current_ || !set()) {
    return;
  }
  const AttemptMode mode = attemptMode();
  attemptMs_ = timeMs;
  failed_ = failed;
  unfinished_ = failed && !gaveUp;
  gaveUp_ = gaveUp;
  const CaseProgress *before = progressOf(current_->caseId);
  bestBefore_ = before ? before->best(mode) : std::nullopt;
  newBest_ = !failed && bestBefore_ && timeMs < *bestBefore_;
  Attempt a{unixNow(), timeMs, failed, mode};
  // On the virtual cube: recognition from the case being set up to the
  // first turn off U and D, execution from there to the stop; only for a
  // case solved, which was recognised rightly.
  if (onVirtualCube() && timingValid_ && firstTurnMs_ > 0 && !failed) {
    a.recognitionMs = std::max<std::int64_t>(1, firstTurnMs_ - shownAtMs_);
    a.executionMs =
        std::max<std::int64_t>(0, timeMs - (firstTurnMs_ - startMs_));
  }
  progress()[current_->caseId].record(a);
  recorded_ = true;
  sessionIndex_ = session_.add({current_->caseId, timeMs, failed, newBest_});
  picker_.requeue(current_->caseId, failed ? settings_.drill.requeueAfter : 0);
  missText_.clear();
  if (unfinished_ && onVirtualCube()) {
    const AlgSet *s = set();
    const CaseGroup *g = s->groupOf(current_->caseId);
    auto c = cubeState();
    if (g && c) {
      StepMiss miss =
          stepMiss(*c, mirrorMask(s->specFor(*g).free, current_->world));
      missText_ =
          miss.auf != 0
              ? tr("Off by an AUF: %1 would have finished it.")
                    .arg(qs(aufText(miss.auf)))
          : miss.pieces == 1
              ? tr("1 piece wasn't where the step leaves it when you stopped "
                   "the timer.")
              : tr("%1 pieces weren't where the step leaves them when you "
                   "stopped the timer.")
                    .arg(miss.pieces);
    }
  }
  guardUntil_ = monotonicMs() + kGuardMs;
  saveProgress();
  selectInTree(current_->caseId);
  setPhase(Phase::Revealed);
  showReveal();
  showResult();
  updateRow(current_->caseId);
  refreshSession();
}

void DrillPage::setWrong(bool wrong) {
  if (!current_ || !recorded_ || gaveUp_ ||
      !(phase_ == Phase::Revealed || inPractice())) {
    showResult();
    return;
  }
  failed_ = wrong;
  unfinished_ = false; // marked by hand now
  missText_.clear();
  newBest_ = !wrong && bestBefore_ && attemptMs_ < *bestBefore_;
  auto &attempts = progress()[current_->caseId].attempts;
  // The attempt just recorded is the last one in this drill mode.
  for (auto it = attempts.rbegin(); it != attempts.rend(); ++it) {
    if (it->mode == attemptMode()) {
      it->failed = failed_;
      if (failed_) {
        it->recognitionMs = 0; // a case failed wasn't recognised rightly
        it->executionMs = 0;
      }
      break;
    }
  }
  saveProgress();
  if (sessionIndex_) {
    session_.setFailed(*sessionIndex_, failed_, newBest_);
  }
  picker_.requeue(current_->caseId, failed_ ? settings_.drill.requeueAfter : 0);
  setPhase(phase_); // what Space does next depends on the DNF
  showResult();
  updateRow(current_->caseId);
  refreshSession();
}

void DrillPage::deleteAttempt() {
  if (!current_ || !recorded_) {
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
  recorded_ = false;
  updateRow(current_->caseId);
  refreshSession();
  if (gaveUp_ && gaveUpBeforeStart_) {
    // Given up by mistake: the case is set up as it was, to try.
    ++practiceToken_;
    gaveUp_ = false;
    failed_ = false;
    missText_.clear();
    reveal_->clear();
    setDiagram({});
    resultRow_->setVisible(false);
    display_->showTime(0);
    clearCaseSelection();
    if (onVirtualCube()) {
      resetCube();
    }
    timingValid_ = false; // the algs were seen
    setPhase(Phase::Waiting);
    statusLabel_->setText(tr("Give-up taken back; nothing was recorded.") +
                          " " + statusLabel_->text());
    return;
  }
  setPhase(phase_);
  showResult();
}

void DrillPage::showResult() {
  if (!(phase_ == Phase::Revealed || inPractice()) || !current_) {
    resultRow_->setVisible(false);
    return;
  }
  resultRow_->setVisible(true);
  wrongButton_->setChecked(failed_);
  wrongButton_->setVisible(!gaveUp_ && recorded_);
  deleteButton_->setVisible(recorded_);
  deleteButton_->setText(gaveUp_ && gaveUpBeforeStart_ ? tr("Take back (Del)")
                                                       : tr("Delete (Del)"));
  const AlgSet *s = set();
  double target = s ? s->targetSeconds : 0.0;
  Solve shown;
  shown.timeMs = attemptMs_;
  shown.penalty = failed_ ? Penalty::Dnf : Penalty::None;
  QColor colour;
  if (!failed_ && target > 0) {
    colour = static_cast<double>(attemptMs_) <= target * 1000.0 ? kGood : kSlow;
  }
  display_->showText(
      gaveUp_ ? tr("DNF") : qs(formatSolve(shown, settings_.decimals)), colour);
  QString text;
  if (!recorded_) {
    text = tr("Deleted");
  } else if (!failed_) {
    text = newBest_ ? tr("New best! Target %1 s").arg(target, 0, 'f', 2)
                    : tr("Target %1 s").arg(target, 0, 'f', 2);
  } else if (gaveUp_) {
    text = tr("Given up");
  } else if (!onVirtualCube()) {
    text = tr("Wrong alg");
  } else if (unfinished_) {
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
  if (onVirtualCube() && failed_ && !attempt_.moves().empty() &&
      phase_ == Phase::Revealed) {
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
