#include "app/learn_page.hpp"

#include "app/widgets.hpp"

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

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

} // namespace

LearnPage::LearnPage(AppSettings &settings, AlgLibrary &library,
                     QWidget *parent)
    : CasePage(settings, library, "learn", parent) {
  learnCount_ = new QSpinBox(left_);
  learnCount_->setRange(1, 50);
  learnCount_->setValue(3);
  learnCount_->setFocusPolicy(Qt::ClickFocus);
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
  auto *options = new QHBoxLayout;
  options->addWidget(new QLabel(tr("View:"), right));
  options->addWidget(createViewSelector(cubeView_, "learn", right));
  options->addStretch();
  options->addWidget(orientation_);
  rightLayout->addLayout(options);

  hintLabel_ = new QLabel(right);
  hintLabel_->setTextFormat(Qt::RichText);
  hintLabel_->setAlignment(Qt::AlignCenter);
  rightLayout->addWidget(hintLabel_);
  rightLayout->addWidget(cubeView_, 3);
  statusLabel_ = new QLabel(right);
  statusLabel_->setAlignment(Qt::AlignCenter);
  statusLabel_->setWordWrap(true);
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
  init();
}

QStringList LearnPage::columns() const { return {tr("Status"), tr("Due")}; }

void LearnPage::applySettings() {
  CubeLook look = cubeView_->look();
  look.scheme = settings_.scheme;
  cubeView_->setLook(look);
  cubeView_->setAnimationMs(settings_.animationMs);
  CasePage::applySettings();
  if (current_) {
    hintLabel_->setText(orientationHint(current_->colours, settings_.scheme));
  }
}

void LearnPage::activated() {
  CasePage::activated();
  if (!current_) {
    nextCase();
  }
}

void LearnPage::deactivated() {
  cubeView_->finishAnimations();
  learnCount_->clearFocus();
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
  item->setText(2, cp && !isQueued(cp->card) ? dueText(cp, unixNow())
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
    due += cp->card.due <= now;
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

QString LearnPage::startCaseLabel() const { return tr("Learn this case now"); }

void LearnPage::startSet() {
  current_.reset();
  last_.clear();
  nextCase();
}

void LearnPage::setReloaded() {
  if (current_ && !eligible(current_->caseId)) {
    current_.reset();
    nextCase();
  } else if (current_ && phase_ == Phase::Done) {
    showReveal();
  } else if (!current_) {
    nextCase();
  }
}

void LearnPage::ticksChanged() {
  if (phase_ == Phase::NoCase) {
    nextCase();
  }
}

void LearnPage::orientationChanged() {
  if (current_) {
    begin(current_->caseId, current_->isNew);
  }
}

void LearnPage::startCase(const std::string &caseId) {
  if (eligible(caseId)) {
    begin(caseId, false);
  }
}

void LearnPage::nextCase() {
  reveal_->clear();
  gradeRow_->setVisible(false);
  hintLabel_->clear();
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

void LearnPage::begin(const std::string &caseId, bool isNew) {
  const CaseInfo *info = infoOf(caseId);
  if (!info) {
    return;
  }
  ColourMap colours = caseColours();
  current_ = Current{caseId, info->state, colours, isNew};
  last_ = caseId;
  hinted_ = false;
  gaveUp_ = false;
  reveal_->clear();
  gradeRow_->setVisible(false);
  hintLabel_->setText(orientationHint(colours, settings_.scheme));
  cubeView_->setColourMap(colours);
  resetCube();
  selectInTree(caseId);
  setPhase(Phase::Solving);
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
  case Phase::Solving:
    statusLabel_->setText(
        current_ && current_->isNew
            ? tr("New case. Tab shows the algs, Space gives up, Esc resets.")
            : tr("Solve the case from memory. Tab shows the algs, Space "
                 "gives up, Esc resets."));
    break;
  case Phase::Done:
    statusLabel_->setText(tr("Space/Enter: next case. 1-4: change the "
                             "grade. Esc: reset the cube to practise."));
    break;
  }
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
  case Phase::Solving:
    if (key == Qt::Key_Tab) {
      hinted_ = true;
      showReveal();
      return true;
    }
    if (key == Qt::Key_Space) {
      gaveUp_ = true;
      finish();
      resetCube(); // so the alg can be tried
      return true;
    }
    if (key == Qt::Key_Escape) {
      resetCube();
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
  FaceletCube f = cube_;
  f.normalizeOrientation();
  auto c = CubieCube::fromFacelets(f);
  const AlgSet *s = set();
  const CaseGroup *g = s ? s->groupOf(current_->caseId) : nullptr;
  if (c && g && stepSolved(*c, s->specFor(*g).free)) {
    finish();
  }
}

void LearnPage::finish() {
  if (!current_ || !set()) {
    return;
  }
  grade_ = learnGrade(hinted_, gaveUp_);
  std::int64_t now = unixNow();
  CaseProgress &cp = progress()[current_->caseId];
  cardBefore_ = cp.card;
  cp.record({now, 0, grade_ == Grade::Again, AttemptMode::Learn});
  review(cp.card, grade_, now, settings_.srs);
  saveProgress();
  setPhase(Phase::Done);
  showReveal();
  showGrade();
  updateRow(current_->caseId);
}

void LearnPage::regrade(Grade grade) {
  if (phase_ != Phase::Done || !current_) {
    showGrade();
    return;
  }
  grade_ = grade;
  CaseProgress &cp = progress()[current_->caseId];
  cp.card = cardBefore_;
  review(cp.card, grade_, unixNow(), settings_.srs);
  saveProgress();
  showGrade();
  updateRow(current_->caseId);
}

void LearnPage::showReveal() {
  if (current_) {
    reveal_->setText(algsHtml(current_->caseId, current_->start));
  }
}

void LearnPage::showGrade() {
  if (phase_ != Phase::Done || !current_) {
    gradeRow_->setVisible(false);
    return;
  }
  gradeRow_->setVisible(true);
  QString why = gaveUp_   ? tr("gave up")
                : hinted_ ? tr("used a hint")
                          : tr("solved from memory");
  gradeLabel_->setText(
      tr("%1 (%2) → next %3")
          .arg(tr(gradeName(grade_)), why,
               dueText(progressOf(current_->caseId), unixNow())));
  for (int g = 0; g < 4; ++g) {
    gradeButtons_[static_cast<std::size_t>(g)]->setChecked(
        static_cast<int>(grade_) == g + 1);
  }
}

} // namespace vcube
