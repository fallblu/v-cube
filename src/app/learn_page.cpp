#include "app/learn_page.hpp"

#include "app/case_history_dialog.hpp"
#include "app/ui_state.hpp"
#include "app/ui_text.hpp"
#include "app/widgets.hpp"

#include <QActionGroup>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace vcube {

namespace {

struct OrderName {
  NewOrder order;
  const char *id;
  const char *label;
};

const OrderName kOrders[] = {
    {NewOrder::ListOrder, "list",
     QT_TRANSLATE_NOOP("vcube::LearnPage", "In list order")},
    {NewOrder::Queued, "queued",
     QT_TRANSLATE_NOOP("vcube::LearnPage", "In the order queued")},
    {NewOrder::Shuffled, "shuffled",
     QT_TRANSLATE_NOOP("vcube::LearnPage", "Shuffled")},
    {NewOrder::RoundRobin, "roundrobin",
     QT_TRANSLATE_NOOP("vcube::LearnPage", "One from each group in turn")},
    {NewOrder::ShortestAlg, "shortest",
     QT_TRANSLATE_NOOP("vcube::LearnPage", "Shortest alg first")},
    {NewOrder::FewestSteps, "steps",
     QT_TRANSLATE_NOOP("vcube::LearnPage", "Fewest steps left first")},
};

} // namespace

LearnPage::LearnPage(AppSettings &settings, AlgLibrary &library,
                     QWidget *parent)
    : CasePage(settings, library, "learn", parent) {
  // The order of new cases: chosen from the new cases strip's menu.
  orderBox_ = new QComboBox(left_);
  orderBox_->setFocusPolicy(Qt::NoFocus);
  for (const OrderName &o : kOrders) {
    orderBox_->addItem(tr(o.label), QString(o.id));
  }
  orderBox_->hide();

  // --- the new cases strip, under the list: the learn queue, adding to
  // it, and what is rarely needed in a menu ---
  panel_->setTickButtonsVisible(false);
  auto *queueRow = new QHBoxLayout;
  queueLabel_ = new QLabel(left_);
  queueLabel_->setWordWrap(true);
  queueLabel_->setTextFormat(Qt::RichText);
  learnCount_ = new QSpinBox(left_);
  learnCount_->setRange(1, 50);
  learnCount_->setValue(3);
  learnCount_->setFocusPolicy(Qt::ClickFocus);
  learnCount_->installEventFilter(this);
  connect(learnCount_, &QSpinBox::editingFinished, learnCount_,
          [this] { learnCount_->clearFocus(); });
  learnButton_ = new QPushButton(tr("Add new cases"), left_);
  learnButton_->setFocusPolicy(Qt::NoFocus);
  learnButton_->setToolTip(
      tr("Put the next N ticked cases you haven't started into your learn "
         "queue, in the set's order for new cases"));
  learnNextButton_ = new QPushButton(tr("Learn next"), left_);
  learnNextButton_->setFocusPolicy(Qt::NoFocus);
  learnNextButton_->setToolTip(
      tr("Introduce the next case in the learn queue now, whatever the daily "
         "limit"));
  auto *more = new QToolButton(left_);
  more->setText(QStringLiteral("⋯"));
  more->setAccessibleName(tr("More"));
  more->setFocusPolicy(Qt::NoFocus);
  more->setPopupMode(QToolButton::InstantPopup);
  more->setToolTip(tr("More: the order of new cases, the queue, ticks"));
  auto *moreMenu = new QMenu(more);
  more->setMenu(moreMenu);
  unqueueAction_ = moreMenu->addAction(
      tr("Take the next case out of the queue"), this, &LearnPage::unqueueHead);
  QMenu *orderMenu = moreMenu->addMenu(tr("Order of new cases"));
  auto *orderGroup = new QActionGroup(orderMenu);
  for (int i = 0; i < orderBox_->count(); ++i) {
    QAction *a = orderMenu->addAction(orderBox_->itemText(i));
    a->setCheckable(true);
    orderGroup->addAction(a);
    connect(a, &QAction::triggered, this, [this, i] {
      orderBox_->setCurrentIndex(i);
      emit orderBox_->activated(i);
    });
  }
  connect(orderMenu, &QMenu::aboutToShow, this, [this, orderGroup] {
    if (QAction *a = orderGroup->actions().value(orderBox_->currentIndex())) {
      a->setChecked(true);
    }
  });
  moreMenu->addSeparator();
  QAction *tickAll = moreMenu->addAction(QString());
  connect(tickAll, &QAction::triggered, this,
          [this] { panel_->tickShown(true); });
  connect(moreMenu, &QMenu::aboutToShow, this,
          [this, tickAll] { tickAll->setText(panel_->tickAllText()); });
  moreMenu->addAction(tr("Tick none"), this,
                      [this] { panel_->tickShown(false); });
  queueRow->addWidget(learnCount_);
  queueRow->addWidget(learnButton_);
  queueRow->addWidget(learnNextButton_);
  queueRow->addStretch();
  queueRow->addWidget(more);
  leftLayout_->insertWidget(leftLayout_->count() - 1, queueLabel_);
  leftLayout_->insertLayout(leftLayout_->count() - 1, queueRow);
  queueMessageTimer_.setSingleShot(true);
  connect(&queueMessageTimer_, &QTimer::timeout, this, &LearnPage::updateQueue);

  // --- right: the options, the virtual cube, what is said about it ---
  stage_ = new CaseStage(this);
  cubeView_ = stage_->cube;
  statusLabel_ = stage_->status;
  reveal_ = stage_->reveal;
  gradeRow_ = stage_->gradeRow;
  OptionBar *options = stage_->options;
  options->addPicker(tr("Alg set:"), panel_->takeSetBox(), 1);
  practiceBox_ = new SegmentedPicker(options);
  practiceBox_->addOption(tr("Solve"), enumData(Practice::Solve));
  practiceBox_->addOption(tr("Recognise"), enumData(Practice::Recognise));
  practiceBox_->addOption(tr("Which case?"), enumData(Practice::Quiz));
  practiceBox_->setToolTip(
      tr("Solve: solve each case from memory. Recognise: reviews only show "
         "the case; press Space once you recall its alg, then grade "
         "yourself (new and learning cases are still solved). Which case?: "
         "name the case among others of its group, not graded."));
  options->addPicker(tr("Practise:"), practiceBox_);
  viewBox_ = createViewSelector(cubeView_, "learn", options);
  viewBox_->setToolTip(
      tr("How the virtual cube is shown, remembered for each alg set"));
  options->addPicker(tr("View:"), viewBox_);
  options->addStretch();
  slotsSpin_ = new QSpinBox(options);
  slotsSpin_->setRange(1, 4);
  slotsSpin_->setValue(2);
  slotsSpin_->setSuffix(tr(" open"));
  slotsSpin_->setFocusPolicy(Qt::NoFocus);
  slotsSpin_->setToolTip(tr("How many slots are open in context practice"));
  contextButton_ = new QPushButton(tr("Practise in context"), options);
  contextButton_->setFocusPolicy(Qt::NoFocus);
  contextButton_->setToolTip(
      tr("Solve several pairs at once on a cube with EO Cross done and some "
         "slots open, then compare your moves with this set's algs and its "
         "cheapest order. Not graded. Backspace returns to learning."));
  options->addWidget(slotsSpin_);
  options->addWidget(contextButton_);
  listButton_ = new QPushButton(options);
  listButton_->setFocusPolicy(Qt::NoFocus);
  listButton_->setToolTip(tr("Hide or show the case list (F9)"));
  options->addWidget(listButton_);
  for (int g = 0; g < 4; ++g) {
    auto grade = static_cast<Grade>(g + 1);
    QPushButton *b = stage_->gradeButtons[static_cast<std::size_t>(g)];
    connect(b, &QPushButton::clicked, this, [this, grade] {
      if (phase() == Phase::Recalled) {
        gradeRecognition(grade);
      } else {
        regrade(grade);
      }
    });
  }
  connect(stage_, &CaseStage::quizAnswered, this, &LearnPage::answerQuiz);

  split_ = new QSplitter(this);
  split_->addWidget(left_);
  split_->addWidget(stage_);
  split_->setStretchFactor(1, 1);
  left_->setMinimumWidth(240);
  split_->setSizes({340, 860});
  auto *layout = new QHBoxLayout(this);
  layout->addWidget(split_);
  split_->restoreState(saved::learnSplitter().get());
  QSplitter *right = stage_->splitter;
  right->restoreState(saved::learnRightSplitter().get());
  // A drag moves a splitter with every mouse move, and each save rewrites
  // the settings file: the sizes are saved once the dragging has paused.
  layoutTimer_.setSingleShot(true);
  layoutTimer_.setInterval(500);
  connect(&layoutTimer_, &QTimer::timeout, this, &LearnPage::saveSplitters);
  connect(split_, &QSplitter::splitterMoved, this, [this] {
    splitMoved_ = true;
    layoutTimer_.start();
  });
  connect(right, &QSplitter::splitterMoved, this, [this] {
    rightSplitMoved_ = true;
    layoutTimer_.start();
  });
  sideHiddenChanged();
  selectEnum(
      practiceBox_,
      practiceFromId(saved::learnPractice().get()).value_or(Practice::Solve));

  idleTimer_.setSingleShot(true);
  connect(&idleTimer_, &QTimer::timeout, this, [this] {
    if (phase() == Phase::NoCase && isVisible()) {
      nextCase();
    }
  });
  connect(learnButton_, &QPushButton::clicked, this, &LearnPage::learnNext);
  connect(learnNextButton_, &QPushButton::clicked, this,
          &LearnPage::learnQueueHead);
  connect(listButton_, &QPushButton::clicked, this, &LearnPage::toggleList);
  connect(orderBox_, &QComboBox::activated, this, [this] {
    saved::learnOrder(setId_).set(orderBox_->currentData().toString());
    updateQueue();
    if (phase() == Phase::NoCase) {
      nextCase();
    }
  });
  connect(practiceBox_, &SegmentedPicker::activated, this, [this] {
    saved::learnPractice().set(practiceId(practice()));
    if (context_) {
      return; // applies when back to learning
    }
    // A case begun is finished first; anything else gives way.
    bool begun =
        phase() == Phase::Demo ||
        (phase() == Phase::Solving &&
         (attempt_.links().size() > 1 || !attempt_.follower().moves().empty() ||
          attempt_.hinted() || attempt_.nextHinted()));
    if (!begun) {
      ready_ = false;
      nextCase();
    }
    updateHeader();
  });
  connect(contextButton_, &QPushButton::clicked, this, [this] {
    if (context_) {
      leaveContext();
    } else {
      startContext();
    }
  });
  connect(viewBox_, &QComboBox::activated, this, [this] {
    saved::learnView(setId_).set(viewBox_->currentData().toString());
  });
  init();
}

bool LearnPage::eventFilter(QObject *watched, QEvent *event) {
  if (watched == learnCount_ && event->type() == QEvent::FocusIn) {
    countTyped_ = false;
  }
  if (watched == learnCount_ && event->type() == QEvent::KeyPress) {
    auto *ke = static_cast<QKeyEvent *>(event);
    int key = ke->key();
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
      // Only a number typed in starts learning: Enter after clicking the
      // arrows is more likely meant for the next case.
      learnCount_->interpretText();
      bool typed = countTyped_;
      learnCount_->clearFocus();
      if (typed) {
        learnNext();
      } else if (!ke->isAutoRepeat()) {
        handleKey(ke, true);
      }
      return true;
    }
    if (key == Qt::Key_Escape) {
      learnCount_->clearFocus();
      return true;
    }
    if ((key >= Qt::Key_0 && key <= Qt::Key_9 &&
         !ke->modifiers().testFlag(Qt::ControlModifier)) ||
        key == Qt::Key_Backspace || key == Qt::Key_Delete) {
      countTyped_ = true;
      return false;
    }
    if (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Left ||
        key == Qt::Key_Right || key == Qt::Key_Home || key == Qt::Key_End) {
      return false;
    }
    // Any other key is meant for the cube (once, if held down).
    learnCount_->clearFocus();
    if (!ke->isAutoRepeat()) {
      handleKey(ke, true);
    }
    return true;
  }
  return CasePage::eventFilter(watched, event);
}

void LearnPage::applySettings() {
  CubeLook look = cubeView_->look();
  look.scheme = settings_.scheme;
  cubeView_->setLook(look);
  cubeView_->setAnimationMs(settings_.animationMs);
  CasePage::applySettings();
  // The limits and steps may have changed what is due.
  if (phase() == Phase::NoCase && set() && isVisible()) {
    nextCase();
  } else if (set()) {
    updateSummary();
  }
}

void LearnPage::activated() {
  CasePage::activated();
  if (phase() == Phase::NoCase && !context_) {
    nextCase(); // something may have fallen due meanwhile
  }
}

void LearnPage::saveSplitters() {
  layoutTimer_.stop();
  if (splitMoved_) {
    saved::learnSplitter().set(split_->saveState());
  }
  if (rightSplitMoved_) {
    saved::learnRightSplitter().set(stage_->splitter->saveState());
  }
  splitMoved_ = false;
  rightSplitMoved_ = false;
}

void LearnPage::aboutToClose() {
  CasePage::aboutToClose();
  saveSplitters();
}

void LearnPage::focusModeChanged() {
  CasePage::focusModeChanged();
  stage_->options->setVisible(!focusMode());
}

void LearnPage::toggleList() { setSideShown(!sideShown()); }

void LearnPage::sideHiddenChanged() {
  listButton_->setText(sideShown() ? tr("Hide list") : tr("Show list"));
}

void LearnPage::deactivated() {
  CasePage::deactivated();
  cubeView_->finishAnimations();
  learnCount_->clearFocus();
  idleTimer_.stop();
  // A case whose clock is running isn't left running: it is taken off
  // the cube and waits for Space on the way back. One already answered
  // stays up to be graded.
  ready_ = false;
  if (context_ || !current_) {
    return;
  }
  Current was = *current_;
  switch (phase()) {
  case Phase::Solving:
  case Phase::Demo:
    begin(was.caseId, was.kind());
    break;
  case Phase::Recognise:
    beginRecognition(was.caseId);
    break;
  case Phase::Quiz:
    nextQuiz();
    break;
  default:
    break;
  }
}

void LearnPage::windowDeactivated() {
  // Another window in front: the case stays up, but the time away isn't
  // a recognition time.
  if (current_ && (phase() == Phase::Solving || phase() == Phase::Recognise)) {
    attempt_.spoilRecognition();
  }
}

// --- the learn queue -------------------------------------------------------

NewOrder LearnPage::newOrder() const {
  QString id = orderBox_->currentData().toString();
  for (const OrderName &o : kOrders) {
    if (id == o.id) {
      return o.order;
    }
  }
  return NewOrder::ListOrder;
}

int LearnPage::algMoves(const std::string &caseId) const {
  const CaseInfo *info = infoOf(caseId);
  return info ? static_cast<int>(parseAlgOrEmpty(info->mainAlg).size()) : 0;
}

// --- practising
// ---------------------------------------------------------------

QString LearnPage::startCaseLabel(const std::string &caseId) const {
  return notDue(caseId) ? tr("Practise this case now (not graded)")
                        : tr("Learn this case now");
}

bool LearnPage::notDue(const std::string &caseId) const {
  const CaseProgress *cp = cardOf(caseId);
  return cp && cp->card.state == CardState::Review &&
         !isDue(cp->card, unixNow(), settings_.srs, localDayEnd());
}

void LearnPage::applySetView() {
  QString name = saved::learnView(setId_).get();
  if (auto mode = CubeView::modeFromName(name)) {
    viewBox_->setCurrentIndex(viewBox_->findData(name));
    cubeView_->setMode(*mode);
  } else if (!setId_.empty()) {
    // The set keeps the view in use.
    saved::learnView(setId_).set(viewBox_->currentData().toString());
  }
}

void LearnPage::adoptSet() {
  pairSet_ = set() && isPairSet(*set());
  slotsSpin_->setVisible(pairSet_);
  contextButton_->setVisible(pairSet_);
  applySetView();
  QString order = saved::learnOrder(setId_).get();
  orderBox_->setCurrentIndex(std::max(0, orderBox_->findData(order)));
  updateQueue();
}

QString LearnPage::practiceId(Practice practice) {
  switch (practice) {
  case Practice::Solve:
    return "solve";
  case Practice::Recognise:
    return "recognise";
  case Practice::Quiz:
    return "quiz";
  }
  return "solve";
}

std::optional<LearnPage::Practice>
LearnPage::practiceFromId(const QString &id) {
  for (Practice practice :
       {Practice::Solve, Practice::Recognise, Practice::Quiz}) {
    if (id == practiceId(practice)) {
      return practice;
    }
  }
  return std::nullopt;
}

LearnPage::Practice LearnPage::practice() const {
  return currentEnum(practiceBox_, Practice::Solve);
}

void LearnPage::startSet() {
  ready_ = false;
  pending_ = nullptr;
  context_.reset();
  contextButton_->setText(tr("Practise in context"));
  adoptSet();
  current_.reset();
  last_.clear();
  undo_.close();
  nextCase();
}

void LearnPage::setReloaded() {
  pairSet_ = set() && isPairSet(*set());
  slotsSpin_->setVisible(pairSet_);
  contextButton_->setVisible(pairSet_);
  if (context_) {
    if (pairSet_) {
      if (phase() == Phase::ContextDone) {
        showContextResult();
      }
      return;
    }
    leaveContext();
    return;
  }
  if (phase() == Phase::Quiz || phase() == Phase::QuizDone) {
    if (!current_ || !eligible(current_->caseId)) {
      nextCase();
    }
    return;
  }
  if (current_ && phase() == Phase::Demo && eligible(current_->caseId)) {
    begin(current_->caseId, current_->kind());
    return;
  }
  if (current_ && !eligible(current_->caseId)) {
    current_.reset();
    nextCase();
  } else if (current_ &&
             (phase() == Phase::Done || phase() == Phase::Recalled)) {
    if (phase() == Phase::Recalled) {
      refreshLink();
    }
    showReveal();
  } else if (current_ && phase() == Phase::Recognise) {
    refreshLink();
  } else if (current_ && phase() == Phase::Solving) {
    if (eligible(linkCase())) {
      refreshLink();
      showSolvingStatus();
    } else {
      begin(current_->caseId, current_->kind());
    }
  } else if (!current_) {
    nextCase();
  }
}

void LearnPage::ticksChanged() {
  if (phase() == Phase::NoCase) {
    nextCase();
  }
}

void LearnPage::neutralityChanged() {
  // Show the case in a hold the new choice allows. Only the colours
  // change, so the turns made so far and a hint taken still count.
  const auto &holds = settings_.neutrality.holds;
  if (current_ &&
      std::find(holds.begin(), holds.end(), current_->hold) == holds.end()) {
    current_->hold = pickHold();
    cubeView_->setColourMap(colourMapOf(current_->hold));
  }
}

void LearnPage::startCase(const std::string &caseId) {
  ready_ = false; // picked in the list: it waits for Space
  if (eligible(caseId)) {
    if (context_) {
      context_.reset();
      contextButton_->setText(tr("Practise in context"));
    }
    begin(caseId, notDue(caseId) ? LearnAttempt::Kind::Practice
                                 : LearnAttempt::Kind::Known);
  }
}

std::vector<std::string> LearnPage::pool() const {
  std::vector<std::string> ticked = tickedEligible();
  std::vector<std::string> cards;
  if (!byFamily()) {
    cards = std::move(ticked);
  } else {
    // One card per family that has a ticked case with an alg.
    for (const auto &id : ticked) {
      std::string card = cardId(id);
      if (std::find(cards.begin(), cards.end(), card) == cards.end()) {
        cards.push_back(card);
      }
    }
  }
  NewOrder order = newOrder();
  if (order == NewOrder::ListOrder) {
    return cards;
  }
  const AlgSet *s = library_.find(setId_);
  std::vector<NewCase> cases;
  cases.reserve(cards.size());
  for (const auto &card : cards) {
    std::string member = card;
    if (const auto *members = families_.familyFor(card);
        members && !members->empty() && (!s || !s->findCase(card))) {
      member = members->front();
    }
    NewCase c;
    c.id = card;
    const CaseGroup *g = s ? s->groupOf(member) : nullptr;
    c.group = g ? g->name : std::string();
    if (order == NewOrder::ShortestAlg) {
      c.algMoves = algMoves(member);
    }
    c.stepsLeft = stepsFrom(member);
    auto it = library_.progress(setId_).find(card);
    c.queued =
        it != library_.progress(setId_).end() && isQueued(it->second.card)
            ? it->second.card.due
            : std::numeric_limits<std::int64_t>::max();
    cases.push_back(std::move(c));
  }
  // The set's shuffled order stays the same from one day to the next.
  Stored<QString> seed = saved::learnSeed(setId_);
  if (!seed.exists()) {
    seed.set(QString::number(QRandomGenerator::global()->generate64()));
  }
  return orderNewCases(cases, order, seed.get().toULongLong());
}

std::string LearnPage::pickMember(const std::string &card) {
  const std::vector<std::string> *members = families_.familyFor(card);
  if (!byFamily() || !members) {
    return card;
  }
  std::vector<std::string> ticked = tickedEligible();
  std::vector<std::string> choices;
  for (const auto &m : *members) {
    if (std::find(ticked.begin(), ticked.end(), m) != ticked.end()) {
      choices.push_back(m);
    }
  }
  if (choices.empty()) {
    return card;
  }
  std::uniform_int_distribution<std::size_t> pick(0, choices.size() - 1);
  return choices[pick(rng_)];
}

void LearnPage::updateCardRows(const std::string &caseId) {
  const std::vector<std::string> *members = families_.familyFor(caseId);
  if (!byFamily() || !members) {
    updateRow(caseId);
    return;
  }
  for (const auto &m : *members) {
    updateRow(m);
  }
}

void LearnPage::nextCase(LearnAhead ahead) {
  pending_ = nullptr; // what comes next is worked out afresh
  idleTimer_.stop();
  stage_->clearCase();
  // Nothing of the last case stays up, e.g. a walkthrough's moves.
  guide_.reset();
  attempt_.clear();
  showGuide();
  if (practice() == Practice::Quiz && !acrossSets()) {
    nextQuiz();
    return;
  }
  // Across sets, the shown set's learning cases can still come early.
  if (acrossSets() && nextAcrossSets()) {
    return;
  }
  if (!set()) {
    current_.reset();
    say(tr("No alg set selected."));
    stage_->setActions({});
    setPhase(Phase::NoCase);
    return;
  }
  LearnPickOptions options;
  options.last = last_;
  options.rng = &rng_;
  options.early = ahead == LearnAhead::Early;
  options.dayEnd = localDayEnd();
  options.newCases = !acrossSets();
  LearnPick pick = pickLearnCase(pool(), progress(), unixNow(), localDayStart(),
                                 settings_.srs, options);
  if (!pick.caseId) {
    showIdle(pick);
    return;
  }
  beginPick(pick);
}

void LearnPage::beginPick(const LearnPick &pick) {
  std::string member = pickMember(*pick.caseId);
  const CaseProgress *cp = progressOf(*pick.caseId);
  if (practice() == Practice::Recognise && !pick.isNew && cp &&
      cp->card.state == CardState::Review) {
    beginRecognition(member);
  } else {
    begin(member,
          pick.isNew ? LearnAttempt::Kind::New : LearnAttempt::Kind::Known);
  }
}

bool LearnPage::nextAcrossSets() {
  std::int64_t now = unixNow();
  std::int64_t dayEnd = localDayEnd();
  std::vector<SetDue> due;
  for (const auto &s : library_.sets()) {
    QStringList unticked = untickedCases(s);
    int n = 0;
    for (const auto &[id, cp] : library_.progress(s.id)) {
      if (!unticked.contains(qs(id)) &&
          isDueNow(cp.card, now, dayEnd, settings_.srs)) {
        ++n;
      }
    }
    due.push_back({s.id, n});
  }
  // A set whose due cards turn out not to be offered (no alg any more,
  // say) is passed over.
  while (auto i = pickSetToReview(due, rng_)) {
    if (due[*i].setId != setId_) {
      showSet(due[*i].setId);
      adoptSet();
    }
    LearnPickOptions options;
    options.last = last_;
    options.rng = &rng_;
    options.newCases = false;
    options.dayEnd = dayEnd;
    LearnPick pick = pickLearnCase(pool(), progress(), now, localDayStart(),
                                   settings_.srs, options);
    if (pick.caseId) {
      beginPick(pick); // never a new case: none are picked across sets
      return true;
    }
    due[*i].due = 0;
  }
  return false;
}

void LearnPage::showIdle(const LearnPick &pick) {
  // Whatever falls due later waits for Space.
  ready_ = false;
  pending_ = nullptr;
  current_.reset();
  cube_ = FaceletCube();
  cubeView_->setState(cube_);
  clearCaseSelection();
  std::int64_t now = unixNow();
  std::int64_t dayStart = localDayStart(), dayEnd = localDayEnd();
  std::vector<std::string> cases = pool();
  QStringList lines;
  QString heading; // set: the lines are shown under it, one per line
  QList<KeyHint> keys;
  std::vector<CaseStage::Action> actions;
  if (tickedCount() == 0) {
    lines << tr("Tick some cases to learn them.");
  } else if (cases.empty() && excludedCount() > 0) {
    lines << tr("None of the ticked cases has an alg that solves it yet. Add "
                "algs in Library mode, e.g. with Paste algs.");
    actions.push_back({tr("Paste algs in Library…"),
                       [this] { emit pasteAlgsRequested(qs(setId_)); }});
  } else if (cases.empty()) {
    lines << tr("Tick some cases that have algs to learn them.");
  } else {
    LearnCounts n =
        learnCounts(cases, progress(), now, dayStart, settings_.srs, dayEnd);
    // A heading, then the numbers and what comes next a line each.
    const bool laterToday = pick.nextDue && *pick.nextDue < dayEnd;
    if (n.doneToday > 0) {
      heading = laterToday ? tr("Done for now") : tr("Done for today");
      lines << tr("Reviewed today: %1").arg(n.doneToday)
            << tr("Again: %1 (%2%)")
                   .arg(n.againToday)
                   .arg(std::lround(100.0 * n.againToday / n.doneToday))
            << tr("Due tomorrow: %1").arg(n.dueTomorrow);
    } else {
      heading = tr("All caught up");
    }
    if (acrossSets()) {
      lines << tr("Nothing is due in any set.");
    } else if (pick.newLimitReached) {
      lines << tr("%1 in your learn queue wait for tomorrow; the daily limit "
                  "for new cases is %2 (see Settings).")
                   .arg(pick.queued == 1 ? tr("1 case")
                                         : tr("%1 cases").arg(pick.queued))
                   .arg(settings_.srs.newPerDay);
    } else if (pick.learningLimitReached) {
      lines << tr("%1 in your learn queue wait until fewer than %2 cases are "
                  "in their learning steps (see Settings).")
                   .arg(pick.queued == 1 ? tr("1 case")
                                         : tr("%1 cases").arg(pick.queued))
                   .arg(settings_.srs.maxLearning);
    }
    if (pick.nextDue) {
      lines << tr("Next review %1.")
                   .arg(dueSentence(*pick.nextDue, unixNow(), localDayEnd()));
    }
    bool unstarted = std::any_of(cases.begin(), cases.end(), [this](auto &id) {
      return statusOf(progressOf(id)) == CaseStatus::Unstarted;
    });
    if (!acrossSets() && pick.queued == 0 && !unstarted) {
      lines << tr("Every ticked case is started.");
    }
    LearnPickOptions early;
    early.early = true;
    early.newCases = false;
    early.dayEnd = dayEnd;
    bool waiting =
        pickLearnCase(cases, progress(), now, dayStart, settings_.srs, early)
            .caseId.has_value();
    keys = {{tr("Space"), waiting ? tr("the next learning case now, early")
                                  : tr("check again")}};
    // What can be done instead of waiting.
    if (waiting) {
      actions.push_back({tr("Next learning case now"),
                         [this] { nextCase(LearnAhead::Early); }});
    }
    if (!acrossSets() && pick.queued == 0 && unstarted) {
      int count = learnCount_->value();
      actions.push_back({count == 1 ? tr("Learn 1 new case")
                                    : tr("Learn %1 new cases").arg(count),
                         [this] { learnNext(); }});
    }
    if (!acrossSets() && pick.queued > 0 && pick.newLimitReached) {
      actions.push_back(
          {tr("Learn the next one anyway"), [this] { learnQueueHead(); }});
    }
    // Drilling what was learned: its weakest cases once it was drilled.
    if (!acrossSets() && n.doneToday > 0) {
      bool drilled = false;
      for (const auto &[id, p] : progress()) {
        drilled = drilled ||
                  std::any_of(p.attempts.begin(), p.attempts.end(),
                              [](const Attempt &a) {
                                return a.mode == AttemptMode::VirtualDrill ||
                                       a.mode == AttemptMode::PhysicalDrill;
                              });
      }
      actions.push_back(
          {drilled ? tr("Drill the weakest cases")
                   : tr("Drill the learned cases"),
           [this, drilled] { emit drillSetRequested(qs(setId_), drilled); }});
    }
  }
  if (!acrossSets()) {
    // Reviews due in the other sets.
    int total = 0, most = 0, setsDue = 0;
    const AlgSet *top = nullptr;
    for (const auto &other : library_.sets()) {
      if (other.id == setId_) {
        continue;
      }
      int due = dueIn(other);
      total += due;
      setsDue += due > 0 ? 1 : 0;
      if (due > most) {
        most = due;
        top = &other;
      }
    }
    if (top) {
      std::string id = top->id;
      actions.push_back({tr("Review %1 due in %2").arg(most).arg(qs(top->name)),
                         [this, id] { chooseSet(id); }});
    }
    if (setsDue > 1) {
      actions.push_back({tr("Review all sets (%1 due)").arg(total),
                         [this] { chooseAcrossSets(); }});
    }
  }
  if (heading.isEmpty()) {
    say(lines.join(' '), keys);
  } else {
    QStringList escaped;
    for (const QString &line : lines) {
      escaped << line.toHtmlEscaped();
    }
    say(QString("<p style=\"font-size:large\"><b>%1</b></p>%2")
            .arg(heading.toHtmlEscaped(), escaped.join("<br>")),
        keys);
  }
  setPhase(Phase::NoCase);
  stage_->setActions(actions);
  // Look again when the next card falls due (learn-ahead included), or
  // when the day changes.
  std::int64_t target = dayEnd;
  if (pick.nextDue) {
    target = std::min(target, *pick.nextDue - settings_.srs.learnAheadSeconds);
  }
  std::int64_t ms =
      std::clamp<std::int64_t>((target - now) * 1000 + 500, 1000, 3600 * 1000);
  idleTimer_.start(static_cast<int>(ms));
}

} // namespace vcube
