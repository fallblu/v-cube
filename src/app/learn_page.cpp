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

  auto *split = new QSplitter(this);
  split->addWidget(left_);
  split->addWidget(right);
  split->setStretchFactor(1, 1);
  left_->setMinimumWidth(360);
  split->setSizes({440, 760});
  auto *layout = new QHBoxLayout(this);
  layout->addWidget(split);

  connect(learnButton, &QPushButton::clicked, this, &LearnPage::learnNext);
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
  if (!current_) {
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
    const CaseProgress *cp = progressOf(id);
    return cp && isQueued(cp->card);
  };
  auto status = [this, queued](CaseStatus st) {
    return [this, queued, st](const std::string &id) {
      return eligible(id) && !queued(id) && statusOf(progressOf(id)) == st;
    };
  };
  return {
      {tr("New"), status(CaseStatus::Unstarted)},
      {tr("In the learn queue"), queued},
      {tr("Learning"), status(CaseStatus::Learning)},
      {tr("Learned"), status(CaseStatus::Learned)},
      {tr("Due"),
       [this, queued](const std::string &id) {
         const CaseProgress *cp = progressOf(id);
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
  const CaseProgress *cp = progressOf(caseId);
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
    const CaseProgress *cp = progressOf(id);
    if (!cp) {
      continue;
    }
    if (isQueued(cp->card)) {
      ++queued;
      continue;
    }
    CaseStatus st = statusOf(cp);
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
    apply(progress()[caseId]);
    saveProgress();
    if (current_ && current_->caseId == caseId && phase_ == Phase::Done) {
      gradeReplaced_ = true; // 1-4 would undo the change
      showGrade();
    }
    updateRow(caseId);
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
  menu.addAction(tr("Reset progress"), this, [change] {
    change([](CaseProgress &p) { p = CaseProgress{}; });
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
    if (statusOf(progressOf(id)) == CaseStatus::Unstarted) {
      startLearning(progress()[id].card, now, settings_.srs);
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
  const CaseProgress *cp = progressOf(caseId);
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
  applySetView();
  current_.reset();
  last_.clear();
  nextCase();
}

void LearnPage::setReloaded() {
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
    follower_.setAlgs(shownAlgs(current_->caseId));
    showSolvingStatus();
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
  begin(*pick.caseId, pick.isNew);
}

void LearnPage::begin(const std::string &caseId, bool isNew, bool practice) {
  const CaseInfo *info = infoOf(caseId);
  if (!info) {
    return;
  }
  Hold hold = pickHold();
  // From a random side, as in Drill, when the set asks for random U turns.
  CubieCube start =
      set() ? withRandomAufs(*set(), info->state, rng_) : info->state;
  current_ = Current{caseId, start, hold, isNew, practice};
  last_ = caseId;
  hinted_ = false;
  nextHinted_ = false;
  gaveUp_ = false;
  leftAlgs_ = false;
  gradeReplaced_ = false;
  follower_ = AlgFollower(start.toFacelets(), shownAlgs(caseId));
  reveal_->clear();
  gradeRow_->setVisible(false);
  cubeView_->setColourMap(colourMapOf(hold));
  // In 3D, look from the corner the group's diagrams are drawn from, so
  // that e.g. a back-left F2L slot can be seen.
  if (const CaseGroup *g = set() ? set()->groupOf(caseId) : nullptr) {
    CubeLook look = cubeView_->look();
    look.yaw = cube_painter::yawFor(set()->specFor(*g).view, CubeLook{}.yaw);
    cubeView_->setLook(look);
  }
  resetCube();
  selectInTree(caseId);
  buildGuide();
  // A guided set walks through a case the first time it comes up.
  setPhase(guide_ && !practice && firstSight(caseId) ? Phase::Demo
                                                     : Phase::Solving);
}

bool LearnPage::guided() {
  const AlgSet *s = set();
  return s && s->guided;
}

bool LearnPage::firstSight(const std::string &caseId) {
  const CaseProgress *cp = progressOf(caseId);
  return !cp || cp->card.reps == 0;
}

bool LearnPage::inLearningSteps(const std::string &caseId) {
  const CaseProgress *cp = progressOf(caseId);
  return cp && cp->card.reps > 0 &&
         (cp->card.state == CardState::Learning ||
          cp->card.state == CardState::Relearning);
}

void LearnPage::buildGuide() {
  guide_.reset();
  if (!current_ || !guided()) {
    return;
  }
  const AlgSet *s = set();
  const CaseGroup *g = s->groupOf(current_->caseId);
  const CaseInfo *info = infoOf(current_->caseId);
  if (!g || !info || info->passing.empty()) {
    return;
  }
  std::optional<AufFit> fit =
      fitAlg(current_->start, parseAlgOrEmpty(info->mainAlg),
             s->specFor(*g).free, s->aufBefore, s->aufAfter);
  if (!fit) {
    return;
  }
  Guide guide;
  guide.moves = parseAlgOrEmpty(withAufs(info->mainAlg, *fit));
  FaceletCube f = current_->start.toFacelets();
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

std::vector<Alg> LearnPage::shownAlgs(const std::string &caseId) {
  const CaseInfo *info = infoOf(caseId);
  const AlgSet *s = set();
  const AlgCase *c = s ? s->findCase(caseId) : nullptr;
  std::vector<Alg> out;
  if (!info || !c) {
    return out;
  }
  for (std::size_t i : info->passing) {
    out.push_back(parseAlgOrEmpty(out.empty() ? info->mainAlg : c->algs[i]));
  }
  return out;
}

void LearnPage::resetCube() {
  if (!current_) {
    return;
  }
  cube_ = current_->start.toFacelets();
  cubeView_->setState(cube_);
}

void LearnPage::setPhase(Phase p) {
  phase_ = p;
  switch (p) {
  case Phase::NoCase:
    break;
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
  QString keys =
      guide_ && !guide_->follow && !guide_->next && !hinted_ && current_ &&
              inLearningSteps(current_->caseId)
          ? tr("Tab shows the next move, Shift+Space gives up, Esc resets.")
          : tr("Tab shows the algs, Shift+Space gives up, Esc resets.");
  if (follower_.deviated()) {
    statusLabel_->setText(
        (follower_.startedOver()
             ? tr("Started over, so the case will be graded Again.")
             : tr("That isn't one of the algs, so the case will be graded "
                  "Again.")) +
        " " + keys);
    statusLabel_->setStyleSheet(warning);
    return;
  }
  statusLabel_->setText(
      (current_ && current_->practice
           ? tr("Practice: the case isn't due, so it won't be graded.")
       : guide_ && guide_->follow    ? tr("Do the alg, one move at a time.")
       : current_ && current_->isNew ? tr("New case.")
                                     : tr("Solve the case from memory.")) +
      " " + keys);
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
          inLearningSteps(current_->caseId)) {
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
  if (phase_ != Phase::Solving || !current_) {
    return; // untimed practice after the case
  }
  bool wasOff = follower_.deviated();
  follower_.move(m);
  if (guide_) {
    trackGuide();
    showGuide();
  }
  FaceletCube f = cube_;
  f.normalizeOrientation();
  auto c = CubieCube::fromFacelets(f);
  const AlgSet *s = set();
  const CaseGroup *g = s ? s->groupOf(current_->caseId) : nullptr;
  // The cube itself must reach the step's end state: a case that leaves
  // the last layer to be lined up (PLL, say) needs its last U turn too,
  // while one that leaves the last-layer permutation free (OLL) doesn't.
  if (c && g && withinMask(*c, s->specFor(*g).free)) {
    finish();
    turnUpright();
  } else if (follower_.deviated() != wasOff) {
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

void LearnPage::finish() {
  if (!current_ || !set()) {
    return;
  }
  leftAlgs_ = !gaveUp_ && !follower_.onAlg();
  grade_ = learnGrade(hinted_, gaveUp_, leftAlgs_);
  if (grade_ == Grade::Good && nextHinted_) {
    grade_ = Grade::Hard;
  }
  autoGrade_ = grade_;
  if (current_->practice) {
    setPhase(Phase::Done); // nothing is recorded
    showReveal();
    showGrade();
    return;
  }
  std::int64_t now = unixNow();
  CaseProgress &cp = progress()[current_->caseId];
  cardBefore_ = cp.card;
  attemptTime_ = now;
  cp.record({now, 0, grade_ == Grade::Again, AttemptMode::Learn});
  review(cp.card, grade_, now, settings_.srs);
  saveProgress();
  setPhase(Phase::Done);
  showReveal();
  showGrade();
  updateRow(current_->caseId);
}

void LearnPage::regrade(Grade grade) {
  if (phase_ != Phase::Done || !current_ || current_->practice ||
      gradeReplaced_) {
    showGrade();
    return;
  }
  grade_ = grade;
  CaseProgress &cp = progress()[current_->caseId];
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
  updateRow(current_->caseId);
}

void LearnPage::showReveal() {
  if (!current_) {
    return;
  }
  QString html = algsHtml(current_->caseId, current_->start);
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
  QString why = grade_ != autoGrade_       ? tr("graded by hand")
                : gaveUp_                  ? tr("gave up")
                : hinted_                  ? tr("used a hint")
                : nextHinted_              ? tr("used the next-move hint")
                : guide_ && guide_->follow ? tr("walked through")
                : leftAlgs_ ? (follower_.startedOver() ? tr("started over")
                                                       : tr("left the algs"))
                            : tr("solved from memory");
  QString next =
      dueText(progressOf(current_->caseId), unixNow(), localDayEnd());
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
