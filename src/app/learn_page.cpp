#include "app/learn_page.hpp"

#include "app/widgets.hpp"

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace vcube {

namespace {

const char *gradeName(Grade g) {
  switch (g) {
  case Grade::Again:
    return "Again";
  case Grade::Hard:
    return "Hard";
  case Grade::Good:
    return "Good";
  case Grade::Easy:
    return "Easy";
  }
  return "";
}

// The moves as done, as rich text, with two quarter turns of a layer the
// same way written as a half turn. The move at `mark` (an index into
// `moves`) is highlighted and kept on its own.
QString movesHtml(const Alg &moves, std::optional<std::size_t> mark) {
  struct Done {
    Move move;
    bool marked;
  };
  std::vector<Done> merged;
  for (std::size_t i = 0; i < moves.size(); ++i) {
    Move m = moves[i];
    bool marked = mark == i;
    if (!merged.empty() && !marked && !merged.back().marked &&
        merged.back().move == m && m.amount != 2) {
      merged.back().move.amount = 2;
    } else {
      merged.push_back({m, marked});
    }
  }
  QStringList parts;
  for (const Done &d : merged) {
    QString text = qs(toString(d.move)).toHtmlEscaped();
    parts << (d.marked ? "<b style='color:#ff5555'><u>" + text + "</u></b>"
                       : text);
  }
  return parts.join(' ');
}

constexpr Slot kAllSlots[] = {Slot::FR, Slot::FL, Slot::BL, Slot::BR};

// Tries for an in-context state whose open pairs are all cases already
// started in Learn, before taking any.
constexpr int kContextTries = 40;

// The cube with its orientation removed, so that states reached holding
// the cube differently compare equal.
FaceletCube oriented(FaceletCube f) {
  f.normalizeOrientation();
  return f;
}

} // namespace

LearnPage::LearnPage(AppSettings &settings, AlgLibrary &library,
                     QWidget *parent)
    : CasePage(settings, library, "learn", parent) {
  learnCount_ = new QSpinBox(left_);
  learnCount_->setRange(1, 50);
  learnCount_->setValue(3);
  learnCount_->setFocusPolicy(Qt::ClickFocus);
  learnCount_->installEventFilter(this);
  connect(learnCount_, &QSpinBox::editingFinished, learnCount_,
          [this] { learnCount_->clearFocus(); });
  auto *learnButton = new QPushButton(tr("Start learning"), left_);
  learnButton->setFocusPolicy(Qt::NoFocus);
  learnButton->setToolTip(
      tr("Put the next N ticked cases you haven't started into your learn "
         "queue"));
  tickRow_->addWidget(learnCount_);
  tickRow_->addWidget(learnButton);

  // --- right: the virtual cube ---
  auto *right = new QWidget(this);
  auto *rightLayout = new QVBoxLayout(right);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  cubeView_ = new CubeView(right);
  cubeView_->setMinimumSize(260, 220);
  optionsRow_ = new QWidget(right);
  auto *options = new QHBoxLayout(optionsRow_);
  options->setContentsMargins(0, 0, 0, 0);
  options->addWidget(new QLabel(tr("View:"), optionsRow_));
  viewBox_ = createViewSelector(cubeView_, "learn", optionsRow_);
  viewBox_->setToolTip(
      tr("How the virtual cube is shown, remembered for each alg set"));
  options->addWidget(viewBox_);
  options->addStretch();
  slotsSpin_ = new QSpinBox(optionsRow_);
  slotsSpin_->setRange(1, 4);
  slotsSpin_->setValue(2);
  slotsSpin_->setSuffix(tr(" open"));
  slotsSpin_->setFocusPolicy(Qt::NoFocus);
  slotsSpin_->setToolTip(tr("How many slots are open in context practice"));
  contextButton_ = new QPushButton(tr("Practise in context"), optionsRow_);
  contextButton_->setFocusPolicy(Qt::NoFocus);
  contextButton_->setToolTip(
      tr("Solve several pairs at once on a cube with EO Cross done and some "
         "slots open, then compare your moves with this set's algs and its "
         "cheapest order. Not graded."));
  options->addWidget(slotsSpin_);
  options->addWidget(contextButton_);
  rightLayout->addWidget(optionsRow_);

  rightLayout->addWidget(cubeView_, 3);
  statusLabel_ = new QLabel(right);
  statusLabel_->setAlignment(Qt::AlignCenter);
  statusLabel_->setWordWrap(true);
  guideLabel_ = new QLabel(right);
  guideLabel_->setAlignment(Qt::AlignCenter);
  guideLabel_->setWordWrap(true);
  guideLabel_->setTextFormat(Qt::RichText);
  QFont gf = guideLabel_->font();
  gf.setPointSize(gf.pointSize() + 5);
  guideLabel_->setFont(gf);
  guideLabel_->setVisible(false);
  rightLayout->addWidget(guideLabel_);
  rightLayout->addWidget(statusLabel_);

  gradeRow_ = new QWidget(right);
  auto *gradeLayout = new QHBoxLayout(gradeRow_);
  gradeLayout->setContentsMargins(0, 0, 0, 0);
  gradeLabel_ = new QLabel(gradeRow_);
  gradeLayout->addStretch();
  gradeLayout->addWidget(gradeLabel_);
  for (int g = 0; g < 4; ++g) {
    auto grade = static_cast<Grade>(g + 1);
    auto *b = new QPushButton(
        QString("%1 %2").arg(g + 1).arg(tr(gradeName(grade))), gradeRow_);
    b->setFocusPolicy(Qt::NoFocus);
    b->setCheckable(true);
    b->setToolTip(tr("Change the grade (key %1)").arg(g + 1));
    gradeButtons_[static_cast<std::size_t>(g)] = b;
    gradeLayout->addWidget(b);
    connect(b, &QPushButton::clicked, this, [this, grade] { regrade(grade); });
  }
  gradeLayout->addStretch();
  rightLayout->addWidget(gradeRow_);

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
  familyRow_ = new QWidget(right);
  new QHBoxLayout(familyRow_);
  familyRow_->setVisible(false);
  rightLayout->addWidget(familyRow_);

  auto *split = new QSplitter(this);
  split->addWidget(left_);
  split->addWidget(right);
  split->setStretchFactor(1, 1);
  left_->setMinimumWidth(360);
  split->setSizes({440, 760});
  auto *layout = new QHBoxLayout(this);
  layout->addWidget(split);

  connect(learnButton, &QPushButton::clicked, this, &LearnPage::learnNext);
  connect(contextButton_, &QPushButton::clicked, this, [this] {
    if (context_) {
      leaveContext();
    } else {
      startContext();
    }
  });
  connect(viewBox_, &QComboBox::activated, this, [this] {
    QSettings().setValue(setSettingsKey(setId_, "learnView"),
                         viewBox_->currentData());
  });
  init();
}

QStringList LearnPage::columns() const { return {tr("Status"), tr("Due")}; }

bool LearnPage::eventFilter(QObject *watched, QEvent *event) {
  if (watched == learnCount_ && event->type() == QEvent::KeyPress) {
    int key = static_cast<QKeyEvent *>(event)->key();
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
      learnCount_->interpretText();
      learnNext();
      return true;
    }
    if (key == Qt::Key_Escape) {
      learnCount_->clearFocus();
      return true;
    }
  }
  return CasePage::eventFilter(watched, event);
}

void LearnPage::applySettings() {
  CubeLook look = cubeView_->look();
  look.scheme = settings_.scheme;
  cubeView_->setLook(look);
  cubeView_->setAnimationMs(settings_.animationMs);
  CasePage::applySettings();
}

void LearnPage::activated() {
  CasePage::activated();
  if (!current_ && !context_) {
    nextCase();
  }
}

void LearnPage::setFocusMode(bool on) {
  CasePage::setFocusMode(on);
  optionsRow_->setVisible(!on);
}

void LearnPage::deactivated() {
  CasePage::deactivated();
  cubeView_->finishAnimations();
  learnCount_->clearFocus();
}

std::vector<CasePage::CaseStatusFilter> LearnPage::statusFilters() {
  // As the Status and Due columns show them.
  auto queued = [this](const std::string &id) {
    const CaseProgress *cp = cardOf(id);
    return cp && isQueued(cp->card);
  };
  auto status = [this, queued](CaseStatus st) {
    return [this, queued, st](const std::string &id) {
      return eligible(id) && !queued(id) && statusOf(cardOf(id)) == st;
    };
  };
  return {
      {tr("New"), status(CaseStatus::Unstarted)},
      {tr("In the learn queue"), queued},
      {tr("Learning"), status(CaseStatus::Learning)},
      {tr("Learned"), status(CaseStatus::Learned)},
      {tr("Due"),
       [this, queued](const std::string &id) {
         const CaseProgress *cp = cardOf(id);
         if (!cp || queued(id) || statusOf(cp) == CaseStatus::Unstarted) {
           return false;
         }
         std::int64_t now = unixNow();
         return statusOf(cp) == CaseStatus::Learned
                    ? isDue(cp->card, now, settings_.srs, localDayEnd())
                    : cp->card.due <= now;
       }},
  };
}

void LearnPage::fillRow(QTreeWidgetItem *item, const std::string &caseId) {
  const CaseProgress *cp = cardOf(caseId);
  static const QColor learning(0xff, 0xb0, 0x30), learned(0x3d, 0xd6, 0x6b);
  QColor muted = palette().color(QPalette::PlaceholderText);
  if (!eligible(caseId)) {
    item->setText(1, tr("no alg"));
    item->setForeground(1, muted);
  } else if (cp && isQueued(cp->card)) {
    item->setText(1, tr("queued"));
    item->setForeground(1, learning);
  } else {
    switch (statusOf(cp)) {
    case CaseStatus::Unstarted:
      item->setText(1, tr("new"));
      item->setForeground(1, muted);
      break;
    case CaseStatus::Learning:
      item->setText(1, tr("learning"));
      item->setForeground(1, learning);
      break;
    case CaseStatus::Learned:
      item->setText(1, tr("learned"));
      item->setForeground(1, learned);
      break;
    }
  }
  item->setText(2, cp && !isQueued(cp->card)
                       ? dueText(cp, unixNow(), localDayEnd())
                       : QString("-"));
}

void LearnPage::updateSummary() {
  AlgSet *s = set();
  if (!s) {
    summary_->setText(tr("No alg sets. Create one in Library mode."));
    return;
  }
  int learning = 0, learned = 0, due = 0, queued = 0;
  std::int64_t now = unixNow();
  for (const auto &id : pool()) {
    const CaseProgress *cp = cardOf(id);
    if (!cp) {
      continue;
    }
    if (isQueued(cp->card)) {
      ++queued;
      continue;
    }
    CaseStatus st = statusOf(cp);
    if (st == CaseStatus::Unstarted) {
      continue; // e.g. only drilled
    }
    learning += st == CaseStatus::Learning;
    learned += st == CaseStatus::Learned;
    due += st == CaseStatus::Learned
               ? isDue(cp->card, now, settings_.srs, localDayEnd())
               : cp->card.due <= now;
  }
  QString text = tr("%1 of %2 ticked. Learning %3, learned %4, due %5, %6 "
                    "in the learn queue.")
                     .arg(tickedCount())
                     .arg(s->caseCount())
                     .arg(learning)
                     .arg(learned)
                     .arg(due)
                     .arg(queued);
  if (int excluded = excludedCount()) {
    text += " " + skippedText(excluded);
  }
  summary_->setText(text);
}

void LearnPage::extendCaseMenu(QMenu &menu, const std::string &caseId) {
  auto change = [this, caseId](auto &&apply) {
    apply(progress()[cardId(caseId)]);
    saveProgress();
    if (current_ && cardId(current_->caseId) == cardId(caseId) &&
        phase_ == Phase::Done) {
      gradeReplaced_ = true; // 1-4 would undo the change
      showGrade();
    }
    updateCardRows(caseId);
    if (phase_ == Phase::NoCase) {
      nextCase();
    }
  };
  menu.addAction(tr("Start learning"), this,
                 [this, change] {
                   change([this](CaseProgress &p) {
                     startLearning(p.card, unixNow(), settings_.srs);
                   });
                 })
      ->setEnabled(eligible(caseId));
  menu.addAction(tr("Mark as learned"), this, [this, change] {
    change([this](CaseProgress &p) {
      markLearned(p.card, unixNow(), settings_.srs);
    });
  });
  const CaseProgress *cp = cardOf(caseId);
  menu.addAction(tr("Remove from learn queue"), this,
                 [change] {
                   change([](CaseProgress &p) { resetCard(p.card); });
                 })
      ->setEnabled(cp && isQueued(cp->card));
  // Its card and Learn attempts; its drill times stay.
  menu.addAction(tr("Reset learning progress"), this, [change] {
    change([](CaseProgress &p) { resetLearning(p); });
  });
}

void LearnPage::learnNext() {
  int n = learnCount_->value();
  learnCount_->clearFocus();
  std::int64_t now = unixNow();
  int started = 0;
  for (const auto &id : pool()) {
    if (started >= n) {
      break;
    }
    if (statusOf(cardOf(id)) == CaseStatus::Unstarted) {
      startLearning(progress()[cardId(id)].card, now, settings_.srs);
      ++started;
    }
  }
  saveProgress();
  updateAllRows();
  emit message(started ? tr("Added %1 cases to your learn queue").arg(started)
                       : tr("No unstarted ticked cases with algs left"));
  if (phase_ == Phase::NoCase) {
    nextCase();
  }
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
  QSettings q;
  QString key = setSettingsKey(setId_, "learnView");
  QString name = q.value(key).toString();
  if (auto mode = CubeView::modeFromName(name)) {
    viewBox_->setCurrentIndex(viewBox_->findData(name));
    cubeView_->setMode(*mode);
  } else if (!setId_.empty()) {
    q.setValue(key, viewBox_->currentData()); // the set keeps the view in use
  }
}

void LearnPage::startSet() {
  context_.reset();
  pairSet_ = set() && isPairSet(*set());
  slotsSpin_->setVisible(pairSet_);
  contextButton_->setVisible(pairSet_);
  contextButton_->setText(tr("Practise in context"));
  applySetView();
  current_.reset();
  last_.clear();
  nextCase();
}

void LearnPage::setReloaded() {
  pairSet_ = set() && isPairSet(*set());
  slotsSpin_->setVisible(pairSet_);
  contextButton_->setVisible(pairSet_);
  if (context_) {
    if (pairSet_) {
      if (phase_ == Phase::ContextDone) {
        showContextResult();
      }
      return;
    }
    leaveContext();
    return;
  }
  if (current_ && phase_ == Phase::Demo && eligible(current_->caseId)) {
    begin(current_->caseId, current_->isNew, current_->practice);
    return;
  }
  if (current_ && !eligible(current_->caseId)) {
    current_.reset();
    nextCase();
  } else if (current_ && phase_ == Phase::Done) {
    showReveal();
  } else if (current_ && phase_ == Phase::Solving) {
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
  if (phase_ == Phase::NoCase) {
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
    begin(caseId, false, notDue(caseId));
  }
}

void LearnPage::nextCase() {
  reveal_->clear();
  familyRow_->setVisible(false);
  gradeRow_->setVisible(false);
  if (!set()) {
    current_.reset();
    statusLabel_->setText(tr("No alg set selected."));
    setPhase(Phase::NoCase);
    return;
  }
  std::vector<std::string> cases = pool();
  LearnPick pick = pickLearnCase(cases, progress(), unixNow(), localDayStart(),
                                 settings_.srs, last_);
  if (!pick.caseId) {
    current_.reset();
    cube_ = FaceletCube();
    cubeView_->setState(cube_);
    QString text;
    if (cases.empty()) {
      text = tr("Tick some cases that have algs to learn them.");
    } else if (pick.newLimitReached) {
      text = tr("All caught up for today. %1 in your learn queue wait for "
                "tomorrow; the daily limit for new cases is %2 (see "
                "Settings).")
                 .arg(pick.queued == 1 ? tr("1 case")
                                       : tr("%1 cases").arg(pick.queued))
                 .arg(settings_.srs.newPerDay);
    } else if (pick.nextDue) {
      text = tr("All caught up! Next review: %1. Start learning more cases "
                "to keep going.")
                 .arg(QDateTime::fromSecsSinceEpoch(*pick.nextDue)
                          .toString("ddd hh:mm"));
    } else {
      text = tr("Nothing to review yet. Use \"Start learning\" to put cases "
                "into your learn queue.");
    }
    statusLabel_->setText(text);
    setPhase(Phase::NoCase);
    return;
  }
  begin(pickMember(*pick.caseId), pick.isNew);
}

std::vector<std::string> LearnPage::pool() const {
  std::vector<std::string> ticked = tickedEligible();
  if (!byFamily()) {
    return ticked;
  }
  // One card per family that has a ticked case with an alg.
  std::vector<std::string> out;
  for (const auto &id : ticked) {
    std::string card = cardId(id);
    if (std::find(out.begin(), out.end(), card) == out.end()) {
      out.push_back(card);
    }
  }
  return out;
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
  Hold hold = pickHold();
  Mirror world = pickWorld(caseId);
  // From a random side, as in Drill, when the set asks for random U turns.
  // A reduction set's cases come up with the step's free pieces anywhere,
  // as they do after a step.
  CubieCube start = withRandomAufs(
      *s,
      setUpCase(info->state, s->specFor(*g).free, world, s->reduction, rng_),
      rng_);
  current_ = Current{caseId, start, hold, isNew, practice, world};
  last_ = cardId(caseId);
  gaveUp_ = false;
  leftAlgs_ = false;
  gradeReplaced_ = false;
  gradedCards_.clear();
  why_.clear();
  linkNote_.clear();
  links_.clear();
  reveal_->clear();
  gradeRow_->setVisible(false);
  familyRow_->setVisible(false);
  cubeView_->setColourMap(colourMapOf(hold));
  cube_ = start.toFacelets();
  cubeView_->setState(cube_);
  startLink(viewOfCase(*s, *g, *c, start, world)
                .value_or(CaseView{caseId, false, 0}));
  selectInTree(caseId);
  // A guided set walks through a case the first time it comes up.
  setPhase(guide_ && !practice && firstSight(caseId) ? Phase::Demo
                                                     : Phase::Solving);
}

bool LearnPage::chained() {
  const AlgSet *s = set();
  return s && s->reduction;
}

std::string LearnPage::linkCase() const {
  return !links_.empty() ? links_.back().view.caseId
         : current_      ? current_->caseId
                         : std::string();
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
  Link link;
  link.view = view;
  link.start = cube_;
  link.state =
      CubieCube::fromFacelets(oriented(cube_)).value_or(current_->start);
  links_.push_back(std::move(link));
  hinted_ = false;
  nextHinted_ = false;
  // In 3D, a mirrored case is looked at from its slot's corner, so that
  // e.g. a back-left slot can be seen. A case whose algs are done as
  // written, such as an OCLL a mirrored step stops at, is looked at from
  // the group's own corner (the front right), as its algs are.
  CubeLook look = cubeView_->look();
  double yaw = yawFor(view.caseId, view.mirrored ? current_->world : Mirror{});
  viewMoved_ = links_.size() > 1 && look.yaw != yaw && !view.mirrored;
  if (look.yaw != yaw) {
    look.yaw = yaw;
    cubeView_->finishAnimations();
    cubeView_->setLook(look);
  }
  follower_ = AlgFollower(cube_, {});
  guide_.reset();
  refreshLink();
  // A case passed on the way that was never learned has its step shown.
  if (links_.size() > 1 && guide_ && firstSight(view.caseId)) {
    guide_->follow = true;
  }
}

void LearnPage::refreshLink() {
  const AlgSet *s = set();
  if (links_.empty() || !s || !current_) {
    return;
  }
  Link &link = links_.back();
  const AlgCase *c = s->findCase(link.view.caseId);
  const CaseGroup *g = s->groupOf(link.view.caseId);
  const CaseInfo *info = infoOf(link.view.caseId);
  link.algs.clear();
  if (c && g && info) {
    link.algs = algsFor(*s, *g, *c, info->analysis, link.view, link.state,
                        current_->world);
  }
  std::vector<Alg> algs;
  for (const ViewAlg &alg : link.algs) {
    algs.push_back(parseAlgOrEmpty(alg.text));
  }
  follower_.setAlgs(std::move(algs));
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
  if (!current_ || !guided() || links_.empty() || links_.back().algs.empty()) {
    return;
  }
  const Link &link = links_.back();
  Guide guide;
  guide.moves = parseAlgOrEmpty(link.algs.front().text);
  if (guide.moves.empty()) {
    return;
  }
  FaceletCube f = link.start;
  guide.states.push_back(oriented(f));
  for (Move m : guide.moves) {
    guide.tokens << qs(toString(m));
    f.apply(m);
    guide.states.push_back(oriented(f));
  }
  guide_ = std::move(guide);
}

void LearnPage::stepDemo(int delta) {
  if (!guide_ || phase_ != Phase::Demo) {
    return;
  }
  Guide &g = *guide_;
  if (delta > 0 && g.at < g.moves.size()) {
    Move m = g.moves[g.at++];
    cube_.apply(m);
    cubeView_->animateMove(m);
  } else if (delta < 0 && g.at > 0) {
    for (Move m : invert(Alg{g.moves[--g.at]})) {
      cube_.apply(m);
      cubeView_->animateMove(m);
    }
  }
  showGuide();
}

void LearnPage::startFollowing() {
  if (!guide_) {
    setPhase(Phase::Solving);
    return;
  }
  resetCube();
  follower_.restart();
  guide_->at = 0;
  guide_->matched = true;
  guide_->follow = true;
  setPhase(Phase::Solving);
}

void LearnPage::trackGuide() {
  if (!guide_) {
    return;
  }
  Guide &g = *guide_;
  FaceletCube now = oriented(cube_);
  // Forward from where it was first, so that a state the alg passes twice
  // counts as the later one.
  std::optional<std::size_t> found;
  for (std::size_t k = g.at; k < g.states.size() && !found; ++k) {
    if (g.states[k] == now) {
      found = k;
    }
  }
  for (std::size_t k = 0; k < g.at && !found; ++k) {
    if (g.states[k] == now) {
      found = k;
    }
  }
  g.matched = found.has_value();
  if (found) {
    g.at = *found;
  }
}

void LearnPage::showGuide() {
  bool shown = guide_ &&
               (phase_ == Phase::Demo ||
                (phase_ == Phase::Solving && (guide_->follow || guide_->next)));
  guideLabel_->setVisible(shown);
  if (!shown) {
    guideLabel_->clear();
    return;
  }
  const Guide &g = *guide_;
  static const QString nextStyle =
      "color:#ffb030; font-weight:bold; text-decoration:underline";
  QString muted = palette().color(QPalette::PlaceholderText).name();
  QString text;
  if (phase_ == Phase::Solving && !g.follow) {
    // A learning-step hint: only the next move.
    if (g.at >= g.moves.size()) {
      text = tr("The alg is done.");
    } else {
      text = tr("Next move: %1")
                 .arg("<span style='" + nextStyle + "'>" +
                      g.tokens[static_cast<qsizetype>(g.at)].toHtmlEscaped() +
                      "</span>");
    }
  } else {
    QStringList parts;
    for (std::size_t i = 0; i < g.moves.size(); ++i) {
      QString t = g.tokens[static_cast<qsizetype>(i)].toHtmlEscaped();
      if (i < g.at) {
        parts << "<span style='color:" + muted + "'>" + t + "</span>";
      } else if (i == g.at) {
        parts << "<span style='" + nextStyle + "'>" + t + "</span>";
      } else {
        parts << t;
      }
    }
    text = parts.join(' ');
  }
  if (phase_ == Phase::Solving && !g.matched) {
    text += "<br><span style='font-size:small; color:" + muted + "'>" +
            (follower_.deviated()
                 ? tr("That isn't the alg: Esc starts the case again.")
                 : tr("Keep going: the cube is between two moves of the "
                      "alg.")) +
            "</span>";
  }
  guideLabel_->setText(text);
}

void LearnPage::resetCube() {
  if (!current_) {
    return;
  }
  // While a case is being solved: back to where it came up. Afterwards:
  // back to the start of the attempt.
  bool solving = phase_ == Phase::Solving || phase_ == Phase::Demo;
  cube_ = solving && !links_.empty() ? links_.back().start
                                     : current_->start.toFacelets();
  cubeView_->setState(cube_);
}

void LearnPage::setPhase(Phase p) {
  phase_ = p;
  switch (p) {
  case Phase::NoCase:
    break;
  case Phase::Context:
  case Phase::ContextDone:
    showGuide();
    gradeRow_->setVisible(false);
    familyRow_->setVisible(false);
    showContextStatus();
    return;
  case Phase::Demo:
    showGuide();
    statusLabel_->setText(
        tr("New case: → and ← step through the main alg, Home goes back to "
           "the start. Space: try it yourself, with each next move shown."));
    statusLabel_->setStyleSheet({});
    return;
  case Phase::Solving:
    showGuide();
    showSolvingStatus();
    return;
  case Phase::Done:
    showGuide();
    statusLabel_->setText(
        current_ && current_->practice
            ? tr("Space/Enter: next case. Esc: reset the cube to practise.")
            : tr("Space/Enter: next case. 1-4: change the grade. Esc: reset "
                 "the cube to practise."));
    break;
  }
  statusLabel_->setStyleSheet({});
}

void LearnPage::showSolvingStatus() {
  static const QString warning = "color: #ffb030";
  std::string id = linkCase();
  bool later = links_.size() > 1;
  QString keys =
      guide_ && !guide_->follow && !guide_->next && !hinted_ && current_ &&
              inLearningSteps(id)
          ? tr("Tab shows the next move, Shift+Space gives up, Esc resets.")
          : tr("Tab shows the algs, Shift+Space gives up, Esc resets.");
  QString note = later && !linkNote_.isEmpty() ? linkNote_ + " " : QString();
  if (follower_.deviated() && !chained()) {
    statusLabel_->setText(
        (follower_.startedOver()
             ? tr("Started over, so the case will be graded Again.")
             : tr("That isn't one of the algs, so the case will be graded "
                  "Again.")) +
        " " + keys);
    statusLabel_->setStyleSheet(warning);
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
    what = tr("Do the alg, one move at a time.");
  } else if (current_ && current_->isNew) {
    what = tr("New case.");
  } else {
    what = chained() ? tr("Do the next step from memory.")
                     : tr("Solve the case from memory.");
  }
  statusLabel_->setText(note + what + " " + keys);
  statusLabel_->setStyleSheet({});
}

bool LearnPage::handleKey(QKeyEvent *e, bool press) {
  int key = e->key();
  if (e->isAutoRepeat()) {
    return key == Qt::Key_Space;
  }
  if (!press) {
    return key == Qt::Key_Space;
  }
  bool next =
      key == Qt::Key_Space || key == Qt::Key_Return || key == Qt::Key_Enter;
  switch (phase_) {
  case Phase::NoCase:
    if (next) {
      nextCase();
      return true;
    }
    return false;
  case Phase::Context:
    if (key == Qt::Key_Tab) {
      // The set's cheapest order as a hint; nothing is graded anyway.
      showContextResult();
      return true;
    }
    if (key == Qt::Key_Space) {
      if (e->modifiers().testFlag(Qt::ShiftModifier)) {
        finishContext(true);
      }
      return true;
    }
    if (key == Qt::Key_Escape) {
      resetContext();
      return true;
    }
    break;
  case Phase::ContextDone:
    if (next) {
      startContext();
      return true;
    }
    if (key == Qt::Key_Escape) {
      resetContext();
      return true;
    }
    break;
  case Phase::Demo:
    if (key == Qt::Key_Right) {
      stepDemo(1);
    } else if (key == Qt::Key_Left) {
      stepDemo(-1);
    } else if (key == Qt::Key_Home || key == Qt::Key_Escape) {
      resetCube();
      guide_->at = 0;
      showGuide();
    } else if (next) {
      startFollowing();
    } else {
      // The turning keys wait for the try, so that the demo stays in step.
      return settings_.keymap.lookup(e).has_value() || key == Qt::Key_Tab;
    }
    return true;
  case Phase::Solving:
    if (key == Qt::Key_Tab) {
      if (guide_ && !guide_->follow && !guide_->next && !hinted_ && current_ &&
          inLearningSteps(linkCase())) {
        // In the learning steps the first hint is the next move only.
        nextHinted_ = true;
        guide_->next = true;
        trackGuide();
        showGuide();
        showSolvingStatus();
        return true;
      }
      hinted_ = true;
      showReveal();
      return true;
    }
    if (key == Qt::Key_Space) {
      // Space alone goes on to the next case once this one is done, so a
      // double tap mustn't give up the next one too.
      if (e->modifiers().testFlag(Qt::ShiftModifier)) {
        gaveUp_ = true;
        endLink(false, std::nullopt);
        finish();
        resetCube(); // so the alg can be tried
      }
      return true;
    }
    if (key == Qt::Key_Escape) {
      resetCube();
      follower_.restart();
      if (guide_) {
        guide_->at = 0;
        guide_->matched = true;
        showGuide();
      }
      showSolvingStatus();
      return true;
    }
    break;
  case Phase::Done:
    if (next) {
      nextCase();
      return true;
    }
    if (key >= Qt::Key_1 && key <= Qt::Key_4) {
      regrade(static_cast<Grade>(key - Qt::Key_1 + 1));
      return true;
    }
    if (key == Qt::Key_Escape) {
      resetCube();
      return true;
    }
    break;
  }
  if (auto m = settings_.keymap.lookup(e)) {
    turn(*m);
    return true;
  }
  return false;
}

void LearnPage::turn(Move m) {
  cube_.apply(m);
  cubeView_->animateMove(m);
  if (phase_ == Phase::Context && context_) {
    context_->moves.push_back(m);
    FaceletCube f = oriented(cube_);
    auto c = CubieCube::fromFacelets(f);
    if (c && eoCrossSolved(*c) &&
        std::all_of(std::begin(kAllSlots), std::end(kAllSlots),
                    [&](Slot slot) { return slotSolved(*c, slot); })) {
      finishContext(false);
    }
    return;
  }
  if (phase_ == Phase::ContextDone) {
    return; // turning freely after the pairs
  }
  if (phase_ != Phase::Solving || !current_) {
    return; // untimed practice after the case
  }
  bool wasOff = follower_.deviated();
  follower_.move(m);
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
  if (chained() && follower_.begun() &&
      (follower_.finished() || follower_.leftAt())) {
    if (auto next = seenCase(*s, *c, current_->world);
        next && eligible(next->caseId)) {
      endLink(false, next);
      startLink(*next);
      showGuide();
      showSolvingStatus();
      return;
    }
  }
  if (follower_.deviated() != wasOff) {
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
  if (links_.empty() || !current_ || !set()) {
    return;
  }
  Link &link = links_.back();
  const std::string id = link.view.caseId;
  const bool first = links_.size() == 1;
  link.over = true;
  link.solved = solved;
  link.reached = next ? next->caseId : std::string();
  link.gaveUp = gaveUp_;
  link.moves = follower_.moves();
  link.setStep = !follower_.leftAt();
  bool wrong = false;
  if (chained()) {
    // Any step counts that leaves fewer steps to go than there were.
    int before = stepsFrom(id);
    int left = solved ? 0 : next ? stepsFrom(next->caseId) : before;
    link.closer = !gaveUp_ && left < before;
    wrong = !link.closer;
    linkNote_ =
        gaveUp_ ? QString()
        : link.closer
            ? (left == 1 ? tr("Closer: 1 step left.")
                         : tr("Closer: %1 steps left.").arg(left))
            : (left == 1
                   ? tr("That didn't get closer: 1 step left.")
                   : tr("That didn't get closer: %1 steps left.").arg(left));
  } else {
    wrong = !gaveUp_ && !follower_.onAlg();
  }
  link.grade = learnGrade(hinted_, gaveUp_, wrong);
  if (link.grade == Grade::Good && nextHinted_) {
    link.grade = Grade::Hard;
  }
  QString why = gaveUp_                    ? tr("gave up")
                : hinted_                  ? tr("used a hint")
                : nextHinted_              ? tr("used the next-move hint")
                : guide_ && guide_->follow ? tr("walked through")
                : !wrong
                    ? (!chained()     ? tr("solved from memory")
                       : link.setStep ? tr("the set's step")
                                      : tr("another step that gets closer"))
                : chained()               ? tr("the step didn't get closer")
                : follower_.startedOver() ? tr("started over")
                                          : tr("left the algs");
  std::int64_t now = unixNow();
  const CaseProgress *card = cardOf(id);
  bool record = false;
  if (first) {
    leftAlgs_ = wrong && !chained();
    grade_ = link.grade;
    autoGrade_ = link.grade;
    why_ = why;
    record = !current_->practice;
  } else if (std::find(gradedCards_.begin(), gradedCards_.end(), cardId(id)) !=
             gradedCards_.end()) {
    link.note = tr("already graded in this attempt");
  } else if (current_->practice) {
    link.note = tr("practice, so not graded");
  } else if (!card || card->card.introduced == 0) {
    link.note = tr("not started yet, so not graded");
  } else if (isDue(card->card, now, settings_.srs, localDayEnd()) ||
             link.grade == Grade::Again) {
    // A case passed on the way is graded when it is due, and counts as
    // forgotten whenever its step is missed.
    record = true;
  } else {
    link.note = tr("not due yet, so unchanged");
  }
  if (!record) {
    return;
  }
  CaseProgress &cp = progress()[cardId(id)];
  if (first) {
    cardBefore_ = cp.card;
    attemptTime_ = now;
  }
  gradedCards_.push_back(cardId(id));
  cp.record({now, 0, link.grade == Grade::Again, AttemptMode::Learn});
  review(cp.card, link.grade, now, settings_.srs);
  saveProgress();
  if (!first) {
    link.note = tr("%1 (%2) → next %3")
                    .arg(tr(gradeName(link.grade)), why,
                         dueText(cardOf(id), now, localDayEnd()));
  }
  updateCardRows(id);
}

void LearnPage::finish() {
  if (!current_ || !set()) {
    return;
  }
  setPhase(Phase::Done);
  showReveal();
  showGrade();
}

void LearnPage::regrade(Grade grade) {
  if (phase_ != Phase::Done || !current_ || current_->practice ||
      gradeReplaced_) {
    showGrade();
    return;
  }
  grade_ = grade;
  if (!links_.empty()) {
    links_.front().grade = grade;
  }
  CaseProgress &cp = progress()[cardId(current_->caseId)];
  cp.card = cardBefore_;
  for (auto a = cp.attempts.rbegin(); a != cp.attempts.rend(); ++a) {
    if (a->mode == AttemptMode::Learn && a->timestamp == attemptTime_) {
      a->failed = grade == Grade::Again;
      break;
    }
  }
  review(cp.card, grade_, unixNow(), settings_.srs);
  saveProgress();
  showGrade();
  updateCardRows(current_->caseId);
}

void LearnPage::startContext() {
  AlgSet *s = set();
  if (!s || !pairSet_) {
    return;
  }
  int count = slotsSpin_->value();
  CubieCube state = randomPairState(count, rng_);
  for (int i = 0; i < kContextTries; ++i) {
    CubieCube candidate = i == 0 ? state : randomPairState(count, rng_);
    std::vector<PairOption> options = pairOptions(candidate, s);
    bool started = std::all_of(
        options.begin(), options.end(), [this](const PairOption &o) {
          return !o.caseId.empty() &&
                 statusOf(cardOf(o.caseId)) != CaseStatus::Unstarted;
        });
    if (started) {
      state = candidate;
      break;
    }
  }
  current_.reset();
  guide_.reset();
  context_ = Context{state, pickHold(), {}, false};
  contextButton_->setText(tr("Back to learning"));
  cubeView_->setColourMap(colourMapOf(context_->hold));
  CubeLook look = cubeView_->look();
  look.yaw = CubeLook{}.yaw;
  cubeView_->setLook(look);
  reveal_->clear();
  resetContext();
}

void LearnPage::resetContext() {
  if (!context_) {
    return;
  }
  context_->moves.clear();
  context_->gaveUp = false;
  cube_ = context_->start.toFacelets();
  cubeView_->setState(cube_);
  reveal_->clear();
  setPhase(Phase::Context);
}

void LearnPage::finishContext(bool gaveUp) {
  if (!context_) {
    return;
  }
  context_->gaveUp = gaveUp;
  setPhase(Phase::ContextDone);
  showContextResult();
}

void LearnPage::leaveContext() {
  context_.reset();
  contextButton_->setText(tr("Practise in context"));
  reveal_->clear();
  nextCase();
}

void LearnPage::showContextStatus() {
  if (!context_) {
    return;
  }
  int open = 0;
  for (Slot slot : kAllSlots) {
    open += slotSolved(context_->start, slot) ? 0 : 1;
  }
  statusLabel_->setStyleSheet({});
  if (phase_ == Phase::Context) {
    statusLabel_->setText(
        (open == 1 ? tr("In context: solve the open pair.")
                   : tr("In context: solve the %1 open pairs in any order.")
                         .arg(open)) +
        " " +
        tr("Tab shows the set's cheapest order, Esc starts again, "
           "Shift+Space gives up. Not graded."));
  } else {
    statusLabel_->setText(tr("Space/Enter: another one. Esc: try this one "
                             "again. \"Back to learning\" returns to your "
                             "cases."));
  }
}

void LearnPage::showContextResult() {
  const AlgSet *s = set();
  if (!context_ || !s) {
    return;
  }
  QString muted = palette().color(QPalette::PlaceholderText).name();
  auto grey = [&](const QString &t) {
    return "<span style='color:" + muted + "'>" + t + "</span>";
  };
  auto moves = [](int n) {
    return n == 1 ? tr("1 move") : tr("%1 moves").arg(n);
  };
  QString html;
  if (phase_ == Phase::ContextDone) {
    html +=
        context_->gaveUp
            ? "<p>" + tr("Gave up.") + "</p>"
            : "<p><b>" +
                  tr("Solved in %1.").arg(moves(htmCount(context_->moves))) +
                  "</b></p>";
    F2LBreakdown b =
        f2lBreakdown(context_->start.toFacelets(), context_->moves, s);
    for (const PairSegment &seg : b.segments) {
      QStringList slotNames;
      for (Slot slot : seg.solved) {
        slotNames << qs(std::string(slotName(slot)));
      }
      QString line =
          tr("%1: you %2").arg(slotNames.join(" + "), moves(seg.htm));
      for (const PairOption &o : seg.options) {
        if (seg.solved.size() == 1 && o.slot == seg.solved.front() &&
            !o.caseId.empty()) {
          line +=
              " · " + tr("%1: %2 %3")
                          .arg(qs(o.caseName).toHtmlEscaped(), moves(o.moves),
                               grey(qs(o.alg).toHtmlEscaped()));
        }
      }
      html += line + "<br>";
    }
  }
  PairPlan plan = cheapestPairOrder(context_->start, *s);
  if (!plan.steps.empty()) {
    html += "<p>" + tr("The set's cheapest order, %1:").arg(moves(plan.moves));
    for (const PairOption &o : plan.steps) {
      html +=
          "<br>" +
          tr("%1 (%2): %3")
              .arg(qs(std::string(slotName(o.slot))),
                   qs(o.caseName).toHtmlEscaped(), qs(o.alg).toHtmlEscaped()) +
          " " + grey(moves(o.moves));
    }
    if (!plan.complete) {
      html += "<br>" + grey(tr("Some pairs have no case in the set."));
    }
    html += "</p>";
  }
  reveal_->setText(html);
}

void LearnPage::showFamily() {
  auto *layout = static_cast<QHBoxLayout *>(familyRow_->layout());
  while (QLayoutItem *item = layout->takeAt(0)) {
    delete item->widget();
    delete item;
  }
  const std::vector<std::string> *members =
      current_ ? families_.familyFor(current_->caseId) : nullptr;
  bool shown =
      byFamily() && phase_ == Phase::Done && members && members->size() > 1;
  familyRow_->setVisible(shown);
  if (!shown) {
    return;
  }
  const AlgSet *s = set();
  QColor muted = palette().color(QPalette::PlaceholderText);
  layout->addStretch();
  for (const auto &id : *members) {
    const CaseInfo *info = infoOf(id);
    const AlgCase *c = s ? s->findCase(id) : nullptr;
    if (!info || !c) {
      continue;
    }
    bool done = id == current_->caseId;
    auto *cell = new QWidget(familyRow_);
    auto *cellLayout = new QVBoxLayout(cell);
    cellLayout->setContentsMargins(6, 0, 6, 0);
    auto *picture = new QLabel(cell);
    picture->setPixmap(caseDiagram(id, info->state, 84,
                                   colourMapOf(settings_.neutrality.main)));
    picture->setAlignment(Qt::AlignCenter);
    auto *text = new QLabel(cell);
    text->setAlignment(Qt::AlignCenter);
    text->setTextFormat(Qt::RichText);
    QString alg = info->passing.empty() ? tr("no alg")
                                        : qs(info->mainAlg).toHtmlEscaped();
    text->setText(QString("<b>%1</b><br><span style='color:%2'>%3</span>")
                      .arg(qs(c->name).toHtmlEscaped(),
                           done ? QString("#ffb030") : muted.name(), alg));
    text->setToolTip(done ? tr("The case you just did")
                          : tr("A mirror image of it"));
    cellLayout->addWidget(picture);
    cellLayout->addWidget(text);
    layout->addWidget(cell);
  }
  layout->addStretch();
}

QString LearnPage::chainHtml() {
  const AlgSet *s = set();
  if (!s || links_.empty()) {
    return {};
  }
  auto nameOf = [s](const std::string &id) {
    const AlgCase *c = s->findCase(id);
    return c ? qs(c->name).toHtmlEscaped() : QString();
  };
  auto grey = [](const QString &t) {
    return "<span style='color:gray'>" + t + "</span>";
  };
  static const QString good = "<span style='color:#3dd66b'>✓</span>";
  static const QString bad = "<span style='color:#ff5555'>✗</span>";
  const std::string &firstId = links_.front().view.caseId;
  const CaseGroup *group = s->groupOf(firstId);
  QString html =
      QString("<p style='font-size:large'><b>%1</b> <span "
              "style='color:gray'>· %2</span></p>")
          .arg(nameOf(firstId), group ? qs(group->name).toHtmlEscaped() : "");
  for (const Link &link : links_) {
    if (!link.over) {
      continue;
    }
    QString line = (link.grade == Grade::Again ? bad : good) + " ";
    if (&link != &links_.front()) {
      line += grey(nameOf(link.view.caseId) + ":") + " ";
    }
    if (link.algs.empty()) {
      line += grey(tr("no alg"));
    } else {
      const ViewAlg &alg = link.algs.front();
      line += "<b>" + qs(alg.text).toHtmlEscaped() + "</b>";
      if (!alg.reducesTo.empty()) {
        line += " " + grey("→ " + nameOf(alg.reducesTo));
      }
    }
    QStringList notes;
    if (link.gaveUp) {
      notes << tr("gave up");
    } else if (!link.setStep) {
      QString to = link.solved ? tr("done") : nameOf(link.reached);
      notes << tr("you: %1 → %2 (%3)")
                   .arg(movesHtml(link.moves, std::nullopt), to,
                        link.closer ? tr("closer") : tr("not closer"));
    }
    if (!link.note.isEmpty()) {
      notes << link.note;
    }
    if (!notes.isEmpty()) {
      line += "<br>" + grey(notes.join(" · "));
    }
    html += "<p>" + line + "</p>";
  }
  return html;
}

void LearnPage::showReveal() {
  showFamily();
  const AlgSet *s = set();
  if (!current_ || !s || links_.empty()) {
    return;
  }
  if (phase_ == Phase::Done && chained()) {
    reveal_->setText(chainHtml());
    return;
  }
  // The case being solved (a hint), or the case that was solved.
  const Link &link = links_.back();
  QString html = viewAlgsHtml(*s, link.view, link.state, current_->world);
  if (phase_ == Phase::Done && leftAlgs_) {
    QString moves = movesHtml(follower_.moves(), follower_.leftAt());
    html += "<p style='color:gray'>" +
            (follower_.startedOver()
                 ? tr("Your moves after starting over: %1").arg(moves)
                 : tr("Your moves: %1").arg(moves));
    if (follower_.leftAt()) {
      html += "<br>" + tr("The marked move is where you left the algs.");
    }
    html += "</p>";
  }
  reveal_->setText(html);
}

void LearnPage::showGrade() {
  if (phase_ != Phase::Done || !current_) {
    gradeRow_->setVisible(false);
    return;
  }
  gradeRow_->setVisible(true);
  for (QPushButton *b : gradeButtons_) {
    b->setVisible(!current_->practice && !gradeReplaced_);
  }
  QString why = grade_ != autoGrade_ ? tr("graded by hand") : why_;
  QString next = dueText(cardOf(current_->caseId), unixNow(), localDayEnd());
  if (gradeReplaced_) {
    gradeLabel_->setText(tr("Changed from the case list → next %1").arg(next));
    return;
  }
  if (current_->practice) {
    gradeLabel_->setText(
        tr("Practice: %1 (%2). Not graded; next review %3.")
            .arg(grade_ == Grade::Again ? tr("missed") : tr("well done"), why,
                 next));
    return;
  }
  gradeLabel_->setText(
      tr("%1 (%2) → next %3").arg(tr(gradeName(grade_)), why, next));
  for (int g = 0; g < 4; ++g) {
    gradeButtons_[static_cast<std::size_t>(g)]->setChecked(
        static_cast<int>(grade_) == g + 1);
  }
}

} // namespace vcube
