#include "app/attempt_page.hpp"

#include "app/review_dialog.hpp"
#include "train/solve_review.hpp"
#include "train/step_solver.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace vcube {

namespace {

QPushButton *flatButton(const QString &text, QWidget *parent) {
  auto *b = new QPushButton(text, parent);
  b->setFocusPolicy(Qt::NoFocus);
  return b;
}

QString movesText(StatValue v, int decimals = 2) {
  return QString::fromStdString(formatMoves(v, decimals));
}

QString dateText(std::int64_t unixSeconds) {
  if (unixSeconds <= 0) {
    return {};
  }
  return QLocale().toString(QDateTime::fromSecsSinceEpoch(unixSeconds),
                            QLocale::ShortFormat);
}

QStringList offeredTypes() {
  QStringList out;
  for (const auto &id : moveCountScrambleTypes()) {
    out << qs(id);
  }
  return out;
}

// The number of digits a typed move count can have.
constexpr int kMaxDigits = 3;

} // namespace

AttemptPage::AttemptPage(AppSettings &settings, MoveCountStore &store,
                         AlgLibrary &library, QWidget *parent)
    : ModePage(parent), settings_(settings), store_(store), library_(library),
      moveCount_(store.mode() == SolveLog::MoveCount), feed_(library) {
  const QString mode = moveCount_ ? "movecount" : "untimed";

  // --- left column: sessions, stats, attempts ---
  auto *left = leftPanel_ = new QWidget(this);
  auto *leftLayout = new QVBoxLayout(left);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  sessionButton_ = new QToolButton(left);
  sessionButton_->setFocusPolicy(Qt::NoFocus);
  sessionButton_->setPopupMode(QToolButton::InstantPopup);
  sessionButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  sessionButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  sessionButton_->setToolTip(
      tr("Switch, add, rename or delete sessions, or retry the worst solves"));
  sessionMenu_ = new QMenu(sessionButton_);
  sessionButton_->setMenu(sessionMenu_);
  leftLayout->addWidget(sessionButton_);

  stats_ = new QLabel(left);
  stats_->setTextFormat(Qt::RichText);
  stats_->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                  Qt::LinksAccessibleByMouse);
  stats_->setFocusPolicy(Qt::NoFocus);
  stats_->setToolTip(tr("Click a statistic to see its solves"));
  leftLayout->addWidget(stats_);

  // Move Count: #, Moves, Opt, ao5, ao12. Untimed: #, Moves, ao5, ao12,
  // with the same columns used for #, Scramble, Date on a physical cube.
  table_ = new QTableWidget(0, moveCount_ ? 5 : 4, left);
  table_->verticalHeader()->setVisible(false);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setFocusPolicy(Qt::NoFocus);
  table_->setContextMenuPolicy(Qt::CustomContextMenu);
  leftLayout->addWidget(table_, 1);

  auto *listRow = new QHBoxLayout;
  deleteButton_ = flatButton(tr("Delete last"), left);
  deleteButton_->setToolTip(tr("Delete the last solve (Delete)"));
  undoButton_ = flatButton(tr("Undo delete"), left);
  undoButton_->setToolTip(tr("Put back the solve last deleted (Ctrl+Z)"));
  undoButton_->setVisible(false);
  listRow->addWidget(deleteButton_);
  listRow->addWidget(undoButton_);
  listRow->addStretch();
  leftLayout->addLayout(listRow);

  // --- right column: scramble, cube or entry, chart ---
  auto *right = new QWidget(this);
  auto *rightLayout = new QVBoxLayout(right);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  typeRow_ = new QWidget(right);
  auto *typeRow = new QHBoxLayout(typeRow_);
  typeRow->setContentsMargins(0, 0, 0, 0);
  if (moveCount_) {
    stepBox_ = new QComboBox(typeRow_);
    stepBox_->setFocusPolicy(Qt::NoFocus);
    for (SolveStep step : solveSteps()) {
      stepBox_->addItem(qs(std::string(stepName(step))),
                        static_cast<int>(step));
    }
    stepBox_->setToolTip(
        tr("The step this session is for (fixed once the session has "
           "solves). On the virtual cube a scramble counts only if the step "
           "is done (on any colour) when you press Space."));
    typeRow->addWidget(new QLabel(tr("Step:"), typeRow_));
    typeRow->addWidget(stepBox_);
    typeRow->addSpacing(12);
    typeBox_ = new ScrambleTypeBox(library, typeRow_, offeredTypes());
    typeBox_->setToolTip(
        tr("What the scramble leaves solved: a full WCA scramble for EO, "
           "EOLine, EO Cross, XEOCross or the cross, EOLine solved, EO Cross "
           "solved for ZZ F2L, or the cross solved for CFOP F2L"));
  } else {
    typeBox_ = new ScrambleTypeBox(library, typeRow_);
  }
  cubeBox_ = new QComboBox(typeRow_);
  cubeBox_->setFocusPolicy(Qt::NoFocus);
  cubeBox_->addItem(tr("Virtual cube"));
  cubeBox_->addItem(tr("Physical cube"));
  cubeBox_->setToolTip(tr("The cube this session's solves are done on (fixed "
                          "once the session has solves)"));
  cubeView_ = new CubeView(right);
  viewPicker_ = new QWidget(typeRow_);
  auto *viewLayout = new QHBoxLayout(viewPicker_);
  viewLayout->setContentsMargins(0, 0, 0, 0);
  viewLayout->addWidget(new QLabel(tr("View:"), viewPicker_));
  viewLayout->addWidget(createViewSelector(cubeView_, mode, viewPicker_));
  skipButton_ = flatButton(tr("Skip scramble"), typeRow_);
  skipButton_->setToolTip(
      tr("Go on to the next scramble without solving this one (Ctrl+→)"));
  typeRow->addWidget(new QLabel(tr("Scramble:"), typeRow_));
  typeRow->addWidget(typeBox_, 1);
  if (!moveCount_) {
    algs_ = new ScrambleAlgs(library, "untimed/showAlgs", right);
    typeRow->addWidget(algs_->button());
  }
  typeRow->addSpacing(12);
  typeRow->addWidget(cubeBox_);
  typeRow->addWidget(viewPicker_);
  typeRow->addWidget(skipButton_);
  rightLayout->addWidget(typeRow_);

  auto *scrambleRow = new QHBoxLayout;
  scrambleLabel_ = new QLabel(right);
  scrambleLabel_->setWordWrap(true);
  scrambleLabel_->setAlignment(Qt::AlignCenter);
  scrambleLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  QFont sf = scrambleLabel_->font();
  sf.setPointSize(17);
  scrambleLabel_->setFont(sf);
  scrambleLabel_->setMinimumHeight(60);
  net_ = new NetPreview(right);
  scrambleRow->addWidget(scrambleLabel_, 1);
  scrambleRow->addWidget(net_);
  rightLayout->addLayout(scrambleRow);

  auto *centre = new QVBoxLayout;
  stepLabel_ = new QLabel(right);
  stepLabel_->setAlignment(Qt::AlignCenter);
  QFont stepFont = stepLabel_->font();
  stepFont.setPointSizeF(stepFont.pointSizeF() * 1.3);
  stepFont.setBold(true);
  stepLabel_->setFont(stepFont);
  stepLabel_->setToolTip(tr("The session and the step it is for"));
  stepLabel_->setVisible(moveCount_);
  display_ = new TimeDisplay(right);
  hintLabel_ = new QLabel(right);
  hintLabel_->setAlignment(Qt::AlignCenter);
  hintLabel_->setWordWrap(true);
  hintLabel_->setForegroundRole(QPalette::PlaceholderText);
  auto *buttonRow = new QHBoxLayout;
  recordButton_ = flatButton(tr("Record (Enter)"), right);
  dnfButton_ = flatButton("DNF", right);
  reviewButton_ = flatButton(tr("Review last"), right);
  reviewButton_->setToolTip(
      tr("Replay the last solve with its steps marked, see the optimal "
         "solutions and how the F2L pairs went (Alt+R; double-click any "
         "solve to review it)"));
  recordButton_->setToolTip(tr("Record the move count for this scramble"));
  dnfButton_->setToolTip(tr("Record this scramble as DNF, e.g. when the step "
                            "went wrong (Alt+3)"));
  buttonRow->addStretch();
  buttonRow->addWidget(recordButton_);
  buttonRow->addWidget(dnfButton_);
  buttonRow->addWidget(reviewButton_);
  buttonRow->addStretch();
  lastLabel_ = new QLabel(right);
  lastLabel_->setAlignment(Qt::AlignCenter);
  lastLabel_->setWordWrap(true);
  focusStats_ = new QLabel(right);
  focusStats_->setAlignment(Qt::AlignCenter);
  focusStats_->setForegroundRole(QPalette::PlaceholderText);
  focusStats_->setVisible(false);
  centre->addStretch(1);
  centre->addWidget(stepLabel_);
  centre->addWidget(cubeView_, 8);
  centre->addWidget(display_);
  centre->addWidget(hintLabel_);
  centre->addLayout(buttonRow);
  centre->addWidget(lastLabel_);
  centre->addWidget(focusStats_);
  if (algs_) {
    centre->addWidget(algs_->label());
  }
  centre->addStretch(1);
  rightLayout->addLayout(centre, 3);

  algPanel_ = new QWidget(right);
  auto *algLayout = new QVBoxLayout(algPanel_);
  algLayout->setContentsMargins(0, 0, 0, 0);
  auto *algRow = new QHBoxLayout;
  algEdit_ = new QLineEdit(algPanel_);
  algEdit_->setFocusPolicy(Qt::ClickFocus);
  algEdit_->setPlaceholderText(
      tr("After a solve: type an alg and press Enter to apply it"));
  algEdit_->installEventFilter(this);
  auto *applyButton = flatButton(tr("Apply"), algPanel_);
  algRow->addWidget(new QLabel(tr("Alg:"), algPanel_));
  algRow->addWidget(algEdit_, 1);
  algRow->addWidget(applyButton);
  algLayout->addLayout(algRow);
  algError_ = new QLabel(algPanel_);
  algError_->setStyleSheet("color: #e66");
  algError_->setVisible(false);
  algLayout->addWidget(algError_);
  rightLayout->addWidget(algPanel_);

  chart_ = new TrendChart(right);
  chart_->setEmptyText(tr("Move counts will be charted here"));
  chart_->setMoveCounts(true);
  chart_->setSettingsKey(mode + "/chart");
  rightLayout->addWidget(chart_, 1);

  auto *split = new QSplitter(this);
  split->addWidget(left);
  split->addWidget(right);
  split->setStretchFactor(0, 0);
  split->setStretchFactor(1, 1);
  split->setSizes({300, 800});
  auto *layout = new QHBoxLayout(this);
  layout->addWidget(split);

  // --- wiring ---
  holdTimer_.setSingleShot(true);
  connect(&holdTimer_, &QTimer::timeout, this, [this] {
    if (hold_ == Hold::Holding) {
      hold_ = Hold::Ready;
      showHold();
    }
  });
  holdWatch_.setInterval(50);
  connect(&holdWatch_, &QTimer::timeout, this, [this] {
    if (QApplication::activePopupWidget() ||
        QApplication::activeModalWidget()) {
      cancelHold(); // the Space release goes to the popup
    }
  });
  connect(&feed_, &ScrambleFeed::currentChanged, this,
          &AttemptPage::showScramble);
  connect(skipButton_, &QPushButton::clicked, this, &AttemptPage::skip);
  connect(typeBox_, &ScrambleTypeBox::typeChanged, this,
          [this](const QString &type) {
            if (session().scrambleType != ss(type)) {
              session().scrambleType = ss(type);
              save();
            }
            if (feed_.type() != type) {
              setEntry({}); // the count was for the old scramble
              history_.clear();
              index_ = 0;
            }
            if (algs_) {
              algs_->setType(type);
            }
            feed_.setType(type);
          });
  if (stepBox_) {
    connect(stepBox_, &QComboBox::activated, this, [this](int index) {
      auto step = static_cast<SolveStep>(stepBox_->itemData(index).toInt());
      if (isBusy() || session().step == step) {
        return;
      }
      if (!session().attempts.empty()) {
        // Its attempts were checked and counted for the step they had.
        stepBox_->setCurrentIndex(
            stepBox_->findData(static_cast<int>(session().step)));
        emit message(tr("This session has solves: start a new session for "
                        "another step"));
        return;
      }
      session().step = step;
      save();
      typeBox_->setCurrentType(qs(defaultScrambleType(step)));
      selectSession(store_.currentIndex());
    });
  }
  connect(cubeBox_, &QComboBox::activated, this, [this](int index) {
    CubeKind kind = index == 0 ? CubeKind::Virtual : CubeKind::Physical;
    if (isBusy() || session().cube == kind) {
      return;
    }
    if (!session().attempts.empty()) {
      cubeBox_->setCurrentIndex(onVirtualCube() ? 0 : 1);
      emit message(tr("This session has solves on the other cube: start a "
                      "new session for this one"));
      return;
    }
    session().cube = kind;
    retry_.reset();
    retryQueue_.clear();
    save();
    showCubeKind();
    showFreshScramble();
    refreshAttempts();
  });
  connect(sessionMenu_, &QMenu::aboutToShow, this,
          &AttemptPage::refreshSessionMenu);
  connect(recordButton_, &QPushButton::clicked, this,
          [this] { recordTyped(false); });
  connect(dnfButton_, &QPushButton::clicked, this,
          [this] { recordTyped(true); });
  connect(deleteButton_, &QPushButton::clicked, this, [this] {
    if (!session().attempts.empty()) {
      deleteAttempt(static_cast<int>(session().attempts.size()) - 1);
    }
  });
  connect(undoButton_, &QPushButton::clicked, this, &AttemptPage::undoDelete);
  connect(table_, &QTableWidget::customContextMenuRequested, this,
          &AttemptPage::showAttemptMenu);
  connect(table_, &QTableWidget::itemDoubleClicked, this,
          [this](QTableWidgetItem *item) {
            int index = item->data(Qt::UserRole).toInt();
            const auto &attempts = session().attempts;
            if (index >= 0 && index < static_cast<int>(attempts.size()) &&
                reviewable(attempts[static_cast<std::size_t>(index)])) {
              review(index);
            } else {
              showDetails(index);
            }
          });
  connect(reviewButton_, &QPushButton::clicked, this,
          [this] { review(static_cast<int>(session().attempts.size()) - 1); });
  connect(stats_, &QLabel::linkActivated, this, &AttemptPage::showStat);
  connect(&optimalWatcher_, &QFutureWatcher<OptimalResults>::finished, this,
          [this] {
            // Results worked out for holds that have changed since are
            // dropped.
            if (optimalRoundRun_ == optimalRound_) {
              for (const auto &[scramble, length] :
                   optimalWatcher_.result()) {
                optimal_[{std::get<0>(optimalFor_), scramble,
                          std::get<2>(optimalFor_)}] = length;
              }
            }
            refreshAttempts();
          });
  connect(&eoWatcher_, &QFutureWatcher<std::optional<int>>::finished, this,
          [this] {
            std::optional<int> best = eoWatcher_.result();
            if (!best || isBusy() || retry_) {
              return;
            }
            QString yours =
                eoYours_ ? QString::number(*eoYours_) : tr("not done");
            QString line =
                tr("EO Cross: yours %1, optimal %2").arg(yours).arg(*best);
            QString was = lastLabel_->text();
            lastLabel_->setText(was.isEmpty() ? line : was + " · " + line);
          });
  connect(algEdit_, &QLineEdit::returnPressed, this, &AttemptPage::applyAlg);
  connect(applyButton, &QPushButton::clicked, this, &AttemptPage::applyAlg);
  connect(algEdit_, &QLineEdit::textChanged, this,
          [this] { algError_->setVisible(false); });

  applySettings();
  selectSession(store_.currentIndex());
  if (algs_) {
    algs_->setType(typeBox_->currentType());
  }
  feed_.setType(typeBox_->currentType()); // if the type didn't change
}

AttemptPage::~AttemptPage() {
  optimalWatcher_.waitForFinished();
  eoWatcher_.waitForFinished();
}

bool AttemptPage::onVirtualCube() const {
  return store_.sessions()[store_.currentIndex()].cube == CubeKind::Virtual;
}

bool AttemptPage::scramblesOnly() const {
  return !moveCount_ && !onVirtualCube();
}

const GeneratedScramble *AttemptPage::shownScramble() const {
  if (retry_) {
    return &retry_->scramble;
  }
  if (!onVirtualCube() && index_ < history_.size()) {
    return &history_[index_];
  }
  const auto &current = feed_.current();
  return current ? &*current : nullptr;
}

void AttemptPage::deactivated() {
  cancelHold();
  cubeView_->finishAnimations();
  returnFocus();
}

void AttemptPage::setFocusMode(bool on) {
  focusMode_ = on;
  if (on) {
    returnFocus();
  }
  leftPanel_->setVisible(!on);
  typeRow_->setVisible(!on);
  // The hint shows the hold's progress in focus mode too.
  hintLabel_->setVisible(!on || hold_ != Hold::None);
  chart_->setVisible(!on && !scramblesOnly());
  focusStats_->setVisible(on && !scramblesOnly());
  algPanel_->setVisible(!on && !moveCount_ && onVirtualCube());
}

void AttemptPage::applySettings() {
  net_->setScheme(settings_.scheme);
  CubeLook look = cubeView_->look();
  look.scheme = settings_.scheme;
  cubeView_->setLook(look);
  cubeView_->setAnimationMs(settings_.animationMs);
  feed_.setNeutrality(settings_.neutrality);
  if (algs_) {
    algs_->refresh();
  }
  // The holds may have changed: results on the way are stale.
  ++optimalRound_;
  optimal_.clear();
  refreshAttempts();
}

void AttemptPage::refreshScrambleTypes(const QString &changedSet) {
  QString before = typeBox_->currentType();
  typeBox_->refresh(); // emits typeChanged if the type went away
  if (typeBox_->currentType() == before && !changedSet.isEmpty() &&
      before == "set:" + changedSet) {
    history_.clear();
    index_ = 0;
    feed_.regenerate(); // the set's cases or ticks may have changed
  }
  if (algs_) {
    algs_->refresh();
  }
}

bool AttemptPage::eventFilter(QObject *obj, QEvent *event) {
  if (obj == algEdit_ && event->type() == QEvent::KeyPress &&
      static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
    returnFocus();
    return true;
  }
  return ModePage::eventFilter(obj, event);
}

void AttemptPage::returnFocus() {
  if (algEdit_->hasFocus()) {
    algEdit_->clearFocus();
  }
}

void AttemptPage::showCubeKind() {
  bool virt = onVirtualCube();
  cubeBox_->setCurrentIndex(virt ? 0 : 1);
  cubeView_->setVisible(virt);
  viewPicker_->setVisible(virt);
  net_->setVisible(!virt);
  bool typed = moveCount_ && !virt; // the count is typed in
  recordButton_->setVisible(typed);
  dnfButton_->setVisible(typed);
  display_->setVisible(moveCount_);
  display_->setToolTip(typed ? tr("The move count you are typing")
                             : tr("Moves so far (HTM)"));
  algPanel_->setVisible(!moveCount_ && virt && typeRow_->isVisibleTo(this));
  if (moveCount_) {
    table_->setHorizontalHeaderLabels(
        {"#", tr("Moves"), tr("Opt"), "ao5", "ao12"});
    table_->horizontalHeaderItem(2)->setToolTip(
        settings_.optimalAnyHold
            ? tr("The optimal move count for the scramble's step in any hold, "
                 "as the step is checked")
            : tr("The optimal move count for the scramble's step in the holds "
                 "of your colour neutrality (either EO axis)"));
  } else if (virt) {
    table_->setHorizontalHeaderLabels({"#", tr("Moves"), "ao5", "ao12"});
  } else {
    table_->setHorizontalHeaderLabels({"#", tr("Scramble"), tr("Date"), ""});
  }
  table_->setColumnHidden(3, scramblesOnly());
  auto *header = table_->horizontalHeader();
  header->setSectionResizeMode(QHeaderView::Stretch);
  header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  if (scramblesOnly()) {
    header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  }
  chart_->setVisible(!scramblesOnly() && !focusMode_);
  focusStats_->setVisible(focusMode_ && !scramblesOnly());
  history_.clear();
  index_ = 0;
  entry_.clear();
}

void AttemptPage::showScramble() {
  const GeneratedScramble *shown = shownScramble();
  if (algs_) {
    algs_->setScramble(shown);
  }
  if (!shown) {
    scrambleLabel_->setText(tr("Generating scramble…"));
    net_->setCube(std::nullopt);
  } else {
    scrambleLabel_->setText(index_ < history_.size()
                                ? tr("%1\n(done %2 back)")
                                      .arg(shown->text)
                                      .arg(history_.size() - index_)
                                : shown->text);
    net_->setScramble(shown->text);
  }
  if (onVirtualCube()) {
    // Shown as scrambled holding white on top and green in front.
    attempt_.load(shown ? parseAlgOrEmpty(ss(shown->text)) : Alg{});
    cubeView_->setState(attempt_.cube());
    setPhase(Phase::Inspect);
  }
  showEntry();
  showHint();
}

void AttemptPage::showFreshScramble() {
  if (phase_ == Phase::Done) {
    // The scramble shown was solved already.
    setPhase(Phase::Inspect);
    feed_.advance();
  } else {
    showScramble();
  }
}

void AttemptPage::showHint() {
  QString text;
  if (!onVirtualCube()) {
    text = moveCount_
               ? tr("Type the number of moves your solution took and press "
                    "Enter. Backspace corrects, Esc clears.")
               : tr("Solve the scramble, then press Space (or →) for the "
                    "next one. Backspace (or ←) goes back.");
  } else if (phase_ == Phase::Inspect) {
    text = tr("Turn the cube as a whole to look at it, then hold Space and "
              "release it to start.");
    if (retry_) {
      QString which = tr("Retry of #%1 (not recorded). ").arg(retry_->index + 1);
      if (retryCount_ > 1) {
        which = tr("Retry %1 of %2, of #%3 (not recorded). ")
                    .arg(retryCount_ - retryQueue_.size())
                    .arg(retryCount_)
                    .arg(retry_->index + 1);
      }
      text = which + text + " " + tr("Skip scramble goes back to new scrambles.");
    }
  } else if (phase_ == Phase::Solving) {
    text = moveCount_ ? tr("Press Space when the step is done. Esc starts the "
                           "scramble again.")
           : settings_.autoStop
               ? tr("The solve ends when the cube is solved (or on Space). "
                    "Esc starts the scramble again.")
               : tr("Press Space when the cube is solved. Esc starts the "
                    "scramble again.");
  } else if (retry_) {
    text = retryQueue_.empty()
               ? tr("The cube turns freely. Space: back to new scrambles. Esc: "
                    "reset the cube to solved.")
               : tr("The cube turns freely. Space: the next retry. Esc: reset "
                    "the cube to solved.");
  } else {
    text = tr("The cube turns freely. Space or Enter: next scramble. Esc: "
              "reset the cube to solved.");
  }
  hintLabel_->setText(text);
}

void AttemptPage::setEntry(const QString &digits) {
  entry_ = digits;
  showEntry();
}

void AttemptPage::showEntry() {
  if (onVirtualCube()) {
    QColor muted = palette().color(QPalette::PlaceholderText);
    if (phase_ == Phase::Solving) {
      display_->showText(
          QString::number(attempt_.count(settings_.simplifiedCounts)));
    } else if (retry_ && retry_->done) {
      // The retry's own result, not the last recorded one.
      display_->showText(qs(formatAttempt(retry_->result)),
                         retry_->result.dnf ? muted : QColor());
    } else if (retry_) {
      display_->showText("–", muted);
    } else if (phase_ == Phase::Done || !session().attempts.empty()) {
      // The last result, until the next solve starts.
      const auto &attempts = session().attempts;
      display_->showText(attempts.empty() ? QString("–")
                                          : qs(formatAttempt(attempts.back())),
                         attempts.empty() || attempts.back().dnf ? muted
                                                                 : QColor());
    } else {
      display_->showText("–", muted);
    }
    return;
  }
  if (entry_.isEmpty()) {
    display_->showText("–", palette().color(QPalette::PlaceholderText));
  } else {
    display_->showText(entry_);
  }
  recordButton_->setEnabled(!entry_.isEmpty() && feed_.current());
  dnfButton_->setEnabled(feed_.current().has_value());
}

void AttemptPage::setPhase(Phase p) {
  cancelHold();
  phase_ = p;
  attempt_.setFree(p == Phase::Done);
  algEdit_->setEnabled(p == Phase::Done);
  if (p != Phase::Done) {
    returnFocus();
    algError_->setVisible(false);
  }
  updateControls();
  showHint();
}

bool AttemptPage::reviewable(const MoveCountAttempt &a) const {
  return !a.solution.empty() || (moveCount_ && !a.scramble.empty());
}

void AttemptPage::updateControls() {
  bool busy = isBusy();
  for (QWidget *w : std::initializer_list<QWidget *>{
           sessionButton_, typeBox_, cubeBox_, skipButton_}) {
    w->setEnabled(!busy);
  }
  if (stepBox_) {
    stepBox_->setEnabled(!busy);
  }
  deleteButton_->setEnabled(!busy && !session().attempts.empty());
  undoButton_->setVisible(deleted_.has_value());
  undoButton_->setEnabled(!busy);
  bool canReview =
      !session().attempts.empty() && reviewable(session().attempts.back());
  reviewButton_->setVisible(canReview);
  reviewButton_->setEnabled(!busy);
}

void AttemptPage::updateStepLabel() {
  if (!stepBox_) {
    return;
  }
  stepBox_->setCurrentIndex(
      stepBox_->findData(static_cast<int>(session().step)));
  QString step = qs(std::string(stepName(session().step)));
  QString name = qs(session().name);
  // "Virtual ZZ F2L" names its step already.
  stepLabel_->setText(name.contains(step, Qt::CaseInsensitive)
                          ? name
                          : tr("%1 (%2)").arg(name, step));
}

bool AttemptPage::handleActionKey(QKeyEvent *e, bool press) {
  Qt::KeyboardModifiers mods = e->modifiers() & ~Qt::KeypadModifier;
  int key = e->key();
  int last = static_cast<int>(session().attempts.size()) - 1;
  bool known =
      (mods == Qt::AltModifier &&
       (key == Qt::Key_3 || key == Qt::Key_C || key == Qt::Key_R)) ||
      (mods == Qt::ControlModifier &&
       (key == Qt::Key_Z || key == Qt::Key_Right)) ||
      (mods == Qt::NoModifier && key == Qt::Key_Delete);
  if (!known) {
    return false;
  }
  if (!press || e->isAutoRepeat() || isBusy() || hold_ != Hold::None) {
    return true;
  }
  if (mods == Qt::AltModifier) {
    if (key == Qt::Key_3) {
      if (moveCount_ && !onVirtualCube()) {
        recordTyped(true);
      }
    } else if (key == Qt::Key_C) {
      commentAttempt(last);
    } else {
      review(last);
    }
  } else if (key == Qt::Key_Z) {
    undoDelete();
  } else if (key == Qt::Key_Right) {
    skip();
  } else if (last >= 0) {
    deleteAttempt(last);
  }
  return true;
}

bool AttemptPage::handleKey(QKeyEvent *e, bool press) {
  int key = e->key();
  if (handleActionKey(e, press)) {
    return true;
  }
  if (onVirtualCube()) {
    std::optional<Move> move = settings_.keymap.lookup(e);
    bool enter = key == Qt::Key_Return || key == Qt::Key_Enter;
    bool known = move || key == Qt::Key_Space || key == Qt::Key_Escape ||
                 (key == Qt::Key_Tab && algs_) ||
                 (enter && phase_ == Phase::Done);
    if (!known) {
      return false;
    }
    if (e->isAutoRepeat()) {
      return true;
    }
    if (!press) {
      if (key == Qt::Key_Space) {
        releaseStart();
      }
      return true;
    }
    if (hold_ != Hold::None) {
      // Holding Space to start: Esc lets go, other keys wait.
      if (key == Qt::Key_Escape) {
        cancelHold();
      }
      return true;
    }
    if (move) {
      turn(*move);
    } else if (key == Qt::Key_Tab) {
      return algs_->toggle();
    } else if (key == Qt::Key_Space || enter) {
      if (phase_ == Phase::Inspect) {
        pressStart();
      } else if (phase_ == Phase::Solving) {
        endSolve();
      } else {
        next();
      }
    } else if (phase_ == Phase::Done) { // Esc
      attempt_.load(Alg{});
      attempt_.setFree(true);
      cubeView_->setState(attempt_.cube());
    } else {
      attempt_.restart();
      cubeView_->setState(attempt_.cube());
      setPhase(Phase::Inspect);
      showEntry();
    }
    return true;
  }

  if (!moveCount_) {
    // Untimed on a physical cube: scrambles only.
    if (key == Qt::Key_Tab && press && algs_->toggle()) {
      return true;
    }
    bool forward = key == Qt::Key_Space || key == Qt::Key_Right;
    bool back = key == Qt::Key_Backspace || key == Qt::Key_Left;
    if (!forward && !back) {
      return false;
    }
    if (press && !e->isAutoRepeat()) {
      if (forward) {
        next();
      } else {
        previous();
      }
    }
    return true;
  }

  // Move Count on a physical cube: the count is typed in.
  bool digit = key >= Qt::Key_0 && key <= Qt::Key_9;
  bool known = digit || key == Qt::Key_Backspace || key == Qt::Key_Return ||
               key == Qt::Key_Enter || key == Qt::Key_Escape;
  if (!known || (e->modifiers() &
                 (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
    return false;
  }
  if (!press) {
    return true;
  }
  if (digit) {
    if (!e->isAutoRepeat() && entry_.size() < kMaxDigits) {
      // No leading zeros: "0" then "7" is 7.
      QString nextDigits = entry_ == "0" ? QString() : entry_;
      setEntry(nextDigits + QChar('0' + (key - Qt::Key_0)));
    }
  } else if (key == Qt::Key_Backspace) {
    setEntry(entry_.left(entry_.size() - 1));
  } else if (key == Qt::Key_Escape) {
    setEntry({});
  } else if (!e->isAutoRepeat()) { // Enter
    recordTyped(false);
  }
  return true;
}

void AttemptPage::turn(Move m) {
  if (!feed_.current() && !retry_) {
    return; // no scramble on the cube yet
  }
  if (!attempt_.turn(m)) {
    emit message(tr("Only rotations until you press Space to start"));
    return;
  }
  cubeView_->animateMove(m);
  if (phase_ == Phase::Solving) {
    showEntry();
    // Untimed can end the solve as soon as the cube is solved.
    if (!moveCount_ && settings_.autoStop && attempt_.cube().isSolved()) {
      endSolve();
    }
  }
}

void AttemptPage::pressStart() {
  if (!feed_.current() && !retry_) {
    emit message(tr("Wait for the scramble before starting"));
    return;
  }
  hold_ = Hold::Holding;
  holdTimer_.start(static_cast<int>(settings_.holdMs));
  holdWatch_.start();
  showHold();
}

void AttemptPage::releaseStart() {
  Hold was = hold_;
  cancelHold();
  if (was == Hold::Ready) {
    startSolve();
  }
}

void AttemptPage::cancelHold() {
  if (hold_ == Hold::None) {
    return;
  }
  hold_ = Hold::None;
  holdTimer_.stop();
  holdWatch_.stop();
  showHold();
}

void AttemptPage::showHold() {
  if (focusMode_) {
    hintLabel_->setVisible(hold_ != Hold::None);
  }
  if (hold_ == Hold::None) {
    hintLabel_->setStyleSheet({});
    showHint();
    showEntry();
    return;
  }
  QColor colour = hold_ == Hold::Ready ? QColor(0x3d, 0xd6, 0x6b)
                                       : QColor(0xff, 0x55, 0x55);
  hintLabel_->setStyleSheet(QString("color: %1").arg(colour.name()));
  hintLabel_->setText(hold_ == Hold::Ready ? tr("Release Space to start")
                                           : tr("Keep holding Space…"));
  if (moveCount_) {
    display_->showText("0", colour);
  }
}

void AttemptPage::startSolve() {
  if (!feed_.current() && !retry_) {
    emit message(tr("Wait for the scramble before starting"));
    return;
  }
  attempt_.start();
  setPhase(Phase::Solving);
  showEntry();
}

void AttemptPage::endSolve() {
  bool reached = moveCount_ ? stepReached(attempt_.cube(), session().step)
                            : attempt_.cube().isSolved();
  int count = attempt_.count(settings_.simplifiedCounts);
  if (retry_) {
    // Compared with the attempt it repeats, and not recorded.
    const MoveCountAttempt &was = retry_->original;
    retry_->result.moves = count;
    retry_->result.dnf = !reached;
    retry_->result.solution = toString(attempt_.solution());
    QString result = !reached     ? tr("DNF (%1)").arg(count)
                     : count == 1 ? tr("1 move")
                                  : tr("%1 moves").arg(count);
    QString text = tr("Retry of #%1: %2, was %3")
                       .arg(retry_->index + 1)
                       .arg(result, qs(formatAttempt(was)));
    if (moveCount_ && hasOptimalSolver(session().step)) {
      if (std::optional<int> best = heldDistance(
              parseAlgOrEmpty(was.scramble), session().step, optimalHolds())) {
        text += tr(", optimal %1").arg(*best);
      }
    }
    lastLabel_->setText(text);
    emit message(text);
    retry_->done = true;
    setPhase(Phase::Done);
    showEntry();
    showHint();
    return;
  }
  record(count, !reached, toString(attempt_.solution()));
  if (moveCount_) {
    next(); // on to the next scramble
  } else {
    setPhase(Phase::Done);
    showEntry();
    analyseEoCross(session().attempts.back());
  }
  if (!reached) {
    emit message(moveCount_
                     ? tr("DNF: %1 wasn't done when you pressed Space")
                           .arg(qs(std::string(stepName(session().step))))
                     : tr("DNF: the cube wasn't solved when you pressed "
                          "Space"));
  }
}

void AttemptPage::analyseEoCross(const MoveCountAttempt &a) {
  ParseResult scramble = parseAlg(a.scramble);
  ParseResult moves = parseAlg(a.solution);
  if (!scramble || !moves || eoWatcher_.isRunning()) {
    return;
  }
  FaceletCube start;
  start.apply(scramble.alg);
  eoYours_.reset();
  for (const StepMark &m : stepMarks(start, moves.alg, {SolveStep::EOCross},
                                     settings_.simplifiedCounts)) {
    eoYours_ = m.htm;
  }
  std::vector<vcube::Hold> holds =
      settings_.optimalAnyHold
          ? std::vector<vcube::Hold>(allHolds().begin(), allHolds().end())
          : withBothEoAxes(settings_.neutrality.holds);
  Alg alg = scramble.alg;
  eoWatcher_.setFuture(QtConcurrent::run(
      [alg, holds] { return heldDistance(alg, SolveStep::EOCross, holds); }));
}

void AttemptPage::record(std::optional<int> count, bool dnf,
                         const std::string &solution) {
  const GeneratedScramble *shown = shownScramble();
  MoveCountAttempt a;
  a.counted = count.has_value();
  a.moves = std::clamp(count.value_or(0), 0, kMaxMoveCount);
  a.dnf = dnf;
  a.scramble = shown ? ss(shown->text) : std::string();
  a.solution = solution;
  a.timestamp = unixNow();
  session().attempts.push_back(a);
  deleted_.reset();
  save();
  refreshAttempts();
  // A new best, as Timed marks it.
  if (a.counted) {
    QStringList bests;
    for (const NewBest &b :
         newBests(attemptsAsSolves(session().attempts), kWcaUnit)) {
      bests << QString("%1 %2").arg(qs(b.name), movesText(b.value,
                                                          b.name == "single"
                                                              ? 0
                                                              : 2));
    }
    if (!bests.isEmpty()) {
      QString text = tr("New best: %1!").arg(bests.join(", "));
      lastLabel_->setText(lastLabel_->text() + " · " + text);
      emit message(text);
    }
  }
}

void AttemptPage::recordTyped(bool dnf) {
  if (!feed_.current()) {
    emit message(tr("Wait for the scramble before recording"));
    return;
  }
  if (!dnf && entry_.isEmpty()) {
    emit message(tr("Type the move count first"));
    return;
  }
  record(entry_.toInt(), dnf);
  entry_.clear();
  next();
}

void AttemptPage::next() {
  if (retry_) {
    endRetry();
    return;
  }
  if (scramblesOnly()) {
    // Untimed on a physical cube: forward through the scrambles gone back
    // to, then the scramble shown is done and kept.
    if (index_ < history_.size()) {
      ++index_;
      showScramble();
      return;
    }
    const auto &current = feed_.current();
    if (!current) {
      return;
    }
    record(std::nullopt, false);
    history_.push_back(*current);
    if (history_.size() > kHistory) {
      history_.pop_front();
    }
    index_ = history_.size();
  }
  entry_.clear();
  feed_.advance(); // shows the next scramble
}

void AttemptPage::previous() {
  if (index_ > 0) {
    --index_;
    showScramble();
  }
}

void AttemptPage::skip() {
  if (isBusy()) {
    return;
  }
  if (retry_) {
    retryQueue_.clear(); // back to new scrambles
    endRetry();
    return;
  }
  entry_.clear();
  index_ = history_.size();
  setPhase(Phase::Inspect);
  feed_.advance();
}

void AttemptPage::applyAlg() {
  QString text = algEdit_->text().trimmed();
  if (text.isEmpty() || phase_ != Phase::Done) {
    returnFocus();
    return;
  }
  ParseResult parsed = parseAlg(ss(text));
  if (!parsed) {
    algError_->setText(tr("Can't read this alg at character %1: %2")
                           .arg(parsed.error->position + 1)
                           .arg(qs(parsed.error->message)));
    algError_->setVisible(true);
    return; // keep the field focused to fix it
  }
  for (Move m : parsed.alg) {
    if (attempt_.turn(m)) {
      cubeView_->animateMove(m);
    }
  }
  algError_->setVisible(false);
  algEdit_->clear();
  returnFocus();
}

void AttemptPage::review(int index) {
  const auto &attempts = session().attempts;
  if (isBusy() || index < 0 || index >= static_cast<int>(attempts.size())) {
    return;
  }
  const MoveCountAttempt &a = attempts[static_cast<std::size_t>(index)];
  if (!reviewable(a)) {
    return;
  }
  ReviewedSolve solve;
  solve.title = moveCount_ ? tr("%1 #%2")
                                 .arg(qs(std::string(stepName(session().step))))
                                 .arg(index + 1)
                           : tr("Solve #%1").arg(index + 1);
  solve.scramble = parseAlgOrEmpty(a.scramble);
  solve.solution = parseAlgOrEmpty(a.solution);
  if (moveCount_) {
    solve.step = session().step;
  }
  solve.dnf = a.dnf;
  if (a.counted && !a.solution.empty()) {
    solve.recorded = a.moves;
  }
  ReviewDialog dialog(solve, settings_, &library_, this);
  dialog.setRetryEnabled(onVirtualCube());
  connect(&dialog, &ReviewDialog::retryRequested, this, [this, index] {
    retryQueue_.clear();
    retryCount_ = 1;
    startRetry(static_cast<std::size_t>(index));
  });
  dialog.exec();
}

void AttemptPage::showDetails(int index) {
  const auto &attempts = session().attempts;
  if (index < 0 || index >= static_cast<int>(attempts.size())) {
    return;
  }
  const MoveCountAttempt &a = attempts[static_cast<std::size_t>(index)];
  QList<QPair<QString, QString>> rows;
  if (a.counted) {
    rows.append({tr("Moves"), qs(formatAttempt(a))});
  }
  if (moveCount_) {
    rows.append({tr("Step"), qs(std::string(stepName(session().step)))});
    if (std::optional<int> best = optimalOf(a); best && *best >= 0) {
      rows.append({tr("Optimal"), QString::number(*best)});
    }
  }
  rows.append({tr("Date"), dateText(a.timestamp)});
  rows.append({"Scramble", qs(a.scramble)});
  if (!a.solution.empty()) {
    rows.append({"Solution", qs(a.solution)});
  }
  rows.append({tr("Comment"), qs(a.comment)});
  showSolveDetails(this, tr("Solve #%1").arg(index + 1), rows,
                   reviewable(a) ? std::function<void()>(
                                       [this, index] { review(index); })
                                 : std::function<void()>{});
}

void AttemptPage::commentAttempt(int index) {
  auto &attempts = session().attempts;
  if (isBusy() || index < 0 || index >= static_cast<int>(attempts.size())) {
    return;
  }
  MoveCountAttempt &a = attempts[static_cast<std::size_t>(index)];
  bool ok = false;
  QString text = QInputDialog::getText(
      this, tr("Comment"), tr("Comment on solve #%1:").arg(index + 1),
      QLineEdit::Normal, qs(a.comment), &ok);
  if (ok) {
    a.comment = ss(text);
    save();
    refreshAttempts();
  }
}

void AttemptPage::startRetry(std::size_t index) {
  const auto &attempts = session().attempts;
  if (isBusy() || index >= attempts.size() || !onVirtualCube()) {
    return;
  }
  Retry r;
  r.index = index;
  r.feedSolved = retry_ ? retry_->feedSolved : phase_ == Phase::Done;
  r.original = attempts[index];
  r.scramble.text = qs(r.original.scramble);
  retry_ = r;
  setPhase(Phase::Inspect);
  showScramble();
  lastLabel_->setText(tr("Retry of #%1, which was %2")
                          .arg(index + 1)
                          .arg(qs(formatAttempt(r.original))));
}

void AttemptPage::endRetry() {
  if (!retryQueue_.empty()) {
    std::size_t nextIndex = retryQueue_.front();
    retryQueue_.pop_front();
    startRetry(nextIndex);
    return;
  }
  bool feedSolved = retry_ && retry_->feedSolved;
  retry_.reset();
  retryCount_ = 0;
  setPhase(Phase::Inspect);
  if (feedSolved) {
    feed_.advance(); // the scramble shown before was solved already
  } else {
    showScramble(); // the scramble that was shown before the retry
  }
  refreshAttempts();
}

void AttemptPage::retryWorst() {
  const auto &attempts = session().attempts;
  if (isBusy() || !onVirtualCube()) {
    return;
  }
  // DNFs first, then the most moves over the optimal (when known), then
  // the most moves.
  struct Ranked {
    std::size_t index;
    bool dnf;
    int over;
    int moves;
  };
  std::vector<Ranked> ranked;
  for (std::size_t i = 0; i < attempts.size(); ++i) {
    const MoveCountAttempt &a = attempts[i];
    if (!a.counted || a.scramble.empty()) {
      continue;
    }
    std::optional<int> best = moveCount_ ? optimalOf(a) : std::nullopt;
    int over = best && *best >= 0 ? a.moves - *best : 0;
    ranked.push_back({i, a.dnf, over, a.moves});
  }
  std::stable_sort(ranked.begin(), ranked.end(),
                   [](const Ranked &a, const Ranked &b) {
                     if (a.dnf != b.dnf) {
                       return a.dnf;
                     }
                     if (a.over != b.over) {
                       return a.over > b.over;
                     }
                     return a.moves > b.moves;
                   });
  if (ranked.empty()) {
    emit message(tr("No solves to retry yet"));
    return;
  }
  retryQueue_.clear();
  for (std::size_t i = 0; i < std::min(kWorst, ranked.size()); ++i) {
    retryQueue_.push_back(ranked[i].index);
  }
  retryCount_ = retryQueue_.size();
  std::size_t first = retryQueue_.front();
  retryQueue_.pop_front();
  startRetry(first);
}

std::vector<vcube::Hold> AttemptPage::optimalHolds() const {
  if (settings_.optimalAnyHold) {
    return {allHolds().begin(), allHolds().end()};
  }
  return withBothEoAxes(settings_.neutrality.holds);
}

std::optional<int> AttemptPage::optimalOf(const MoveCountAttempt &a) const {
  SolveStep step = store_.sessions()[store_.currentIndex()].step;
  auto it = optimal_.find({step, a.scramble, settings_.optimalAnyHold});
  if (it == optimal_.end()) {
    return std::nullopt;
  }
  return it->second;
}

void AttemptPage::requestOptimal() {
  if (!moveCount_ || optimalWatcher_.isRunning() ||
      !hasOptimalSolver(session().step)) {
    return;
  }
  SolveStep step = session().step;
  bool anyHold = settings_.optimalAnyHold;
  std::vector<std::string> missing;
  for (const MoveCountAttempt &a : session().attempts) {
    if (!a.scramble.empty() && !optimal_.contains({step, a.scramble, anyHold}) &&
        std::find(missing.begin(), missing.end(), a.scramble) ==
            missing.end()) {
      missing.push_back(a.scramble);
    }
  }
  if (missing.empty()) {
    return;
  }
  optimalFor_ = {step, std::string(), anyHold};
  optimalRoundRun_ = optimalRound_;
  std::vector<vcube::Hold> holds = optimalHolds();
  optimalWatcher_.setFuture(QtConcurrent::run([missing, step, holds] {
    OptimalResults out;
    for (const std::string &scramble : missing) {
      ParseResult parsed = parseAlg(scramble);
      std::optional<int> best =
          parsed ? heldDistance(parsed.alg, step, holds) : std::nullopt;
      // An unreadable scramble is remembered as -1 so it isn't tried again.
      out.emplace_back(scramble, best.value_or(-1));
    }
    return out;
  }));
}

void AttemptPage::refreshSessionMenu() {
  sessionMenu_->clear();
  auto *group = new QActionGroup(sessionMenu_);
  for (std::size_t i = 0; i < store_.sessions().size(); ++i) {
    const MoveCountSession &s = store_.sessions()[i];
    QAction *a = sessionMenu_->addAction(
        tr("%1 (%2)").arg(qs(s.name)).arg(s.attempts.size()), this,
        [this, i] { selectSession(i); });
    a->setCheckable(true);
    a->setChecked(i == store_.currentIndex());
    group->addAction(a);
  }
  sessionMenu_->addSeparator();
  sessionMenu_->addAction(tr("New session…"), this, &AttemptPage::addSession);
  sessionMenu_->addAction(tr("Rename…"), this, &AttemptPage::renameSession);
  sessionMenu_->addAction(tr("Delete…"), this, &AttemptPage::deleteSession);
  if (onVirtualCube() && !scramblesOnly()) {
    sessionMenu_->addSeparator();
    QAction *worst = sessionMenu_->addAction(
        tr("Retry the worst %1").arg(kWorst), this, &AttemptPage::retryWorst);
    worst->setToolTip(moveCount_ ? tr("Retry the DNFs and the solves furthest "
                                      "over the optimal, one after another")
                                 : tr("Retry the DNFs and the longest solves, "
                                      "one after another"));
    worst->setEnabled(!session().attempts.empty());
  }
}

void AttemptPage::selectSession(std::size_t index) {
  if (isBusy()) {
    return;
  }
  if (index != store_.currentIndex()) {
    deleted_.reset(); // undo is for the session it was deleted from
  }
  retry_.reset();
  retryQueue_.clear();
  store_.setCurrentIndex(index);
  sessionButton_->setText(tr("Session: %1").arg(qs(session().name)));
  updateStepLabel();
  showCubeKind();
  typeBox_->setCurrentType(qs(session().scrambleType));
  showFreshScramble(); // onto the session's cube
  refreshAttempts();
  save();
}

void AttemptPage::refreshAttempts() {
  const auto &attempts = session().attempts;
  std::vector<Solve> solves = attemptsAsSolves(attempts);
  auto ao5 = rollingAverages(solves, 5);
  auto ao12 = rollingAverages(solves, 12);
  bool onlyScrambles = scramblesOnly();
  table_->setRowCount(static_cast<int>(attempts.size()));
  if (moveCount_) {
    table_->setColumnHidden(2, !hasOptimalSolver(session().step));
  }
  std::size_t counted = 0; // index into solves
  for (std::size_t i = 0; i < attempts.size(); ++i) {
    const MoveCountAttempt &a = attempts[i];
    int row = static_cast<int>(attempts.size() - 1 - i);
    auto set = [&](int col, const QString &text) {
      auto *item = new QTableWidgetItem(text);
      item->setData(Qt::UserRole, static_cast<int>(i));
      if (col > 0 && !(onlyScrambles && col == 1)) {
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      }
      table_->setItem(row, col, item);
    };
    int col = 0;
    // A solve with a comment is marked.
    set(col++, a.comment.empty() ? QString::number(i + 1)
                                 : QString("%1 *").arg(i + 1));
    if (onlyScrambles) {
      set(col++, qs(a.scramble));
      set(col++, dateText(a.timestamp));
      set(col++, QString());
    } else {
      set(col++, qs(formatAttempt(a)));
      if (moveCount_) {
        std::optional<int> best = hasOptimalSolver(session().step)
                                      ? optimalOf(a)
                                      : std::optional<int>(-1);
        set(col++, !best       ? QString("…")
                   : *best < 0 ? QString("-")
                               : QString::number(*best));
      }
      if (a.counted) {
        set(col++, movesText(ao5[counted]));
        set(col++, movesText(ao12[counted]));
        ++counted;
      } else {
        set(col++, "-");
        set(col++, "-");
      }
    }
    QStringList tip;
    if (!a.comment.empty()) {
      tip << tr("Comment: %1").arg(qs(a.comment));
    }
    tip << dateText(a.timestamp);
    tip << tr("Scramble: %1").arg(qs(a.scramble));
    if (!a.solution.empty()) {
      tip << tr("Solution: %1").arg(qs(a.solution));
    }
    for (int c = 0; c < table_->columnCount(); ++c) {
      table_->item(row, c)->setToolTip(tip.join("\n"));
    }
  }
  if (onlyScrambles) {
    // No counts: how many scrambles were done, and when.
    std::int64_t today = localDayStart();
    std::size_t todayCount = static_cast<std::size_t>(
        std::count_if(attempts.begin(), attempts.end(),
                      [today](const MoveCountAttempt &a) {
                        return a.timestamp >= today;
                      }));
    QString html = tr("<p>Solves: %1 &nbsp; Today: %2</p>")
                       .arg(attempts.size())
                       .arg(todayCount);
    if (!attempts.empty()) {
      html += tr("<p>First: %1<br>Last: %2</p>")
                  .arg(dateText(attempts.front().timestamp),
                       dateText(attempts.back().timestamp));
    }
    stats_->setText(html);
    chart_->setSolves({});
    lastLabel_->clear();
    updateControls();
    return;
  }
  SessionSummary sum = summarize(solves);
  QString html =
      "<table cellspacing='4' width='100%'><tr><th></th><th align='right'>" +
      tr("current") + "</th><th align='right'>" + tr("best") + "</th></tr>";
  QString colour = palette().color(QPalette::WindowText).name();
  auto link = [&colour](const QString &href, StatValue v,
                        const QString &text) {
    return v ? QString("<a href='%1' style='text-decoration:none; color:%3'>"
                       "%2</a>")
                   .arg(href, text, colour)
             : text;
  };
  auto row = [&](const QString &name, const QString &id, StatValue cur,
                 StatValue best, int decimals) {
    html += QString("<tr><td>%1</td><td align='right'>%2</td><td "
                    "align='right'><b>%3</b></td></tr>")
                .arg(name, link("cur:" + id, cur, movesText(cur, decimals)),
                     link("best:" + id, best, movesText(best, decimals)));
  };
  row(tr("single"), "single",
      solves.empty() ? StatValue{} : StatValue{effectiveTime(solves.back())},
      sum.best, 0);
  row("mo3", "mo3", currentMean(solves, 3), bestMean(solves, 3), 2);
  for (std::size_t n : {5u, 12u, 100u}) {
    QString id = QString("ao%1").arg(n);
    row(id, id, currentAverage(solves, n), bestAverage(solves, n), 2);
  }
  html += "</table>";
  html += tr("<p>Solves: %1%2 &nbsp; Mean: %3</p>")
              .arg(attempts.size())
              .arg(sum.dnfs ? QString(" (%1 DNF)").arg(sum.dnfs) : QString())
              .arg(link("mean:all", sum.mean, movesText(sum.mean)));
  if (moveCount_ && hasOptimalSolver(session().step)) {
    // How far over the optimal the counted, finished attempts were.
    int over = 0, known = 0, optimal = 0;
    for (const MoveCountAttempt &a : attempts) {
      std::optional<int> best = optimalOf(a);
      if (a.counted && !a.dnf && best && *best >= 0) {
        over += a.moves - *best;
        optimal += a.moves <= *best;
        ++known;
      }
    }
    if (known > 0) {
      double mean = static_cast<double>(over) / known;
      html += tr("<p>Over optimal: %1 on average; optimal %2 of %3</p>")
                  .arg(QString("%1%2")
                           .arg(mean >= 0 ? "+" : "")
                           .arg(mean, 0, 'f', 2))
                  .arg(optimal)
                  .arg(known);
    }
    requestOptimal();
  }
  stats_->setText(html);
  focusStats_->setText(tr("ao5 %1 · ao12 %2 · best %3 · %4 solves")
                           .arg(movesText(currentAverage(solves, 5)),
                                movesText(currentAverage(solves, 12)),
                                movesText(sum.best, 0))
                           .arg(attempts.size()));
  chart_->setSolves(solves);
  updateControls();
  if (retry_) {
    return; // the label is the retry's
  }
  if (attempts.empty() || !attempts.back().counted) {
    lastLabel_->clear();
  } else {
    const MoveCountAttempt &last = attempts.back();
    lastLabel_->setText(last.dnf ? tr("Last: %1").arg(qs(formatAttempt(last)))
                        : last.moves == 1
                            ? tr("Last: 1 move")
                            : tr("Last: %1 moves").arg(last.moves));
  }
}

void AttemptPage::showStat(const QString &link) {
  const auto &attempts = session().attempts;
  std::vector<Solve> solves = attemptsAsSolves(attempts);
  QStringList parts = link.split(':');
  if (parts.size() != 2 || solves.empty()) {
    return;
  }
  // The counted attempts, by their numbers in the whole list.
  std::vector<std::size_t> numbers;
  for (std::size_t i = 0; i < attempts.size(); ++i) {
    if (attempts[i].counted) {
      numbers.push_back(i);
    }
  }
  for (std::size_t i = 0; i < solves.size(); ++i) {
    solves[i].scramble = attempts[numbers[i]].scramble;
    solves[i].comment = attempts[numbers[i]].comment;
  }
  bool best = parts[0] == "best";
  QString id = parts[1];
  auto result = [&](std::size_t from) {
    return [&, from](std::size_t i) {
      return qs(formatAttempt(attempts[numbers[from + i]]));
    };
  };
  if (id == "single") {
    std::size_t at = solves.size() - 1;
    if (best) {
      for (std::size_t i = 0; i < solves.size(); ++i) {
        if (effectiveTime(solves[i]) < effectiveTime(solves[at])) {
          at = i;
        }
      }
    }
    showDetails(static_cast<int>(numbers[at]));
    return;
  }
  if (parts[0] == "mean") {
    showTextDialog(this, tr("Session mean"),
                   statListing(tr("Mean of %1: %2")
                                   .arg(solves.size())
                                   .arg(movesText(summarize(solves).mean)),
                               solves, 1, false, result(0)));
    return;
  }
  bool mean = id == "mo3";
  std::size_t n = mean ? 3 : id.mid(2).toULongLong();
  if (n == 0 || solves.size() < n) {
    return;
  }
  std::size_t from = solves.size() - n;
  if (best) {
    std::optional<std::size_t> at = bestAverageStart(solves, n, mean);
    if (!at) {
      return;
    }
    from = *at;
  }
  auto span = std::span<const Solve>(solves).subspan(from, n);
  StatValue value = mean ? meanOf(span) : averageOf(span);
  QString heading = (mean ? tr("Mean of %1: %2") : tr("Average of %1: %2"))
                        .arg(n)
                        .arg(movesText(value));
  showTextDialog(this, best ? tr("Best %1").arg(id) : tr("Current %1").arg(id),
                 statListing(heading, span, numbers[from] + 1, !mean,
                             result(from)));
}

void AttemptPage::deleteAttempt(int index) {
  auto &attempts = session().attempts;
  if (isBusy() || index < 0 || index >= static_cast<int>(attempts.size())) {
    return;
  }
  deleted_ = {static_cast<std::size_t>(index),
              attempts[static_cast<std::size_t>(index)]};
  attempts.erase(attempts.begin() + index);
  if (retry_) {
    // The attempts after it have moved up one.
    retryQueue_.clear();
  }
  save();
  refreshAttempts();
  showEntry();
  emit message(tr("Deleted solve #%1 (Ctrl+Z puts it back)").arg(index + 1));
}

void AttemptPage::undoDelete() {
  if (!deleted_ || isBusy()) {
    return;
  }
  auto &attempts = session().attempts;
  auto at =
      static_cast<std::ptrdiff_t>(std::min(deleted_->first, attempts.size()));
  attempts.insert(attempts.begin() + at, deleted_->second);
  emit message(tr("Put back solve #%1").arg(at + 1));
  deleted_.reset();
  save();
  refreshAttempts();
  showEntry();
}

void AttemptPage::showAttemptMenu(const QPoint &pos) {
  auto *item = table_->itemAt(pos);
  if (!item || isBusy()) {
    return; // nothing changes in the middle of a solve
  }
  int index = item->data(Qt::UserRole).toInt();
  auto &attempts = session().attempts;
  if (index < 0 || index >= static_cast<int>(attempts.size())) {
    return;
  }
  MoveCountAttempt &a = attempts[static_cast<std::size_t>(index)];
  QMenu menu(this);
  QAction *edit = nullptr;
  QAction *dnf = nullptr;
  if (a.counted) {
    // A virtual solve's count is its solution's.
    if (a.solution.empty()) {
      edit = menu.addAction(tr("Edit move count…"));
    }
    dnf = menu.addAction("DNF");
    dnf->setCheckable(true);
    dnf->setChecked(a.dnf);
    menu.addSeparator();
  }
  QAction *reviewAction = nullptr;
  if (reviewable(a)) {
    reviewAction = menu.addAction(tr("Review…"));
    menu.setDefaultAction(reviewAction);
  }
  auto *details = menu.addAction(tr("Details…"));
  auto *comment = menu.addAction(tr("Comment…"));
  auto *copy = menu.addAction(tr("Copy scramble"));
  QAction *copySolution = nullptr;
  if (!a.solution.empty()) {
    copySolution = menu.addAction(tr("Copy solution"));
  }
  menu.addSeparator();
  auto *del = menu.addAction(tr("Delete"));
  QAction *chosen = menu.exec(table_->viewport()->mapToGlobal(pos));
  if (!chosen || isBusy()) {
    return;
  }
  if (chosen == reviewAction) {
    review(index);
    return;
  }
  if (chosen == details) {
    showDetails(index);
    return;
  }
  if (chosen == edit) {
    bool ok = false;
    int moves = QInputDialog::getInt(this, tr("Edit move count"), tr("Moves:"),
                                     a.moves, 0, kMaxMoveCount, 1, &ok);
    if (!ok) {
      return;
    }
    a.moves = moves;
  } else if (chosen == dnf) {
    a.dnf = !a.dnf;
  } else if (chosen == comment) {
    commentAttempt(index);
    return;
  } else if (chosen == copy) {
    QApplication::clipboard()->setText(qs(a.scramble));
    return;
  } else if (copySolution && chosen == copySolution) {
    QApplication::clipboard()->setText(qs(a.solution));
    return;
  } else if (chosen == del) {
    deleteAttempt(index);
    return;
  }
  save();
  refreshAttempts();
  showEntry();
}

void AttemptPage::addSession() {
  bool ok = false;
  QString suggested = moveCount_
                          ? qs(std::string(stepName(session().step)))
                          : tr("Session %1").arg(store_.sessions().size() + 1);
  QString name = QInputDialog::getText(
      this, tr("New session"),
      moveCount_ ? tr("Name (e.g. the step, such as EO Cross):") : tr("Name:"),
      QLineEdit::Normal, suggested, &ok);
  if (!ok || name.trimmed().isEmpty()) {
    return;
  }
  SolveStep step = session().step;
  CubeKind cube = session().cube;
  // A session named after a step is for that step.
  if (moveCount_) {
    step = stepFromName(ss(name.trimmed())).value_or(step);
  }
  MoveCountSession &s = store_.addSession(ss(name.trimmed()));
  s.scrambleType = moveCount_ && step != session().step
                       ? defaultScrambleType(step)
                       : ss(typeBox_->currentType());
  s.step = step;
  s.cube = cube;
  selectSession(store_.currentIndex());
}

void AttemptPage::renameSession() {
  bool ok = false;
  QString name =
      QInputDialog::getText(this, tr("Rename session"), tr("Name:"),
                            QLineEdit::Normal, qs(session().name), &ok);
  if (!ok || name.trimmed().isEmpty()) {
    return;
  }
  // Only the name changes: the scramble, a typed count and a retry stay.
  session().name = ss(name.trimmed());
  sessionButton_->setText(tr("Session: %1").arg(qs(session().name)));
  updateStepLabel();
  save();
}

void AttemptPage::deleteSession() {
  auto answer =
      QMessageBox::question(this, tr("Delete session"),
                            tr("Delete session \"%1\" and its %2 solves?")
                                .arg(qs(session().name))
                                .arg(session().attempts.size()));
  if (answer != QMessageBox::Yes) {
    return;
  }
  store_.removeSession(store_.currentIndex());
  deleted_.reset();
  selectSession(store_.currentIndex());
}

void AttemptPage::save() {
  QString error;
  if (!store_.save(&error)) {
    emit message(tr("Could not save sessions: %1").arg(error));
  }
}

} // namespace vcube
