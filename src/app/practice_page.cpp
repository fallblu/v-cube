#include "app/practice_page.hpp"

#include "app/session_panel.hpp"
#include "app/text_style.hpp"
#include "app/train_keys.hpp"
#include "app/ui_state.hpp"
#include "app/ui_text.hpp"
#include "io/session_store.hpp"
#include "train/f2l.hpp"
#include "train/move_count.hpp"
#include "train/stats.hpp"

#include <QApplication>
#include <QComboBox>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QTextDocumentFragment>
#include <QToolButton>
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

PracticePage::PracticePage(AppSettings &settings, PracticeStore &store,
                           QWidget *parent)
    : ModePage(parent), settings_(settings), store_(store), rng_(makeRng()) {}

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

  // The lines of the block under the options: centred, and wrapped.
  auto line = [right](const char *name) {
    auto *label = new QLabel(right);
    label->setObjectName(name);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    return label;
  };
  prompt_ = line("");
  prompt_->setTextFormat(Qt::RichText);
  setTextRole(prompt_, TextRole::Title);

  scrambleLabel_ = line("");
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
  driver_.setDisplay(display_);
  // A hint asked for is what is being looked for: under the cube, as large
  // as an alg being done.
  requestedHint_ = line("practiceHint");
  requestedHint_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  setTextRole(requestedHint_, TextRole::Title, true);
  requestedHint_->hide();
  hint_ = new HintBar(right);
  verdict_ = line("practiceVerdict");
  verdict_->setTextFormat(Qt::RichText);
  setTextRole(verdict_, TextRole::Heading);
  progressLabel_ = line("practiceProgress");
  progressLabel_->setTextFormat(Qt::RichText);
  detail_ = line("practiceDetail");
  detail_->setTextFormat(Qt::RichText);
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

  QSplitter *split = mainSplitter(this, {list_, right}, {380, 800});
  split->setStretchFactor(0, 0);

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
  connect(&tick_, &QTimer::timeout, this, &PracticePage::onTick);

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
    undo_ = {}; // the grades to take back are gone
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
  saveChoices();
  flow_.leaveCase();
  flow_.noCase();
  // Space never practises after a DNF here: the practice modes have no
  // practice stage (Esc tries the same state again instead).
  driver_.configure(!physical_, false);
  item_ = {};
  progress_.clear();
  readyPending_ = true;
  showBar();
  fillList();
  if (active_) {
    next();
  } else {
    showAll();
  }
}

void PracticePage::applySettings() {
  driver_.configure(!physical_, false);
  display_->setDecimals(settings_.decimals);
  cubeView_->applySettings(settings_);
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
  updateTick();
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
    updateTick();
  }
}

void PracticePage::focusModeChanged() {
  options_->setVisible(!focusMode());
  tabs_->setVisible(!focusMode());
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

QString PracticePage::mutedColour() const {
  return palette().color(QPalette::PlaceholderText).name();
}

QString PracticePage::playLink(const Alg &alg, const QString &text) {
  return QString("<a href='play:%1'>%2</a>")
      .arg(QString::fromLatin1(QUrl::toPercentEncoding(qs(toString(alg)))),
           text);
}

PracticePage::Slots PracticePage::loadSlots(const char *name) const {
  const QStringList names =
      saved::practice(modeId(), name, QStringList{"FR", "FL"})
          .get()
          .toStringList();
  Slots picked{};
  for (Slot s : kF2LSlots) {
    picked[static_cast<std::size_t>(s)] = names.contains(slotText(s));
  }
  if (std::ranges::none_of(picked, std::identity{})) {
    picked = {true, true, false, false};
  }
  return picked;
}

void PracticePage::saveSlots(const char *name, const Slots &picked) const {
  QStringList names;
  for (Slot s : kF2LSlots) {
    if (picked[static_cast<std::size_t>(s)]) {
      names << slotText(s);
    }
  }
  saved::practice(modeId(), name).set(names);
}

QWidget *
PracticePage::addSlotPicker(OptionBar *bar, QToolButton *&button,
                            const Slots &current,
                            const std::function<void(const Slots &)> &chosen) {
  button = new QToolButton(bar);
  button->setFocusPolicy(Qt::NoFocus);
  button->setPopupMode(QToolButton::InstantPopup);
  auto *menu = new QMenu(button);
  for (Slot s : kF2LSlots) {
    QAction *a = menu->addAction(slotText(s));
    a->setCheckable(true);
    a->setData(static_cast<int>(s));
    connect(a, &QAction::toggled, this,
            [shown = button, &current, chosen, s](bool on) {
              Slots picked = current;
              picked[static_cast<std::size_t>(s)] = on;
              if (std::ranges::none_of(picked, std::identity{})) {
                showSlots(shown, current); // one slot at least
                return;
              }
              chosen(picked);
            });
  }
  button->setMenu(menu);
  return bar->addPicker(tr("Last slot:"), button);
}

void PracticePage::showSlots(QToolButton *button, const Slots &picked) {
  QStringList names;
  for (QAction *a : button->menu()->actions()) {
    QSignalBlocker block(a);
    bool on = picked[static_cast<std::size_t>(a->data().toInt())];
    a->setChecked(on);
    if (on) {
      names << a->text();
    }
  }
  button->setText(names.join(" / "));
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

void PracticePage::showIdle(const QString &text, Phase phase) {
  item_ = {};
  item_.id = ++items_;
  phase_ = phase;
  idleText_ = text;
  cube_ = FaceletCube();
  cubeView_->setColourMap(colourMapOf(settings_.neutrality.main));
  cubeView_->clearMarks();
  cubeView_->setState(cube_);
  if (phase == Phase::Ready) {
    progress_.clear();
    showHidden(false);
    display_->setText({});
  }
  showAll();
}

void PracticePage::setProgress(const QStringList &lines) {
  progress_ = lines;
  showVerdict();
}

void PracticePage::begin(const Setup &setup) {
  item_ = {};
  item_.id = ++items_;
  item_.group = setup.group;
  item_.start = setup.cube;
  item_.scrambleState = setup.state;
  item_.hold = setup.hold;
  item_.began = monotonicMs();
  waiting_ = false;
  demoShown_ = false;
  detailOpen_ = false;
  progress_.clear();
  note_.clear();
  item_.inspecting = tab_ == Tab::Inspection;
  cube_ = item_.start;
  cubeView_->finishAnimations();
  cubeView_->setColourMap(colourMapOf(item_.hold));
  cubeView_->clearMarks();
  cubeView_->setState(cube_);
  showHidden(false);
}

void PracticePage::nextLearn(LearnAhead ahead) {
  if (tab_ == Tab::Inspection && readyPending_) {
    showIdle({}, Phase::Ready);
    return;
  }
  if (tab_ != Tab::Learn) {
    std::optional<Setup> s = setUp({});
    if (!s) {
      showIdle(tr("No state could be set up with these options"));
      return;
    }
    begin(*s);
    phase_ = Phase::Solving;
    cubeChanged();
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
                .arg(dueSentence(*pick.nextDue, now, localDayEnd()));
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
  if (!startCard(*pick.caseId, pick.isNew, !pick.isNew)) {
    showIdle(tr("%1 can't be set up with these options")
                 .arg(cardName(*pick.caseId)));
  }
}

bool PracticePage::startCard(const std::string &card, bool isNew, bool graded) {
  std::optional<Setup> s = setUp(card);
  if (!s) {
    return false;
  }
  begin(*s);
  item_.card = card;
  item_.isNew = isNew;
  item_.graded = graded;
  lastCard_ = card;
  phase_ = isNew ? Phase::Demo : Phase::Solving;
  cubeChanged();
  if (needsChoice()) {
    enterChoice(phase_);
    return true;
  }
  showAll();
  return true;
}

void PracticePage::undoLast() {
  std::optional<LearnUndo> u = undo_.pop();
  if (!u || u->cards.empty()) {
    emit message(tr("Nothing to take back"));
    return;
  }
  takeBack(store_.data.cards, *u);
  save();
  refreshList();
  emit message(tr("Took back the grade of %1")
                   .arg(QTextDocumentFragment::fromHtml(cardName(u->caseId))
                            .toPlainText()));
  // The card comes up again, to be done over.
  if (!startCard(u->caseId, u->isNew, !u->isNew)) {
    next();
  }
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
      const Grade g = practiceGrade({.solvedWell = item_.outcome->result.good,
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
  // Practice isn't the card last picked.
  const std::string last = lastCard_;
  if (!startCard(card, false, false)) {
    emit message(
        tr("%1 can't be set up with these options").arg(cardName(card)));
  }
  lastCard_ = last;
}

void PracticePage::nextTimed() {
  driver_.configure(!physical_, false);
  std::optional<Setup> s = setUp({});
  if (!s) {
    flow_.noCase();
    showIdle(tr("No state could be set up with these options"));
    return;
  }
  begin(*s);
  flow_.setUp(monotonicMs());
  display_->showTime(0);
  cubeChanged();
  if (physical_) {
    // setFuture() drops the result of a request still on its way.
    scrambles_.setFuture(generateScramble(item_.scrambleState, item_.hold));
  }
  showAll();
}

void PracticePage::sameAgain() {
  if (item_.id == 0) {
    return;
  }
  const Item was = std::move(item_);
  begin({was.start, was.scrambleState, was.hold, was.group});
  item_.id = was.id; // the mode's analysis of the state still holds
  item_.card = was.card;
  item_.scramble = was.scramble;
  item_.retry = true;
  restarted();
  cubeChanged();
  if (tab_ == Tab::Timed) {
    flow_.setUp(monotonicMs());
    display_->showTime(0);
  } else {
    phase_ = Phase::Solving;
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

void PracticePage::scrambleReady() {
  if (tab_ != Tab::Timed || !physical_ || item_.id == 0) {
    return;
  }
  item_.scramble = scrambles_.result().text;
  showAll();
}

// --- keys --------------------------------------------------------------------

bool PracticePage::handleKey(QKeyEvent *e, bool press) {
  struct Update {
    PracticePage *page;
    ~Update() { page->updateTick(); }
  } update{this};
  TrainKeyEvent k = trainKeyEvent(e, press, KeyChords::Undo);
  // Of the keys held with Ctrl, Alt or Meta only Ctrl+Z, on the Learn tab.
  if (k.modified && k.key != TrainKey::Undo) {
    return false;
  }
  if (tab_ == Tab::Timed) {
    return !k.modified && handleTimedKey(e, press);
  }
  if (!press || item_.id == 0) {
    return false;
  }
  k.move = settings_.keymap.lookup(e);
  return perform(PracticeFlow::key(k, flowState()), e);
}

PracticeFlow::State PracticePage::flowState() {
  return {.tab = tab_,
          .phase = phase_,
          .choosingAgain = afterChoice_ == Phase::Done,
          .hidden = item_.hidden,
          .done = item_.done,
          .inspecting = item_.inspecting,
          .hints = hasHints() && !blind(),
          .waiting = waiting_,
          .best = item_.outcome && !item_.outcome->best.empty(),
          .detailOpen = detailOpen_,
          .graded = item_.graded && item_.grade.has_value(),
          .chooser = choicePanel() && !item_.card.empty(),
          .recorded = item_.recorded && item_.card.empty(),
          .shiftSpaceGivesUp = settings_.drill.shiftSpaceGivesUp};
}

bool PracticePage::perform(const PracticeFlow::Step &step, QKeyEvent *e) {
  using A = PracticeFlow::Action;
  switch (step.action) {
  case A::None:
    break;
  case A::Undo:
    undoLast();
    break;
  case A::NextEarly:
    next(LearnAhead::Early);
    break;
  case A::Start:
    readyPending_ = false;
    next();
    break;
  case A::ConfirmChoice:
    leaveChoice(Choice::Confirmed);
    break;
  case A::CancelChoice:
    leaveChoice(Choice::Cancelled);
    break;
  case A::ChoiceKey:
    choiceKey(step.digit);
    break;
  case A::ChooseFirst:
    emit message(tr("Choose first: Enter when your solutions are ticked"));
    break;
  case A::Next:
    next();
    break;
  case A::StillWorking:
    emit message(tr("Still working out the best solutions"));
    break;
  case A::Introduced:
    introduced();
    break;
  case A::Reveal:
    showHidden(false);
    finishLearn(step.gaveUp ? Try::GaveUp : Try::Finished);
    break;
  case A::GiveUp:
    showHidden(false);
    finishLearn(Try::GaveUp);
    break;
  case A::GiveUpKeyHint:
    timedSay(timedHintText(DrillFlow::Hint::GiveUpKey, settings_.drill));
    break;
  case A::Hint:
    if (const QString text = hint(); text.isEmpty()) {
      emit message(tr("Still working out the best solutions"));
    } else {
      item_.hint = true;
      requestedHint_->setText(text);
      showKeys();
    }
    break;
  case A::ToggleDetail:
    toggleDetail();
    break;
  case A::DeleteTry:
    deleteLearnTry();
    break;
  case A::Retry:
    sameAgain();
    break;
  case A::ResetCube:
    resetCube();
    showAll();
    break;
  case A::PlayDemo:
    playDemo();
    break;
  case A::PlayBest:
    playBest();
    break;
  case A::Regrade:
    regrade(step.grade);
    item_.gradedByHand = true;
    break;
  case A::ChooseAgain:
    chooseAgain();
    break;
  case A::ModeKey:
    // The mode's own keys first; the turning keys turn the cube freely.
    if (doneKey(e)) {
      return true;
    }
    if (step.move) {
      learnTurn(*step.move);
      return true;
    }
    return false;
  case A::Turn:
    learnTurn(*step.move);
    break;
  }
  return step.handled;
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
    item_.inspectMs = std::max<std::int64_t>(1, monotonicMs() - item_.began);
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
    item_.inspectMs = std::max<std::int64_t>(1, monotonicMs() - item_.began);
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
      const Grade g = practiceGrade({.solvedWell = item_.outcome->result.good,
                                     .hinted = item_.hint,
                                     .gaveUp = item_.gaveUp});
      review(p.card, g, now, settings_.srs, &rng_);
      p.record({now, 0, g == Grade::Again, AttemptMode::Learn});
      undo_.push({.setId = modeId().toStdString(),
                  .caseId = item_.card,
                  .cards = {{item_.card, item_.cardBefore, now, {}}}},
                 false);
      item_.grade = g;
      item_.recorded = true;
    } else if (item_.card.empty()) {
      PracticeTry t;
      t.timestamp = now;
      t.moves = item_.outcome->result.moves;
      t.best = item_.outcome->result.bestMoves;
      t.extra = item_.outcome->result.extra;
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
  undo_.push({.setId = modeId().toStdString(),
              .caseId = item_.card,
              .isNew = true,
              .cards = {{item_.card, p.card, 0, {}}}},
             false);
  introduce(p.card, now, settings_.srs);
  save();
  refreshList();
  phase_ = Phase::Done;
  item_.outcome = Outcome{};
  item_.outcome->verdict =
      tr("Shown. It comes back %1, to do from memory.")
          .arg(dueSentence(p.card.due, now, localDayEnd()));
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

bool PracticePage::handleTimedKey(QKeyEvent *e, bool press) {
  if (item_.id == 0) {
    return false;
  }
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
  return driver_.key(e, press, settings_.keymap,
                     scrambleShown ? Scramble::Shown : Scramble::Awaited);
}

void PracticePage::timedRotate(Move m) {
  // Looking at the cube before the start: part of the solve's moves, so
  // that the hold it is solved in is known.
  applyTurn(m);
  item_.moves.push_back(m);
  item_.moveMs.push_back(0);
  cubeChanged();
}

std::optional<DrillFlow::Turn> PracticePage::timedTurn(Move m,
                                                       std::int64_t now) {
  applyTurn(m);
  item_.moves.push_back(m);
  item_.moveMs.push_back(now - item_.began);
  item_.done = turned();
  cubeChanged();
  return isRotation(m.type) ? DrillFlow::Turn::Rotation
                            : DrillFlow::Turn::OffUdFace;
}

void PracticePage::timedSay(const QString &text) {
  hint_->setHint(text, hint_->keys());
}

void PracticePage::timedCancelled(const QString &why) {
  showAll();
  timedSay(why);
}

void PracticePage::timedFinish(std::int64_t ms, DrillFlow::Result result) {
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
    t.moves = item_.outcome->result.moves;
    t.best = item_.outcome->result.bestMoves;
    t.extra = item_.outcome->result.extra;
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

void PracticePage::timedToggleWrong() {
  if (!item_.recorded || !flow_.setWrong(!flow_.failed())) {
    return;
  }
  if (PracticeTry *t = newestTry(store_.data.timed, timedGroupKey())) {
    t->dnf = flow_.failed();
  }
  item_.gaveUp = flow_.failed();
  display_->showTime(item_.ms, flow_.failed() ? Penalty::Dnf : Penalty::None);
  save();
  refreshList();
  showAll();
}

void PracticePage::timedDelete() {
  if (item_.recorded) {
    dropNewestTry(store_.data.timed, timedGroupKey());
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

void PracticePage::deleteLearnTry() {
  // Only an Untimed or Inspection try: a card's grade is changed with 1-4.
  if (!item_.recorded || !item_.card.empty()) {
    return;
  }
  dropNewestTry(tabTries(store_.data, tab_), item_.group);
  item_.recorded = false;
  save();
  refreshList();
  note_ = tr("Deleted; nothing is recorded.");
  showAll();
}

void PracticePage::showInspection() {
  if (tab_ != Tab::Inspection) {
    return;
  }
  const std::int64_t limit = inspectionLimitMs();
  const std::int64_t used =
      item_.inspecting ? monotonicMs() - item_.began : item_.inspectMs;
  display_->setText(inspectionClock(used, limit, item_.inspecting,
                                    palette().color(QPalette::WindowText)));
}

void PracticePage::updateTick() {
  // The clock is only redrawn while it changes: a hold of Space or a solve
  // on the Timed tab, the inspection clock running.
  const bool busy = active_ && ((tab_ == Tab::Timed && flow_.busy()) ||
                                (tab_ == Tab::Inspection && item_.inspecting &&
                                 phase_ == Phase::Solving));
  if (busy != tick_.isActive()) {
    if (busy) {
      tick_.start(16); // as Drill's and the timer's
    } else {
      tick_.stop();
    }
  }
}

void PracticePage::onTick() {
  if (tab_ == Tab::Inspection && item_.inspecting && phase_ == Phase::Solving) {
    showInspection();
  } else if (tab_ == Tab::Timed && driver_.tick(monotonicMs())) {
    updateTick();
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
    PracticeTry *t =
        newestTry(tabTries(store_.data, tab_),
                  tab_ == Tab::Timed ? timedGroupKey() : item_.group);
    if (t && (before.result.extra != item_.outcome->result.extra ||
              before.result.bestMoves != item_.outcome->result.bestMoves)) {
      t->best = item_.outcome->result.bestMoves;
      t->extra = item_.outcome->result.extra;
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
  updateTick();
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
  if (item_.id == 0) {
    hint_->setHint({}, {});
    return;
  }
  const bool best = item_.outcome && !item_.outcome->best.empty();
  const PracticeKeys k =
      tab_ != Tab::Timed
          ? untimedKeys(flowState(), doneKeys())
          : timedKeys(flow_, settings_.drill,
                      {.scrambleShown = !item_.scramble.isEmpty(),
                       .best = best,
                       .detailOpen = detailOpen_,
                       .recorded = item_.recorded});
  hint_->setHint(k.message, k.keys);
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
  return vcube::timedLine(
      {.moves = item_.outcome ? item_.outcome->result.moves : 0,
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

void PracticePage::refreshList() { list_->refresh(); }

void PracticePage::showSummary() {
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
