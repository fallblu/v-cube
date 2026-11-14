#include "app/learn_page.hpp"

#include "app/case_history_dialog.hpp"
#include "app/ui_state.hpp"
#include "app/widgets.hpp"

#include <QActionGroup>
#include <QComboBox>
#include <QDateTime>
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

// Every reason a case went as it did, e.g. both a hint and leaving the
// algs.
QString reasonsText(const GradeWhy &w) {
  QStringList reasons;
  if (w.gaveUp) {
    reasons << LearnPage::tr("gave up");
  } else if (w.miss != GradeWhy::Miss::None) {
    reasons << (w.miss == GradeWhy::Miss::NotCloser
                    ? LearnPage::tr("the step didn't get closer")
                : w.miss == GradeWhy::Miss::StartedOver
                    ? LearnPage::tr("started over")
                    : LearnPage::tr("left the algs"));
  }
  if (w.hint) {
    reasons << LearnPage::tr("used a hint");
  } else if (w.nextHint) {
    reasons << LearnPage::tr("used the next-move hint");
  }
  if (reasons.isEmpty()) {
    switch (w.kept) {
    case GradeWhy::Kept::WalkedThrough:
      reasons << LearnPage::tr("walked through");
      break;
    case GradeWhy::Kept::FromMemory:
      reasons << LearnPage::tr("solved from memory");
      break;
    case GradeWhy::Kept::SetStep:
      reasons << LearnPage::tr("the set's step");
      break;
    case GradeWhy::Kept::AnotherStep:
      reasons << LearnPage::tr("another step that gets closer");
      break;
    }
  }
  return reasons.join(LearnPage::tr(" and "));
}

const StatusColour kLearning{Status::Warning};

// Choices in "which case is this?".
constexpr std::size_t kQuizChoices = 6;

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
  practiceBox_->addOption(tr("Solve"), "solve");
  practiceBox_->addOption(tr("Recognise"), "recognise");
  practiceBox_->addOption(tr("Which case?"), "quiz");
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
    b->setToolTip(tr("Grade the case %1 (key %2)")
                      .arg(tr(gradeName(grade)).toLower())
                      .arg(g + 1));
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
  left_->setMinimumWidth(300);
  split_->setSizes({340, 860});
  auto *layout = new QHBoxLayout(this);
  layout->addWidget(split_);
  split_->restoreState(saved::learnSplitter().get());
  QSplitter *right = stage_->splitter;
  right->restoreState(saved::learnRightSplitter().get());
  connect(split_, &QSplitter::splitterMoved, this,
          [this] { saved::learnSplitter().set(split_->saveState()); });
  connect(right, &QSplitter::splitterMoved, this,
          [right] { saved::learnRightSplitter().set(right->saveState()); });
  sideHiddenChanged();
  int p = practiceBox_->findData(saved::learnPractice().get());
  practiceBox_->setCurrentIndex(std::max(0, p));

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
    saved::learnPractice().set(practiceBox_->currentData().toString());
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

void LearnPage::focusModeChanged() {
  CasePage::focusModeChanged();
  stage_->options->setVisible(!focusMode());
}

void LearnPage::toggleList() { setSideHidden(!sideHidden()); }

void LearnPage::sideHiddenChanged() {
  listButton_->setText(sideHidden() ? tr("Show list") : tr("Hide list"));
}

void LearnPage::deactivated() {
  CasePage::deactivated();
  cubeView_->finishAnimations();
  learnCount_->clearFocus();
  idleTimer_.stop();
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

QString LearnPage::startCaseLabel(const std::string &caseId) {
  return notDue(caseId) ? tr("Practise this case now (not graded)")
                        : tr("Learn this case now");
}

bool LearnPage::notDue(const std::string &caseId) {
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

LearnPage::Practice LearnPage::practice() const {
  return static_cast<Practice>(std::max(0, practiceBox_->currentIndex()));
}

void LearnPage::startSet() {
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
    begin(current_->caseId, current_->isNew, current_->practice);
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
      begin(current_->caseId, current_->isNew, current_->practice);
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
  if (eligible(caseId)) {
    if (context_) {
      context_.reset();
      contextButton_->setText(tr("Practise in context"));
    }
    begin(caseId, false, notDue(caseId));
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

void LearnPage::nextCase(bool early) {
  idleTimer_.stop();
  reveal_->clear();
  stage_->hideFamily();
  gradeRow_->setVisible(false);
  stage_->hideQuiz();
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
  options.early = early;
  options.dayEnd = localDayEnd();
  options.newCases = !acrossSets();
  LearnPick pick = pickLearnCase(pool(), progress(), unixNow(), localDayStart(),
                                 settings_.srs, options);
  if (!pick.caseId) {
    showIdle(pick);
    return;
  }
  std::string member = pickMember(*pick.caseId);
  const CaseProgress *cp = progressOf(*pick.caseId);
  if (practice() == Practice::Recognise && !pick.isNew && cp &&
      cp->card.state == CardState::Review) {
    beginRecognition(member);
  } else {
    begin(member, pick.isNew);
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
      std::string member = pickMember(*pick.caseId);
      const CaseProgress *cp = progressOf(*pick.caseId);
      if (practice() == Practice::Recognise && cp &&
          cp->card.state == CardState::Review) {
        beginRecognition(member);
      } else {
        begin(member, false);
      }
      return true;
    }
    due[*i].due = 0;
  }
  return false;
}

QString LearnPage::intervalText(std::int64_t seconds) {
  if (seconds < 60) {
    return tr("<1 min");
  }
  if (seconds < 3600) {
    return tr("%1 min").arg((seconds + 59) / 60);
  }
  if (seconds < kSecondsPerDay) {
    return tr("%1 h").arg((seconds + 1799) / 3600);
  }
  return tr("%1 d").arg((seconds + kSecondsPerDay / 2) / kSecondsPerDay);
}

QString LearnPage::whenText(std::int64_t due) {
  std::int64_t now = unixNow();
  if (due >= localDayEnd()) {
    // A review is due all day: only the day matters.
    QDate day = QDateTime::fromSecsSinceEpoch(due).date();
    qint64 days = QDate::currentDate().daysTo(day);
    return days == 1 ? tr("tomorrow")
                     : tr("on %1").arg(QLocale().toString(day, "ddd d MMM"));
  }
  std::int64_t d = due - now;
  if (d <= 0) {
    return tr("now");
  }
  if (d < 3600) {
    return tr("in %1 min").arg((d + 59) / 60);
  }
  return tr("at %1").arg(QDateTime::fromSecsSinceEpoch(due).toString("hh:mm"));
}

void LearnPage::showIdle(const LearnPick &pick) {
  current_.reset();
  cube_ = FaceletCube();
  cubeView_->setState(cube_);
  clearCaseSelection();
  std::int64_t now = unixNow();
  std::int64_t dayStart = localDayStart(), dayEnd = localDayEnd();
  std::vector<std::string> cases = pool();
  QStringList lines;
  QList<KeyHint> keys;
  std::vector<CaseStage::Action> actions;
  if (tickedCount() == 0) {
    lines << tr("Tick some cases to learn them.");
  } else if (cases.empty() && excludedCount() > 0) {
    lines << tr("None of the ticked cases has an alg that solves it yet. Add "
                "algs in Library mode, e.g. with Paste algs.");
  } else if (cases.empty()) {
    lines << tr("Tick some cases that have algs to learn them.");
  } else {
    LearnCounts n =
        learnCounts(cases, progress(), now, dayStart, settings_.srs, dayEnd);
    if (n.doneToday > 0) {
      lines << tr("Done for now: %1 reviewed today, %2 Again (%3%). Due "
                  "tomorrow: %4.")
                   .arg(n.doneToday)
                   .arg(n.againToday)
                   .arg(std::lround(100.0 * n.againToday / n.doneToday))
                   .arg(n.dueTomorrow);
    } else {
      lines << tr("All caught up.");
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
      lines << tr("Next review %1.").arg(whenText(*pick.nextDue));
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
      actions.push_back(
          {tr("Next learning case now"), [this] { nextCase(true); }});
    }
    if (!acrossSets() && pick.queued == 0 && unstarted) {
      int n = learnCount_->value();
      actions.push_back(
          {n == 1 ? tr("Learn 1 new case") : tr("Learn %1 new cases").arg(n),
           [this] { learnNext(); }});
    }
    if (!acrossSets() && pick.queued > 0 && pick.newLimitReached) {
      actions.push_back(
          {tr("Learn the next one anyway"), [this] { learnQueueHead(); }});
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
  say(lines.join(' '), keys);
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

CubieCube LearnPage::setUp(const std::string &caseId, Mirror world,
                           bool scramble) {
  const CaseInfo *info = infoOf(caseId);
  const AlgSet *s = set();
  const CaseGroup *g = s ? s->groupOf(caseId) : nullptr;
  if (!info || !g) {
    return {};
  }
  // From a random side, as in Drill, when the set asks for random U turns,
  // and with the pieces the step leaves free anywhere when `scramble`.
  return withRandomAufs(
      *s, setUpCase(info->state, s->specFor(*g).free, world, scramble, rng_),
      rng_);
}

void LearnPage::begin(const std::string &caseId, bool isNew, bool practice) {
  const CaseInfo *info = infoOf(caseId);
  const AlgSet *s = set();
  const AlgCase *c = s ? s->findCase(caseId) : nullptr;
  const CaseGroup *g = s ? s->groupOf(caseId) : nullptr;
  if (!info || !c || !g) {
    return;
  }
  if (context_) {
    context_.reset();
    contextButton_->setText(tr("Practise in context"));
  }
  idleTimer_.stop();
  stage_->hideQuiz();
  Hold hold = pickHold();
  Mirror world = pickWorld(caseId);
  // A reduction set's cases come up with the step's free pieces anywhere,
  // as they do after a step, and so do learned cases (so that e.g. an OLL
  // isn't known by its permutation); a new case keeps the picture it was
  // learned from.
  const CaseProgress *cp = cardOf(caseId);
  bool reviewed = cp && (cp->card.state == CardState::Review ||
                         cp->card.state == CardState::Relearning);
  CubieCube start = setUp(caseId, world, s->reduction || reviewed);
  current_ = Current{caseId, start, hold, isNew, practice, world};
  last_ = cardId(caseId);
  undo_.close();
  attempt_.begin(caseId, isNew, practice);
  linkNote_.clear();
  reveal_->clear();
  gradeRow_->setVisible(false);
  stage_->hideFamily();
  cubeView_->setColourMap(colourMapOf(hold));
  cube_ = start.toFacelets();
  cubeView_->setState(cube_);
  startLink(viewOfCase(*s, *g, *c, start, world)
                .value_or(CaseView{caseId, false, 0}));
  // The list doesn't name the case before it is revealed.
  clearCaseSelection();
  // A guided set walks through a case the first time it comes up.
  Phase p =
      guide_ && !practice && firstSight(caseId) ? Phase::Demo : Phase::Solving;
  attempt_.startTiming(monotonicMs(), p == Phase::Demo);
  setPhase(p);
  if (p == Phase::Demo) {
    selectInTree(caseId);
  }
}

// --- recognition reviews and the quiz ----------------------------------------

void LearnPage::beginRecognition(const std::string &caseId) {
  const AlgSet *s = set();
  const AlgCase *c = s ? s->findCase(caseId) : nullptr;
  const CaseGroup *g = s ? s->groupOf(caseId) : nullptr;
  if (!c || !g || !infoOf(caseId)) {
    return;
  }
  idleTimer_.stop();
  Hold hold = pickHold();
  Mirror world = pickWorld(caseId);
  CubieCube start = setUp(caseId, world, true);
  current_ = Current{caseId, start, hold, false, false, world};
  last_ = cardId(caseId);
  undo_.close();
  attempt_.begin(caseId, false, false);
  reveal_->clear();
  gradeRow_->setVisible(false);
  stage_->hideFamily();
  stage_->hideQuiz();
  cubeView_->setColourMap(colourMapOf(hold));
  cube_ = start.toFacelets();
  cubeView_->setState(cube_);
  startLink(viewOfCase(*s, *g, *c, start, world)
                .value_or(CaseView{caseId, false, 0}));
  guide_.reset();
  clearCaseSelection();
  attempt_.startTiming(monotonicMs());
  setPhase(Phase::Recognise);
}

void LearnPage::recalled(bool knew) {
  if (phase() != Phase::Recognise || !current_) {
    return;
  }
  attempt_.recalled(knew, monotonicMs(), unixNow(), grading());
  setPhase(Phase::Recalled);
  showReveal();
  selectInTree(current_->caseId);
  showGrade();
}

void LearnPage::gradeRecognition(Grade grade) {
  if (phase() != Phase::Recalled || !current_ ||
      attempt_.gradedCard().empty()) {
    return;
  }
  attempt_.gradeRecognition(grade, unixNow(), grading());
  saveProgress();
  updateCardRows(current_->caseId);
  nextCase();
}

void LearnPage::nextQuiz() {
  current_.reset();
  attempt_.clear();
  guide_.reset();
  showGuide();
  reveal_->clear();
  gradeRow_->setVisible(false);
  stage_->hideFamily();
  const AlgSet *s = set();
  std::vector<std::string> ticked =
      s ? tickedEligible() : std::vector<std::string>{};
  // Cases already started are asked first.
  std::vector<std::string> started;
  for (const auto &id : ticked) {
    if (statusOf(cardOf(id)) != CaseStatus::Unstarted) {
      started.push_back(id);
    }
  }
  const std::vector<std::string> &from = started.size() >= 2 ? started : ticked;
  if (from.size() < 2) {
    stage_->hideQuiz();
    cube_ = FaceletCube();
    cubeView_->setState(cube_);
    say(tr("The quiz needs two or more ticked cases with algs (cases you "
           "have started come first)."));
    setPhase(Phase::NoCase);
    return;
  }
  std::vector<std::string> candidates;
  for (const auto &id : from) {
    if (id != last_) {
      candidates.push_back(id);
    }
  }
  std::uniform_int_distribution<std::size_t> pickOne(0, candidates.size() - 1);
  std::string id = candidates[pickOne(rng_)];
  // The others from its group, the look-alikes, else from the rest.
  const CaseGroup *g = s->groupOf(id);
  std::vector<std::string> others;
  for (const auto &t : ticked) {
    if (t != id && s->groupOf(t) == g) {
      others.push_back(t);
    }
  }
  if (others.empty()) {
    for (const auto &t : from) {
      if (t != id) {
        others.push_back(t);
      }
    }
  }
  std::shuffle(others.begin(), others.end(), rng_);
  if (others.size() > kQuizChoices - 1) {
    others.resize(kQuizChoices - 1);
  }
  others.push_back(id);
  std::shuffle(others.begin(), others.end(), rng_);
  quizChoices_ = others;
  quizAnswer_ = static_cast<std::size_t>(
      std::find(others.begin(), others.end(), id) - others.begin());

  Hold hold = pickHold();
  CubieCube start = setUp(id, Mirror{}, true);
  current_ = Current{id, start, hold, false, true, Mirror{}};
  last_ = id;
  cubeView_->setColourMap(colourMapOf(hold));
  CubeLook look = cubeView_->look();
  look.yaw = yawFor(id, Mirror{});
  cubeView_->finishAnimations();
  cubeView_->setLook(look);
  cube_ = start.toFacelets();
  cubeView_->setState(cube_);

  QStringList names;
  for (const auto &choice : quizChoices_) {
    const AlgCase *c = s->findCase(choice);
    names << (c ? qs(c->name) : QString());
  }
  stage_->setQuizChoices(names);
  clearCaseSelection();
  quizShownAtMs_ = monotonicMs();
  session_.setQuizChoices(quizChoices_.size());
  setPhase(Phase::Quiz);
}

void LearnPage::answerQuiz(std::size_t choice) {
  if (phase() != Phase::Quiz || !current_ || choice >= quizChoices_.size()) {
    return;
  }
  std::int64_t ms = monotonicMs() - quizShownAtMs_;
  bool right = choice == quizAnswer_;
  session_.answered(right, ms);
  stage_->showQuizAnswer(quizAnswer_, choice);
  reveal_->setText(algsHtml(current_->caseId, current_->start));
  selectInTree(current_->caseId);
  setPhase(Phase::QuizDone);
  const AlgCase *c = set() ? set()->findCase(current_->caseId) : nullptr;
  say(right ? tr("Right, in %1 s.").arg(secondsText(static_cast<double>(ms)))
            : tr("It was %1.").arg(c ? qs(c->name) : QString()),
      {{tr("Space"), tr("another one")}});
  statusLabel_->setStyleSheet(right ? QString() : "color: " + kLearning.name());
  updateHeader();
}

// --- undo --------------------------------------------------------------------

void LearnPage::undoLast() {
  std::optional<LearnUndo> popped = undo_.pop();
  if (!popped) {
    emit message(tr("Nothing to take back"));
    return;
  }
  LearnUndo u = std::move(*popped);
  if (!library_.find(u.setId)) {
    emit message(tr("The set of that grade is gone"));
    return;
  }
  takeBack(library_.progress(u.setId), u);
  if (!library_.saveProgress(u.setId)) {
    emit message(tr("Could not save progress for %1").arg(qs(u.setId)));
  }
  if (u.setId != setId_) {
    if (!acrossSets()) {
      emit message(tr("Took back a grade in another set"));
      return;
    }
    showSet(u.setId);
    adoptSet();
  }
  updateAllRows();
  const AlgCase *c = set() ? set()->findCase(u.caseId) : nullptr;
  emit message(
      tr("Took back the grade of %1").arg(c ? qs(c->name) : QString()));
  if (context_ || !eligible(u.caseId)) {
    if (phase() == Phase::NoCase) {
      nextCase();
    }
    return;
  }
  // The case comes up again to be done over.
  if (u.recognition) {
    beginRecognition(u.caseId);
  } else {
    begin(u.caseId, u.isNew);
  }
}

bool LearnPage::chained() {
  const AlgSet *s = set();
  return s && s->reduction;
}

std::string LearnPage::linkCase() const {
  return attempt_.hasLinks() ? attempt_.lastLink()->view.caseId
         : current_          ? current_->caseId
                             : std::string();
}

LearnGrading LearnPage::grading() {
  LearnGrading g;
  g.progress = &progress();
  g.undo = &undo_;
  g.srs = &settings_.srs;
  g.rng = &rng_;
  g.setId = setId_;
  g.chained = chained();
  g.dayEnd = localDayEnd();
  g.cardId = [this](const std::string &id) { return cardId(id); };
  g.stepsFrom = [this](const std::string &id) { return stepsFrom(id); };
  return g;
}

PieceMask LearnPage::linkFree() {
  const AlgSet *s = set();
  const CaseGroup *g = s ? s->groupOf(linkCase()) : nullptr;
  if (!g || !current_) {
    return {};
  }
  return mirrorMask(s->specFor(*g).free, current_->world);
}

void LearnPage::startLink(const CaseView &view) {
  attempt_.startLink(
      view, cube_,
      CubieCube::fromFacelets(oriented(cube_)).value_or(current_->start));
  const std::size_t links = attempt_.links().size();
  if (links > 1) {
    reveal_->clear(); // a hint for the last case isn't this one's
  }
  // In 3D, a mirrored case is looked at from its slot's side (from the
  // front, as a back slot is done). A case whose algs are done as
  // written, such as an OCLL a mirrored step stops at, is looked at from
  // the group's own corner (the front right), as its algs are.
  CubeLook look = cubeView_->look();
  double yaw = yawFor(view.caseId, view.mirrored ? current_->world : Mirror{});
  viewMoved_ = links > 1 && look.yaw != yaw && !view.mirrored;
  if (look.yaw != yaw) {
    look.yaw = yaw;
    cubeView_->finishAnimations();
    cubeView_->setLook(look);
  }
  guide_.reset();
  refreshLink();
  // A case passed on the way that was never learned has its step shown.
  if (links > 1 && guide_ && firstSight(view.caseId)) {
    guide_->follow = true;
  }
}

void LearnPage::refreshLink() {
  const AlgSet *s = set();
  const LearnLink *link = attempt_.lastLink();
  if (!link || !s || !current_) {
    return;
  }
  const AlgCase *c = s->findCase(link->view.caseId);
  const CaseGroup *g = s->groupOf(link->view.caseId);
  const CaseInfo *info = infoOf(link->view.caseId);
  std::vector<ViewAlg> algs;
  if (c && g && info) {
    algs = algsFor(*s, *g, *c, info->analysis, link->view, link->state,
                   current_->world);
  }
  attempt_.setLinkAlgs(std::move(algs));
  bool follow = guide_ && guide_->follow;
  buildGuide();
  if (guide_ && follow) {
    guide_->follow = true;
    trackGuide();
  }
}

bool LearnPage::guided() {
  const AlgSet *s = set();
  return s && s->guided;
}

bool LearnPage::firstSight(const std::string &caseId) {
  const CaseProgress *cp = cardOf(caseId);
  return !cp || cp->card.introduced == 0;
}

bool LearnPage::inLearningSteps(const std::string &caseId) {
  const CaseProgress *cp = cardOf(caseId);
  return cp && cp->card.introduced != 0 &&
         (cp->card.state == CardState::Learning ||
          cp->card.state == CardState::Relearning);
}

void LearnPage::buildGuide() {
  guide_.reset();
  const LearnLink *link = attempt_.lastLink();
  if (!current_ || !guided() || !link || link->algs.empty()) {
    return;
  }
  AlgGuide guide(parseAlgOrEmpty(link->algs.front().text), link->start);
  if (!guide.empty()) {
    guide_ = std::move(guide);
  }
}

void LearnPage::stepDemo(int delta) {
  if (!guide_ || phase() != Phase::Demo) {
    return;
  }
  for (Move m : guide_->step(delta)) {
    cube_.apply(m);
    cubeView_->animateMove(m);
  }
  showGuide();
}

void LearnPage::startFollowing() {
  if (!guide_) {
    setPhase(Phase::Solving);
    return;
  }
  resetCube();
  attempt_.restartCase();
  guide_->restart();
  guide_->follow = true;
  setPhase(Phase::Solving);
}

void LearnPage::trackGuide() {
  if (guide_) {
    guide_->track(cube_);
  }
}

void LearnPage::showGuide() {
  bool shown =
      guide_ && (phase() == Phase::Demo || (phase() == Phase::Solving &&
                                            (guide_->follow || guide_->next)));
  const bool solving = phase() == Phase::Solving;
  stage_->showGuide(shown ? &*guide_ : nullptr,
                    shown && solving && !guide_->follow, solving,
                    attempt_.follower().deviated());
}

void LearnPage::resetCube() {
  if (!current_) {
    return;
  }
  // While a case is being solved: back to where it came up. Afterwards:
  // back to the start of the attempt, seen as the first case was.
  bool solving = phase() == Phase::Solving || phase() == Phase::Demo;
  const std::vector<LearnLink> &links = attempt_.links();
  cube_ = solving && !links.empty() ? links.back().start
                                    : current_->start.toFacelets();
  if (!solving && !links.empty()) {
    const CaseView &first = links.front().view;
    CubeLook look = cubeView_->look();
    double yaw =
        yawFor(first.caseId, first.mirrored ? current_->world : Mirror{});
    if (look.yaw != yaw) {
      look.yaw = yaw;
      cubeView_->finishAnimations();
      cubeView_->setLook(look);
    }
  }
  cubeView_->setState(cube_);
}

void LearnPage::setPhase(Phase p) {
  session_.setPhase(p);
  statusLabel_->setStyleSheet({});
  if (p != Phase::NoCase) {
    stage_->setActions({}); // the idle screen's offers
  }
  switch (p) {
  case Phase::NoCase:
    break;
  case Phase::Context:
  case Phase::ContextDone:
    showGuide();
    gradeRow_->setVisible(false);
    stage_->hideFamily();
    showContextStatus();
    break;
  case Phase::Demo:
    showGuide();
    say(tr("New case: step through its main alg, then try it yourself with "
           "each next move shown."),
        {{tr("→ ←"), tr("step")},
         {tr("Home"), tr("back to the start")},
         {tr("Space"), tr("try it")}});
    break;
  case Phase::Solving:
    showGuide();
    showSolvingStatus();
    break;
  case Phase::Done:
    showGuide();
    if (current_ && current_->practice) {
      say({}, {{tr("Space"), tr("next case")}, {tr("Esc"), tr("practise it")}});
    } else {
      say({}, {{tr("Space"), tr("next case")},
               {tr("1-4"), tr("change the grade")},
               {tr("Esc"), tr("practise it")},
               {tr("Ctrl+Z"), tr("take the grade back")}});
    }
    break;
  case Phase::Recognise:
    showGuide();
    say(tr("Recognise the case. Rotations and U and D turns look at it."),
        {{tr("Space"), tr("I recall its alg")},
         {tr("Shift+Space"), tr("I can't")}});
    break;
  case Phase::Recalled:
    showGuide();
    say(tr("Grade yourself."), {{tr("1-4"), tr("grade and go on")},
                                {tr("Space"), tr("the grade shown")},
                                {tr("Ctrl+Z"), tr("take a grade back")}});
    break;
  case Phase::Quiz:
    showGuide();
    say(tr("Which case is this? Rotations and U and D turns look at it."),
        {{tr("1-%1").arg(quizChoices_.size()), tr("answer")}});
    break;
  case Phase::QuizDone:
    break;
  }
  updateHeader();
}

void LearnPage::showSolvingStatus() {
  std::string id = linkCase();
  bool later = attempt_.links().size() > 1;
  const AlgFollower &follower = attempt_.follower();
  bool nextMoveHint = guide_ && !guide_->follow && !guide_->next &&
                      !attempt_.hinted() && current_ && inLearningSteps(id);
  const QList<KeyHint> keys = {
      {tr("Tab"), nextMoveHint ? tr("next move") : tr("show the algs")},
      {tr("Shift+Space"), tr("give up")},
      {tr("Esc"), tr("reset")}};
  QString note = later && !linkNote_.isEmpty() ? linkNote_ + " " : QString();
  if (follower.deviated() && !chained()) {
    bool practice = current_ && current_->practice;
    QString what =
        follower.startedOver()
            ? (practice ? tr("Started over.")
                        : tr("Started over, so the case will be graded "
                             "Again."))
            : (practice ? tr("That isn't one of the algs.")
                        : tr("That isn't one of the algs, so the case will "
                             "be graded Again."));
    say(what, keys, true);
    return;
  }
  QString what;
  if (later) {
    what = guide_ && guide_->follow
               ? tr("You haven't learned this case yet: its step is shown.")
               : tr("Carry on from this case.");
    if (viewMoved_) {
      what += " " + tr("Its alg is done as written, so the cube is seen "
                       "from the front right again.");
    }
  } else if (current_ && current_->practice) {
    what = tr("Practice: the case isn't due, so it won't be graded.");
  } else if (guide_ && guide_->follow) {
    what = tr("Do the alg, one move at a time (not graded: the case comes "
              "back soon to do from memory).");
  } else if (current_ && current_->isNew) {
    what = tr("New case.");
  } else {
    what = chained() ? tr("Do the next step from memory.")
                     : tr("Solve the case from memory.");
  }
  say(note + what, keys);
}

void LearnPage::say(const QString &message, const QList<KeyHint> &keys,
                    bool warn) {
  statusLabel_->setText(message);
  statusLabel_->setStyleSheet(
      warn ? "color: " + statusColour(Status::Warning).name() : QString());
  stage_->setKeys(keys);
}

bool LearnPage::handleKey(QKeyEvent *e, bool press) {
  LearnSession::Step step = session_.key(sessionKey(e, press));
  perform(step);
  return step.handled;
}

LearnSession::KeyEvent LearnPage::sessionKey(const QKeyEvent *e,
                                             bool press) const {
  using Key = LearnSession::Key;
  LearnSession::KeyEvent k;
  k.press = press;
  k.shift = e->modifiers().testFlag(Qt::ShiftModifier);
  const bool ctrl = e->modifiers().testFlag(Qt::ControlModifier);
  const int key = e->key();
  switch (key) {
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
  case Qt::Key_Escape:
    k.key = Key::Escape;
    break;
  case Qt::Key_Left:
    k.key = Key::Left;
    break;
  case Qt::Key_Right:
    k.key = Key::Right;
    break;
  case Qt::Key_Home:
    k.key = Key::Home;
    break;
  case Qt::Key_Z:
    k.key = ctrl ? Key::Undo : Key::Other;
    break;
  default:
    if (key >= Qt::Key_1 && key <= Qt::Key_9) {
      k.key = Key::Digit;
      k.digit = key - Qt::Key_0;
    }
    break;
  }
  k.move = settings_.keymap.lookup(e);
  return k;
}

void LearnPage::perform(const LearnSession::Step &step) {
  using A = LearnSession::Action;
  switch (step.action) {
  case A::None:
    break;
  case A::Undo:
    undoLast();
    break;
  case A::ToggleList:
    toggleList();
    break;
  case A::NextEarly:
    // Checks again, and takes a learning case early if nothing is due.
    nextCase(true);
    break;
  case A::NextCase:
    nextCase();
    break;
  case A::Turn:
    turn(*step.move);
    break;
  case A::Look:
    if (isRotation(step.move->type) || udTurn(cube_, *step.move)) {
      cube_.apply(*step.move);
      cubeView_->animateMove(*step.move);
    }
    break;
  case A::ResetCube:
    resetCube();
    break;
  case A::DemoStep:
    stepDemo(step.step);
    break;
  case A::DemoRestart:
    resetCube();
    if (guide_) {
      guide_->restart();
    }
    showGuide();
    break;
  case A::StartFollowing:
    startFollowing();
    break;
  case A::Hint:
    solvingHint();
    break;
  case A::GiveUp:
    attempt_.giveUp();
    endLink(false, std::nullopt);
    finish();
    resetCube(); // so the alg can be tried
    break;
  case A::ResetCase:
    resetCube();
    attempt_.restartCase();
    if (guide_) {
      guide_->restart();
      showGuide();
    }
    showSolvingStatus();
    break;
  case A::Regrade:
    regrade(*step.grade);
    break;
  case A::Recalled:
    recalled(step.knew);
    break;
  case A::GradeRecognition:
    gradeRecognition(step.grade.value_or(attempt_.grade()));
    break;
  case A::Answer:
    answerQuiz(step.choice);
    break;
  case A::NextQuiz:
    nextQuiz();
    break;
  case A::ContextHint:
    // The set's cheapest order as a hint; nothing is graded anyway.
    showContextResult();
    break;
  case A::ContextGiveUp:
    finishContext(true);
    break;
  case A::ContextReset:
    resetContext();
    break;
  case A::ContextLeave:
    leaveContext();
    break;
  case A::ContextNext:
    startContext();
    break;
  }
}

void LearnPage::solvingHint() {
  if (guide_ && !guide_->follow && !guide_->next && !attempt_.hinted() &&
      current_ && inLearningSteps(linkCase())) {
    // In the learning steps the first hint is the next move only.
    attempt_.nextMoveHint();
    guide_->next = true;
    trackGuide();
    showGuide();
    showSolvingStatus();
    return;
  }
  attempt_.hint();
  showReveal();
  if (attempt_.links().size() == 1 && current_) {
    selectInTree(current_->caseId);
  }
}

void LearnPage::turn(Move m) {
  cube_.apply(m);
  cubeView_->animateMove(m);
  if (phase() == Phase::Context && context_) {
    if (context_->turned(m, cube_)) {
      finishContext(false);
    }
    return;
  }
  if (phase() == Phase::ContextDone) {
    return; // turning freely after the pairs
  }
  if (phase() != Phase::Solving || !current_) {
    return; // untimed practice after the case
  }
  bool wasOff = attempt_.follower().deviated();
  attempt_.turned(m, monotonicMs());
  if (guide_) {
    trackGuide();
    showGuide();
  }
  auto c = CubieCube::fromFacelets(oriented(cube_));
  const AlgSet *s = set();
  if (!c || !s) {
    return;
  }
  // The cube itself must reach the step's end state: a case that leaves
  // the last layer to be lined up (PLL, say) needs its last U turn too,
  // while one that leaves the last-layer permutation free (OLL) doesn't.
  if (withinMask(*c, linkFree())) {
    endLink(true, std::nullopt);
    finish();
    turnUpright();
    return;
  }
  // A step of a reduction set is over when the cube shows a case again:
  // at the end of one of the case's algs, or wherever other moves get to
  // (an alg may pass through other cases on its way).
  const AlgFollower &follower = attempt_.follower();
  if (chained() && follower.begun() &&
      (follower.finished() || follower.leftAt())) {
    if (auto next = seenCase(*s, *c, current_->world);
        next && eligible(next->caseId)) {
      endLink(false, next);
      startLink(*next);
      showGuide();
      showSolvingStatus();
      return;
    }
  }
  if (follower.deviated() != wasOff) {
    showSolvingStatus();
  }
}

void LearnPage::turnUpright() {
  FaceletCube upright = cube_;
  for (Move m : upright.normalizeOrientation()) {
    cube_.apply(m);
    cubeView_->animateMove(m);
  }
}

void LearnPage::endLink(bool solved, const std::optional<CaseView> &next) {
  if (!attempt_.hasLinks() || !current_ || !set()) {
    return;
  }
  const std::string id = attempt_.lastLink()->view.caseId;
  bool changed = attempt_.endLink(
      solved, next ? std::optional<std::string>(next->caseId) : std::nullopt,
      guide_ && guide_->follow, unixNow(), monotonicMs(), grading());
  if (chained()) {
    const LearnLink &link = *attempt_.lastLink();
    const int left = link.stepsLeft;
    linkNote_ =
        link.gaveUp ? QString()
        : link.closer
            ? (left == 1 ? tr("Closer: 1 step left.")
                         : tr("Closer: %1 steps left.").arg(left))
            : (left == 1
                   ? tr("That didn't get closer: 1 step left.")
                   : tr("That didn't get closer: %1 steps left.").arg(left));
  }
  if (changed) {
    saveProgress();
    updateCardRows(id);
  }
}

void LearnPage::finish() {
  if (!current_ || !set()) {
    return;
  }
  setPhase(Phase::Done);
  showReveal();
  showGrade();
  selectInTree(current_->caseId);
}

void LearnPage::regrade(Grade grade) {
  if (phase() != Phase::Done || !current_ ||
      !attempt_.regrade(grade, unixNow(), grading())) {
    showGrade();
    return;
  }
  saveProgress();
  showGrade();
  if (chained()) {
    showReveal(); // the list's marks follow the grade
  }
  updateCardRows(current_->caseId);
}

void LearnPage::startContext() {
  AlgSet *s = set();
  if (!s || !pairSet_) {
    return;
  }
  ContextPractice practice(
      *s, slotsSpin_->value(), rng_, [this](const std::string &id) {
        return statusOf(cardOf(id)) != CaseStatus::Unstarted;
      });
  idleTimer_.stop();
  current_.reset();
  guide_.reset();
  attempt_.clear();
  stage_->hideQuiz();
  gradeRow_->setVisible(false);
  practice.setHold(pickHold());
  context_ = std::move(practice);
  contextButton_->setText(tr("Back to learning"));
  learnNextButton_->setEnabled(false);
  cubeView_->setColourMap(colourMapOf(context_->hold()));
  CubeLook look = cubeView_->look();
  look.yaw = CubeLook{}.yaw;
  cubeView_->setLook(look);
  reveal_->clear();
  clearCaseSelection();
  resetContext();
}

void LearnPage::resetContext() {
  if (!context_) {
    return;
  }
  context_->reset();
  cube_ = context_->start().toFacelets();
  cubeView_->setState(cube_);
  reveal_->clear();
  setPhase(Phase::Context);
}

void LearnPage::finishContext(bool gaveUp) {
  if (!context_) {
    return;
  }
  context_->finish(gaveUp);
  setPhase(Phase::ContextDone);
  showContextResult();
}

void LearnPage::leaveContext() {
  context_.reset();
  contextButton_->setText(tr("Practise in context"));
  reveal_->clear();
  updateQueue();
  nextCase();
}

void LearnPage::showContextStatus() {
  if (!context_) {
    return;
  }
  statusLabel_->setStyleSheet({});
  bool done = phase() != Phase::Context;
  say(context_->statusText(done),
      done ? QList<KeyHint>{{tr("Space"), tr("another one")},
                            {tr("Esc"), tr("this one again")},
                            {tr("Backspace"), tr("back to learning")}}
           : QList<KeyHint>{{tr("Tab"), tr("cheapest order")},
                            {tr("Esc"), tr("start again")},
                            {tr("Shift+Space"), tr("give up")},
                            {tr("Backspace"), tr("back to learning")}});
}

void LearnPage::showContextResult() {
  const AlgSet *s = set();
  if (!context_ || !s) {
    return;
  }
  reveal_->setText(
      context_->resultHtml(*s, phase() == Phase::ContextDone,
                           palette().color(QPalette::PlaceholderText).name()));
}

void LearnPage::showFamily() {
  const std::vector<std::string> *members =
      current_ ? families_.familyFor(current_->caseId) : nullptr;
  bool shown = byFamily() &&
               (phase() == Phase::Done || phase() == Phase::Recalled) &&
               members && members->size() > 1;
  if (!shown) {
    stage_->hideFamily();
    return;
  }
  const AlgSet *s = set();
  std::vector<CaseStage::FamilyCase> cases;
  for (const auto &id : *members) {
    const CaseInfo *info = infoOf(id);
    const AlgCase *c = s ? s->findCase(id) : nullptr;
    if (!info || !c) {
      continue;
    }
    cases.push_back({caseDiagram(id, info->state, 84,
                                 colourMapOf(settings_.neutrality.main)),
                     qs(c->name),
                     info->passing.empty() ? tr("no alg")
                                           : qs(info->mainAlg).toHtmlEscaped(),
                     id == current_->caseId});
  }
  stage_->showFamily(cases);
}

QString LearnPage::chainHtml() {
  const AlgSet *s = set();
  if (!s) {
    return {};
  }
  return linksHtml(*s, attempt_.links(), [this](const LearnLink &link) {
    switch (link.note) {
    case LearnLink::Note::None:
      break;
    case LearnLink::Note::AlreadyGraded:
      return tr("already graded in this attempt");
    case LearnLink::Note::Practice:
      return tr("practice, so not graded");
    case LearnLink::Note::NotStarted:
      return tr("not started yet, so not graded");
    case LearnLink::Note::NotDue:
      return tr("not due yet, so unchanged");
    case LearnLink::Note::Graded:
      return tr("%1 (%2) → next %3")
          .arg(tr(gradeName(link.grade)), reasonsText(link.why),
               dueText(cardOf(link.view.caseId), link.notedAt, localDayEnd()));
    }
    return QString();
  });
}

void LearnPage::showReveal() {
  showFamily();
  const AlgSet *s = set();
  if (!current_ || !s) {
    return;
  }
  if (phase() == Phase::Recalled) {
    // Every step in a set solved a step at a time.
    reveal_->setText(
        algsHtml(current_->caseId, current_->start, current_->world));
    return;
  }
  if (!attempt_.hasLinks()) {
    return;
  }
  if (phase() == Phase::Done && chained()) {
    reveal_->setText(chainHtml());
    return;
  }
  // The case being solved (a hint), or the case that was solved.
  const LearnLink &link = *attempt_.lastLink();
  QString html = viewAlgsHtml(*s, link.view, link.state, current_->world);
  const AlgFollower &follower = attempt_.follower();
  if (phase() == Phase::Done && attempt_.leftAlgs()) {
    QString moves = movesHtml(follower.moves(), follower.leftAt());
    html += "<p style='color:gray'>" +
            (follower.startedOver()
                 ? tr("Your moves after starting over: %1").arg(moves)
                 : tr("Your moves: %1").arg(moves));
    if (follower.leftAt()) {
      html += "<br>" + tr("The marked move is where you left the algs.");
    }
    html += "</p>";
  }
  reveal_->setText(html);
}

QString LearnPage::whyText() const {
  if (attempt_.byHand() || attempt_.grade() != attempt_.autoGrade()) {
    return tr("graded by hand");
  }
  if (phase() == Phase::Recalled) {
    std::int64_t ms = attempt_.recognitionMs();
    return ms > 0 ? tr("recalled in %1 s")
                        .arg(secondsText(static_cast<double>(ms)))
                  : tr("not recalled");
  }
  return reasonsText(attempt_.why());
}

void LearnPage::showGrade() {
  bool shown =
      (phase() == Phase::Done || phase() == Phase::Recalled) && current_;
  if (!shown) {
    gradeRow_->setVisible(false);
    return;
  }
  gradeRow_->setVisible(true);
  const Grade chosen = attempt_.grade();
  bool recalled = phase() == Phase::Recalled;
  bool buttons = recalled || (!current_->practice &&
                              !attempt_.gradeReplaced() && attempt_.graded());
  std::int64_t when = recalled ? unixNow() : attempt_.gradeTime();
  for (int g = 0; g < 4; ++g) {
    auto grade = static_cast<Grade>(g + 1);
    QPushButton *b = stage_->gradeButtons[static_cast<std::size_t>(g)];
    b->setVisible(buttons);
    // What the grade would give, from the card as it was before.
    b->setText(QString("%1 %2 · %3")
                   .arg(g + 1)
                   .arg(tr(gradeName(grade)))
                   .arg(intervalText(nextInterval(attempt_.cardBefore(), grade,
                                                  when, settings_.srs))));
    b->setChecked(!attempt_.walkedThrough() && chosen == grade);
  }
  const QString grey = "#6b6f78";
  auto outcome = [this](const QString &text, const QString &colour,
                        const QString &detail) {
    stage_->showOutcome(text, colour, detail);
  };
  QString why = whyText();
  const CaseProgress *card =
      progressOf(attempt_.gradedCard().empty() ? cardId(current_->caseId)
                                               : attempt_.gradedCard());
  QString next = dueText(card, unixNow(), localDayEnd());
  if (recalled) {
    outcome(tr(gradeName(chosen)), gradeColour(chosen),
            tr("%1. Grade yourself:").arg(why));
    return;
  }
  if (attempt_.gradeReplaced()) {
    outcome(tr("Changed"), grey, tr("from the case list → next %1").arg(next));
    return;
  }
  if (current_->practice) {
    outcome(chosen == Grade::Again ? tr("Missed") : tr("Well done"),
            chosen == Grade::Again ? gradeColour(Grade::Again) : grey,
            tr("Practice (%1), not graded; next review %2.").arg(why, next));
    return;
  }
  if (attempt_.walkedThrough()) {
    outcome(tr("Walked through"), "#5a6b80",
            tr("Not graded: it comes back in %1 to do from "
               "memory. 1-4 grade it now instead.")
                .arg(next));
    return;
  }
  if (!attempt_.graded()) {
    outcome(tr(gradeName(chosen)), grey, tr("(%1), not graded").arg(why));
    return;
  }
  outcome(tr(gradeName(chosen)), gradeColour(chosen),
          tr("%1 → next %2").arg(why, next));
}

} // namespace vcube
