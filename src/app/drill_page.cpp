#include "app/drill_page.hpp"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace vcube {

namespace {

const QColor kGood(0x3d, 0xd6, 0x6b);
const QColor kSlow(0xff, 0xb0, 0x30);

QString poolName(int i) {
  static const char *names[] = {"learned", "ticked", "learning"};
  return names[std::clamp(i, 0, 2)];
}

QString orderName(PickMode m) {
  switch (m) {
  case PickMode::Random:
    return "random";
  case PickMode::Weighted:
    return "weighted";
  case PickMode::InOrder:
    return "inorder";
  }
  return "random";
}

} // namespace

DrillPage::DrillPage(AppSettings &settings, AlgLibrary &library,
                     QWidget *parent)
    : CasePage(settings, library, "drill", parent) {
  poolBox_ = new QComboBox(left_);
  poolBox_->setFocusPolicy(Qt::NoFocus);
  poolBox_->addItem(tr("Learned cases"), poolName(0));
  poolBox_->addItem(tr("All ticked"), poolName(1));
  poolBox_->addItem(tr("Learning queue"), poolName(2));
  poolBox_->setToolTip(
      tr("Which ticked cases to drill: those learned in Learn mode, all of "
         "them, or those still being learned"));
  orderBox_ = new QComboBox(left_);
  orderBox_->setFocusPolicy(Qt::NoFocus);
  orderBox_->addItem(tr("Random"), orderName(PickMode::Random));
  orderBox_->addItem(tr("Weighted (slow and failed cases more often)"),
                     orderName(PickMode::Weighted));
  orderBox_->addItem(tr("In order"), orderName(PickMode::InOrder));
  form_->addRow(tr("Cases:"), poolBox_);
  form_->addRow(tr("Order:"), orderBox_);

  // --- right: the drill ---
  auto *right = new QWidget(this);
  auto *rightLayout = new QVBoxLayout(right);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  scrambleLabel_ = new QLabel(right);
  scrambleLabel_->setWordWrap(true);
  scrambleLabel_->setAlignment(Qt::AlignCenter);
  scrambleLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  QFont sf = scrambleLabel_->font();
  sf.setPointSize(17);
  scrambleLabel_->setFont(sf);
  scrambleLabel_->setMinimumHeight(56);
  rightLayout->addWidget(scrambleLabel_);

  auto *middle = new QHBoxLayout;
  auto *timeColumn = new QVBoxLayout;
  display_ = new TimeDisplay(right);
  statusLabel_ = new QLabel(right);
  statusLabel_->setAlignment(Qt::AlignCenter);
  statusLabel_->setWordWrap(true);
  resultRow_ = new QWidget(right);
  auto *resultLayout = new QHBoxLayout(resultRow_);
  resultLayout->setContentsMargins(0, 0, 0, 0);
  resultLabel_ = new QLabel(resultRow_);
  wrongButton_ = new QPushButton(tr("Wrong alg (Backspace)"), resultRow_);
  wrongButton_->setFocusPolicy(Qt::NoFocus);
  wrongButton_->setCheckable(true);
  resultLayout->addStretch();
  resultLayout->addWidget(resultLabel_);
  resultLayout->addWidget(wrongButton_);
  resultLayout->addStretch();
  timeColumn->addStretch();
  timeColumn->addWidget(display_);
  timeColumn->addWidget(statusLabel_);
  timeColumn->addWidget(resultRow_);
  timeColumn->addStretch();
  // An empty column as wide as the diagram keeps the time centred, as in
  // Timer mode. Both are hidden until there is a diagram to show.
  diagramBalance_ = new QWidget(right);
  diagramBalance_->setMinimumSize(200, 0);
  middle->addWidget(diagramBalance_, 2);
  middle->addLayout(timeColumn, 3);
  diagram_ = new QLabel(right);
  diagram_->setAlignment(Qt::AlignCenter);
  diagram_->setMinimumSize(200, 200);
  middle->addWidget(diagram_, 2);
  setDiagram({});
  rightLayout->addLayout(middle, 3);

  reveal_ = new QLabel(right);
  reveal_->setTextFormat(Qt::RichText);
  reveal_->setWordWrap(true);
  reveal_->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
  reveal_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  QFont rf = reveal_->font();
  rf.setPointSize(rf.pointSize() + 3);
  reveal_->setFont(rf);
  reveal_->setMinimumHeight(110);
  rightLayout->addWidget(reveal_, 1);

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
  connect(poolBox_, &QComboBox::activated, this, [this] {
    QSettings().setValue(setSettingsKey(setId_, "drillPool"),
                         poolBox_->currentData());
    updateSummary();
    nextCase();
  });
  connect(orderBox_, &QComboBox::activated, this, [this] {
    QSettings().setValue(setSettingsKey(setId_, "drillOrder"),
                         orderBox_->currentData());
    picker_.setMode(static_cast<PickMode>(orderBox_->currentIndex()));
    nextCase();
  });
  connect(wrongButton_, &QPushButton::clicked, this,
          [this](bool on) { setWrong(on); });
  init();
}

QStringList DrillPage::columns() const {
  return {tr("Avg"), tr("Best"), tr("Fail")};
}

bool DrillPage::isBusy() const {
  auto s = timer_.state();
  return s == SolveTimer::State::Holding || s == SolveTimer::State::Ready ||
         s == SolveTimer::State::Running;
}

void DrillPage::applySettings() {
  timer_.setConfig({settings_.holdMs, false, 15000});
  display_->setDecimals(settings_.decimals);
  CasePage::applySettings();
  if (current_ && phase_ == Phase::Revealed) {
    showReveal();
    showResult();
  }
}

void DrillPage::activated() {
  CasePage::activated();
  tick_.start(16);
  if (!current_) {
    nextCase();
  }
}

void DrillPage::deactivated() {
  tick_.stop();
  timer_.cancel();
  updateControls();
}

void DrillPage::updateControls() {
  bool busy = isBusy();
  if (busy == controlsLocked_) {
    return;
  }
  controlsLocked_ = busy;
  for (QWidget *w :
       std::initializer_list<QWidget *>{setBox_, poolBox_, orderBox_}) {
    w->setEnabled(!busy);
  }
}

void DrillPage::fillRow(QTreeWidgetItem *item, const std::string &caseId) {
  const CaseProgress *cp = progressOf(caseId);
  std::optional<double> mean = cp ? cp->recentMean() : std::nullopt;
  std::optional<double> best;
  if (cp && cp->best()) {
    best = static_cast<double>(*cp->best());
  }
  std::size_t count = cp ? cp->drillCount() : 0;
  item->setText(1, secondsText(mean));
  item->setText(2, secondsText(best));
  item->setText(3, count ? QString("%1%").arg(std::lround(cp->failRate() * 100))
                         : QString("-"));
  const AlgSet *s = set();
  double target = s ? s->targetSeconds * 1000.0 : 0.0;
  if (mean && target > 0) {
    item->setForeground(1, *mean <= target ? kGood : kSlow);
  } else {
    item->setForeground(1, palette().color(QPalette::Text));
  }
  QString tip =
      tr("Mean of the last 5 good drill attempts; %1 drill attempts in all")
          .arg(count);
  for (int c = 1; c <= 3; ++c) {
    item->setToolTip(c, tip);
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
  if (int excluded = excludedCount()) {
    text += " " + skippedText(excluded);
  }
  summary_->setText(text);
}

DrillPage::Pool DrillPage::poolChoice() const {
  return static_cast<Pool>(std::max(0, poolBox_->currentIndex()));
}

std::vector<std::string> DrillPage::pool() {
  Pool choice = poolChoice();
  std::vector<std::string> out;
  for (const auto &id : tickedEligible()) {
    CaseStatus st = statusOf(progressOf(id));
    if (choice == Pool::Ticked ||
        (choice == Pool::Learned && st == CaseStatus::Learned) ||
        (choice == Pool::Learning && st == CaseStatus::Learning)) {
      out.push_back(id);
    }
  }
  return out;
}

void DrillPage::loadChoices() {
  QSettings q;
  int p = poolBox_->findData(
      q.value(setSettingsKey(setId_, "drillPool"), poolName(0)));
  poolBox_->setCurrentIndex(std::max(0, p));
  int o = orderBox_->findData(q.value(setSettingsKey(setId_, "drillOrder"),
                                      orderName(PickMode::Random)));
  orderBox_->setCurrentIndex(std::max(0, o));
  picker_.setMode(static_cast<PickMode>(orderBox_->currentIndex()));
}

// --- drilling
// ------------------------------------------------------------------

QString DrillPage::startCaseLabel() const { return tr("Drill this case now"); }

void DrillPage::startSet() {
  loadChoices();
  current_.reset();
  updateSummary();
  nextCase();
}

void DrillPage::setReloaded() {
  updateSummary();
  if (current_ && !eligible(current_->caseId)) {
    current_.reset();
    nextCase();
  } else if (current_ && phase_ == Phase::Revealed) {
    showReveal();
  } else if (!current_) {
    nextCase();
  }
}

void DrillPage::ticksChanged() {
  if (phase_ == Phase::NoCase) {
    nextCase();
  }
}

void DrillPage::neutralityChanged() {
  // A new scramble for the case, meant for a hold the new choice allows.
  if (current_ && phase_ == Phase::Waiting) {
    current_->hold = pickHold();
    retryCase();
  }
}

void DrillPage::startCase(const std::string &caseId) {
  if (eligible(caseId) && !isBusy()) {
    current_ = Current{caseId, {}, pickHold(), {}};
    retryCase();
  }
}

void DrillPage::setPhase(Phase p) {
  phase_ = p;
  QString status;
  switch (p) {
  case Phase::NoCase:
    return;
  case Phase::Waiting:
    status = tr("Scramble, then hold Space and release to start.");
    break;
  case Phase::Running:
    status = tr("Any key stops the timer; Esc cancels the solve.");
    break;
  case Phase::Revealed:
    status = tr("Space: next case. Backspace: wrong alg. Esc: try the case "
                "again.");
    break;
  }
  statusLabel_->setText(status);
}

void DrillPage::nextCase() {
  timer_.cancel();
  reveal_->clear();
  setDiagram({});
  resultRow_->setVisible(false);
  const AlgSet *s = set();
  std::vector<std::string> cases = s ? pool() : std::vector<std::string>{};
  picker_.setPool(cases);
  picker_.setTargetMs(s ? s->targetSeconds * 1000.0 : 0.0);
  std::optional<std::string> id = picker_.next(progress(), rng_);
  if (!id) {
    current_.reset();
    scrambleLabel_->clear();
    display_->showTime(0);
    QString text;
    if (!s) {
      text = tr("No alg set selected.");
    } else if (tickedEligible().empty()) {
      text = tr("Tick some cases that have algs to drill them.");
    } else if (poolChoice() == Pool::Learned) {
      text = tr("None of the ticked cases is learned yet. Learn some in "
                "Learn mode, or drill \"All ticked\".");
    } else {
      text = tr("No ticked case is in the learn queue. Choose another case "
                "pool.");
    }
    statusLabel_->setText(text);
    setPhase(Phase::NoCase);
    return;
  }
  current_ = Current{*id, {}, pickHold(), {}};
  retryCase();
}

void DrillPage::retryCase() {
  timer_.cancel();
  const AlgSet *s = set();
  const AlgCase *c = s && current_ ? s->findCase(current_->caseId) : nullptr;
  const CaseGroup *g = c ? s->groupOf(current_->caseId) : nullptr;
  const CaseInfo *info = current_ ? infoOf(current_->caseId) : nullptr;
  if (!c || !g || !info) {
    nextCase();
    return;
  }
  AlgCase shown = *c;
  shown.state = info->state; // as the case is drawn
  current_->state = drillState(*s, *g, shown, rng_);
  current_->scramble.clear();
  failed_ = false;
  reveal_->clear();
  setDiagram({});
  resultRow_->setVisible(false);
  display_->showTime(0);
  scrambleLabel_->setText(tr("Generating scramble…"));
  selectInTree(current_->caseId);
  requestScramble();
  setPhase(Phase::Waiting);
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
  if (e->isAutoRepeat()) {
    return e->key() == Qt::Key_Space;
  }
  std::int64_t now = monotonicMs();
  int key = e->key();

  switch (phase_) {
  case Phase::NoCase:
    if (press && (key == Qt::Key_Space || key == Qt::Key_Return ||
                  key == Qt::Key_Enter)) {
      nextCase();
      return true;
    }
    return false;
  case Phase::Revealed:
    if (!press) {
      return key == Qt::Key_Space;
    }
    if (key == Qt::Key_Space || key == Qt::Key_Return || key == Qt::Key_Enter) {
      nextCase();
      return true;
    }
    if (key == Qt::Key_Backspace) {
      setWrong(!failed_);
      return true;
    }
    if (key == Qt::Key_Escape) {
      retryCase();
      return true;
    }
    return false;
  case Phase::Running:
    if (press && key == Qt::Key_Escape) {
      // Nothing is recorded; the same case is set up to try again.
      timer_.cancel();
      display_->showTime(0);
      setPhase(Phase::Waiting);
      statusLabel_->setText(tr("Solve cancelled; nothing was recorded. Hold "
                               "Space to try the case again."));
      return true;
    }
    if (press) {
      timer_.pressStop(now);
      if (auto r = timer_.takeResult()) {
        finish(r->timeMs);
      }
      return true;
    }
    return false;
  case Phase::Waiting:
    break;
  }

  if (key == Qt::Key_Escape && press) {
    timer_.cancel();
    display_->showTime(0);
    return true;
  }
  if (key != Qt::Key_Space) {
    return false;
  }
  if (press) {
    if (!current_ || current_->scramble.isEmpty()) {
      return true; // wait for the scramble
    }
    timer_.pressStart(now);
  } else {
    timer_.releaseStart(now);
    if (timer_.state() == SolveTimer::State::Running) {
      setPhase(Phase::Running);
    }
  }
  display_->showTimer(timer_, now);
  return true;
}

void DrillPage::finish(std::int64_t timeMs) {
  if (!current_ || !set()) {
    return;
  }
  attemptMs_ = timeMs;
  failed_ = false;
  progress()[current_->caseId].record(
      {unixNow(), timeMs, false, AttemptMode::Drill});
  saveProgress();
  setPhase(Phase::Revealed);
  showReveal();
  showResult();
  updateRow(current_->caseId);
}

void DrillPage::setWrong(bool wrong) {
  if (phase_ != Phase::Revealed || !current_) {
    showResult();
    return;
  }
  failed_ = wrong;
  auto &attempts = progress()[current_->caseId].attempts;
  // The attempt just recorded is the last drill attempt.
  for (auto it = attempts.rbegin(); it != attempts.rend(); ++it) {
    if (it->mode == AttemptMode::Drill) {
      it->failed = failed_;
      break;
    }
  }
  saveProgress();
  showResult();
  updateRow(current_->caseId);
}

void DrillPage::showResult() {
  if (phase_ != Phase::Revealed || !current_) {
    resultRow_->setVisible(false);
    return;
  }
  resultRow_->setVisible(true);
  wrongButton_->setChecked(failed_);
  const AlgSet *s = set();
  double target = s ? s->targetSeconds : 0.0;
  Solve shown;
  shown.timeMs = attemptMs_;
  shown.penalty = failed_ ? Penalty::Dnf : Penalty::None;
  QColor colour;
  if (!failed_ && target > 0) {
    colour = static_cast<double>(attemptMs_) <= target * 1000.0 ? kGood : kSlow;
  }
  display_->showText(qs(formatSolve(shown, settings_.decimals)), colour);
  resultLabel_->setText(failed_ ? tr("Wrong alg")
                                : tr("Target %1 s").arg(target, 0, 'f', 2));
}

void DrillPage::showReveal() {
  if (!current_) {
    return;
  }
  reveal_->setText(algsHtml(current_->caseId, current_->state,
                            isNeutral() ? std::optional<Hold>(current_->hold)
                                        : std::nullopt));
  setDiagram(caseDiagram(current_->caseId, current_->state, 200,
                         colourMapOf(current_->hold)));
}

void DrillPage::setDiagram(const QPixmap &pixmap) {
  diagram_->setPixmap(pixmap);
  diagram_->setVisible(!pixmap.isNull());
  diagramBalance_->setVisible(!pixmap.isNull());
}

} // namespace vcube
