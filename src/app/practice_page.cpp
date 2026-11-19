#include "app/practice_page.hpp"

#include "app/session_panel.hpp"
#include "app/text_style.hpp"
#include "app/train_keys.hpp"
#include "app/ui_state.hpp"
#include "app/ui_text.hpp"
#include "io/session_store.hpp"
#include "train/move_count.hpp"
#include "train/stats.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace vcube {

QString PracticePage::tabId(Tab t) {
  switch (t) {
  case Tab::Learn:
    return "learn";
  case Tab::Inspection:
    return "inspection";
  case Tab::Untimed:
    return "untimed";
  case Tab::Timed:
    return "timed";
  }
  return "learn";
}

QString PracticePage::tabName(Tab t) {
  switch (t) {
  case Tab::Learn:
    return tr("Learn");
  case Tab::Inspection:
    return tr("Inspection");
  case Tab::Untimed:
    return tr("Untimed");
  case Tab::Timed:
    return tr("Timed");
  }
  return {};
}

int PracticePage::tabIndex(Tab t) const {
  const std::vector<Tab> all = tabs();
  auto it = std::find(all.begin(), all.end(), t);
  return it == all.end() ? 0 : static_cast<int>(it - all.begin());
}

PracticePage::PracticePage(AppSettings &settings, const QString &dataDir,
                           const QString &file, QWidget *parent)
    : ModePage(parent), settings_(settings),
      store_(QDir(dataDir).filePath(file)), rng_(makeRng()) {
  // What the mode kept before it had these tabs is dropped (TSLE's OCLL
  // algs are carried over).
  const QJsonObject old = removeOldPracticeFile(store_.path());
  QString error;
  if (!store_.load(&error)) {
    QString kept =
        store_.setAsidePath().isEmpty()
            ? tr("It is left where it is, and nothing will be saved over it.")
            : tr("It was kept as %1.")
                  .arg(QDir::toNativeSeparators(store_.setAsidePath()));
    QMessageBox::warning(
        this, "v-cube",
        tr("Could not read %1 (%2), so the mode starts afresh.")
                .arg(file, error) +
            " " + kept);
  }
  bool carried = false;
  for (const auto &[key, value] : oldOcllNotes(old)) {
    carried |= store_.data.notes.emplace(key, value).second;
  }
  // Saved now: nothing else may save before the app is closed, and the
  // old file is no longer where the next start would look for it.
  if (carried && !store_.save(&error)) {
    QMessageBox::warning(
        this, "v-cube",
        tr("Could not save the OCLL algs carried over to %1: %2")
            .arg(file, error));
  }
}

PracticePage::~PracticePage() = default;

// --- building ----------------------------------------------------------------

void PracticePage::build() {
  buildSide();

  auto *right = new QWidget(this);
  auto *rightLayout = new QVBoxLayout(right);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  options_ = new OptionBar(right);
  cubeView_ = new CubeView(right);
  cubeView_->setFillsArea(true);
  cubeView_->setMinimumSize(200, 160);

  addOptions(options_);
  cubeBox_ = new QComboBox(options_);
  cubeBox_->setFocusPolicy(Qt::NoFocus);
  cubeBox_->addItem(tr("Virtual"));
  cubeBox_->addItem(tr("Physical"));
  cubeItem_ = options_->addPicker(tr("Cube:"), cubeBox_);
  viewItem_ = options_->addViewPicker(cubeView_, modeId());
  options_->addStretch();
  addButtons(options_);
  rightLayout->addWidget(options_);

  prompt_ = new QLabel(right);
  prompt_->setAlignment(Qt::AlignCenter);
  prompt_->setWordWrap(true);
  prompt_->setTextFormat(Qt::RichText);
  setTextRole(prompt_, TextRole::Title);

  scrambleLabel_ = new QLabel(right);
  scrambleLabel_->setAlignment(Qt::AlignCenter);
  scrambleLabel_->setWordWrap(true);
  scrambleLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  markScrambleLabel(scrambleLabel_);

  cubeStack_ = new QStackedWidget(right);
  hiddenLabel_ = new QLabel(tr("The cube is hidden."), cubeStack_);
  hiddenLabel_->setAlignment(Qt::AlignCenter);
  hiddenLabel_->setEnabled(false);
  cubeStack_->addWidget(cubeView_);
  cubeStack_->addWidget(hiddenLabel_);

  display_ = new TimeDisplay(right);
  display_->setAlignment(Qt::AlignCenter);
  // A hint asked for is what is being looked for: under the cube, as large
  // as an alg being done.
  requestedHint_ = new QLabel(right);
  requestedHint_->setObjectName("practiceHint");
  requestedHint_->setAlignment(Qt::AlignCenter);
  requestedHint_->setWordWrap(true);
  requestedHint_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  setTextRole(requestedHint_, TextRole::Title, true);
  requestedHint_->hide();
  hint_ = new HintBar(right);
  verdict_ = new QLabel(right);
  verdict_->setObjectName("practiceVerdict");
  verdict_->setAlignment(Qt::AlignCenter);
  verdict_->setWordWrap(true);
  verdict_->setTextFormat(Qt::RichText);
  setTextRole(verdict_, TextRole::Heading);
  progressLabel_ = new QLabel(right);
  progressLabel_->setObjectName("practiceProgress");
  progressLabel_->setAlignment(Qt::AlignCenter);
  progressLabel_->setWordWrap(true);
  progressLabel_->setTextFormat(Qt::RichText);
  detail_ = new QLabel(right);
  detail_->setObjectName("practiceDetail");
  detail_->setTextFormat(Qt::RichText);
  detail_->setWordWrap(true);
  detail_->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
  detail_->setTextInteractionFlags(Qt::TextBrowserInteraction);
  detail_->setFocusPolicy(Qt::NoFocus);
  detail_->setOpenExternalLinks(false);
  detailScroll_ = new QScrollArea(right);
  detailScroll_->setFrameShape(QFrame::NoFrame);
  detailScroll_->setWidgetResizable(true);
  detailScroll_->setFocusPolicy(Qt::NoFocus);
  detailScroll_->setWidget(detail_);
  choiceScroll_ = new QScrollArea(right);
  choiceScroll_->setFrameShape(QFrame::NoFrame);
  choiceScroll_->setWidgetResizable(true);
  choiceScroll_->setFocusPolicy(Qt::NoFocus);
  if (QWidget *panel = choicePanel()) {
    choiceScroll_->setWidget(panel);
  }
  choiceScroll_->hide();

  // One block centred in the height: the prompt, the cube, and right
  // under it the time, the verdict, the progress, the keys and any
  // details.
  rightLayout->addStretch(1);
  rightLayout->addWidget(prompt_);
  rightLayout->addWidget(scrambleLabel_);
  rightLayout->addWidget(cubeStack_, 4);
  rightLayout->addWidget(requestedHint_);
  rightLayout->addWidget(display_);
  rightLayout->addWidget(verdict_);
  rightLayout->addWidget(progressLabel_);
  rightLayout->addWidget(hint_);
  // The details take room from the cube only while they are open.
  rightLayout->addWidget(detailScroll_, 4);
  rightLayout->addWidget(choiceScroll_, 4);
  rightLayout->addStretch(1);

  auto *split = new QSplitter(this);
  split->setObjectName("main");
  split->addWidget(list_);
  split->addWidget(right);
  split->setStretchFactor(0, 0);
  split->setStretchFactor(1, 1);
  split->setSizes({380, 800});

  tabs_ = new QTabBar(this);
  tabs_->setFocusPolicy(Qt::NoFocus);
  tabs_->setExpanding(false);
  for (Tab t : tabs()) {
    tabs_->addTab(tabName(t));
  }
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(tabs_);
  layout->addWidget(split, 1);

  connect(tabs_, &QTabBar::currentChanged, this, [this](int index) {
    const std::vector<Tab> all = tabs();
    if (index >= 0 && index < static_cast<int>(all.size()) &&
        all[static_cast<std::size_t>(index)] != tab_) {
      selectTab(all[static_cast<std::size_t>(index)]);
    }
  });
  connect(cubeBox_, &QComboBox::activated, this,
          [this](int index) { setPhysical(index == 1); });
  connect(detail_, &QLabel::linkActivated, this, [this](const QString &link) {
    if (link.startsWith("play:") && finished()) {
      play(parseAlgOrEmpty(
          QUrl::fromPercentEncoding(link.mid(5).toLatin1()).toStdString()));
    }
  });
  connect(&scrambles_, &QFutureWatcher<GeneratedScramble>::finished, this,
          &PracticePage::scrambleReady);
  tick_.setInterval(37);
  connect(&tick_, &QTimer::timeout, this, &PracticePage::onTick);

  built_ = true;
  loadChoices();
  applySettings();
  showBar();
  showAll();
}

void PracticePage::buildSide() {
  list_ = new PracticeList(store_.data, settings_, this);
  list_->setBusyCheck([this] { return isBusy(); });
  connect(list_, &PracticeList::activated, this,
          [this](const std::string &key, bool card) {
            if (card) {
              practiseCard(key);
            } else {
              groupActivated(key);
            }
          });
  connect(list_, &PracticeList::cleared, this, [this] {
    save();
    showSummary();
    if (active_ && tab_ == Tab::Learn) {
      next();
    }
  });
}

void PracticePage::loadChoices() {
  const QString mode = modeId();
  const std::vector<Tab> all = tabs();
  const QString saved = saved::practice(mode, "tab", 0).get().toString();
  tab_ = all.front();
  if (saved == "1") {
    tab_ = Tab::Timed; // as the two tabs of before had it
  }
  for (Tab t : all) {
    if (saved == tabId(t)) {
      tab_ = t;
    }
  }
  physical_ = saved::practice(mode, "physical", false).get().toBool();
  QSignalBlocker block(tabs_);
  tabs_->setCurrentIndex(tabIndex(tab_));
}

void PracticePage::saveChoices() const {
  const QString mode = modeId();
  saved::practice(mode, "tab").set(tabId(tab_));
  saved::practice(mode, "physical").set(physical_);
}

void PracticePage::showBar() {
  {
    QSignalBlocker b(cubeBox_), c(tabs_);
    cubeBox_->setCurrentIndex(physical_ ? 1 : 0);
    tabs_->setCurrentIndex(tabIndex(tab_));
  }
  cubeItem_->setVisible(tab_ == Tab::Timed);
  viewItem_->setVisible(tab_ == Tab::Learn || !physical_);
  showOptions();
  showCubeKind();
}

DrillFlow::Config PracticePage::flowConfig() const {
  const DrillSettings &d = settings_.drill;
  return {!physical_,          d.tapToArm, d.autoStop,
          d.shiftSpaceGivesUp, false,      d.holdMs};
}

// --- the page ----------------------------------------------------------------

void PracticePage::selectTab(Tab t) {
  if (tab_ == Tab::Timed && flow_.leave()) {
    flow_.cancelSolve();
  }
  tab_ = t;
  optionChanged();
}

void PracticePage::showCards() {
  if (tab_ != Tab::Learn && hasCards()) {
    tab_ = Tab::Learn;
    optionChanged();
  }
}

void PracticePage::setPhysical(bool on) {
  physical_ = on;
  optionChanged();
}

void PracticePage::optionChanged() {
  if (!built_) {
    return;
  }
  saveChoices();
  flow_.leaveCase();
  flow_.noCase();
  flow_.setConfig(flowConfig());
  item_ = {};
  progress_.clear();
  readyPending_ = true;
  // Only Timed and Inspection have a clock to keep up.
  if (tab_ != Tab::Timed && tab_ != Tab::Inspection) {
    tick_.stop();
  }
  showBar();
  fillList();
  if (active_) {
    next();
  } else {
    showAll();
  }
}

void PracticePage::applySettings() {
  if (!built_) {
    return;
  }
  flow_.setConfig(flowConfig());
  display_->setDecimals(settings_.decimals);
  CubeLook look = cubeView_->look();
  look.scheme = settings_.scheme;
  cubeView_->setLook(look);
  cubeView_->setAnimationMs(settings_.animationMs);
  fillList();
  if (active_ && item_.id != 0) {
    showAll();
  }
}

bool PracticePage::isBusy() const { return tab_ == Tab::Timed && flow_.busy(); }

void PracticePage::activated() {
  active_ = true;
  // A solve under way in Learn is taken up where it was left; anything
  // else starts with a new state.
  // An Inspection solve whose clock ran is put away: its state waits.
  const bool resume = (tab_ == Tab::Learn || tab_ == Tab::Untimed) &&
                      item_.id != 0 &&
                      (phase_ == Phase::Solving || phase_ == Phase::Demo ||
                       phase_ == Phase::Choose || phase_ == Phase::Done);
  if (!resume) {
    readyPending_ = true;
    next();
  } else {
    refreshList();
    showSummary();
  }
  if (tab_ == Tab::Timed || tab_ == Tab::Inspection) {
    tick_.start();
  }
}

void PracticePage::deactivated() {
  active_ = false;
  tick_.stop();
  cubeView_->finishAnimations();
  if (tab_ == Tab::Timed) {
    if (flow_.leave()) {
      flow_.cancelSolve();
    }
    flow_.noCase();
    item_ = {};
  }
  if (tab_ == Tab::Inspection) {
    item_ = {};
  }
}

void PracticePage::windowDeactivated() {
  if (tab_ == Tab::Timed && flow_.loseKeyboard()) {
    display_->showTimer(flow_.timer(), monotonicMs());
  }
}

void PracticePage::focusModeChanged() {
  if (built_) {
    options_->setVisible(!focusMode());
    tabs_->setVisible(!focusMode());
  }
}

bool PracticePage::finished() const {
  return item_.id != 0 &&
         (tab_ != Tab::Timed ? phase_ == Phase::Done && !waiting_
                             : flow_.phase() == DrillFlow::Phase::Revealed);
}

bool PracticePage::cubeHidden() const {
  return cubeStack_->isHidden() || cubeStack_->currentWidget() != cubeView_;
}

Alg PracticePage::bestSolution() const {
  return item_.outcome ? item_.outcome->best : Alg{};
}

Alg PracticePage::shownDemo() const {
  return tab_ != Tab::Timed && phase_ == Phase::Demo
             ? const_cast<PracticePage *>(this)->demoAlg()
             : Alg{};
}

QString PracticePage::promptHtml() const { return prompt_->text(); }
QString PracticePage::verdictHtml() const { return verdict_->text(); }
QString PracticePage::detailHtml() const {
  return detailOpen_ ? detail_->text() : QString();
}

QString PracticePage::clockText(std::int64_t ms) const {
  return vcube::clockText(ms, settings_.decimals);
}

QString PracticePage::mutedColour() const {
  return palette().color(QPalette::PlaceholderText).name();
}

QString PracticePage::playLink(const Alg &alg, const QString &text) {
  return QString("<a href='play:%1'>%2</a>")
      .arg(QString::fromLatin1(QUrl::toPercentEncoding(qs(toString(alg)))),
           text);
}

void PracticePage::save() {
  saveInBackground(store_, this, [this](const QString &error) {
    emit message(
        tr("Could not save your %1 progress: %2").arg(stepName(), error));
  });
}

// --- items -------------------------------------------------------------------

void PracticePage::next(LearnAhead ahead) {
  if (tab_ == Tab::Timed) {
    nextTimed();
  } else {
    nextLearn(ahead);
  }
}

void PracticePage::showIdle(const QString &text) {
  item_ = {};
  item_.id = ++items_;
  phase_ = Phase::Idle;
  idleText_ = text;
  cube_ = FaceletCube();
  cubeView_->setColourMap(colourMapOf(settings_.neutrality.main));
  cubeView_->clearMarks();
  cubeView_->setState(cube_);
  showAll();
}

void PracticePage::showReady() {
  item_ = {};
  item_.id = ++items_;
  phase_ = Phase::Ready;
  progress_.clear();
  cube_ = FaceletCube();
  cubeView_->setColourMap(colourMapOf(settings_.neutrality.main));
  cubeView_->clearMarks();
  cubeView_->setState(cube_);
  showHidden(false);
  display_->setText({});
  showAll();
}

void PracticePage::setProgress(const QStringList &lines) {
  progress_ = lines;
  showVerdict();
}

void PracticePage::begin(const Setup &setup, Extent extent) {
  item_ = {};
  item_.id = ++items_;
  item_.whole = extent == Extent::Whole;
  item_.group = setup.group;
  item_.start = setup.cube;
  item_.colours = setup.colours;
  item_.scrambleState = setup.state;
  item_.scrambleHold = setup.hold;
  item_.began = monotonicMs();
  waiting_ = false;
  demoShown_ = false;
  detailOpen_ = false;
  progress_.clear();
  note_.clear();
  if (tab_ == Tab::Inspection) {
    item_.inspecting = true;
    item_.inspectStart = item_.began;
  }
  cube_ = item_.start;
  cubeView_->finishAnimations();
  cubeView_->setColourMap(colourMapOf(item_.colours));
  cubeView_->clearMarks();
  cubeView_->setState(cube_);
  showHidden(false);
}

void PracticePage::nextLearn(LearnAhead ahead) {
  if (tab_ == Tab::Inspection && readyPending_) {
    showReady();
    return;
  }
  if (whole()) {
    std::optional<Setup> s = setUp(Extent::Whole, {});
    if (!s) {
      showIdle(tr("No state could be set up with these options"));
      return;
    }
    begin(*s, Extent::Whole);
    phase_ = Phase::Solving;
    cubeChanged();
    if (tab_ == Tab::Inspection && active_) {
      tick_.start();
    }
    showAll();
    return;
  }
  const std::vector<std::string> pool = cardPool();
  const std::int64_t now = unixNow();
  if (queueUnstarted(pool, store_.data.cards, now, settings_.srs)) {
    save();
  }
  LearnPickOptions options;
  options.last = lastCard_;
  options.rng = &rng_;
  options.dayEnd = localDayEnd();
  options.early = ahead == LearnAhead::Early;
  LearnPick pick = pickLearnCase(pool, store_.data.cards, now, localDayStart(),
                                 settings_.srs, options);
  if (!pick.caseId) {
    QString why;
    if (pick.nextDue) {
      why = tr("The next card is due %1.")
                .arg(dueText(*pick.nextDue, now, localDayEnd()));
    }
    if (pick.queued > 0) {
      why += " " +
             (pick.newLimitReached
                  ? tr("New cases wait for tomorrow (%1 a day, in Settings).")
                        .arg(settings_.srs.newPerDay)
                  : tr("New cases come in once fewer are in their first "
                       "steps."));
    }
    showIdle(tr("Nothing is due") +
             smallLine(why.trimmed() + " " +
                       tr("The Untimed tab is there to practise meanwhile.")));
    refreshList();
    return;
  }
  std::optional<Setup> s = setUp(Extent::Unit, *pick.caseId);
  if (!s) {
    showIdle(tr("%1 can't be set up with these options")
                 .arg(cardName(*pick.caseId)));
    return;
  }
  begin(*s, Extent::Unit);
  item_.card = *pick.caseId;
  item_.isNew = pick.isNew;
  item_.graded = !pick.isNew;
  lastCard_ = item_.card;
  phase_ = pick.isNew ? Phase::Demo : Phase::Solving;
  cubeChanged();
  if (needsChoice()) {
    enterChoice(phase_);
    return;
  }
  showAll();
}

void PracticePage::enterChoice(Phase after) {
  afterChoice_ = after;
  phase_ = Phase::Choose;
  detailOpen_ = false;
  startChoosing();
  showAll();
}

void PracticePage::chooseAgain() {
  if (tab_ == Tab::Learn && phase_ == Phase::Done && !waiting_ &&
      !item_.card.empty() && choicePanel()) {
    enterChoice(Phase::Done);
  }
}

void PracticePage::showChoosing() {
  if (phase_ == Phase::Choose) {
    showAll();
  }
}

void PracticePage::leaveChoice(Choice choice) {
  if (choice == Choice::Confirmed) {
    if (!confirmChoice()) {
      return;
    }
  } else {
    cancelChoice();
  }
  phase_ = afterChoice_;
  if (phase_ != Phase::Done) {
    resetCube();
  } else {
    cubeView_->finishAnimations();
    cubeView_->setState(cube_);
    analysisArrived(item_.id); // judged against the solutions as they are
    // The grade follows the verdict, unless you gave it yourself.
    if (item_.graded && item_.recorded && item_.grade && item_.outcome &&
        !item_.gradedByHand) {
      const Grade g = practiceGrade({.solvedWell = item_.outcome->good,
                                     .hinted = item_.hint,
                                     .gaveUp = item_.gaveUp});
      if (g != *item_.grade) {
        regrade(g);
      }
    }
  }
  showAll();
}

void PracticePage::practiseCard(const std::string &card) {
  if (tab_ != Tab::Learn) {
    tab_ = Tab::Learn;
    saveChoices();
    flow_.noCase();
    showBar();
    fillList();
  }
  std::optional<Setup> s = setUp(Extent::Unit, card);
  if (!s) {
    emit message(
        tr("%1 can't be set up with these options").arg(cardName(card)));
    return;
  }
  begin(*s, Extent::Unit);
  item_.card = card;
  phase_ = Phase::Solving;
  cubeChanged();
  if (needsChoice()) {
    enterChoice(Phase::Solving);
    return;
  }
  showAll();
}

void PracticePage::nextTimed() {
  flow_.setConfig(flowConfig());
  std::optional<Setup> s = setUp(Extent::Whole, {});
  if (!s) {
    flow_.noCase();
    showIdle(tr("No state could be set up with these options"));
    return;
  }
  begin(*s, Extent::Whole);
  flow_.setUp(monotonicMs());
  display_->showTime(0);
  cubeChanged();
  if (physical_) {
    requestScramble();
  }
  if (active_) {
    tick_.start();
  }
  showAll();
}

void PracticePage::sameAgain() {
  if (item_.id == 0) {
    return;
  }
  Item again;
  again.id = item_.id; // the mode's analysis of the state still holds
  again.whole = item_.whole;
  again.card = item_.card;
  again.group = item_.group;
  again.start = item_.start;
  again.colours = item_.colours;
  again.scrambleState = item_.scrambleState;
  again.scrambleHold = item_.scrambleHold;
  again.scramble = item_.scramble;
  again.retry = true;
  again.began = monotonicMs();
  if (tab_ == Tab::Inspection) {
    again.inspecting = true;
    again.inspectStart = again.began;
  }
  item_ = std::move(again);
  waiting_ = false;
  detailOpen_ = false;
  progress_.clear();
  note_.clear();
  cubeView_->finishAnimations();
  cube_ = item_.start;
  cubeView_->setState(cube_);
  showHidden(false);
  restarted();
  cubeChanged();
  if (tab_ == Tab::Timed) {
    flow_.setUp(monotonicMs());
    display_->showTime(0);
  } else {
    phase_ = Phase::Solving;
  }
  if (tab_ == Tab::Inspection && active_) {
    tick_.start();
  }
  showAll();
}

void PracticePage::resetCube() {
  demoShown_ = false;
  cubeView_->finishAnimations();
  cube_ = item_.start;
  cubeView_->setState(cube_);
  item_.moves.clear();
  item_.moveMs.clear();
  item_.done = false;
  progress_.clear();
  showHidden(false);
  restarted();
  cubeChanged();
}

void PracticePage::applyTurn(Move m) {
  cube_.apply(m);
  cubeView_->animateMove(m);
}

void PracticePage::showHidden(bool hidden) {
  item_.hidden = hidden;
  cubeStack_->setCurrentWidget(hidden ? static_cast<QWidget *>(hiddenLabel_)
                                      : cubeView_);
}

void PracticePage::showCubeKind() {
  // A physical attempt shows its scramble; the cube comes back with the
  // result, to play the best solution on.
  const bool scramble = tab_ == Tab::Timed && physical_;
  const bool revealed = flow_.phase() == DrillFlow::Phase::Revealed;
  scrambleLabel_->setVisible(scramble && item_.id != 0);
  cubeStack_->setVisible(!scramble || revealed);
  display_->setVisible(
      (tab_ == Tab::Timed && flow_.phase() != DrillFlow::Phase::NoCase) ||
      (tab_ == Tab::Inspection && phase_ == Phase::Solving));
}

void PracticePage::requestScramble() {
  // setFuture() drops the result of a request still on its way.
  ColourNeutrality hold;
  hold.main = item_.scrambleHold;
  hold.holds = {item_.scrambleHold};
  scrambles_.setFuture(
      generateScramble([state = item_.scrambleState](
                           Rng &) { return ScrambleSetup{state, {}, {}}; },
                       hold));
}

void PracticePage::scrambleReady() {
  if (tab_ != Tab::Timed || !physical_ || item_.id == 0) {
    return;
  }
  item_.scramble = scrambles_.result().text;
  showAll();
}

// --- keys --------------------------------------------------------------------

bool PracticePage::handleKey(QKeyEvent *e, bool press) {
  if (!built_ || (e->modifiers() &
                  (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
    return false;
  }
  if (tab_ == Tab::Timed) {
    return handleTimedKey(e, press);
  }
  return press && item_.id != 0 && handleLearnKey(e);
}

bool PracticePage::handleChooseKey(QKeyEvent *e) {
  switch (e->key()) {
  case Qt::Key_Return:
  case Qt::Key_Enter:
    leaveChoice(Choice::Confirmed);
    return true;
  case Qt::Key_Escape:
    if (afterChoice_ == Phase::Done) {
      leaveChoice(Choice::Cancelled);
    }
    return true;
  case Qt::Key_Space:
  case Qt::Key_Tab:
  case Qt::Key_Backspace:
    return true;
  default:
    break;
  }
  if (e->key() >= Qt::Key_1 && e->key() <= Qt::Key_9) {
    choiceKey(e->key() - Qt::Key_0);
    return true;
  }
  if (settings_.keymap.lookup(e)) {
    emit message(tr("Choose first: Enter when your solutions are ticked"));
    return true;
  }
  return false;
}

bool PracticePage::handleLearnKey(QKeyEvent *e) {
  const bool shift = e->modifiers().testFlag(Qt::ShiftModifier);
  if (phase_ == Phase::Idle) {
    if (e->key() == Qt::Key_Space || e->key() == Qt::Key_Return ||
        e->key() == Qt::Key_Enter) {
      next(LearnAhead::Early);
      return true;
    }
    return false;
  }
  if (phase_ == Phase::Ready) {
    if (e->key() == Qt::Key_Space) {
      readyPending_ = false;
      next();
      return true;
    }
    return false;
  }
  if (phase_ == Phase::Choose) {
    return handleChooseKey(e);
  }
  switch (e->key()) {
  case Qt::Key_Space:
    if (phase_ == Phase::Done) {
      if (waiting_) {
        emit message(tr("Still working out the best solutions"));
      } else {
        next();
      }
    } else if (phase_ == Phase::Demo) {
      introduced();
    } else if (item_.hidden) {
      showHidden(false);
      finishLearn(item_.done ? Try::Finished : Try::GaveUp);
    } else if (shift) {
      finishLearn(Try::GaveUp);
    }
    return true;
  case Qt::Key_Tab:
    if (phase_ == Phase::Solving && hasHints() && !item_.hidden) {
      const QString text = hint();
      if (text.isEmpty()) {
        emit message(tr("Still working out the best solutions"));
      } else {
        item_.hint = true;
        requestedHint_->setText(text);
        showKeys();
      }
    } else if (phase_ == Phase::Done) {
      toggleDetail();
    }
    return true;
  case Qt::Key_Backspace:
    if (phase_ == Phase::Solving) {
      showHidden(false);
      finishLearn(Try::GaveUp);
    }
    return true;
  case Qt::Key_Escape:
    if (phase_ == Phase::Done) {
      if (!waiting_) {
        sameAgain();
      }
    } else if (tab_ == Tab::Inspection) {
      sameAgain(); // the clock again too, and not recorded
    } else {
      resetCube();
      showAll();
    }
    return true;
  case Qt::Key_Return:
  case Qt::Key_Enter:
    if (phase_ == Phase::Demo) {
      playDemo();
    } else if (phase_ == Phase::Done) {
      playBest();
    }
    return true;
  default:
    break;
  }
  if (phase_ == Phase::Done && item_.graded && item_.grade && !waiting_ &&
      e->key() >= Qt::Key_1 && e->key() <= Qt::Key_4) {
    regrade(static_cast<Grade>(e->key() - Qt::Key_1 + 1));
    item_.gradedByHand = true;
    return true;
  }
  if (phase_ == Phase::Done && !waiting_ && doneKey(e)) {
    return true;
  }
  if (phase_ == Phase::Done && e->key() == Qt::Key_0 && choicePanel() &&
      !item_.card.empty()) {
    chooseAgain();
    return true;
  }
  if (auto m = settings_.keymap.lookup(e)) {
    learnTurn(*m);
    return true;
  }
  return false;
}

// --- Learn -------------------------------------------------------------------

void PracticePage::learnTurn(Move m) {
  endDemo();
  if (phase_ == Phase::Done) {
    applyTurn(m); // the cube turns freely now
    cubeChanged();
    return;
  }
  if (item_.inspecting && phase_ == Phase::Solving && !isRotation(m.type)) {
    // The plan is made: the clock stops at the first face turn.
    item_.inspecting = false;
    item_.inspectMs =
        std::max<std::int64_t>(1, monotonicMs() - item_.inspectStart);
    showInspection();
  }
  if (blind() && phase_ == Phase::Solving && !isRotation(m.type) &&
      !item_.hidden) {
    cubeView_->finishAnimations();
    showHidden(true);
    showKeys();
  }
  applyTurn(m);
  item_.moves.push_back(m);
  item_.moveMs.push_back(monotonicMs() - item_.began);
  item_.done = turned();
  cubeChanged();
  if (!item_.done) {
    return;
  }
  if (phase_ == Phase::Demo) {
    introduced();
  } else if (!item_.hidden) {
    finishLearn(Try::Finished);
  }
}

void PracticePage::finishLearn(Try how) {
  const bool gaveUp = how == Try::GaveUp;
  if (phase_ != Phase::Solving) {
    return;
  }
  if (item_.inspecting) {
    // Given up while looking: the time looked is what it took.
    item_.inspecting = false;
    item_.inspectMs =
        std::max<std::int64_t>(1, monotonicMs() - item_.inspectStart);
    showInspection();
  }
  item_.gaveUp = gaveUp;
  phase_ = Phase::Done;
  if (waitsForAnalysis()) {
    waiting_ = true;
    showAll();
    return;
  }
  conclude();
}

void PracticePage::conclude() {
  waiting_ = false;
  item_.outcome = judge();
  if (!item_.retry && !item_.recorded) {
    const std::int64_t now = unixNow();
    if (item_.graded) {
      CaseProgress &p = store_.data.cards[item_.card];
      item_.cardBefore = p.card;
      const Grade g = practiceGrade({.solvedWell = item_.outcome->good,
                                     .hinted = item_.hint,
                                     .gaveUp = item_.gaveUp});
      review(p.card, g, now, settings_.srs, &rng_);
      p.record({now, 0, g == Grade::Again, AttemptMode::Learn});
      item_.grade = g;
      item_.recorded = true;
    } else if (item_.card.empty()) {
      PracticeTry t;
      t.timestamp = now;
      t.moves = item_.outcome->moves;
      t.best = item_.outcome->bestMoves;
      t.extra = item_.outcome->extra;
      t.dnf = item_.gaveUp;
      t.hint = item_.hint;
      if (tab_ == Tab::Inspection) {
        t.inspectMs = item_.inspectMs;
        const std::int64_t limit = inspectionLimitMs();
        t.over = limit > 0 && item_.inspectMs > limit;
        store_.data.addInspection(item_.group, t);
      } else {
        store_.data.addLearn(item_.group, t);
      }
      item_.recorded = true;
    }
    if (item_.recorded) {
      save();
      refreshList();
    }
  }
  showAll();
}

void PracticePage::introduced() {
  endDemo();
  CaseProgress &p = store_.data.cards[item_.card];
  const std::int64_t now = unixNow();
  introduce(p.card, now, settings_.srs);
  save();
  refreshList();
  phase_ = Phase::Done;
  item_.outcome = Outcome{};
  item_.outcome->verdict = tr("Shown. It comes back %1, to do from memory.")
                               .arg(dueText(p.card.due, now, localDayEnd()));
  item_.outcome->best = demoAlg();
  showAll();
}

void PracticePage::regrade(Grade g) {
  CaseProgress &p = store_.data.cards[item_.card];
  p.card = item_.cardBefore;
  review(p.card, g, unixNow(), settings_.srs, &rng_);
  if (!p.attempts.empty()) {
    p.attempts.back().failed = g == Grade::Again;
  }
  item_.grade = g;
  save();
  refreshList();
  showVerdict();
  showSummary();
}

void PracticePage::playDemo() {
  const Alg alg = demoAlg();
  if (alg.empty()) {
    emit message(tr("Still working out the best solutions"));
    return;
  }
  // Only the picture turns; the cube is back at the start to do it.
  resetCube();
  cubeView_->animateSequence(alg);
  demoShown_ = true;
  // Only the latest play ends the demo: one played again part-way must
  // not be cut short by the first play's timer.
  const int id = item_.id;
  const int play = ++demoPlays_;
  QTimer::singleShot(
      static_cast<int>(alg.size()) * settings_.animationMs * 2 + 900, this,
      [this, id, play] {
        if (id == item_.id && play == demoPlays_ && phase_ == Phase::Demo) {
          endDemo();
        }
      });
}

void PracticePage::endDemo() {
  if (demoShown_) {
    demoShown_ = false;
    cubeView_->finishAnimations();
    cubeView_->setState(cube_);
    cubeChanged();
  }
}

// --- Timed -------------------------------------------------------------------

std::string PracticePage::timedGroupKey() const {
  return timedKey(cubeKind(), item_.group);
}

DrillFlow::KeyEvent PracticePage::flowKey(const QKeyEvent *e,
                                          bool press) const {
  DrillFlow::KeyEvent k = trainKeyEvent(e, press, KeyChords::None);
  if (!physical_) {
    k.move = settings_.keymap.lookup(e);
  }
  return k;
}

bool PracticePage::handleTimedKey(QKeyEvent *e, bool press) {
  if (item_.id == 0) {
    return false;
  }
  const std::int64_t now = monotonicMs();
  if (flow_.phase() == DrillFlow::Phase::NoCase) {
    if (press && e->key() == Qt::Key_Space) {
      next();
      return true;
    }
    return false;
  }
  if (press && flow_.phase() == DrillFlow::Phase::Revealed &&
      (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)) {
    playBest();
    return true;
  }
  const bool scrambleShown = !physical_ || !item_.scramble.isEmpty();
  DrillFlow::Step step =
      flow_.key(flowKey(e, press), now,
                scrambleShown ? Scramble::Shown : Scramble::Awaited);
  perform(step, now);
  return step.handled;
}

void PracticePage::perform(const DrillFlow::Step &step, std::int64_t now) {
  using A = DrillFlow::Action;
  switch (step.action) {
  case A::None:
  case A::PracticeReset:
  case A::PracticeTurn:
    break;
  case A::NextFromIdle:
  case A::NextCase:
    next();
    break;
  case A::Retry:
    sameAgain();
    break;
  case A::ResetWaiting:
    display_->showTime(0);
    if (!physical_) {
      resetCube();
    }
    showKeys();
    break;
  case A::Rotate:
    // Looking at the cube before the start: part of the solve's moves, so
    // that the hold it is solved in is known.
    applyTurn(*step.move);
    item_.moves.push_back(*step.move);
    item_.moveMs.push_back(0);
    cubeChanged();
    break;
  case A::Start:
    item_.began = now;
    showKeys();
    if (step.move) {
      runningTurn(*step.move, now);
    } else {
      display_->showTimer(flow_.timer(), now);
    }
    break;
  case A::Armed:
    display_->showTimer(flow_.timer(), now);
    showKeys();
    break;
  case A::TimerShown:
    display_->showTimer(flow_.timer(), now);
    break;
  case A::Turn:
    runningTurn(*step.move, now);
    break;
  case A::Stopped:
    // On the virtual cube a DNF unless the cube shows the step done.
    finishTimed(step.timeMs, !physical_ && !item_.done
                                 ? DrillFlow::Result::Failed
                                 : DrillFlow::Result::Solved);
    break;
  case A::CancelNoTurn:
    cancelTimed(tr("Stopped before any face turn: cancelled, nothing was "
                   "recorded."));
    break;
  case A::Cancel:
    cancelTimed(tr("Solve cancelled; nothing was recorded."));
    break;
  case A::GiveUp:
    finishTimed(flow_.giveUp(now), DrillFlow::Result::GaveUp);
    break;
  case A::Practise:
    toggleDetail();
    break;
  case A::ToggleWrong:
    toggleDnf();
    break;
  case A::Delete:
    deleteTry();
    break;
  case A::Hint: {
    QString text;
    switch (step.hint) {
    case DrillFlow::Hint::GiveUpKey:
      text = tr("Shift+Space gives the solve up.");
      break;
    case DrillFlow::Hint::KeepHolding:
      text = tr("Keep holding Space until the time turns green, then let go "
                "or start turning.");
      break;
    case DrillFlow::Hint::OnlyRotations:
      text = settings_.drill.tapToArm
                 ? tr("Only rotations until the timer starts: tap Space first.")
                 : tr("Only rotations until the timer starts (hold Space).");
      break;
    case DrillFlow::Hint::None:
      break;
    }
    hint_->setHint(text, hint_->keys());
    break;
  }
  }
}

void PracticePage::runningTurn(Move m, std::int64_t now) {
  const bool face = !isRotation(m.type);
  applyTurn(m);
  item_.moves.push_back(m);
  item_.moveMs.push_back(now - item_.began);
  item_.done = turned();
  cubeChanged();
  std::optional<std::int64_t> stopped = flow_.turned(
      face ? DrillFlow::Turn::OffUdFace : DrillFlow::Turn::Rotation, now,
      [this] { return item_.done; });
  if (stopped) {
    finishTimed(*stopped, DrillFlow::Result::Solved);
  }
}

void PracticePage::cancelTimed(const QString &why) {
  flow_.cancelSolve();
  display_->showTime(0);
  if (!physical_) {
    resetCube();
  }
  showAll();
  hint_->setHint(why, hint_->keys());
}

void PracticePage::finishTimed(std::int64_t ms, DrillFlow::Result result) {
  const bool failed = result != DrillFlow::Result::Solved;
  cubeView_->finishAnimations();
  item_.ms = ms;
  item_.gaveUp = failed;
  const std::string key = timedGroupKey();
  std::optional<std::int64_t> bestBefore;
  item_.meanBefore.reset();
  if (auto it = store_.data.timed.find(key); it != store_.data.timed.end()) {
    const PracticeSummary before = summarize(it->second);
    bestBefore = before.bestMs;
    item_.meanBefore = before.meanMs;
  }
  flow_.finished(ms, result, bestBefore, monotonicMs());
  item_.outcome = judge();
  if (!item_.retry) {
    PracticeTry t;
    t.timestamp = unixNow();
    t.ms = ms;
    t.moves = item_.outcome->moves;
    t.best = item_.outcome->bestMoves;
    t.extra = item_.outcome->extra;
    t.dnf = failed;
    store_.data.addTimed(key, t);
    item_.recorded = true;
    save();
    refreshList();
  } else {
    flow_.forgotten();
  }
  display_->showTime(ms, failed ? Penalty::Dnf : Penalty::None);
  showAll();
}

void PracticePage::toggleDnf() {
  if (!item_.recorded || !flow_.setWrong(!flow_.failed())) {
    return;
  }
  auto tries = store_.data.timed.find(timedGroupKey());
  if (tries != store_.data.timed.end() && !tries->second.empty()) {
    tries->second.back().dnf = flow_.failed();
  }
  item_.gaveUp = flow_.failed();
  display_->showTime(item_.ms, flow_.failed() ? Penalty::Dnf : Penalty::None);
  save();
  refreshList();
  showAll();
}

void PracticePage::deleteTry() {
  if (item_.recorded) {
    auto tries = store_.data.timed.find(timedGroupKey());
    if (tries != store_.data.timed.end() && !tries->second.empty()) {
      tries->second.pop_back();
      if (tries->second.empty()) {
        store_.data.timed.erase(tries);
      }
    }
    item_.recorded = false;
    save();
    refreshList();
  }
  if (flow_.deleted()) {
    // Given up before the start: taken back, to try.
    item_.outcome.reset();
    item_.gaveUp = false;
    display_->showTime(0);
    if (!physical_) {
      resetCube();
    }
  } else {
    note_ = tr("Deleted; nothing is recorded.");
  }
  showAll();
}

void PracticePage::showInspection() {
  if (tab_ != Tab::Inspection) {
    return;
  }
  const std::int64_t limit = inspectionLimitMs();
  const std::int64_t used =
      item_.inspecting ? monotonicMs() - item_.inspectStart : item_.inspectMs;
  display_->setText(inspectionClock(used, limit, item_.inspecting,
                                    palette().color(QPalette::WindowText)));
}

void PracticePage::onTick() {
  if (tab_ == Tab::Inspection) {
    if (item_.inspecting && phase_ == Phase::Solving) {
      showInspection();
    }
    return;
  }
  if (tab_ != Tab::Timed) {
    return;
  }
  const std::int64_t now = monotonicMs();
  // A popup or dialog took the keyboard: Space's release won't come.
  if (flow_.holding() && (QApplication::activePopupWidget() ||
                          QApplication::activeModalWidget())) {
    flow_.loseKeyboard();
    display_->showTimer(flow_.timer(), now);
    return;
  }
  flow_.tick(now);
  if (flow_.busy()) {
    display_->showTimer(flow_.timer(), now);
  }
}

// --- analysis ----------------------------------------------------------------

void PracticePage::analysisArrived(int id) {
  if (id != item_.id || item_.id == 0) {
    return;
  }
  if (waiting_) {
    conclude();
    return;
  }
  if (!finished() || !item_.outcome || (item_.moves.empty() && !item_.gaveUp)) {
    // e.g. a new case's solution to show; one only shown has no try to
    // judge.
    showAll();
    return;
  }
  // The verdict that is up is given again, and what was kept of the solve
  // completed.
  const Outcome before = *item_.outcome;
  item_.outcome = judge();
  if (item_.recorded && item_.card.empty()) {
    PracticeTries &all = tabTries(store_.data, tab_);
    auto tries = all.find(tab_ == Tab::Timed ? timedGroupKey() : item_.group);
    if (tries != all.end() && !tries->second.empty() &&
        (before.extra != item_.outcome->extra ||
         before.bestMoves != item_.outcome->bestMoves)) {
      tries->second.back().best = item_.outcome->bestMoves;
      tries->second.back().extra = item_.outcome->extra;
      save();
      refreshList();
    }
  }
  showAll();
}

// --- what is shown -----------------------------------------------------------

void PracticePage::play(const Alg &alg) {
  if (alg.empty() || item_.id == 0) {
    return;
  }
  cubeView_->finishAnimations();
  cube_ = item_.start;
  cubeView_->setState(cube_);
  cubeView_->animateSequence(alg);
  cube_.apply(alg);
  cubeChanged();
}

void PracticePage::playBest() {
  if (finished() && item_.outcome) {
    play(item_.outcome->best);
  }
}

void PracticePage::toggleDetail() {
  if (!finished()) {
    return;
  }
  detailOpen_ = !detailOpen_;
  showAll();
}

void PracticePage::showAll() {
  if (!built_) {
    return;
  }
  // Tab's hint stays while the case is being solved.
  if (!item_.hint || phase_ != Phase::Solving) {
    requestedHint_->clear();
  }
  showCubeKind();
  showPrompt();
  showKeys();
  showVerdict();
  showSummary();
}

void PracticePage::showPrompt() {
  QString text;
  if (item_.id == 0) {
    text.clear();
  } else if (tab_ != Tab::Timed && phase_ == Phase::Idle) {
    text = idleText_;
  } else if (phase_ == Phase::Ready && tab_ == Tab::Inspection) {
    text = inspectionReadyPrompt(inspectionLimitMs());
  } else if (tab_ == Tab::Learn && phase_ == Phase::Choose) {
    text = (afterChoice_ == Phase::Done
                ? tr("Your solutions for %1").arg(cardName(item_.card))
                : tr("New case: %1").arg(cardName(item_.card))) +
           smallLine(choosingText());
  } else if (tab_ == Tab::Learn && phase_ == Phase::Demo) {
    text = tr("New case: %1").arg(cardName(item_.card)) + smallLine(demoText());
  } else {
    QStringList notes;
    if (!item_.card.empty()) {
      notes << cardName(item_.card);
    }
    if (!taskNote().isEmpty()) {
      notes << taskNote();
    }
    if (item_.retry) {
      notes << tr("again, not recorded");
    } else if (tab_ == Tab::Learn && !item_.card.empty() && !item_.graded &&
               !item_.isNew) {
      notes << tr("practice, not graded");
    }
    // The case isn't named while it is being recognised.
    if (tab_ == Tab::Learn && phase_ == Phase::Solving && !item_.card.empty()) {
      notes.removeFirst();
    }
    text = task() + smallLine(notes.join(" · "));
  }
  prompt_->setText(text);
  scrambleLabel_->setText(item_.scramble.isEmpty()
                              ? tr("Working out the scramble…")
                              : item_.scramble);
}

void PracticePage::showKeys() {
  requestedHint_->setVisible(item_.hint && phase_ == Phase::Solving &&
                             !requestedHint_->text().isEmpty());
  QList<KeyHint> keys;
  QString message;
  const bool best = item_.outcome && !item_.outcome->best.empty();
  const KeyHint detailKey{"Tab",
                          detailOpen_ ? tr("hide the details") : tr("details")};
  if (item_.id == 0) {
    hint_->setHint({}, {});
    return;
  }
  if (tab_ != Tab::Timed) {
    switch (phase_) {
    case Phase::Idle:
      keys = {{"Space", tr("look again")}};
      break;
    case Phase::Ready:
      keys = {{"Space", tr("start")}};
      break;
    case Phase::Choose:
      keys = {{"1-9", tr("tick or untick")},
              {"Enter", afterChoice_ == Phase::Done ? tr("keep these")
                                                    : tr("done choosing")}};
      if (afterChoice_ == Phase::Done) {
        keys << KeyHint{"Esc", tr("leave them as they were")};
      }
      break;
    case Phase::Demo:
      keys = {{"Enter", tr("show it")},
              {"Space", tr("skip: I know this case")},
              {"Esc", tr("start again")}};
      break;
    case Phase::Solving:
      if (item_.hidden) {
        message = tr("The cube is hidden: do what you planned.");
        keys = {{"Space", tr("show the cube")},
                {"Backspace", tr("give up")},
                {"Esc", tr("start again")}};
        break;
      }
      if (item_.inspecting) {
        message = tr("Plan: the cube hides at your first face turn.");
      }
      if (hasHints() && !blind()) {
        keys << KeyHint{"Tab", tr("hint")};
      }
      keys << KeyHint{"Backspace", tr("give up")}
           << KeyHint{"Esc", tr("start again")};
      break;
    case Phase::Done:
      if (waiting_) {
        message = tr("Working out the best solutions…");
        break;
      }
      keys << KeyHint{"Space", tr("next")};
      if (best) {
        keys << KeyHint{"Enter", tr("play the best")};
      }
      keys << detailKey;
      if (item_.graded && item_.grade) {
        keys << KeyHint{"1-4", tr("change the grade")};
      }
      keys << doneKeys();
      if (choicePanel() && !item_.card.empty()) {
        keys << KeyHint{"0", tr("change your solutions")};
      }
      keys << KeyHint{"Esc", tr("retry, not recorded")};
      break;
    }
    hint_->setHint(message, keys);
    return;
  }
  const KeyHint giveUp{settings_.drill.shiftSpaceGivesUp ? tr("Shift+Space")
                                                         : tr("Backspace"),
                       tr("give up")};
  switch (flow_.phase()) {
  case DrillFlow::Phase::NoCase:
    keys = {{"Space", tr("look again")}};
    break;
  case DrillFlow::Phase::Waiting:
    if (physical_) {
      message = item_.scramble.isEmpty()
                    ? QString()
                    : tr("Scramble, then start the timer.");
      keys = {{tr("hold Space"), tr("start on letting go")}, giveUp};
    } else if (flow_.armed()) {
      message = tr("Ready: the first face turn starts the timer.");
      keys = {{"Esc", tr("not yet")}, giveUp};
    } else if (settings_.drill.tapToArm) {
      message = tr("Turn the cube as a whole to look at it.");
      keys = {{"Space", tr("ready: the first face turn starts the timer")},
              giveUp};
    } else {
      message = tr("Turn the cube as a whole to look at it.");
      keys = {{tr("hold Space"), tr("start on letting go or turning")}, giveUp};
    }
    break;
  case DrillFlow::Phase::Running:
    if (physical_) {
      keys = {{tr("any key"), tr("stop")}, giveUp, {"Esc", tr("cancel")}};
    } else if (settings_.drill.autoStop) {
      message = tr("The timer stops when the step is done.");
      keys = {{"Space", tr("stop now (a DNF unless done)")},
              giveUp,
              {"Esc", tr("cancel")}};
    } else {
      keys = {{"Space", tr("stop")}, giveUp, {"Esc", tr("cancel")}};
    }
    break;
  case DrillFlow::Phase::Revealed:
    keys << KeyHint{"Space", tr("next")};
    if (best) {
      keys << KeyHint{"Enter", tr("play the best")};
    }
    keys << detailKey;
    if (item_.recorded && !flow_.gaveUp()) {
      keys << KeyHint{"Backspace",
                      flow_.failed() ? tr("not a DNF") : tr("DNF")};
    }
    if (item_.recorded) {
      keys << KeyHint{"Delete", flow_.gaveUp() && flow_.gaveUpBeforeStart()
                                    ? tr("take it back")
                                    : tr("delete it")};
    }
    keys << KeyHint{"Esc", tr("retry, not recorded")};
    break;
  case DrillFlow::Phase::Aided:
  case DrillFlow::Phase::Unaided:
    break;
  }
  hint_->setHint(message, keys);
}

QString PracticePage::gradeLine() const {
  if (!item_.graded || !item_.grade) {
    return {};
  }
  auto it = store_.data.cards.find(item_.card);
  if (it == store_.data.cards.end()) {
    return {};
  }
  return vcube::gradeLine(*item_.grade, it->second.card.due, unixNow(),
                          localDayEnd());
}

QString PracticePage::timedLine() const {
  return vcube::timedLine({.moves = item_.outcome ? item_.outcome->moves : 0,
                           .ms = item_.ms,
                           .dnf = item_.gaveUp,
                           .meanBefore = item_.meanBefore},
                          settings_.decimals);
}

QString PracticePage::inspectionLine() const {
  return tab_ == Tab::Inspection
             ? vcube::inspectionLine(item_.inspectMs, inspectionLimitMs())
             : QString();
}

void PracticePage::showVerdict() {
  PracticeVerdict v;
  v.waiting = waiting_;
  v.note = note_;
  if (!waiting_ && finished() && item_.outcome) {
    const Outcome &o = *item_.outcome;
    if (tab_ == Tab::Timed) {
      // How efficient it was is in the details.
      v.result = timedLine();
    } else {
      v.result = o.verdict;
      const QString looked = inspectionLine();
      if (!looked.isEmpty()) {
        v.result += (v.result.isEmpty() ? QString() : " · ") + looked;
      }
      v.best = bestLine(o);
    }
    v.grade = gradeLine();
  }
  const QString verdict = vcube::verdictHtml(v, mutedColour());
  verdict_->setText(verdict);
  verdict_->setVisible(!verdict.isEmpty());
  // The units done so far, while the solve is under way.
  const bool solving = tab_ == Tab::Untimed && phase_ == Phase::Solving;
  progressLabel_->setText(solving ? progress_.join("<br>") : QString());
  progressLabel_->setVisible(solving && !progress_.isEmpty());
  const bool choosing = tab_ == Tab::Learn && phase_ == Phase::Choose;
  choiceScroll_->setVisible(choosing && choiceScroll_->widget());
  const bool open = detailOpen_ && finished() && !choosing;
  detailScroll_->setVisible(open);
  if (open) {
    QString html;
    if (tab_ == Tab::Timed) {
      html += splitsHtml(splits(), item_.moves, item_.moveMs,
                         settings_.decimals, mutedColour());
      if (item_.outcome) {
        html += efficiencyHtml(*item_.outcome);
      }
    }
    html += detail();
    detail_->setText(html);
  } else {
    detail_->clear();
  }
}

// --- the list ----------------------------------------------------------------

void PracticePage::fillList() {
  if (!built_) {
    return;
  }
  std::vector<ListRow> rows;
  switch (tab_) {
  case Tab::Learn:
    rows = cardRows();
    break;
  case Tab::Inspection:
  case Tab::Untimed:
    rows = untimedGroups();
    break;
  case Tab::Timed:
    rows = timedGroups();
    break;
  }
  list_->fill({.tab = tab_,
               .cube = cubeKind(),
               .cards = tab_ == Tab::Learn && hasCards(),
               .step = stepName(),
               .help = listHelp()},
              rows);
  showSummary();
}

void PracticePage::refreshList() {
  if (built_) {
    list_->refresh();
  }
}

void PracticePage::showSummary() {
  if (!built_) {
    return;
  }
  std::vector<std::string> pool;
  std::string group;
  switch (tab_) {
  case Tab::Learn:
    pool = cardPool();
    break;
  case Tab::Inspection:
  case Tab::Untimed:
    group = item_.group;
    break;
  case Tab::Timed:
    if (item_.id != 0) {
      group = timedGroupKey();
    }
    break;
  }
  list_->showSummary(pool, group);
}

} // namespace vcube
