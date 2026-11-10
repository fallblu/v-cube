#include "app/attempt_page.hpp"

#include "io/data_file.hpp"

#include "app/flow_layout.hpp"
#include "app/review_dialog.hpp"
#include "train/step_solver.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
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
  sessionButton_->setToolTip(tr("Switch, add, rename or delete sessions"));
  sessionMenu_ = new QMenu(sessionButton_);
  sessionButton_->setMenu(sessionMenu_);
  leftLayout->addWidget(sessionButton_);

  stats_ = new QLabel(left);
  stats_->setTextFormat(Qt::RichText);
  stats_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  leftLayout->addWidget(stats_);

  if (moveCount_) {
    table_ = new QTableWidget(0, 5, left);
    table_->setHorizontalHeaderLabels(
        {"#", tr("Moves"), tr("Opt"), "ao5", "ao12"});
    table_->horizontalHeaderItem(2)->setToolTip(
        tr("The optimal move count for the scramble's step in the holds of "
           "your colour neutrality (either EO axis)"));
  } else {
    table_ = new QTableWidget(0, 4, left);
    table_->setHorizontalHeaderLabels({"#", tr("Moves"), "ao5", "ao12"});
  }
  table_->verticalHeader()->setVisible(false);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setFocusPolicy(Qt::NoFocus);
  table_->setContextMenuPolicy(Qt::CustomContextMenu);
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::ResizeToContents);
  leftLayout->addWidget(table_, 1);

  // --- right column: scramble, cube or entry, chart ---
  auto *right = new QWidget(this);
  auto *rightLayout = new QVBoxLayout(right);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  typeRow_ = new QWidget(right);
  auto *typeRow = new FlowLayout(typeRow_);
  typeRow->setContentsMargins(0, 0, 0, 0);
  if (moveCount_) {
    stepBox_ = new QComboBox(typeRow_);
    stepBox_->setFocusPolicy(Qt::NoFocus);
    for (SolveStep step : solveSteps()) {
      stepBox_->addItem(qs(std::string(stepName(step))));
    }
    stepBox_->setToolTip(
        tr("The step this session is for. On the virtual cube a scramble "
           "counts only if the step is done (on any colour) when you press "
           "Space."));
    typeRow->addWidget(new QLabel(tr("Step:"), typeRow_));
    typeRow->addWidget(stepBox_);
    typeRow->addSpacing(12);
    typeBox_ = new ScrambleTypeBox(library, typeRow_, offeredTypes());
    typeBox_->setToolTip(
        tr("What the scramble leaves solved: a full WCA scramble for EO, "
           "EOLine, EO Cross or cross, EO Cross solved for ZZ F2L, or the "
           "cross solved for CFOP F2L"));
  } else {
    typeBox_ = new ScrambleTypeBox(library, typeRow_);
  }
  cubeBox_ = new QComboBox(typeRow_);
  cubeBox_->setFocusPolicy(Qt::NoFocus);
  cubeBox_->addItem(tr("Virtual cube"));
  cubeBox_->addItem(tr("Physical cube"));
  cubeBox_->setToolTip(tr("The cube this session's solves are done on"));
  cubeView_ = new CubeView(right);
  viewPicker_ = new QWidget(typeRow_);
  auto *viewLayout = new QHBoxLayout(viewPicker_);
  viewLayout->setContentsMargins(0, 0, 0, 0);
  viewLayout->addWidget(new QLabel(tr("View:"), viewPicker_));
  viewLayout->addWidget(createViewSelector(cubeView_, mode, viewPicker_));
  skipButton_ = flatButton(tr("Skip scramble"), typeRow_);
  skipButton_->setToolTip(
      tr("Go on to the next scramble without recording this one"));
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
  deleteButton_ = flatButton(tr("Delete last"), right);
  reviewButton_ = flatButton(tr("Review last"), right);
  reviewButton_->setToolTip(
      tr("Replay the last solve with its steps marked, see the optimal "
         "solutions and how the F2L pairs went (double-click any solve to "
         "review it)"));
  recordButton_->setToolTip(tr("Record the move count for this scramble"));
  dnfButton_->setToolTip(
      tr("Record this scramble as DNF, e.g. when the step went wrong"));
  deleteButton_->setToolTip(tr("Delete the last recorded solve"));
  buttonRow->addStretch();
  buttonRow->addWidget(recordButton_);
  buttonRow->addWidget(dnfButton_);
  buttonRow->addWidget(deleteButton_);
  buttonRow->addWidget(reviewButton_);
  buttonRow->addStretch();
  lastLabel_ = new QLabel(right);
  lastLabel_->setAlignment(Qt::AlignCenter);
  centre->addStretch(1);
  centre->addWidget(stepLabel_);
  centre->addWidget(cubeView_, 8);
  centre->addWidget(display_);
  centre->addWidget(hintLabel_);
  centre->addLayout(buttonRow);
  centre->addWidget(lastLabel_);
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
      SolveStep step = solveSteps()[static_cast<std::size_t>(index)];
      if (isBusy() || session().step == step) {
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
    session().cube = kind;
    retry_.reset();
    save();
    showCubeKind();
    showFreshScramble();
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
  connect(table_, &QTableWidget::customContextMenuRequested, this,
          &AttemptPage::showAttemptMenu);
  connect(table_, &QTableWidget::itemDoubleClicked, this,
          [this](QTableWidgetItem *item) {
            review(item->data(Qt::UserRole).toInt());
          });
  connect(reviewButton_, &QPushButton::clicked, this,
          [this] { review(static_cast<int>(session().attempts.size()) - 1); });
  connect(&optimalWatcher_,
          &QFutureWatcher<std::vector<std::pair<std::string, int>>>::finished,
          this, [this] {
            for (const auto &[scramble, length] : optimalWatcher_.result()) {
              optimal_[{optimalStep_, scramble}] = length;
            }
            refreshAttempts();
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

bool AttemptPage::onVirtualCube() const {
  return store_.sessions()[store_.currentIndex()].cube == CubeKind::Virtual;
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
  if (on) {
    returnFocus();
  }
  leftPanel_->setVisible(!on);
  typeRow_->setVisible(!on);
  hintLabel_->setVisible(!on);
  chart_->setVisible(!on);
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
  optimal_.clear(); // the holds may have changed
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
      text = tr("Retry of #%1 (not recorded). ").arg(retry_->index + 1) + text +
             " " + tr("Skip scramble goes back to new scrambles.");
    }
  } else if (phase_ == Phase::Solving) {
    text = moveCount_
               ? tr("Press Space when the step is done. Esc starts the "
                    "scramble again.")
               : tr("Press Space when the cube is solved. Esc starts the "
                    "scramble again.");
  } else if (retry_) {
    text = tr("The cube turns freely. Space: back to new scrambles. Esc: "
              "reset the cube to solved.");
  } else {
    text = tr("The cube turns freely. Space: next scramble. Esc: reset the "
              "cube to solved.");
  }
  hintLabel_->setText(text);
}

void AttemptPage::setEntry(const QString &digits) {
  entry_ = digits;
  showEntry();
}

void AttemptPage::showEntry() {
  if (onVirtualCube()) {
    if (phase_ == Phase::Solving) {
      display_->showText(QString::number(attempt_.count()));
    } else if (phase_ == Phase::Done || !session().attempts.empty()) {
      // The last result, until the next solve starts.
      const auto &attempts = session().attempts;
      display_->showText(attempts.empty() ? QString("–")
                                          : qs(formatAttempt(attempts.back())),
                         attempts.empty() || attempts.back().dnf
                             ? palette().color(QPalette::PlaceholderText)
                             : QColor());
    } else {
      display_->showText("–", palette().color(QPalette::PlaceholderText));
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

void AttemptPage::updateControls() {
  bool busy = isBusy();
  for (QWidget *w : std::initializer_list<QWidget *>{
           sessionButton_, typeBox_, cubeBox_, skipButton_, deleteButton_}) {
    w->setEnabled(!busy);
  }
  if (stepBox_) {
    stepBox_->setEnabled(!busy);
  }
  deleteButton_->setEnabled(!busy && !session().attempts.empty());
  bool reviewable = false;
  if (!session().attempts.empty()) {
    const MoveCountAttempt &last = session().attempts.back();
    reviewable =
        !last.solution.empty() || (moveCount_ && !last.scramble.empty());
  }
  reviewButton_->setVisible(reviewable);
  reviewButton_->setEnabled(!busy);
}

bool AttemptPage::handleKey(QKeyEvent *e, bool press) {
  int key = e->key();
  if (onVirtualCube()) {
    std::optional<Move> move = settings_.keymap.lookup(e);
    bool known = move || key == Qt::Key_Space || key == Qt::Key_Escape ||
                 (key == Qt::Key_Tab && algs_);
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
    } else if (key == Qt::Key_Space) {
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
  if (!feed_.current()) {
    return; // no scramble on the cube yet
  }
  if (!attempt_.turn(m)) {
    emit message(tr("Only rotations until you press Space to start"));
    return;
  }
  cubeView_->animateMove(m);
  if (phase_ == Phase::Solving) {
    showEntry();
  }
}

void AttemptPage::pressStart() {
  if (!feed_.current()) {
    emit message(tr("Wait for the scramble before starting"));
    return;
  }
  hold_ = Hold::Holding;
  holdTimer_.start(static_cast<int>(settings_.holdMs));
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
  showHold();
}

void AttemptPage::showHold() {
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
  if (!feed_.current()) {
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
  if (retry_) {
    // Compared with the attempt it repeats, and not recorded.
    int count = attempt_.count();
    const MoveCountAttempt &was = retry_->original;
    QString result = !reached     ? tr("DNF (%1)").arg(count)
                     : count == 1 ? tr("1 move")
                                  : tr("%1 moves").arg(count);
    QString text = tr("Retry of #%1: %2, was %3")
                       .arg(retry_->index + 1)
                       .arg(result, qs(formatAttempt(was)));
    if (moveCount_ && hasOptimalSolver(session().step)) {
      if (std::optional<int> best =
              heldDistance(parseAlgOrEmpty(was.scramble), session().step,
                           withBothEoAxes(settings_.neutrality.holds))) {
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
  record(attempt_.count(), !reached, toString(attempt_.solution()));
  if (moveCount_) {
    next(); // on to the next scramble
  } else {
    setPhase(Phase::Done);
    showEntry();
  }
  if (!reached) {
    emit message(moveCount_
                     ? tr("DNF: %1 wasn't done when you pressed Space")
                           .arg(qs(std::string(stepName(session().step))))
                     : tr("DNF: the cube wasn't solved when you pressed "
                          "Space"));
  }
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
  save();
  refreshAttempts();
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
  if (!onVirtualCube() && !moveCount_) {
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
    endRetry();
    return;
  }
  entry_.clear();
  index_ = history_.size();
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
  Alg applied;
  for (Move m : parsed.alg) {
    if (attempt_.turn(m)) {
      applied.push_back(m);
    }
  }
  cubeView_->animateSequence(applied);
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
  if (a.solution.empty() && !(moveCount_ && !a.scramble.empty())) {
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
  ReviewDialog dialog(solve, settings_, &library_, this);
  dialog.setRetryEnabled(onVirtualCube());
  connect(&dialog, &ReviewDialog::retryRequested, this,
          [this, index] { startRetry(static_cast<std::size_t>(index)); });
  dialog.exec();
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
  bool feedSolved = retry_ && retry_->feedSolved;
  retry_.reset();
  setPhase(Phase::Inspect);
  if (feedSolved) {
    feed_.advance(); // the scramble shown before was solved already
  } else {
    showScramble(); // the scramble that was shown before the retry
  }
  refreshAttempts();
}

std::optional<int> AttemptPage::optimalOf(const MoveCountAttempt &a) const {
  SolveStep step = store_.sessions()[store_.currentIndex()].step;
  auto it = optimal_.find({step, a.scramble});
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
  std::vector<std::string> missing;
  for (const MoveCountAttempt &a : session().attempts) {
    if (!a.scramble.empty() && !optimal_.contains({step, a.scramble}) &&
        std::find(missing.begin(), missing.end(), a.scramble) ==
            missing.end()) {
      missing.push_back(a.scramble);
    }
  }
  if (missing.empty()) {
    return;
  }
  optimalStep_ = step;
  std::vector<vcube::Hold> holds = withBothEoAxes(settings_.neutrality.holds);
  optimalWatcher_.setFuture(QtConcurrent::run([missing, step, holds] {
    std::vector<std::pair<std::string, int>> out;
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
}

void AttemptPage::selectSession(std::size_t index) {
  if (isBusy()) {
    return;
  }
  retry_.reset();
  store_.setCurrentIndex(index);
  sessionButton_->setText(tr("Session: %1").arg(qs(session().name)));
  if (stepBox_) {
    stepBox_->setCurrentIndex(static_cast<int>(session().step));
    QString step = qs(std::string(stepName(session().step)));
    QString name = qs(session().name);
    stepLabel_->setText(name == step ? name : tr("%1 (%2)").arg(name, step));
  }
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
      if (col > 0) {
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      }
      table_->setItem(row, col, item);
    };
    int col = 0;
    set(col++, QString::number(i + 1));
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
    QStringList tip;
    if (!a.comment.empty()) {
      tip << qs(a.comment);
    }
    tip << tr("Scramble: %1").arg(qs(a.scramble));
    if (!a.solution.empty()) {
      tip << tr("Solution: %1").arg(qs(a.solution));
    }
    table_->item(row, 1)->setToolTip(tip.join("\n"));
  }
  SessionSummary sum = summarize(solves);
  QString html =
      "<table cellspacing='4' width='100%'><tr><th></th><th align='right'>" +
      tr("current") + "</th><th align='right'>" + tr("best") + "</th></tr>";
  auto row = [&](const QString &name, const QString &cur, const QString &best) {
    html += QString("<tr><td>%1</td><td align='right'>%2</td><td "
                    "align='right'><b>%3</b></td></tr>")
                .arg(name, cur, best);
  };
  row(tr("single"),
      solves.empty() ? QString("-")
                     : movesText(effectiveTime(solves.back()), 0),
      movesText(sum.best, 0));
  row("mo3", movesText(currentMean(solves, 3)), movesText(bestMean(solves, 3)));
  row("ao5", movesText(currentAverage(solves, 5)),
      movesText(bestAverage(solves, 5)));
  row("ao12", movesText(currentAverage(solves, 12)),
      movesText(bestAverage(solves, 12)));
  row("ao100", movesText(currentAverage(solves, 100)),
      movesText(bestAverage(solves, 100)));
  html += "</table>";
  html += (moveCount_ ? tr("<p>Attempts: %1%2 &nbsp; Mean: %3</p>")
                      : tr("<p>Solves: %1%2 &nbsp; Mean moves: %3</p>"))
              .arg(attempts.size())
              .arg(sum.dnfs ? QString(" (%1 DNF)").arg(sum.dnfs) : QString())
              .arg(movesText(sum.mean));
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
      html += tr("<p>Over optimal: +%1 on average; optimal %2 of %3</p>")
                  .arg(static_cast<double>(over) / known, 0, 'f', 2)
                  .arg(optimal)
                  .arg(known);
    }
    requestOptimal();
  }
  stats_->setText(html);
  chart_->setSolves(solves);
  updateControls();
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

void AttemptPage::deleteAttempt(int index) {
  auto &attempts = session().attempts;
  if (index < 0 || index >= static_cast<int>(attempts.size())) {
    return;
  }
  attempts.erase(attempts.begin() + index);
  save();
  refreshAttempts();
  showEntry();
}

void AttemptPage::showAttemptMenu(const QPoint &pos) {
  auto *item = table_->itemAt(pos);
  if (!item) {
    return;
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
    edit = menu.addAction(tr("Edit move count…"));
    dnf = menu.addAction("DNF");
    dnf->setCheckable(true);
    dnf->setChecked(a.dnf);
    menu.addSeparator();
  }
  QAction *reviewAction = nullptr;
  if (!a.solution.empty() || (moveCount_ && !a.scramble.empty())) {
    reviewAction = menu.addAction(tr("Review…"));
    menu.setDefaultAction(reviewAction);
  }
  auto *comment = menu.addAction(tr("Comment…"));
  auto *copy = menu.addAction(tr("Copy scramble"));
  QAction *copySolution = nullptr;
  if (!a.solution.empty()) {
    copySolution = menu.addAction(tr("Copy solution"));
  }
  menu.addSeparator();
  auto *del = menu.addAction(tr("Delete"));
  QAction *chosen = menu.exec(table_->viewport()->mapToGlobal(pos));
  if (!chosen) {
    return;
  }
  if (chosen == reviewAction) {
    review(index);
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
    bool ok = false;
    QString text = QInputDialog::getText(this, tr("Comment"), tr("Comment:"),
                                         QLineEdit::Normal, qs(a.comment), &ok);
    if (!ok) {
      return;
    }
    a.comment = ss(text);
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
  MoveCountSession &s = store_.addSession(ss(name.trimmed()));
  s.scrambleType = ss(typeBox_->currentType());
  s.step = step;
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
  session().name = ss(name.trimmed());
  selectSession(store_.currentIndex());
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
  selectSession(store_.currentIndex());
}

void AttemptPage::save() {
  // Written in the background from a copy, so that saving never holds up
  // the next attempt.
  writeInBackground(
      store_.path(),
      [copy = store_](QString *error) { return copy.save(error); }, this,
      [this](const QString &error) {
        emit message(tr("Could not save sessions: %1").arg(error));
      });
}

} // namespace vcube
