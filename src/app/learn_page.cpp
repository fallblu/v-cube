#include "app/learn_page.hpp"

#include "app/case_history_dialog.hpp"
#include "app/ui_state.hpp"
#include "app/widgets.hpp"

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
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

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

QString gradeColour(Grade g) {
  switch (g) {
  case Grade::Again:
    return "#d9453b";
  case Grade::Hard:
    return "#d98a1c";
  case Grade::Good:
    return "#2f9e55";
  case Grade::Easy:
    return "#3d7ee0";
  }
  return "#777777";
}

const StatusColour kLearning{Status::Warning};
const StatusColour kLearned{Status::Good};
const StatusColour kLeech{Status::Bad};

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
    parts << (d.marked ? "<b style='color:" + statusColour(Status::Bad).name() +
                             "'><u>" + text + "</u></b>"
                       : text);
  }
  return parts.join(' ');
}

constexpr Slot kAllSlots[] = {Slot::FR, Slot::FL, Slot::BL, Slot::BR};

// Tries for an in-context state whose open pairs are all cases already
// started in Learn, before taking any.
constexpr int kContextTries = 40;

// Choices in "which case is this?".
constexpr std::size_t kQuizChoices = 6;

// The cube with its orientation removed, so that states reached holding
// the cube differently compare equal.
FaceletCube oriented(FaceletCube f) {
  f.normalizeOrientation();
  return f;
}

// The cubes are held the same way.
bool sameCentres(const FaceletCube &a, const FaceletCube &b) {
  for (int face = 0; face < 6; ++face) {
    if (a[face * 9 + 4] != b[face * 9 + 4]) {
      return false;
    }
  }
  return true;
}

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

// Every set's due cards (whatever is ticked), for the set drop-down.
int dueCards(const SetProgress &progress, std::int64_t now, std::int64_t dayEnd,
             const SrsConfig &config) {
  int n = 0;
  for (const auto &[id, p] : progress) {
    n += isDueNow(p.card, now, dayEnd, config) ? 1 : 0;
  }
  return n;
}

} // namespace

LearnPage::LearnPage(AppSettings &settings, AlgLibrary &library,
                     QWidget *parent)
    : CasePage(settings, library, "learn", parent) {
  orderBox_ = new QComboBox(left_);
  orderBox_->setFocusPolicy(Qt::NoFocus);
  for (const OrderName &o : kOrders) {
    orderBox_->addItem(tr(o.label), QString(o.id));
  }
  orderBox_->setToolTip(
      tr("The order new cases are put into the learn queue and introduced "
         "in, chosen for each alg set. \"Fewest steps left first\" is for "
         "sets solved a step at a time."));
  form_->addRow(tr("New cases:"), orderBox_);

  learnCount_ = new QSpinBox(left_);
  learnCount_->setRange(1, 50);
  learnCount_->setValue(3);
  learnCount_->setFocusPolicy(Qt::ClickFocus);
  learnCount_->installEventFilter(this);
  connect(learnCount_, &QSpinBox::editingFinished, learnCount_,
          [this] { learnCount_->clearFocus(); });
  learnButton_ = new QPushButton(tr("Start learning"), left_);
  learnButton_->setFocusPolicy(Qt::NoFocus);
  learnButton_->setToolTip(
      tr("Put the next N ticked cases you haven't started into your learn "
         "queue, in the set's order for new cases"));
  tickRow_->addWidget(learnCount_);
  tickRow_->addWidget(learnButton_);

  // The learn queue: what comes next, and how many were introduced today.
  auto *queueRow = new QHBoxLayout;
  queueLabel_ = new QLabel(left_);
  queueLabel_->setWordWrap(true);
  queueLabel_->setTextFormat(Qt::RichText);
  learnNextButton_ = new QPushButton(tr("Learn next"), left_);
  learnNextButton_->setFocusPolicy(Qt::NoFocus);
  learnNextButton_->setToolTip(
      tr("Introduce the next case in the learn queue now, whatever the daily "
         "limit"));
  unqueueButton_ = new QPushButton(tr("Remove"), left_);
  unqueueButton_->setFocusPolicy(Qt::NoFocus);
  unqueueButton_->setToolTip(
      tr("Take the next case out of the learn queue (it becomes new again)"));
  queueRow->addWidget(queueLabel_, 1);
  queueRow->addWidget(learnNextButton_);
  queueRow->addWidget(unqueueButton_);
  leftLayout_->insertLayout(leftLayout_->count() - 1, queueRow);
  queueMessageTimer_.setSingleShot(true);
  connect(&queueMessageTimer_, &QTimer::timeout, this, &LearnPage::updateQueue);

  // --- right: the virtual cube above, what is said about it below ---
  auto *top = new QWidget(this);
  auto *topLayout = new QVBoxLayout(top);
  topLayout->setContentsMargins(0, 0, 0, 0);
  // Stays in focus mode.
  headerLabel_ = new QLabel(top);
  headerLabel_->setTextFormat(Qt::RichText);
  headerLabel_->setAlignment(Qt::AlignCenter);
  QFont hf = headerLabel_->font();
  hf.setPointSize(hf.pointSize() + 2);
  headerLabel_->setFont(hf);
  topLayout->addWidget(headerLabel_);
  cubeView_ = new CubeView(top);
  cubeView_->setMinimumSize(200, 160);
  optionsRow_ = new QWidget(top);
  auto *options = new QHBoxLayout(optionsRow_);
  options->setContentsMargins(0, 0, 0, 0);
  options->addWidget(new QLabel(tr("View:"), optionsRow_));
  viewBox_ = createViewSelector(cubeView_, "learn", optionsRow_);
  viewBox_->setToolTip(
      tr("How the virtual cube is shown, remembered for each alg set"));
  options->addWidget(viewBox_);
  practiceBox_ = new QComboBox(optionsRow_);
  practiceBox_->setFocusPolicy(Qt::NoFocus);
  practiceBox_->addItem(tr("Solve"), "solve");
  practiceBox_->addItem(tr("Recognise"), "recognise");
  practiceBox_->addItem(tr("Which case?"), "quiz");
  practiceBox_->setToolTip(
      tr("Solve: solve each case from memory. Recognise: reviews only show "
         "the case; press Space once you recall its alg, then grade "
         "yourself (new and learning cases are still solved). Which case?: "
         "name the case among others of its group, not graded."));
  options->addWidget(practiceBox_);
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
         "cheapest order. Not graded. Backspace returns to learning."));
  options->addWidget(slotsSpin_);
  options->addWidget(contextButton_);
  listButton_ = new QPushButton(optionsRow_);
  listButton_->setFocusPolicy(Qt::NoFocus);
  listButton_->setToolTip(tr("Hide or show the case list (F9)"));
  options->addWidget(listButton_);
  topLayout->addWidget(optionsRow_);
  topLayout->addWidget(cubeView_, 1);

  auto *bottom = new QWidget(this);
  auto *bottomLayout = new QVBoxLayout(bottom);
  bottomLayout->setContentsMargins(0, 0, 0, 0);
  statusLabel_ = new QLabel(bottom);
  statusLabel_->setAlignment(Qt::AlignCenter);
  statusLabel_->setWordWrap(true);
  guideLabel_ = new QLabel(bottom);
  guideLabel_->setAlignment(Qt::AlignCenter);
  guideLabel_->setWordWrap(true);
  guideLabel_->setTextFormat(Qt::RichText);
  QFont gf = guideLabel_->font();
  gf.setPointSize(gf.pointSize() + 5);
  guideLabel_->setFont(gf);
  guideLabel_->setVisible(false);
  bottomLayout->addWidget(guideLabel_);
  bottomLayout->addWidget(statusLabel_);

  // The outcome on one line, the grade buttons on the next.
  gradeRow_ = new QWidget(bottom);
  auto *gradeRows = new QVBoxLayout(gradeRow_);
  gradeRows->setContentsMargins(0, 0, 0, 0);
  auto *outcome = new QHBoxLayout;
  gradeBadge_ = new QLabel(gradeRow_);
  QFont bf = gradeBadge_->font();
  bf.setPointSize(bf.pointSize() + 3);
  bf.setBold(true);
  gradeBadge_->setFont(bf);
  gradeBadge_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  gradeLabel_ = new QLabel(gradeRow_);
  gradeLabel_->setWordWrap(true);
  outcome->addStretch();
  outcome->addWidget(gradeBadge_);
  outcome->addWidget(gradeLabel_);
  outcome->addStretch();
  gradeRows->addLayout(outcome);
  auto *gradeLayout = new QHBoxLayout;
  gradeRows->addLayout(gradeLayout);
  gradeLayout->addStretch();
  for (int g = 0; g < 4; ++g) {
    auto grade = static_cast<Grade>(g + 1);
    auto *b = new QPushButton(gradeRow_);
    b->setFocusPolicy(Qt::NoFocus);
    b->setCheckable(true);
    b->setToolTip(tr("Grade the case %1 (key %2)")
                      .arg(tr(gradeName(grade)).toLower())
                      .arg(g + 1));
    gradeButtons_[static_cast<std::size_t>(g)] = b;
    gradeLayout->addWidget(b);
    connect(b, &QPushButton::clicked, this, [this, grade] {
      if (phase_ == Phase::Recalled) {
        gradeRecognition(grade);
      } else {
        regrade(grade);
      }
    });
  }
  gradeLayout->addStretch();
  // The row keeps its place while hidden, so that the cube doesn't change
  // size from one phase to the next.
  QSizePolicy keep = gradeRow_->sizePolicy();
  keep.setRetainSizeWhenHidden(true);
  gradeRow_->setSizePolicy(keep);
  bottomLayout->addWidget(gradeRow_);

  quizRow_ = new QWidget(bottom);
  auto *quizLayout = new QHBoxLayout(quizRow_);
  quizLayout->setContentsMargins(0, 0, 0, 0);
  quizRow_->setVisible(false);
  bottomLayout->addWidget(quizRow_);

  reveal_ = new QLabel(bottom);
  reveal_->setTextFormat(Qt::RichText);
  reveal_->setWordWrap(true);
  reveal_->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
  reveal_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  QFont rf = reveal_->font();
  rf.setPointSize(rf.pointSize() + 3);
  reveal_->setFont(rf);
  auto *revealScroll = new QScrollArea(bottom);
  revealScroll->setWidget(reveal_);
  revealScroll->setWidgetResizable(true);
  revealScroll->setFrameShape(QFrame::NoFrame);
  revealScroll->setFocusPolicy(Qt::NoFocus);
  bottomLayout->addWidget(revealScroll, 1);
  familyRow_ = new QWidget(bottom);
  new QHBoxLayout(familyRow_);
  familyRow_->setVisible(false);
  bottomLayout->addWidget(familyRow_);

  // The cube and what is below it share the height as the splitter
  // between them says, whatever is shown below.
  auto *right = new QSplitter(Qt::Vertical, this);
  right->setObjectName("learnRight");
  right->addWidget(top);
  right->addWidget(bottom);
  right->setStretchFactor(0, 1);
  right->setChildrenCollapsible(false);
  right->setSizes({520, 260});

  split_ = new QSplitter(this);
  split_->addWidget(left_);
  split_->addWidget(right);
  split_->setStretchFactor(1, 1);
  left_->setMinimumWidth(300);
  split_->setSizes({400, 800});
  auto *layout = new QHBoxLayout(this);
  layout->addWidget(split_);
  split_->restoreState(saved::learnSplitter().get());
  right->restoreState(saved::learnRightSplitter().get());
  connect(split_, &QSplitter::splitterMoved, this,
          [this] { saved::learnSplitter().set(split_->saveState()); });
  connect(right, &QSplitter::splitterMoved, this,
          [right] { saved::learnRightSplitter().set(right->saveState()); });
  listHidden_ = saved::learnListHidden().get();
  left_->setVisible(!listHidden_);
  listButton_->setText(listHidden_ ? tr("Show list") : tr("Hide list"));
  int p = practiceBox_->findData(saved::learnPractice().get());
  practiceBox_->setCurrentIndex(std::max(0, p));

  idleTimer_.setSingleShot(true);
  connect(&idleTimer_, &QTimer::timeout, this, [this] {
    if (phase_ == Phase::NoCase && isVisible()) {
      nextCase();
    }
  });
  connect(learnButton_, &QPushButton::clicked, this, &LearnPage::learnNext);
  connect(learnNextButton_, &QPushButton::clicked, this,
          &LearnPage::learnQueueHead);
  connect(unqueueButton_, &QPushButton::clicked, this, &LearnPage::unqueueHead);
  connect(listButton_, &QPushButton::clicked, this, &LearnPage::toggleList);
  connect(orderBox_, &QComboBox::activated, this, [this] {
    saved::learnOrder(setId_).set(orderBox_->currentData().toString());
    updateQueue();
    if (phase_ == Phase::NoCase) {
      nextCase();
    }
  });
  connect(practiceBox_, &QComboBox::activated, this, [this] {
    saved::learnPractice().set(practiceBox_->currentData().toString());
    if (context_) {
      return; // applies when back to learning
    }
    // A case begun is finished first; anything else gives way.
    bool begun = phase_ == Phase::Demo ||
                 (phase_ == Phase::Solving &&
                  (links_.size() > 1 || !follower_.moves().empty() || hinted_ ||
                   nextHinted_));
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

QStringList LearnPage::columns() const {
  return {tr("Status"), tr("Due"), tr("Recog"), tr("Lapses"), tr("Ease")};
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
  if (phase_ == Phase::NoCase && set() && isVisible()) {
    nextCase();
  } else if (set()) {
    updateSummary();
  }
}

void LearnPage::activated() {
  CasePage::activated();
  if (phase_ == Phase::NoCase && !context_) {
    nextCase(); // something may have fallen due meanwhile
  }
}

void LearnPage::focusModeChanged() {
  CasePage::focusModeChanged();
  left_->setVisible(!focusMode() && !listHidden_);
  optionsRow_->setVisible(!focusMode());
}

void LearnPage::toggleList() {
  listHidden_ = !listHidden_;
  saved::learnListHidden().set(listHidden_);
  left_->setVisible(!listHidden_ && !focusMode());
  listButton_->setText(listHidden_ ? tr("Show list") : tr("Hide list"));
}

void LearnPage::deactivated() {
  CasePage::deactivated();
  cubeView_->finishAnimations();
  learnCount_->clearFocus();
  idleTimer_.stop();
}

std::vector<CasePage::CaseStatusFilter> LearnPage::statusFilters() {
  // As the Status and Due columns show them.
  auto queued = [this](const std::string &id) {
    const CaseProgress *cp = cardOf(id);
    return cp && isQueued(cp->card);
  };
  auto status = [this, queued](CaseStatus st) {
    return [this, queued, st](const std::string &id) {
      const CaseProgress *cp = cardOf(id);
      return eligible(id) && !queued(id) && statusOf(cp) == st &&
             !(cp && isHeld(cp->card, unixNow()));
    };
  };
  return {
      {tr("New"), status(CaseStatus::Unstarted)},
      {tr("In the learn queue"), queued},
      {tr("Learning"), status(CaseStatus::Learning)},
      {tr("Learned"), status(CaseStatus::Learned)},
      {tr("Due"),
       [this](const std::string &id) {
         const CaseProgress *cp = cardOf(id);
         return cp &&
                isDueNow(cp->card, unixNow(), localDayEnd(), settings_.srs);
       }},
      {tr("Suspended or buried"),
       [this](const std::string &id) {
         const CaseProgress *cp = cardOf(id);
         return cp && isHeld(cp->card, unixNow());
       }},
      {tr("Leeches"),
       [this](const std::string &id) {
         const CaseProgress *cp = cardOf(id);
         return cp && isLeech(cp->card, settings_.srs);
       }},
      {tr("Slow to recognise"),
       [this](const std::string &id) {
         std::optional<double> mean = recognitionMean(attemptsOf(id));
         return mean && *mean > settings_.slowRecognitionMs;
       }},
  };
}

void LearnPage::fillRow(QTreeWidgetItem *item, const std::string &caseId) {
  const CaseProgress *cp = cardOf(caseId);
  QColor muted = palette().color(QPalette::PlaceholderText);
  QColor text = palette().color(QPalette::Text);
  std::int64_t now = unixNow();
  QString status;
  QColor colour = muted;
  if (!eligible(caseId)) {
    status = tr("no alg");
  } else if (cp && isQueued(cp->card)) {
    status = tr("queued");
    colour = kLearning;
  } else {
    switch (statusOf(cp)) {
    case CaseStatus::Unstarted:
      status = tr("new");
      break;
    case CaseStatus::Learning:
      status = tr("learning");
      colour = kLearning;
      break;
    case CaseStatus::Learned:
      status = tr("learned");
      colour = kLearned;
      break;
    }
  }
  if (cp && cp->card.suspended) {
    status = tr("suspended");
    colour = muted;
  } else if (cp && isBuried(cp->card, now)) {
    status = tr("buried");
    colour = muted;
  }
  QString tip;
  if (cp && isLeech(cp->card, settings_.srs)) {
    status += " " + tr("· leech");
    colour = kLeech;
    tip = tr("Forgotten %1 times: worth a new alg, a mnemonic or a closer "
             "look (right-click: History)")
              .arg(cp->card.lapses);
  }
  item->setText(1, status);
  item->setForeground(1, colour);
  item->setToolTip(1, tip);
  item->setText(2, cp && !isQueued(cp->card) ? dueText(cp, now, localDayEnd())
                                             : QString("-"));
  std::optional<double> recog = recognitionMean(attemptsOf(caseId));
  item->setText(3, secondsText(recog));
  item->setForeground(
      3, recog && *recog > settings_.slowRecognitionMs ? kLearning : text);
  item->setToolTip(3, tr("Mean recognition time of the last 5 attempts that "
                         "measured it, in Learn and on the virtual cube in "
                         "Drill: from the case being set up to the first "
                         "turn off U and D"));
  bool started = cp && cp->card.introduced != 0;
  item->setText(4, started ? QString::number(cp->card.lapses) : QString("-"));
  item->setText(5, started
                       ? QString("%1%").arg(std::lround(cp->card.ease * 100))
                       : QString("-"));
  item->setToolTip(4, tr("Times the case was forgotten after it was learned"));
  item->setToolTip(5, tr("How much further apart the reviews get each time "
                         "the case goes well"));
}

void LearnPage::updateHeader() {
  std::int64_t now = unixNow();
  LearnCounts n;
  if (acrossSets()) {
    for (const auto &s : library_.sets()) {
      const SetProgress &p = library_.progress(s.id);
      std::vector<std::string> ids;
      for (const auto &[id, cp] : p) {
        ids.push_back(id);
      }
      LearnCounts c = learnCounts(ids, p, now, localDayStart(), settings_.srs,
                                  localDayEnd());
      n.queued += c.queued;
      n.learning += c.learning;
      n.due += c.due;
      n.doneToday += c.doneToday;
      n.againToday += c.againToday;
    }
  } else if (set()) {
    n = learnCounts(pool(), progress(), now, localDayStart(), settings_.srs,
                    localDayEnd());
  }
  QString muted = palette().color(QPalette::PlaceholderText).name();
  auto part = [](const QString &label, int count, const QString &colour) {
    return QString("%1 <b style='color:%2'>%3</b>")
        .arg(label.toHtmlEscaped(), colour)
        .arg(count);
  };
  QStringList parts;
  if (!acrossSets()) {
    parts << part(tr("Queued"), n.queued, muted);
  }
  parts << part(tr("Learning"), n.learning, kLearning.name())
        << part(tr("Due"), n.due, "#5aa0ff")
        << part(tr("Done today"), n.doneToday, kLearned.name());
  if (n.againToday > 0) {
    parts.back() += QString(" <span style='color:%1'>(%2)</span>")
                        .arg(muted, tr("%1 Again").arg(n.againToday));
  }
  QString html = parts.join(" &nbsp;·&nbsp; ");
  if (acrossSets()) {
    html = "<span style='color:" + muted + "'>" +
           tr("All sets").toHtmlEscaped() + ":</span> " + html;
  }
  if (practice() == Practice::Quiz && quizAsked_ > 0) {
    html +=
        " &nbsp;·&nbsp; " +
        tr("Quiz %1/%2, mean %3 s")
            .arg(quizRight_)
            .arg(quizAsked_)
            .arg(secondsText(static_cast<double>(quizTotalMs_) / quizAsked_));
  }
  headerLabel_->setText(html);
}

void LearnPage::updateSummary() {
  AlgSet *s = set();
  if (!s) {
    summary_->setText(tr("No alg sets. Create one in Library mode."));
    updateHeader();
    return;
  }
  LearnCounts n = learnCounts(pool(), progress(), unixNow(), localDayStart(),
                              settings_.srs, localDayEnd());
  QString text = tr("%1 of %2 ticked. Learning %3, learned %4, due %5, %6 "
                    "in the learn queue.")
                     .arg(tickedCount())
                     .arg(s->caseCount())
                     .arg(n.learning)
                     .arg(n.learned)
                     .arg(n.due)
                     .arg(n.queued);
  if (n.held > 0) {
    text += " " + tr("%1 suspended or buried.").arg(n.held);
  }
  if (int excluded = excludedCount()) {
    text += " " + skippedText(excluded);
  }
  summary_->setText(text);
  updateHeader();
  refreshSetLabels();
  if (!queueMessageTimer_.isActive()) {
    updateQueue();
  }
}

QString LearnPage::setItemText(const AlgSet &s) {
  int due = dueCards(library_.progress(s.id), unixNow(), localDayEnd(),
                     settings_.srs);
  QString text = CasePage::setItemText(s);
  return due > 0 ? tr("%1 · %2 due").arg(text).arg(due) : text;
}

QString LearnPage::acrossItemText() {
  int due = 0;
  for (const auto &s : library_.sets()) {
    due += dueCards(library_.progress(s.id), unixNow(), localDayEnd(),
                    settings_.srs);
  }
  return tr("All sets: due reviews (%1)").arg(due);
}

void LearnPage::extendCaseMenu(QMenu &menu,
                               const std::vector<std::string> &caseIds) {
  // The cards of the cases (a family's once); for several cases, only of
  // those that can be learned.
  std::vector<std::string> cards;
  for (const auto &id : caseIds) {
    if (caseIds.size() > 1 && !eligible(id)) {
      continue;
    }
    std::string card = cardId(id);
    if (std::find(cards.begin(), cards.end(), card) == cards.end()) {
      cards.push_back(card);
    }
  }
  if (cards.empty()) {
    menu.addAction(tr("None of these cases has an alg yet"))->setEnabled(false);
    return;
  }
  auto change = [this, cards, caseIds](auto &&apply) {
    for (const auto &card : cards) {
      apply(progress()[card]);
    }
    saveProgress();
    if (current_ && phase_ == Phase::Done &&
        std::find(cards.begin(), cards.end(), gradedCard_) != cards.end()) {
      gradeReplaced_ = true; // 1-4 would undo the change
      showGrade();
    }
    if (caseIds.size() > 1) {
      updateAllRows();
    } else {
      updateCardRows(caseIds.front());
    }
    if (phase_ == Phase::NoCase) {
      nextCase();
    }
  };
  auto any = [this, &cards](auto &&test) {
    return std::any_of(cards.begin(), cards.end(), [&](const std::string &c) {
      const CaseProgress *cp = progressOf(c);
      return test(cp);
    });
  };
  bool single = caseIds.size() == 1;
  bool anyEligible =
      std::any_of(caseIds.begin(), caseIds.end(),
                  [this](const auto &id) { return eligible(id); });
  // "Start learning" queues them in the set's order for new cases.
  menu.addAction(single ? tr("Start learning") : tr("Start learning all"), this,
                 [this, caseIds] {
                   std::vector<std::string> order;
                   for (const auto &id : pool()) {
                     for (const auto &c : caseIds) {
                       if (cardId(c) == id && eligible(c)) {
                         order.push_back(id);
                         break;
                       }
                     }
                   }
                   std::int64_t now = unixNow();
                   std::int64_t i = 0;
                   for (const auto &card : order) {
                     CaseProgress &p = progress()[card];
                     if (statusOf(&p) == CaseStatus::Unstarted ||
                         caseIds.size() == 1) {
                       startLearning(p.card, now + i++, settings_.srs);
                     }
                   }
                   saveProgress();
                   updateAllRows();
                   if (phase_ == Phase::NoCase) {
                     nextCase();
                   }
                 })
      ->setEnabled(anyEligible);
  menu.addAction(single ? tr("Mark as learned") : tr("Mark all as learned"),
                 this, [this, change] {
                   change([this](CaseProgress &p) {
                     markLearned(p.card, unixNow(), settings_.srs);
                   });
                 });
  menu.addAction(tr("Remove from learn queue"), this,
                 [change] {
                   change([](CaseProgress &p) {
                     if (isQueued(p.card)) {
                       resetCard(p.card);
                     }
                   });
                 })
      ->setEnabled(
          any([](const CaseProgress *cp) { return cp && isQueued(cp->card); }));
  menu.addSeparator();
  bool anySuspended =
      any([](const CaseProgress *cp) { return cp && cp->card.suspended; });
  bool allSuspended = std::all_of(cards.begin(), cards.end(), [this](auto &c) {
    const CaseProgress *cp = progressOf(c);
    return cp && cp->card.suspended;
  });
  // Suspending keeps the case ticked, so Drill still has it.
  menu.addAction(allSuspended ? tr("Unsuspend") : tr("Suspend"), this,
                 [change, allSuspended] {
                   change([allSuspended](CaseProgress &p) {
                     p.card.suspended = !allSuspended;
                   });
                 })
      ->setToolTip(tr("Leave it out of Learn (Drill still has it)"));
  if (anySuspended && !allSuspended) {
    menu.addAction(tr("Unsuspend"), this, [change] {
      change([](CaseProgress &p) { p.card.suspended = false; });
    });
  }
  bool anyBuried = any([](const CaseProgress *cp) {
    return cp && isBuried(cp->card, unixNow());
  });
  menu.addAction(tr("Bury until tomorrow"), this, [change] {
    change([](CaseProgress &p) { bury(p.card, localDayEnd()); });
  });
  if (anyBuried) {
    menu.addAction(tr("Unbury"), this, [change] {
      change([](CaseProgress &p) { p.card.buriedUntil = 0; });
    });
  }
  menu.addSeparator();
  // Its card and Learn attempts; its drill times stay.
  menu.addAction(
      single ? tr("Reset learning progress")
             : tr("Reset learning progress of all"),
      this, [change] { change([](CaseProgress &p) { resetLearning(p); }); });
  if (single) {
    std::string id = caseIds.front();
    menu.addAction(tr("History…"), this, [this, id] {
      const AlgSet *s = set();
      const AlgCase *c = s ? s->findCase(id) : nullptr;
      if (c) {
        showCaseHistory(this, qs(c->name), cardOf(id), attemptsOf(id),
                        byFamily() && cardId(id) != id, settings_.srs);
      }
    });
  }
}

std::vector<Attempt> LearnPage::attemptsOf(const std::string &caseId) {
  const CaseProgress *own = progressOf(caseId);
  const CaseProgress *card = cardOf(caseId);
  return caseAttempts(own, card != own ? card : nullptr);
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

std::vector<std::string> LearnPage::queueOrder() {
  std::vector<std::string> out;
  std::int64_t now = unixNow();
  for (const auto &id : pool()) {
    const CaseProgress *cp = progressOf(id);
    if (cp && isQueued(cp->card) && !isHeld(cp->card, now)) {
      out.push_back(id);
    }
  }
  return out;
}

void LearnPage::showQueueMessage(const QString &text) {
  queueLabel_->setText("<b>" + text.toHtmlEscaped() + "</b>");
  queueMessageTimer_.start(4000);
}

void LearnPage::updateQueue() {
  const AlgSet *s = set();
  std::vector<std::string> queue =
      s ? queueOrder() : std::vector<std::string>{};
  learnNextButton_->setEnabled(!queue.empty() && !context_);
  unqueueButton_->setEnabled(!queue.empty());
  if (!s) {
    queueLabel_->clear();
    return;
  }
  auto nameOf = [this, s](const std::string &card) {
    const AlgCase *c = s->findCase(card);
    if (!c) {
      if (const auto *members = families_.familyFor(card);
          members && !members->empty()) {
        c = s->findCase(members->front());
      }
    }
    return c ? qs(c->name).toHtmlEscaped() : QString("?");
  };
  QString muted = palette().color(QPalette::PlaceholderText).name();
  QString text;
  if (queue.empty()) {
    text = tr("The learn queue is empty.");
  } else {
    QStringList names;
    for (std::size_t i = 0; i < queue.size() && i < 3; ++i) {
      names << nameOf(queue[i]);
    }
    text = tr("Next: %1").arg(names.join(", "));
    if (queue.size() > 3) {
      text += " " + tr("(+%1)").arg(queue.size() - 3);
    }
  }
  int today = newCasesIntroducedSince(progress(), localDayStart());
  QString limits =
      tr("New today %1 of %2").arg(today).arg(settings_.srs.newPerDay);
  if (settings_.srs.maxLearning > 0) {
    limits += ", " + tr("learning %1 of %2")
                         .arg(learningCount(pool(), progress()))
                         .arg(settings_.srs.maxLearning);
  }
  queueLabel_->setText(text + "<br><span style='color:" + muted + "'>" +
                       limits + "</span>");
}

void LearnPage::learnQueueHead() {
  std::vector<std::string> queue = queueOrder();
  if (queue.empty() || context_) {
    return;
  }
  std::string member = pickMember(queue.front());
  if (eligible(member)) {
    begin(member, true);
  }
}

void LearnPage::unqueueHead() {
  std::vector<std::string> queue = queueOrder();
  if (queue.empty()) {
    return;
  }
  resetCard(progress()[queue.front()].card);
  saveProgress();
  updateCardRows(queue.front());
  if (current_ && cardId(current_->caseId) == queue.front() &&
      current_->isNew && phase_ == Phase::Demo) {
    nextCase();
  }
}

void LearnPage::learnNext() {
  int n = learnCount_->value();
  learnCount_->clearFocus();
  std::int64_t now = unixNow();
  int started = 0;
  // In the set's order for new cases; each queued a second apart, so that
  // "in the order queued" keeps this order.
  for (const auto &id : pool()) {
    if (started >= n) {
      break;
    }
    if (statusOf(progressOf(id)) == CaseStatus::Unstarted) {
      startLearning(progress()[id].card, now + started, settings_.srs);
      ++started;
    }
  }
  saveProgress();
  updateAllRows();
  if (started) {
    showQueueMessage(
        started == 1 ? tr("Added 1 case to your learn queue")
                     : tr("Added %1 cases to your learn queue").arg(started));
  } else {
    showQueueMessage(tr("Every ticked case with an alg is started already"));
  }
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
  undoOpen_ = false;
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
  if (phase_ == Phase::Quiz || phase_ == Phase::QuizDone) {
    if (!current_ || !eligible(current_->caseId)) {
      nextCase();
    }
    return;
  }
  if (current_ && phase_ == Phase::Demo && eligible(current_->caseId)) {
    begin(current_->caseId, current_->isNew, current_->practice);
    return;
  }
  if (current_ && !eligible(current_->caseId)) {
    current_.reset();
    nextCase();
  } else if (current_ && (phase_ == Phase::Done || phase_ == Phase::Recalled)) {
    if (phase_ == Phase::Recalled) {
      refreshLink();
    }
    showReveal();
  } else if (current_ && phase_ == Phase::Recognise) {
    refreshLink();
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
  familyRow_->setVisible(false);
  gradeRow_->setVisible(false);
  quizRow_->setVisible(false);
  // Nothing of the last case stays up, e.g. a walkthrough's moves.
  guide_.reset();
  links_.clear();
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
    statusLabel_->setText(tr("No alg set selected."));
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
                  "for new cases is %2 (see Settings, or \"Learn next\").")
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
    if (!acrossSets() && pick.queued == 0) {
      lines << (unstarted ? tr("Use \"Start learning\" to put more cases into "
                               "your learn queue.")
                          : tr("Every ticked case is started."));
    }
    LearnPickOptions early;
    early.early = true;
    early.newCases = false;
    early.dayEnd = dayEnd;
    bool waiting =
        pickLearnCase(cases, progress(), now, dayStart, settings_.srs, early)
            .caseId.has_value();
    lines << (waiting ? tr("Space: the next learning case now, early.")
                      : tr("Space checks again."));
  }
  statusLabel_->setText(lines.join(' '));
  setPhase(Phase::NoCase);
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
  quizRow_->setVisible(false);
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
  gaveUp_ = false;
  leftAlgs_ = false;
  gradeReplaced_ = false;
  walkedThrough_ = false;
  graded_ = false;
  undoOpen_ = false;
  gradedCard_.clear();
  attemptTime_ = 0;
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
  // The list doesn't name the case before it is revealed.
  clearCaseSelection();
  // A guided set walks through a case the first time it comes up.
  Phase p =
      guide_ && !practice && firstSight(caseId) ? Phase::Demo : Phase::Solving;
  shownAtMs_ = monotonicMs();
  recognitionMs_ = 0;
  recognitionSpoiled_ = p == Phase::Demo;
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
  gaveUp_ = false;
  leftAlgs_ = false;
  gradeReplaced_ = false;
  walkedThrough_ = false;
  undoOpen_ = false;
  gradedCards_.clear();
  links_.clear();
  reveal_->clear();
  gradeRow_->setVisible(false);
  familyRow_->setVisible(false);
  quizRow_->setVisible(false);
  cubeView_->setColourMap(colourMapOf(hold));
  cube_ = start.toFacelets();
  cubeView_->setState(cube_);
  startLink(viewOfCase(*s, *g, *c, start, world)
                .value_or(CaseView{caseId, false, 0}));
  guide_.reset();
  clearCaseSelection();
  shownAtMs_ = monotonicMs();
  recognitionMs_ = 0;
  setPhase(Phase::Recognise);
}

void LearnPage::recalled(bool knew) {
  if (phase_ != Phase::Recognise || !current_) {
    return;
  }
  recognitionMs_ =
      knew ? std::max<std::int64_t>(1, monotonicMs() - shownAtMs_) : 0;
  gradedCard_ = cardId(current_->caseId);
  const CaseProgress *cp = progressOf(gradedCard_);
  cardBefore_ = cp ? cp->card : SrsCard{};
  gradeTime_ = unixNow();
  grade_ = autoGrade_ = knew ? Grade::Good : Grade::Again;
  why_ = knew ? tr("recalled in %1 s")
                    .arg(secondsText(static_cast<double>(recognitionMs_)))
              : tr("not recalled");
  setPhase(Phase::Recalled);
  showReveal();
  selectInTree(current_->caseId);
  showGrade();
}

void LearnPage::gradeRecognition(Grade grade) {
  if (phase_ != Phase::Recalled || !current_ || gradedCard_.empty()) {
    return;
  }
  std::int64_t now = unixNow();
  CaseProgress &cp = progress()[gradedCard_];
  Undo u;
  u.setId = setId_;
  u.caseId = current_->caseId;
  u.recognition = true;
  u.cards.push_back({gradedCard_, cp.card, now});
  Attempt a{now, 0, grade == Grade::Again, AttemptMode::Learn};
  a.recognitionMs = recognitionMs_;
  a.recognitionOnly = true;
  cp.record(a);
  review(cp.card, grade, now, settings_.srs, &rng_);
  saveProgress();
  pushUndo(std::move(u));
  updateCardRows(current_->caseId);
  nextCase();
}

void LearnPage::nextQuiz() {
  current_.reset();
  links_.clear();
  guide_.reset();
  showGuide();
  reveal_->clear();
  gradeRow_->setVisible(false);
  familyRow_->setVisible(false);
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
    quizRow_->setVisible(false);
    cube_ = FaceletCube();
    cubeView_->setState(cube_);
    statusLabel_->setText(tr("The quiz needs two or more ticked cases with "
                             "algs (cases you have started come first)."));
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

  auto *layout = static_cast<QHBoxLayout *>(quizRow_->layout());
  while (QLayoutItem *item = layout->takeAt(0)) {
    delete item->widget();
    delete item;
  }
  layout->addStretch();
  for (std::size_t i = 0; i < quizChoices_.size(); ++i) {
    const AlgCase *c = s->findCase(quizChoices_[i]);
    auto *b = new QPushButton(
        QString("%1 %2").arg(i + 1).arg(c ? qs(c->name) : QString()), quizRow_);
    b->setFocusPolicy(Qt::NoFocus);
    connect(b, &QPushButton::clicked, this, [this, i] { answerQuiz(i); });
    layout->addWidget(b);
  }
  layout->addStretch();
  quizRow_->setVisible(true);
  clearCaseSelection();
  shownAtMs_ = monotonicMs();
  setPhase(Phase::Quiz);
}

void LearnPage::answerQuiz(std::size_t choice) {
  if (phase_ != Phase::Quiz || !current_ || choice >= quizChoices_.size()) {
    return;
  }
  std::int64_t ms = monotonicMs() - shownAtMs_;
  bool right = choice == quizAnswer_;
  ++quizAsked_;
  quizRight_ += right ? 1 : 0;
  quizTotalMs_ += ms;
  auto *layout = quizRow_->layout();
  int button = 0;
  for (int i = 0; i < layout->count(); ++i) {
    auto *b = qobject_cast<QPushButton *>(layout->itemAt(i)->widget());
    if (!b) {
      continue;
    }
    auto k = static_cast<std::size_t>(button++);
    if (k == quizAnswer_) {
      b->setStyleSheet("background:#2f9e55; color:white; font-weight:bold");
    } else if (k == choice) {
      b->setStyleSheet("background:#d9453b; color:white");
    }
    b->setEnabled(k == quizAnswer_ || k == choice);
  }
  reveal_->setText(algsHtml(current_->caseId, current_->start));
  selectInTree(current_->caseId);
  setPhase(Phase::QuizDone);
  const AlgCase *c = set() ? set()->findCase(current_->caseId) : nullptr;
  statusLabel_->setText(
      (right ? tr("Right, in %1 s.").arg(secondsText(static_cast<double>(ms)))
             : tr("It was %1.").arg(c ? qs(c->name) : QString())) +
      " " + tr("Space/Enter: another one."));
  statusLabel_->setStyleSheet(right ? QString() : "color: " + kLearning.name());
  updateHeader();
}

// --- undo --------------------------------------------------------------------

void LearnPage::pushUndo(Undo entry) {
  undo_.push_back(std::move(entry));
  while (undo_.size() > kUndoDepth) {
    undo_.pop_front();
  }
}

void LearnPage::undoLast() {
  if (undo_.empty()) {
    emit message(tr("Nothing to take back"));
    return;
  }
  Undo u = std::move(undo_.back());
  undo_.pop_back();
  undoOpen_ = false;
  if (!library_.find(u.setId)) {
    emit message(tr("The set of that grade is gone"));
    return;
  }
  SetProgress &p = library_.progress(u.setId);
  // Latest first, so that a card graded twice in the attempt ends up as it
  // was at the start.
  for (auto c = u.cards.rbegin(); c != u.cards.rend(); ++c) {
    CaseProgress &cp = p[c->cardId];
    cp.card = c->before;
    if (c->attempt != 0) {
      for (auto a = cp.attempts.rbegin(); a != cp.attempts.rend(); ++a) {
        if (a->mode == AttemptMode::Learn && a->timestamp == c->attempt) {
          cp.attempts.erase(std::next(a).base());
          break;
        }
      }
    }
  }
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
    if (phase_ == Phase::NoCase) {
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
  if (links_.size() > 1) {
    reveal_->clear(); // a hint for the last case isn't this one's
  }
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
  guide.held.push_back(f);
  for (Move m : guide.moves) {
    guide.tokens << qs(toString(m));
    f.apply(m);
    guide.states.push_back(oriented(f));
    guide.held.push_back(f);
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
  if (!found) {
    return;
  }
  // A rotation leaves the state as it was: it is done once the cube isn't
  // held as before it any more (as after it, or some other way when the
  // cube was turned differently from the start).
  std::size_t k = *found;
  while (
      k < g.moves.size() && isRotation(g.moves[k].type) &&
      (sameCentres(cube_, g.held[k + 1]) || !sameCentres(cube_, g.held[k]))) {
    ++k;
  }
  g.at = k;
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
  const QString nextStyle = "color:" + statusColour(Status::Warning).name() +
                            "; font-weight:bold; text-decoration:underline";
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
  // back to the start of the attempt, seen as the first case was.
  bool solving = phase_ == Phase::Solving || phase_ == Phase::Demo;
  cube_ = solving && !links_.empty() ? links_.back().start
                                     : current_->start.toFacelets();
  if (!solving && !links_.empty()) {
    const CaseView &first = links_.front().view;
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
  phase_ = p;
  statusLabel_->setStyleSheet({});
  switch (p) {
  case Phase::NoCase:
    break;
  case Phase::Context:
  case Phase::ContextDone:
    showGuide();
    gradeRow_->setVisible(false);
    familyRow_->setVisible(false);
    showContextStatus();
    break;
  case Phase::Demo:
    showGuide();
    statusLabel_->setText(
        tr("New case: → and ← step through the main alg, Home goes back to "
           "the start. Space: try it yourself, with each next move shown."));
    break;
  case Phase::Solving:
    showGuide();
    showSolvingStatus();
    break;
  case Phase::Done:
    showGuide();
    statusLabel_->setText(
        current_ && current_->practice
            ? tr("Space/Enter: next case. Esc: reset the cube to practise.")
            : tr("Space/Enter: next case. 1-4: change the grade. Esc: reset "
                 "the cube to practise. Ctrl+Z: take the grade back."));
    break;
  case Phase::Recognise:
    showGuide();
    statusLabel_->setText(
        tr("Recognise the case: Space as soon as you recall its alg, "
           "Shift+Space if you can't. Rotations and U and D turns turn the "
           "cube to look at it."));
    break;
  case Phase::Recalled:
    showGuide();
    statusLabel_->setText(tr("1-4: grade yourself and go on (Space/Enter: "
                             "the grade shown). Ctrl+Z takes a grade "
                             "back."));
    break;
  case Phase::Quiz:
    showGuide();
    statusLabel_->setText(
        tr("Which case is this? 1-%1 or click its name. Rotations and U and "
           "D turns turn the cube to look at it.")
            .arg(quizChoices_.size()));
    break;
  case Phase::QuizDone:
    break;
  }
  updateHeader();
}

void LearnPage::showSolvingStatus() {
  const QString warning = "color: " + statusColour(Status::Warning).name();
  std::string id = linkCase();
  bool later = links_.size() > 1;
  QString keys =
      guide_ && !guide_->follow && !guide_->next && !hinted_ && current_ &&
              inLearningSteps(id)
          ? tr("Tab shows the next move, Shift+Space gives up, Esc resets.")
          : tr("Tab shows the algs, Shift+Space gives up, Esc resets.");
  QString note = later && !linkNote_.isEmpty() ? linkNote_ + " " : QString();
  if (follower_.deviated() && !chained()) {
    bool practice = current_ && current_->practice;
    QString what =
        follower_.startedOver()
            ? (practice ? tr("Started over.")
                        : tr("Started over, so the case will be graded "
                             "Again."))
            : (practice ? tr("That isn't one of the algs.")
                        : tr("That isn't one of the algs, so the case will "
                             "be graded Again."));
    statusLabel_->setText(what + " " + keys);
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
    what = tr("Do the alg, one move at a time (not graded: the case comes "
              "back soon to do from memory).");
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
  const bool ctrl = e->modifiers().testFlag(Qt::ControlModifier);
  const bool shift = e->modifiers().testFlag(Qt::ShiftModifier);
  if (key == Qt::Key_Z && ctrl) {
    if (press) {
      undoLast();
    }
    return true;
  }
  if (key == Qt::Key_F9) {
    if (press) {
      toggleList();
    }
    return true;
  }
  if (!press) {
    return key == Qt::Key_Space;
  }
  bool next =
      key == Qt::Key_Space || key == Qt::Key_Return || key == Qt::Key_Enter;
  // Recognition and the quiz: the cube only turns as a whole (and U and
  // D), to look at the case from another side.
  auto look = [this, e] {
    if (auto m = settings_.keymap.lookup(e)) {
      if (isRotation(m->type) || udTurn(cube_, *m)) {
        cube_.apply(*m);
        cubeView_->animateMove(*m);
      }
      return true;
    }
    return false;
  };
  switch (phase_) {
  case Phase::NoCase:
    if (next) {
      // Checks again, and takes a learning case early if nothing is due.
      nextCase(true);
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
      if (shift) {
        finishContext(true);
      }
      return true;
    }
    if (key == Qt::Key_Escape) {
      resetContext();
      return true;
    }
    if (key == Qt::Key_Backspace) {
      leaveContext();
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
    if (key == Qt::Key_Backspace) {
      leaveContext();
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
      if (!follower_.begun()) {
        recognitionSpoiled_ = true; // the algs were seen first
      }
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
      if (links_.size() == 1 && current_) {
        selectInTree(current_->caseId);
      }
      return true;
    }
    if (key == Qt::Key_Space) {
      // Space alone goes on to the next case once this one is done, so a
      // double tap mustn't give up the next one too.
      if (shift) {
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
    if (key == Qt::Key_Space && shift) {
      return true; // Shift still held from giving up: keep the reveal up
    }
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
  case Phase::Recognise:
    if (key == Qt::Key_Space) {
      recalled(!shift);
      return true;
    }
    if (key == Qt::Key_Escape) {
      resetCube();
      return true;
    }
    return look();
  case Phase::Recalled:
    if (key >= Qt::Key_1 && key <= Qt::Key_4) {
      gradeRecognition(static_cast<Grade>(key - Qt::Key_1 + 1));
      return true;
    }
    if (next) {
      if (!shift) {
        gradeRecognition(grade_);
      }
      return true;
    }
    if (key == Qt::Key_Escape) {
      resetCube();
      return true;
    }
    return look();
  case Phase::Quiz:
    if (key >= Qt::Key_1 &&
        key < Qt::Key_1 + static_cast<int>(quizChoices_.size())) {
      answerQuiz(static_cast<std::size_t>(key - Qt::Key_1));
      return true;
    }
    if (key == Qt::Key_Escape) {
      resetCube();
      return true;
    }
    return look();
  case Phase::QuizDone:
    if (next) {
      nextQuiz();
      return true;
    }
    return look();
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
  bool wasBegun = follower_.begun();
  follower_.move(m);
  // Recognition ends at the first turn of a layer off the U-D axis.
  if (!wasBegun && follower_.begun() && links_.size() == 1 &&
      recognitionMs_ == 0) {
    recognitionMs_ = std::max<std::int64_t>(1, monotonicMs() - shownAtMs_);
  }
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
  // Every reason the case went as it did, e.g. both a hint and leaving
  // the algs.
  QStringList reasons;
  if (gaveUp_) {
    reasons << tr("gave up");
  } else if (wrong) {
    reasons << (chained()                 ? tr("the step didn't get closer")
                : follower_.startedOver() ? tr("started over")
                                          : tr("left the algs"));
  }
  if (hinted_) {
    reasons << tr("used a hint");
  } else if (nextHinted_) {
    reasons << tr("used the next-move hint");
  }
  if (reasons.isEmpty()) {
    reasons << (guide_ && guide_->follow ? tr("walked through")
                : !chained()             ? tr("solved from memory")
                : link.setStep           ? tr("the set's step")
                                         : tr("another step that gets closer"));
  }
  QString why = reasons.join(tr(" and "));
  std::int64_t now = unixNow();
  const CaseProgress *card = cardOf(id);
  bool record = false;
  if (first) {
    leftAlgs_ = wrong && !chained();
    grade_ = link.grade;
    autoGrade_ = link.grade;
    why_ = why;
    gradeTime_ = now;
    gradedCard_ = cardId(id);
    if (!current_->practice && guide_ && guide_->follow && firstSight(id)) {
      // A walkthrough isn't recall: the case is introduced and comes back
      // at its first learning step to be done from memory.
      CaseProgress &cp = progress()[gradedCard_];
      cardBefore_ = cp.card;
      Undo u;
      u.setId = setId_;
      u.caseId = current_->caseId;
      u.isNew = current_->isNew;
      u.cards.push_back({gradedCard_, cp.card, 0});
      pushUndo(std::move(u));
      undoOpen_ = true;
      gradedCards_.push_back(gradedCard_);
      introduce(cp.card, now, settings_.srs);
      walkedThrough_ = true;
      graded_ = true;
      saveProgress();
      updateCardRows(id);
      return;
    }
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
    Undo u;
    u.setId = setId_;
    u.caseId = current_->caseId;
    u.isNew = current_->isNew;
    u.cards.push_back({cardId(id), cp.card, now});
    pushUndo(std::move(u));
    undoOpen_ = true;
    graded_ = true;
  } else if (undoOpen_ && !undo_.empty()) {
    undo_.back().cards.push_back({cardId(id), cp.card, now});
  }
  gradedCards_.push_back(cardId(id));
  Attempt attempt{now, 0, link.grade == Grade::Again, AttemptMode::Learn};
  // Only a case recognised rightly (its alg kept to) has a recognition
  // time worth keeping.
  if (first && recognitionMs_ > 0 && !recognitionSpoiled_ &&
      link.grade != Grade::Again) {
    attempt.recognitionMs = recognitionMs_;
    attempt.executionMs =
        std::max<std::int64_t>(0, monotonicMs() - shownAtMs_ - recognitionMs_);
  }
  cp.record(attempt);
  review(cp.card, link.grade, now, settings_.srs, &rng_);
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
  selectInTree(current_->caseId);
}

void LearnPage::regrade(Grade grade) {
  if (phase_ != Phase::Done || !current_ || current_->practice ||
      gradeReplaced_ || !graded_ || gradedCard_.empty()) {
    showGrade();
    return;
  }
  grade_ = grade;
  if (!links_.empty()) {
    links_.front().grade = grade;
  }
  // The card the grade went to, even if learning by family was turned on
  // or off since.
  CaseProgress &cp = progress()[gradedCard_];
  cp.card = cardBefore_;
  std::int64_t now = unixNow();
  if (attemptTime_ == 0) {
    // A walkthrough graded by hand: now it is an attempt.
    attemptTime_ = now;
    cp.record({now, 0, grade == Grade::Again, AttemptMode::Learn});
    if (undoOpen_ && !undo_.empty()) {
      for (auto &c : undo_.back().cards) {
        if (c.cardId == gradedCard_ && c.attempt == 0) {
          c.attempt = now;
        }
      }
    }
  } else {
    for (auto a = cp.attempts.rbegin(); a != cp.attempts.rend(); ++a) {
      if (a->mode == AttemptMode::Learn && a->timestamp == attemptTime_) {
        a->failed = grade == Grade::Again;
        break;
      }
    }
  }
  if (walkedThrough_) {
    walkedThrough_ = false;
    why_ = tr("graded by hand");
    autoGrade_ = grade;
  }
  review(cp.card, grade_, now, settings_.srs, &rng_);
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
  idleTimer_.stop();
  current_.reset();
  guide_.reset();
  links_.clear();
  quizRow_->setVisible(false);
  gradeRow_->setVisible(false);
  context_ = Context{state, pickHold(), {}, false};
  contextButton_->setText(tr("Back to learning"));
  learnNextButton_->setEnabled(false);
  cubeView_->setColourMap(colourMapOf(context_->hold));
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
  updateQueue();
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
           "Shift+Space gives up, Backspace goes back to learning. Not "
           "graded."));
  } else {
    statusLabel_->setText(tr("Space/Enter: another one. Esc: try this one "
                             "again. Backspace: back to learning."));
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
  bool shown = byFamily() &&
               (phase_ == Phase::Done || phase_ == Phase::Recalled) &&
               members && members->size() > 1;
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
    text->setText(
        QString("<b>%1</b><br><span style='color:%2'>%3</span>")
            .arg(qs(c->name).toHtmlEscaped(),
                 done ? statusColour(Status::Warning).name() : muted.name(),
                 alg));
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
  const QString good =
      "<span style='color:" + statusColour(Status::Good).name() + "'>✓</span>";
  const QString bad =
      "<span style='color:" + statusColour(Status::Bad).name() + "'>✗</span>";
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
  if (!current_ || !s) {
    return;
  }
  if (phase_ == Phase::Recalled) {
    // Every step in a set solved a step at a time.
    reveal_->setText(
        algsHtml(current_->caseId, current_->start, current_->world));
    return;
  }
  if (links_.empty()) {
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
  bool shown = (phase_ == Phase::Done || phase_ == Phase::Recalled) && current_;
  if (!shown) {
    gradeRow_->setVisible(false);
    return;
  }
  gradeRow_->setVisible(true);
  bool recalled = phase_ == Phase::Recalled;
  bool buttons =
      recalled || (!current_->practice && !gradeReplaced_ && graded_);
  std::int64_t when = recalled ? unixNow() : gradeTime_;
  for (int g = 0; g < 4; ++g) {
    auto grade = static_cast<Grade>(g + 1);
    QPushButton *b = gradeButtons_[static_cast<std::size_t>(g)];
    b->setVisible(buttons);
    // What the grade would give, from the card as it was before.
    b->setText(QString("%1 %2 · %3")
                   .arg(g + 1)
                   .arg(tr(gradeName(grade)))
                   .arg(intervalText(
                       nextInterval(cardBefore_, grade, when, settings_.srs))));
    b->setChecked(!walkedThrough_ && grade_ == grade);
  }
  auto badge = [this](const QString &text, const QString &colour) {
    gradeBadge_->setText(text);
    gradeBadge_->setStyleSheet(
        QString("QLabel { background: %1; color: white; border-radius: 4px; "
                "padding: 2px 10px; }")
            .arg(colour));
  };
  const QString grey = "#6b6f78";
  // On one line beside the badge while it fits, wrapped after that.
  auto detail = [this](const QString &text) {
    gradeLabel_->setText(text);
    gradeLabel_->setMinimumWidth(
        std::min(gradeLabel_->fontMetrics().horizontalAdvance(text) + 8, 420));
  };
  QString why = grade_ != autoGrade_ ? tr("graded by hand") : why_;
  const CaseProgress *card =
      progressOf(gradedCard_.empty() ? cardId(current_->caseId) : gradedCard_);
  QString next = dueText(card, unixNow(), localDayEnd());
  if (recalled) {
    badge(tr(gradeName(grade_)), gradeColour(grade_));
    detail(tr("%1. Grade yourself:").arg(why));
    return;
  }
  if (gradeReplaced_) {
    badge(tr("Changed"), grey);
    detail(tr("from the case list → next %1").arg(next));
    return;
  }
  if (current_->practice) {
    badge(grade_ == Grade::Again ? tr("Missed") : tr("Well done"),
          grade_ == Grade::Again ? gradeColour(Grade::Again) : grey);
    detail(tr("Practice (%1), not graded; next review %2.").arg(why, next));
    return;
  }
  if (walkedThrough_) {
    badge(tr("Walked through"), "#5a6b80");
    detail(tr("Not graded: it comes back in %1 to do from "
              "memory. 1-4 grade it now instead.")
               .arg(next));
    return;
  }
  if (!graded_) {
    badge(tr(gradeName(grade_)), grey);
    detail(tr("(%1), not graded").arg(why));
    return;
  }
  badge(tr(gradeName(grade_)), gradeColour(grade_));
  detail(tr("%1 → next %2").arg(why, next));
}

} // namespace vcube
