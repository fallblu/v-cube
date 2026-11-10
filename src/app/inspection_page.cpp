#include "app/inspection_page.hpp"

#include "app/flow_layout.hpp"

#include "io/data_file.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace vcube {

namespace {

QPushButton *flatButton(const QString &text, QWidget *parent) {
  auto *b = new QPushButton(text, parent);
  b->setFocusPolicy(Qt::NoFocus);
  return b;
}

QComboBox *flatBox(QWidget *parent) {
  auto *b = new QComboBox(parent);
  b->setFocusPolicy(Qt::NoFocus);
  return b;
}

// A label and a widget kept together, to show or hide as one.
QWidget *picker(const QString &label, QWidget *widget, QWidget *parent) {
  auto *w = new QWidget(parent);
  auto *layout = new QHBoxLayout(w);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(new QLabel(label, w));
  layout->addWidget(widget);
  widget->setParent(w);
  return w;
}

QString movesText(StatValue v, int decimals = 2) {
  return QString::fromStdString(formatMoves(v, decimals));
}

QString secondsText(std::int64_t ms) {
  return QString::number(static_cast<double>(ms) / 1000.0, 'f', 1) + " s";
}

// The time limits offered, in seconds (0: none); 15 is WCA inspection with
// the 8 and 12 second calls.
constexpr std::array<int, 5> kLimits = {15, 10, 20, 30, 0};

// The two colours of an axis of a hold: 0 front-back, 1 right-left.
QString axisColours(Hold h, int axis) {
  std::array<int, 6> colours = holdColours(h);
  int a = colours[axis == 0 ? 2 : 1];
  int b = colours[axis == 0 ? 5 : 4];
  return QString("%1–%2").arg(colourName(a).toLower(), colourName(b).toLower());
}

const char *axisName(int axis) { return axis == 0 ? "F/B" : "R/L"; }

// A Space this soon after the time limit hid the cube was meant to hide it.
constexpr std::int64_t kHideGraceMs = 600;
// Optimal solutions worked out per hold, to show three that differ.
constexpr int kSolutionsPerHold = 12;

constexpr std::array<Slot, 4> kSlots4 = {Slot::FL, Slot::BL, Slot::FR,
                                         Slot::BR};

// "BL" -> "back-left".
QString slotWords(Slot s) {
  switch (s) {
  case Slot::FR:
    return QObject::tr("front-right");
  case Slot::FL:
    return QObject::tr("front-left");
  case Slot::BL:
    return QObject::tr("back-left");
  case Slot::BR:
    return QObject::tr("back-right");
  }
  return {};
}

// The hold a step is done in, of those it is: the one the cube is held
// in now if it is one of them, else one with the same top colour, else the
// first.
std::optional<Hold> doneHold(const FaceletCube &cube, SolveStep step) {
  std::vector<Hold> holds = stepReachedHolds(cube, step);
  if (holds.empty()) {
    return std::nullopt;
  }
  if (std::optional<Hold> now = holdOf(cube)) {
    if (std::find(holds.begin(), holds.end(), *now) != holds.end()) {
      return *now;
    }
    for (Hold h : holds) {
      if (h.top == now->top) {
        return h;
      }
    }
  }
  return holds.front();
}

// What a cube that didn't reach `step` did get: "EO not done (3 bad edges
// left); 2 of 4 cross edges solved".
QString progressText(const FaceletCube &cube, SolveStep step) {
  StepProgress p = stepProgress(cube);
  if (step == SolveStep::EO) {
    return p.badEdges == 1 ? QObject::tr("1 bad edge left")
                           : QObject::tr("%1 bad edges left").arg(p.badEdges);
  }
  QString eo =
      p.badEdges == 0 ? QObject::tr("EO done")
      : p.badEdges == 1
          ? QObject::tr("EO not done (1 bad edge left)")
          : QObject::tr("EO not done (%1 bad edges left)").arg(p.badEdges);
  QString cross = QObject::tr("%1 of 4 cross edges solved").arg(p.crossEdges);
  if (step == SolveStep::EOLine) {
    cross += p.line ? QObject::tr(", the line among them")
                    : QObject::tr(", not the line (DF and DB)");
  }
  return eo + "; " + cross;
}

const StatusColour kGood{Status::Good};
const StatusColour kBad{Status::Bad};
const StatusColour kCall{Status::Warning};
// Ring colours for the bad edges of the two axes.
const QColor kRing[2] = {QColor(0xff, 0x2d, 0xd4), QColor(0x2d, 0xd4, 0xff)};

} // namespace

InspectionPage::InspectionPage(AppSettings &settings, const QString &dataDir,
                               QWidget *parent)
    : ModePage(parent), settings_(settings),
      store_(QDir(dataDir).filePath("inspection.json")),
      noSets_(QString(), QString()), feed_(noSets_) {
  QString error;
  if (!store_.load(&error)) {
    QString kept =
        store_.setAsidePath().isEmpty()
            ? tr("It is left where it is, and nothing will be saved over "
                 "it.")
            : tr("It was kept as %1.")
                  .arg(QDir::toNativeSeparators(store_.setAsidePath()));
    QMessageBox::warning(this, "v-cube",
                         tr("Could not read your inspection practice (%1), so "
                            "Inspection starts with the default sessions.")
                                 .arg(error) +
                             " " + kept);
  }

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
  stats_->setWordWrap(true);
  stats_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  leftLayout->addWidget(stats_);

  table_ = new QTableWidget(0, 4, left);
  table_->verticalHeader()->setVisible(false);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setFocusPolicy(Qt::NoFocus);
  table_->setContextMenuPolicy(Qt::CustomContextMenu);
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::ResizeToContents);
  leftLayout->addWidget(table_, 1);

  // --- right column: options, cube, display, result, chart ---
  auto *right = new QWidget(this);
  auto *rightLayout = new QVBoxLayout(right);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  optionsRow_ = new QWidget(right);
  auto *options = new FlowLayout(optionsRow_);
  options->setContentsMargins(0, 0, 0, 0);
  drillBox_ = flatBox(optionsRow_);
  drillBox_->addItem(tr("Plan a step"));
  drillBox_->addItem(tr("Count bad edges"));
  drillBox_->setToolTip(
      tr("What this session practises: planning a step in inspection and "
         "doing it with the cube hidden, or counting bad edges"));
  stepBox_ = flatBox(optionsRow_);
  for (SolveStep step : planSteps()) {
    stepBox_->addItem(qs(std::string(stepName(step))));
  }
  stepBox_->setToolTip(tr("The step to plan in inspection"));
  axesBox_ = flatBox(optionsRow_);
  axesBox_->addItem(tr("F/B"));
  axesBox_->addItem(tr("R/L"));
  axesBox_->addItem(tr("F/B, then R/L"));
  axesBox_->setToolTip(
      tr("The axes to count bad edges for, as the cube is held: F/B is the "
         "ZZ axis"));
  pairBox_ = flatBox(optionsRow_);
  pairBox_->addItem(tr("None"));
  for (Slot slot : kSlots4) {
    pairBox_->addItem(
        tr("%1 pair (%2)")
            .arg(qs(std::string(slotName(slot))), slotWords(slot)));
  }
  pairBox_->setToolTip(
      tr("After EO Cross, also solve this F2L pair with the cube hidden, as "
         "you hold the cube when the cross is done"));
  limitBox_ = flatBox(optionsRow_);
  limitBox_->setToolTip(tr(
      "How long inspection may take: WCA inspection (15 s, with calls at 8 "
      "and 12 s), another limit, or none. The cube is hidden when it ends."));
  cubeView_ = new CubeView(right);
  stepPicker_ = picker(tr("Step:"), stepBox_, optionsRow_);
  axesPicker_ = picker(tr("Axes:"), axesBox_, optionsRow_);
  pairPicker_ = picker(tr("Then:"), pairBox_, optionsRow_);
  limitPicker_ = picker(tr("Inspection:"), limitBox_, optionsRow_);
  viewPicker_ = picker(tr("View:"),
                       createViewSelector(cubeView_, "inspection", optionsRow_),
                       optionsRow_);
  skipButton_ = flatButton(tr("Skip scramble"), optionsRow_);
  skipButton_->setToolTip(tr("Go on to the next scramble"));
  options->addWidget(new QLabel(tr("Drill:"), optionsRow_));
  options->addWidget(drillBox_);
  options->addSpacing(12);
  options->addWidget(stepPicker_);
  options->addWidget(pairPicker_);
  options->addWidget(axesPicker_);
  options->addSpacing(12);
  options->addWidget(limitPicker_);
  options->addStretch();
  options->addWidget(viewPicker_);
  options->addWidget(skipButton_);
  rightLayout->addWidget(optionsRow_);

  stepLabel_ = new QLabel(right);
  stepLabel_->setAlignment(Qt::AlignCenter);
  QFont stepFont = stepLabel_->font();
  stepFont.setPointSizeF(stepFont.pointSizeF() * 1.3);
  stepFont.setBold(true);
  stepLabel_->setFont(stepFont);
  stepLabel_->setToolTip(tr("The session and what it practises"));
  rightLayout->addWidget(stepLabel_);

  scrambleLabel_ = new QLabel(right);
  scrambleLabel_->setWordWrap(true);
  scrambleLabel_->setTextFormat(Qt::RichText);
  scrambleLabel_->setAlignment(Qt::AlignCenter);
  scrambleLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  QFont sf = scrambleLabel_->font();
  sf.setPointSize(sf.pointSize() + 3);
  scrambleLabel_->setFont(sf);
  markScrambleLabel(scrambleLabel_);
  rightLayout->addWidget(scrambleLabel_);

  cubeStack_ = new QStackedWidget(right);
  hiddenLabel_ = new QLabel(cubeStack_);
  hiddenLabel_->setAlignment(Qt::AlignCenter);
  hiddenLabel_->setWordWrap(true);
  hiddenLabel_->setForegroundRole(QPalette::PlaceholderText);
  QFont hf = hiddenLabel_->font();
  hf.setPointSize(hf.pointSize() + 4);
  hiddenLabel_->setFont(hf);
  cubeView_->setParent(cubeStack_);
  cubeStack_->addWidget(cubeView_);
  cubeStack_->addWidget(hiddenLabel_);
  cubeStack_->setMinimumHeight(220);
  rightLayout->addWidget(cubeStack_, 8);

  display_ = new TimeDisplay(right);
  rightLayout->addWidget(display_);
  hintLabel_ = new QLabel(right);
  hintLabel_->setAlignment(Qt::AlignCenter);
  hintLabel_->setWordWrap(true);
  hintLabel_->setForegroundRole(QPalette::PlaceholderText);
  rightLayout->addWidget(hintLabel_);
  resultLabel_ = new QLabel(right);
  resultLabel_->setAlignment(Qt::AlignCenter);
  resultLabel_->setWordWrap(true);
  resultLabel_->setTextFormat(Qt::RichText);
  resultLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  rightLayout->addWidget(resultLabel_, 2);
  auto *buttonRow = new QHBoxLayout;
  reviewButton_ = flatButton(tr("Review last"), right);
  reviewButton_->setToolTip(
      tr("Replay the last attempt and see its optimal solutions (double-click "
         "any attempt to review it)"));
  retryButton_ = flatButton(tr("Retry"), right);
  retryButton_->setToolTip(
      tr("Plan the last scramble again; the retry isn't recorded"));
  deleteButton_ = flatButton(tr("Delete last"), right);
  deleteButton_->setToolTip(tr("Delete the last recorded attempt"));
  buttonRow->addStretch();
  buttonRow->addWidget(reviewButton_);
  buttonRow->addWidget(retryButton_);
  buttonRow->addWidget(deleteButton_);
  buttonRow->addStretch();
  rightLayout->addLayout(buttonRow);

  chartBox_ = flatBox(right);
  chartBox_->addItem(tr("Moves over optimal"));
  chartBox_->addItem(tr("Optimal rate (last 12)"));
  chartBox_->addItem(tr("Moves"));
  chartBox_->setToolTip(tr("What the chart shows for Plan sessions"));
  chartBox_->setCurrentIndex(
      std::clamp(QSettings().value("inspection/chart", 0).toInt(), 0,
                 chartBox_->count() - 1));
  chart_ = new TrendChart(right);
  auto *chartRow = new QHBoxLayout;
  chartRow->addWidget(chart_, 1);
  chartRow->addWidget(chartBox_, 0, Qt::AlignTop);
  rightLayout->addLayout(chartRow, 1);

  auto *split = new QSplitter(this);
  split->addWidget(left);
  split->addWidget(right);
  split->setStretchFactor(0, 0);
  split->setStretchFactor(1, 1);
  split->setSizes({300, 800});
  auto *layout = new QHBoxLayout(this);
  layout->addWidget(split);

  // --- wiring ---
  tick_.setInterval(30);
  connect(&tick_, &QTimer::timeout, this, &InspectionPage::tick);
  connect(&feed_, &ScrambleFeed::currentChanged, this,
          &InspectionPage::showScramble);
  connect(&analysis_, &QFutureWatcher<Analysis>::finished, this,
          &InspectionPage::showAnalysis);
  connect(&backfill_, &QFutureWatcher<std::vector<Filled>>::finished, this,
          [this] {
            if (backfillSession_ >= store_.sessions().size()) {
              return;
            }
            auto &attempts = store_.sessions()[backfillSession_].attempts;
            bool changed = false;
            for (const Filled &f : backfill_.result()) {
              if (f.attempt < attempts.size() &&
                  attempts[f.attempt].scramble == f.scramble &&
                  !attempts[f.attempt].optimal) {
                attempts[f.attempt].optimal = f.optimal;
                changed = true;
              }
            }
            if (changed) {
              save();
              refreshAttempts();
            }
          });
  connect(skipButton_, &QPushButton::clicked, this, &InspectionPage::skip);
  connect(drillBox_, &QComboBox::activated, this, [this](int index) {
    auto drill = index == 0 ? InspectionDrill::Plan : InspectionDrill::Edges;
    if (isBusy() || session().drill == drill) {
      return;
    }
    if (!session().attempts.empty()) {
      drillBox_->setCurrentIndex(planning() ? 0 : 1);
      emit message(tr("This session has attempts: start a new session for "
                      "the other drill"));
      return;
    }
    session().drill = drill;
    if (drill == InspectionDrill::Plan && session().limitSeconds == 0) {
      // Edges sessions have no limit (as read from a file): start with WCA
      // inspection.
      session().limitSeconds = 15;
      session().wca = true;
    }
    save();
    selectSession(store_.currentIndex());
  });
  connect(stepBox_, &QComboBox::activated, this, [this](int index) {
    SolveStep step = planSteps()[static_cast<std::size_t>(index)];
    if (isBusy() || session().step == step) {
      return;
    }
    if (!session().attempts.empty()) {
      // Its attempts were checked against the step they had.
      auto at =
          std::find(planSteps().begin(), planSteps().end(), session().step);
      stepBox_->setCurrentIndex(
          static_cast<int>(std::distance(planSteps().begin(), at)));
      emit message(tr("This session has attempts: start a new session for "
                      "the other step"));
      return;
    }
    session().step = step;
    save();
    selectSession(store_.currentIndex());
  });
  connect(axesBox_, &QComboBox::activated, this, [this](int index) {
    auto axes = static_cast<EdgeAxes>(index);
    if (isBusy() || session().axes == axes) {
      return;
    }
    session().axes = axes;
    save();
    selectSession(store_.currentIndex());
  });
  connect(limitBox_, &QComboBox::activated, this, &InspectionPage::chooseLimit);
  connect(pairBox_, &QComboBox::activated, this, [this](int index) {
    std::optional<Slot> pair;
    if (index > 0) {
      pair = kSlots4[static_cast<std::size_t>(index - 1)];
    }
    if (isBusy() || session().pair == pair) {
      return;
    }
    if (!session().attempts.empty()) {
      showDrill();
      emit message(tr("This session has attempts: start a new session to "
                      "track another pair"));
      return;
    }
    session().pair = pair;
    save();
    selectSession(store_.currentIndex());
  });
  connect(chartBox_, &QComboBox::activated, this, [this](int index) {
    QSettings().setValue("inspection/chart", index);
    showChart();
  });
  connect(reviewButton_, &QPushButton::clicked, this, [this] {
    requestReview(static_cast<int>(session().attempts.size()) - 1);
  });
  connect(retryButton_, &QPushButton::clicked, this, [this] {
    startRetry(static_cast<int>(session().attempts.size()) - 1);
  });
  connect(deleteButton_, &QPushButton::clicked, this, [this] {
    if (!isBusy() && !session().attempts.empty()) {
      deleteAttempt(static_cast<int>(session().attempts.size()) - 1);
    }
  });
  connect(sessionMenu_, &QMenu::aboutToShow, this,
          &InspectionPage::refreshSessionMenu);
  connect(table_, &QTableWidget::customContextMenuRequested, this,
          &InspectionPage::showAttemptMenu);
  connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
    if (auto *item = table_->item(row, 0)) {
      requestReview(item->data(Qt::UserRole).toInt());
    }
  });

  applySettings();
  selectSession(store_.currentIndex());
  feed_.setType("333");
}

bool InspectionPage::isBusy() const {
  return phase_ == Phase::Inspecting || phase_ == Phase::Executing ||
         phase_ == Phase::Counting;
}

void InspectionPage::deactivated() {
  cancel();
  cubeView_->finishAnimations();
}

void InspectionPage::windowDeactivated() { cancel(); }

void InspectionPage::focusModeChanged() {
  const bool on = focusMode();
  leftPanel_->setVisible(!on);
  optionsRow_->setVisible(!on);
  chart_->setVisible(!on);
  chartBox_->setVisible(!on && planning());
}

std::optional<Slot> InspectionPage::trackedPair() const {
  const InspectionSession &s = session();
  if (s.drill != InspectionDrill::Plan || s.step != SolveStep::EOCross) {
    return std::nullopt;
  }
  return s.pair;
}

void InspectionPage::applySettings() {
  CubeLook look = cubeView_->look();
  look.scheme = settings_.scheme;
  cubeView_->setLook(look);
  cubeView_->setAnimationMs(settings_.animationMs);
  feed_.setNeutrality(settings_.neutrality);
  refreshAttempts();
}

// --- showing ---------------------------------------------------------------

void InspectionPage::setPhase(Phase p) {
  phase_ = p;
  bool timing = p == Phase::Inspecting || p == Phase::Counting;
  if (timing != tick_.isActive()) {
    if (timing) {
      tick_.start();
    } else {
      tick_.stop();
    }
  }
  showCube(p == Phase::Inspecting || p == Phase::Counting ||
           p == Phase::Result);
  if (p != Phase::Result) {
    resultLabel_->clear();
  }
  updateControls();
  showHint();
  tick();
}

void InspectionPage::showCube(bool shown) {
  if (!shown) {
    hiddenLabel_->setText(
        phase_ == Phase::Executing
            ? (trackedPair()
                   ? tr("The cube is hidden: do your plan, then the %1 "
                        "pair.\nSpace when you are done.")
                         .arg(qs(std::string(slotName(*trackedPair()))))
                   : tr("The cube is hidden: do your plan.\nSpace when you "
                        "are done."))
        : !retry_ && !feed_.current() ? tr("Generating scramble…")
        : planning()                  ? tr("Space: start inspection")
                                      : tr("Space: show the cube"));
  }
  cubeStack_->setCurrentWidget(shown ? static_cast<QWidget *>(cubeView_)
                                     : hiddenLabel_);
}

void InspectionPage::showHint() {
  QString text;
  switch (phase_) {
  case Phase::Ready:
    text = planning()
               ? tr("Space shows the scrambled cube and starts inspection.")
               : tr("Space shows the scrambled cube; count its bad edges.");
    if (retry_) {
      text = tr("Retry of #%1, not recorded. ").arg(retry_->index + 1) + text +
             tr(" Esc ends the retry.");
    }
    break;
  case Phase::Inspecting:
    text = tr("Plan %1, turning the cube only as a whole. Space (or the time "
              "limit) hides it. Esc puts the scramble back.")
               .arg(qs(std::string(stepName(session().step))));
    break;
  case Phase::Executing:
    text = tr("Do your plan with the turning keys, then press Space to see "
              "the cube. Esc puts the scramble back.");
    break;
  case Phase::Counting:
    break; // tick() shows what to type and the time
  case Phase::Result:
    text = !planning() && session().axes == EdgeAxes::Both
               ? tr("Space: next scramble. Tab: ring the other axis's bad "
                    "edges. Rotations turn the cube to look at it.")
               : tr("Space: next scramble. Rotations turn the cube to look "
                    "at it.");
    break;
  }
  if (phase_ != Phase::Counting) {
    hintLabel_->setStyleSheet({});
    hintLabel_->setText(text);
  }
}

void InspectionPage::updateControls() {
  bool busy = isBusy();
  for (QWidget *w : std::initializer_list<QWidget *>{
           sessionButton_, drillBox_, stepBox_, axesBox_, limitBox_,
           skipButton_, pairBox_}) {
    w->setEnabled(!busy);
  }
  bool any = !session().attempts.empty();
  reviewButton_->setVisible(planning());
  retryButton_->setVisible(planning());
  reviewButton_->setEnabled(!busy && any);
  retryButton_->setEnabled(!busy && any);
  deleteButton_->setEnabled(!busy && any);
}

void InspectionPage::showDrill() {
  const InspectionSession &s = session();
  bool plan = s.drill == InspectionDrill::Plan;
  drillBox_->setCurrentIndex(plan ? 0 : 1);
  stepPicker_->setVisible(plan);
  limitPicker_->setVisible(plan);
  axesPicker_->setVisible(!plan);
  pairPicker_->setVisible(plan && s.step == SolveStep::EOCross);
  int pairIndex = 0;
  for (std::size_t i = 0; i < kSlots4.size(); ++i) {
    if (s.pair == kSlots4[i]) {
      pairIndex = static_cast<int>(i) + 1;
    }
  }
  pairBox_->setCurrentIndex(pairIndex);
  chartBox_->setVisible(plan && !focusMode());
  auto stepIt = std::find(planSteps().begin(), planSteps().end(), s.step);
  stepBox_->setCurrentIndex(static_cast<int>(
      stepIt == planSteps().end()
          ? planSteps().size() - 1
          : static_cast<std::size_t>(stepIt - planSteps().begin())));
  axesBox_->setCurrentIndex(static_cast<int>(s.axes));

  limitBox_->clear();
  for (int seconds : kLimits) {
    limitBox_->addItem(seconds == 15  ? tr("WCA (15 s)")
                       : seconds == 0 ? tr("No limit")
                                      : tr("%1 s").arg(seconds),
                       seconds);
  }
  bool standard = std::find(kLimits.begin(), kLimits.end(), s.limitSeconds) !=
                  kLimits.end();
  if (!standard || (s.limitSeconds == 15 && !s.wca)) {
    // A custom limit (15 s without the calls counts as one).
    limitBox_->insertItem(limitBox_->count() - 1,
                          tr("%1 s").arg(s.limitSeconds), -s.limitSeconds);
  }
  limitBox_->addItem(tr("Custom…"), QVariant());
  int want = standard && !(s.limitSeconds == 15 && !s.wca) ? s.limitSeconds
                                                           : -s.limitSeconds;
  limitBox_->setCurrentIndex(std::max(0, limitBox_->findData(want)));

  QString what = plan ? qs(std::string(stepName(s.step)))
                      : tr("bad edges, %1").arg(axesBox_->currentText());
  if (std::optional<Slot> pair = trackedPair()) {
    what =
        tr("%1, then the %2 pair").arg(what, qs(std::string(slotName(*pair))));
  }
  QString name = qs(s.name);
  if (!plan && name.compare(tr("Bad edges"), Qt::CaseInsensitive) == 0) {
    what = axesBox_->currentText(); // "Bad edges (F/B, then R/L)"
  }
  stepLabel_->setText(name == what ? name : tr("%1 (%2)").arg(name, what));
  table_->setHorizontalHeaderLabels(
      plan ? QStringList{"#", tr("Moves"), tr("Optimal"), tr("Inspection")}
           : QStringList{"#", tr("F/B"), tr("R/L"), tr("Time")});
}

void InspectionPage::chooseLimit(int index) {
  if (isBusy()) {
    return;
  }
  QVariant d = limitBox_->itemData(index);
  int seconds = 0;
  bool wca = false;
  if (!d.isValid()) {
    bool ok = false;
    seconds = QInputDialog::getInt(
        this, tr("Inspection time"),
        tr("Seconds of inspection (the cube is hidden after them):"),
        session().limitSeconds > 0 ? session().limitSeconds : 15, 1, 300, 1,
        &ok);
    if (!ok) {
      showDrill();
      return;
    }
  } else {
    int v = d.toInt();
    seconds = v < 0 ? -v : v;
    wca = v == 15;
  }
  session().limitSeconds = seconds;
  session().wca = wca;
  save();
  showDrill();
}

void InspectionPage::showScramble() {
  if (phase_ != Phase::Ready) {
    return; // the attempt keeps its own scramble
  }
  scrambleLabel_->setText(retry_ ? tr("Retry of #%1").arg(retry_->index + 1)
                          : feed_.current() ? tr("Scramble ready")
                                            : tr("Generating scramble…"));
  showCube(false);
}

void InspectionPage::tick() {
  std::int64_t elapsed = monotonicMs() - started_;
  if (phase_ == Phase::Inspecting) {
    int limit = session().limitSeconds;
    std::int64_t limitMs = static_cast<std::int64_t>(limit) * 1000;
    if (limit > 0 && elapsed >= limitMs) {
      startExecution();
      autoHiddenAt_ = monotonicMs();
      return;
    }
    // With a limit, the seconds left, counted down as Timed mode's
    // inspection is; without one, the seconds used.
    std::int64_t seconds = elapsed / 1000;
    std::int64_t shown = limit > 0 ? (limitMs - elapsed + 999) / 1000 : seconds;
    QColor colour;
    QString call;
    if (session().wca) {
      if (seconds >= 12) {
        colour = kBad;
        call = tr("12 seconds!");
      } else if (seconds >= 8) {
        colour = kCall;
        call = tr("8 seconds!");
      }
    } else if (limit > 0 && limit - seconds <= 3) {
      colour = kBad;
    }
    display_->showText(QString::number(shown), colour);
    if (!call.isEmpty()) {
      hintLabel_->setStyleSheet(QString("color: %1").arg(colour.name()));
      hintLabel_->setText(call);
    }
    return;
  }
  if (phase_ == Phase::Counting) {
    QString typed = entry_.isEmpty() ? QString("_") : entry_;
    display_->showText(QString("%1  %2").arg(tr(axisName(axis_)), typed));
    hintLabel_->setStyleSheet({});
    hintLabel_->setText(
        tr("Type how many edges are bad for the %1 axis (%2), then Enter. "
           "Backspace corrects, Esc puts the scramble back. %3")
            .arg(tr(axisName(axis_)), axisColours(hold_, axis_),
                 secondsText(elapsed)));
    return;
  }
  if (phase_ == Phase::Executing) {
    display_->showText(QString::number(attempt_.count()),
                       palette().color(QPalette::PlaceholderText));
  } else if (phase_ == Phase::Ready) {
    display_->showText("–", palette().color(QPalette::PlaceholderText));
  }
}

// --- Plan ------------------------------------------------------------------

void InspectionPage::startInspection() {
  if (retry_) {
    GeneratedScramble again;
    again.text = qs(retry_->original.scramble);
    scramble_ = again;
  } else {
    const auto &current = feed_.current();
    if (!current) {
      emit message(tr("Wait for the scramble before starting"));
      return;
    }
    scramble_ = *current;
  }
  // Scrambled holding white on top and green in front, as in Move Count.
  attempt_.load(parseAlgOrEmpty(ss(scramble_->text)));
  cubeView_->clearMarks();
  cubeView_->setState(attempt_.cube());
  scrambleLabel_->setText(scramble_->text.toHtmlEscaped());
  started_ = monotonicMs();
  usedMs_ = 0;
  setPhase(Phase::Inspecting);
}

void InspectionPage::startExecution() {
  std::int64_t used = monotonicMs() - started_;
  int limit = session().limitSeconds;
  if (limit > 0) {
    used =
        std::min<std::int64_t>(used, static_cast<std::int64_t>(limit) * 1000);
  }
  usedMs_ = used;
  autoHiddenAt_ = -1;
  attempt_.start();
  setPhase(Phase::Executing);
}

void InspectionPage::finishPlan() {
  if (!scramble_) {
    return;
  }
  SolveStep step = session().step;
  const FaceletCube &end = attempt_.cube();
  bool reached = stepReached(end, step);
  if (attempt_.count() == 0 && !reached) {
    // Most likely a Space meant to hide the cube: nothing is recorded.
    emit message(tr("No moves done yet: do your plan, then press Space (Esc "
                    "puts the scramble back)"));
    return;
  }
  InspectionAttempt a;
  a.scramble = ss(scramble_->text);
  a.timestamp = unixNow();
  a.ms = usedMs_;
  a.moves = std::clamp(attempt_.count(), 0, kMaxMoveCount);
  a.dnf = !reached;
  a.solution = toString(attempt_.solution());
  if (reached) {
    a.solvedIn = doneHold(end, step);
  }
  std::optional<Slot> pair = trackedPair();
  if (pair) {
    a.pairSolved = false;
    if (reached && a.solvedIn) {
      // The moves to EO Cross are compared with the optimum; the rest went
      // on the pair.
      FaceletCube start;
      start.apply(parseAlgOrEmpty(a.scramble));
      std::vector<StepMark> marks =
          stepMarks(start, attempt_.solution(), {SolveStep::EOCross});
      if (!marks.empty()) {
        a.pairMoves = std::max(0, a.moves - marks.front().htm);
        a.moves = marks.front().htm;
      }
      std::optional<CubieCube> held = heldCubie(end, *a.solvedIn);
      a.pairSolved = held && slotSolved(*held, *pair);
    }
  }

  QString stepText = qs(std::string(stepName(step)));
  QString moves = a.moves == 1 ? tr("1 move") : tr("%1 moves").arg(a.moves);
  QString head =
      reached ? QString("<b style='color:%1'>%2</b>")
                    .arg(kGood.name(), tr("%1 done in %2").arg(stepText, moves))
              : QString("<b style='color:%1'>DNF</b>: %2")
                    .arg(kBad.name(),
                         tr("%1 wasn't done (%2): %3")
                             .arg(stepText, moves, progressText(end, step))
                             .toHtmlEscaped());
  if (pair && reached) {
    QString pairText = tr("the %1 pair").arg(qs(std::string(slotName(*pair))));
    head +=
        "<br>" +
        (*a.pairSolved
             ? QString("<b style='color:%1'>%2</b>")
                   .arg(kGood.name(),
                        tr("then %1 in %2")
                            .arg(pairText,
                                 a.pairMoves == 1
                                     ? tr("1 move")
                                     : tr("%1 moves").arg(a.pairMoves)))
             : QString("<b style='color:%1'>%2</b>")
                   .arg(kBad.name(), tr("%1 wasn't solved as the cross is held")
                                         .arg(pairText)));
  }
  if (retry_) {
    const InspectionAttempt &was = retry_->original;
    head += "<br>" + tr("Retry of #%1 (not recorded); it was %2")
                         .arg(retry_->index + 1)
                         .arg(was.dnf ? QString("DNF(%1)").arg(was.moves)
                                      : QString::number(was.moves));
  }
  resultHead_ = "<p style='font-size:large'>" + head + "</p>";
  QString holdText =
      a.solvedIn
          ? " " + tr("in %1").arg(holdHtml(*a.solvedIn, settings_.scheme))
          : QString();
  resultHead_ +=
      "<p>" +
      tr("Inspection: %1. Your moves%2: %3")
          .arg(secondsText(a.ms), holdText,
               a.solution.empty()
                   ? tr("none")
                   : "<code>" + qs(a.solution).toHtmlEscaped() + "</code>") +
      "</p>";

  std::size_t index = 0;
  if (retry_) {
    index = retry_->index;
  } else {
    session().attempts.push_back(a);
    index = session().attempts.size() - 1;
    save();
    refreshAttempts();
    feed_.advance(); // this scramble has been seen: the next one is new
  }
  shownMoves_ = a.dnf ? std::nullopt : std::optional<int>(a.moves);
  lookTurns_.clear();
  cubeView_->setState(end);
  setPhase(Phase::Result);
  display_->showText(a.dnf ? QString("DNF") : QString::number(a.moves),
                     a.dnf ? kBad : kGood);
  lastAnalysis_.reset();
  analyse(a.scramble, step, index);
  showAnalysis();
}

void InspectionPage::analyse(const std::string &scramble, SolveStep step,
                             std::size_t attempt) {
  std::vector<Hold> holds = withBothEoAxes(settings_.neutrality.holds);
  Analysis job;
  job.session = store_.currentIndex();
  job.attempt = attempt;
  job.scramble = scramble;
  job.step = step;
  analysis_.setFuture(QtConcurrent::run([job, holds]() mutable {
    Alg alg = parseAlgOrEmpty(job.scramble);
    job.neutral = solveStepHeld(alg, job.step, holds, kSolutionsPerHold);
    std::vector<Hold> all(allHolds().begin(), allHolds().end());
    std::vector<HeldOptimum> best = solveStepHeld(alg, job.step, all, 1);
    if (!best.empty()) {
      job.overall = best.front().optimum.length;
    }
    return job;
  }));
}

void InspectionPage::showAnalysis() {
  if (analysis_.isFinished() && analysis_.future().resultCount() > 0) {
    Analysis a = analysis_.result();
    if (!lastAnalysis_ || lastAnalysis_->scramble != a.scramble) {
      lastAnalysis_ = a;
      // Keep the optimum with the attempt, if it is still there.
      if (a.session < store_.sessions().size() && !a.neutral.empty()) {
        auto &attempts = store_.sessions()[a.session].attempts;
        if (a.attempt < attempts.size() &&
            attempts[a.attempt].scramble == a.scramble &&
            !attempts[a.attempt].optimal) {
          attempts[a.attempt].optimal = a.neutral.front().optimum.length;
          save();
          refreshAttempts();
        }
      }
    }
  }
  if (phase_ != Phase::Result || !planning() || !scramble_) {
    return;
  }
  QString html = resultHead_;
  bool ready = lastAnalysis_ && lastAnalysis_->scramble == ss(scramble_->text);
  if (!ready) {
    html += "<p style='color:gray'>" +
            tr("Working out the optimal solutions…") + "</p>";
    resultLabel_->setText(html);
    return;
  }
  const Analysis &a = *lastAnalysis_;
  if (a.neutral.empty()) {
    resultLabel_->setText(html);
    return;
  }
  int best = a.neutral.front().optimum.length;
  QString yours;
  if (shownMoves_) {
    int over = *shownMoves_ - best;
    yours =
        over <= 0 ? tr(" (yours is optimal)") : tr(" (yours: +%1)").arg(over);
  }
  html += "<p>" +
          tr("Optimal in your holds: <b>%1</b>%2 · in any hold: %3")
              .arg(best)
              .arg(yours)
              .arg(a.overall ? QString::number(*a.overall) : QString("-")) +
          "</p>";
  // Up to three, none a trivial variant of another in the same hold: the
  // most different of each optimal hold in turn, then the next ones.
  std::vector<std::pair<const HeldOptimum *, std::vector<std::size_t>>> picks;
  for (const HeldOptimum &h : a.neutral) {
    if (h.optimum.length != best) {
      break;
    }
    picks.emplace_back(&h, distinctSolutions(h.optimum.solutions, 3));
  }
  QStringList lines;
  for (std::size_t rank = 0; rank < 3 && lines.size() < 3; ++rank) {
    for (const auto &[h, chosen] : picks) {
      if (rank >= chosen.size() || lines.size() >= 3) {
        continue;
      }
      const Alg &solution = h->optimum.solutions[chosen[rank]];
      QString rotation = qs(toString(holdRotation(h->hold)));
      QString moves =
          solution.empty() ? tr("(already done)") : qs(toString(solution));
      QString text = rotation.isEmpty() ? moves : rotation + " · " + moves;
      lines << stepHoldHtml(h->hold, session().step == SolveStep::EO,
                            settings_.scheme) +
                   ": <code>" + text.toHtmlEscaped() + "</code>";
    }
  }
  if (!lines.isEmpty()) {
    html += "<p>" + lines.join("<br>") + "</p>";
  }
  resultLabel_->setText(html);
}

void InspectionPage::backfillOptima() {
  if (backfill_.isRunning() || !planning()) {
    return;
  }
  std::vector<Filled> todo;
  const auto &attempts = session().attempts;
  for (std::size_t i = 0; i < attempts.size(); ++i) {
    if (!attempts[i].optimal && !attempts[i].scramble.empty()) {
      todo.push_back({i, attempts[i].scramble, 0});
    }
  }
  // The newest attempt's analysis is on its way already.
  if (todo.empty() || (todo.size() == 1 && analysis_.isRunning() &&
                       todo.front().attempt + 1 == attempts.size())) {
    return;
  }
  backfillSession_ = store_.currentIndex();
  std::vector<Hold> holds = withBothEoAxes(settings_.neutrality.holds);
  SolveStep step = session().step;
  backfill_.setFuture(QtConcurrent::run([todo, holds, step]() mutable {
    std::vector<Filled> out;
    for (Filled &f : todo) {
      if (auto d = heldDistance(parseAlgOrEmpty(f.scramble), step, holds)) {
        f.optimal = *d;
        out.push_back(f);
      }
    }
    return out;
  }));
}

// --- Edges -----------------------------------------------------------------

void InspectionPage::startCounting() {
  const auto &current = feed_.current();
  if (!current) {
    emit message(tr("Wait for the scramble before starting"));
    return;
  }
  scramble_ = *current;
  hold_ = scramble_->hold;
  // Scrambled holding white top and green front, then turned to the hold.
  FaceletCube f;
  f.apply(parseAlgOrEmpty(ss(scramble_->text)));
  f.apply(holdRotation(hold_));
  attempt_.load(f);
  cubeView_->clearMarks();
  cubeView_->setState(attempt_.cube());
  scrambleLabel_->setText(scramble_->text.toHtmlEscaped() + "<br>" +
                          tr("Held %1").arg(holdHtml(hold_, settings_.scheme)));
  answers_ = {-1, -1};
  entry_.clear();
  axis_ = asksAxis(session().axes, 0) ? 0 : 1;
  started_ = monotonicMs();
  setPhase(Phase::Counting);
}

void InspectionPage::typeDigit(int digit) {
  QString nextDigits = entry_ == "0" ? QString() : entry_;
  nextDigits += QChar('0' + digit);
  if (nextDigits.size() <= 2 && nextDigits.toInt() <= kEdges) {
    entry_ = nextDigits;
  }
  tick();
}

void InspectionPage::enterCount() {
  if (entry_.isEmpty()) {
    emit message(tr("Type the number of bad edges first"));
    return;
  }
  answers_[static_cast<std::size_t>(axis_)] = entry_.toInt();
  entry_.clear();
  if (axis_ == 0 && asksAxis(session().axes, 1)) {
    axis_ = 1;
    tick();
    return;
  }
  finishEdges();
}

void InspectionPage::finishEdges() {
  if (!scramble_) {
    return;
  }
  FaceletCube f;
  f.apply(parseAlgOrEmpty(ss(scramble_->text)));
  CubieCube held =
      solverState(CubieCube::fromFacelets(f).value_or(CubieCube{}), hold_);
  InspectionAttempt a;
  a.scramble = ss(scramble_->text);
  a.timestamp = unixNow();
  a.ms = monotonicMs() - started_;
  a.hold = hold_;
  for (int axis = 0; axis < 2; ++axis) {
    if (asksAxis(session().axes, axis)) {
      auto i = static_cast<std::size_t>(axis);
      a.answer[i] = answers_[i];
      a.correct[i] = badEdgeCount(held, axis);
    }
  }
  session().attempts.push_back(a);
  save();
  refreshAttempts();
  feed_.advance(); // this scramble has been seen: the next one is new

  QStringList lines;
  // Ring the first axis asked, or the first one that was wrong.
  ringAxis_ = -1;
  bool wrongRinged = false;
  for (int axis = 0; axis < 2; ++axis) {
    auto i = static_cast<std::size_t>(axis);
    if (a.correct[i] < 0) {
      continue;
    }
    bool right = a.answer[i] == a.correct[i];
    if (ringAxis_ < 0 || (!right && !wrongRinged)) {
      ringAxis_ = axis;
      wrongRinged = !right;
    }
    QString what =
        QString("%1 (%2)").arg(tr(axisName(axis)), axisColours(a.hold, axis));
    lines << (right ? QString("<b style='color:%1'>%2</b>")
                          .arg(kGood.name(),
                               tr("%1: %2, right").arg(what).arg(a.answer[i]))
                    : QString("<b style='color:%1'>%2</b>")
                          .arg(kBad.name(), tr("%1: you said %2, it's %3")
                                                .arg(what)
                                                .arg(a.answer[i])
                                                .arg(a.correct[i])));
  }
  lines << tr("Time: %1").arg(secondsText(a.ms));
  resultHead_ = "<p style='font-size:large'>" + lines.join("<br>") + "</p>";
  resultLabel_->setText(resultHead_);
  // Back to the hold it was shown in, with the bad edges ringed.
  attempt_.restart();
  lookTurns_.clear();
  cubeView_->setState(attempt_.cube());
  setPhase(Phase::Result);
  display_->showText(a.right() ? tr("Right") : tr("Wrong"),
                     a.right() ? kGood : kBad);
  showRings();
}

void InspectionPage::showRings() {
  if (!scramble_ || ringAxis_ < 0) {
    cubeView_->clearMarks();
    return;
  }
  FaceletCube f;
  f.apply(parseAlgOrEmpty(ss(scramble_->text)));
  CubieCube held =
      solverState(CubieCube::fromFacelets(f).value_or(CubieCube{}), hold_);
  std::array<bool, kEdges> bad = badEdges(held, ringAxis_);
  StickerMarks marks;
  marks.ringColour = kRing[ringAxis_];
  for (std::size_t e = 0; e < kEdges; ++e) {
    if (bad[e]) {
      for (std::uint8_t facelet : kEdgeFacelets[e]) {
        marks.rings.push_back(facelet);
      }
    }
  }
  // Moved with the cube if it was turned to look at it.
  for (Move m : lookTurns_) {
    marks = movedMarks(marks, m);
  }
  cubeView_->setMarks(marks);
  hintLabel_->setText(
      tr("Ringed: the bad edges for the %1 axis (%2). %3")
          .arg(tr(axisName(ringAxis_)), axisColours(hold_, ringAxis_),
               session().axes == EdgeAxes::Both
                   ? tr("Tab: the other axis. Space: next scramble.")
                   : tr("Space: next scramble.")));
}

// --- attempts --------------------------------------------------------------

bool InspectionPage::handleKey(QKeyEvent *e, bool press) {
  int key = e->key();
  std::optional<Move> move = settings_.keymap.lookup(e);
  bool counting = phase_ == Phase::Counting;
  bool digit = key >= Qt::Key_0 && key <= Qt::Key_9;
  bool entryKey = counting && (digit || key == Qt::Key_Backspace ||
                               key == Qt::Key_Return || key == Qt::Key_Enter);
  if (entryKey && (e->modifiers() & (Qt::ControlModifier | Qt::AltModifier |
                                     Qt::MetaModifier))) {
    return false; // e.g. Ctrl+7 switches modes
  }
  bool known = move || entryKey || key == Qt::Key_Space ||
               key == Qt::Key_Escape ||
               (key == Qt::Key_Tab && phase_ == Phase::Result);
  if (!known) {
    return false;
  }
  if (!press) {
    return true;
  }
  if (entryKey) {
    if (digit) {
      typeDigit(key - Qt::Key_0);
    } else if (key == Qt::Key_Backspace) {
      entry_.chop(1);
      tick();
    } else {
      enterCount();
    }
    return true;
  }
  switch (key) {
  case Qt::Key_Space:
    switch (phase_) {
    case Phase::Ready:
      if (planning()) {
        startInspection();
      } else {
        startCounting();
      }
      break;
    case Phase::Inspecting:
      startExecution();
      break;
    case Phase::Executing:
      if (autoHiddenAt_ >= 0 && monotonicMs() - autoHiddenAt_ < kHideGraceMs) {
        break; // meant to hide the cube, which the limit just did
      }
      finishPlan();
      break;
    case Phase::Counting:
      break;
    case Phase::Result:
      next();
      break;
    }
    return true;
  case Qt::Key_Escape:
    if (isBusy()) {
      cancel();
    } else if (retry_) {
      endRetry();
    }
    return true;
  case Qt::Key_Tab:
    if (!planning() && session().axes == EdgeAxes::Both && ringAxis_ >= 0) {
      ringAxis_ = 1 - ringAxis_;
      showRings();
    }
    return true;
  default:
    break;
  }
  if (move) {
    turn(*move);
  }
  return true;
}

void InspectionPage::turn(Move m) {
  switch (phase_) {
  case Phase::Inspecting:
  case Phase::Counting:
    if (!attempt_.turn(m)) {
      emit message(tr("Only rotations while you look at the cube"));
      return;
    }
    cubeView_->animateMove(m);
    break;
  case Phase::Executing:
    attempt_.turn(m); // not shown: the cube is hidden
    tick();
    break;
  case Phase::Result:
    look(m);
    break;
  case Phase::Ready:
    break;
  }
}

void InspectionPage::look(Move m) {
  if (!isRotation(m.type)) {
    emit message(tr("Only rotations here: turn the cube as a whole to look "
                    "at it"));
    return;
  }
  lookTurns_.push_back(m);
  cubeView_->animateMove(m);
  cubeView_->setMarks(movedMarks(cubeView_->marks(), m));
}

void InspectionPage::cancel() {
  if (!isBusy()) {
    return;
  }
  attempt_.restart();
  cubeView_->clearMarks();
  entry_.clear();
  setPhase(Phase::Ready);
  showScramble();
}

void InspectionPage::next() {
  // The feed moved on when the attempt was recorded (a retry doesn't use
  // it), so its scramble is a new one.
  retry_.reset();
  scramble_.reset();
  shownMoves_.reset();
  cubeView_->clearMarks();
  setPhase(Phase::Ready);
  showScramble();
}

void InspectionPage::skip() {
  if (isBusy()) {
    return;
  }
  if (!retry_ && phase_ == Phase::Ready) {
    feed_.advance(); // the scramble waiting is skipped
  }
  next();
}

void InspectionPage::startRetry(int index) {
  const auto &attempts = session().attempts;
  if (isBusy() || !planning() || index < 0 ||
      index >= static_cast<int>(attempts.size())) {
    return;
  }
  Retry r;
  r.index = static_cast<std::size_t>(index);
  r.original = attempts[r.index];
  retry_ = r;
  scramble_.reset();
  shownMoves_.reset();
  cubeView_->clearMarks();
  setPhase(Phase::Ready);
  showScramble();
}

void InspectionPage::endRetry() {
  if (!retry_ || isBusy()) {
    return;
  }
  next();
}

void InspectionPage::refreshSessionMenu() {
  sessionMenu_->clear();
  auto *group = new QActionGroup(sessionMenu_);
  for (std::size_t i = 0; i < store_.sessions().size(); ++i) {
    const InspectionSession &s = store_.sessions()[i];
    QAction *a = sessionMenu_->addAction(
        tr("%1 (%2)").arg(qs(s.name)).arg(s.attempts.size()), this,
        [this, i] { selectSession(i); });
    a->setCheckable(true);
    a->setChecked(i == store_.currentIndex());
    group->addAction(a);
  }
  sessionMenu_->addSeparator();
  sessionMenu_->addAction(tr("New session…"), this,
                          &InspectionPage::addSession);
  sessionMenu_->addAction(tr("Rename…"), this, &InspectionPage::renameSession);
  sessionMenu_->addAction(tr("Delete…"), this, &InspectionPage::deleteSession);
}

void InspectionPage::selectSession(std::size_t index) {
  if (isBusy()) {
    return;
  }
  store_.setCurrentIndex(index);
  sessionButton_->setText(tr("Session: %1").arg(qs(session().name)));
  showDrill();
  retry_.reset();
  shownMoves_.reset();
  scramble_.reset();
  cubeView_->clearMarks();
  setPhase(Phase::Ready);
  showScramble();
  refreshAttempts();
  backfillOptima();
  save();
}

void InspectionPage::refreshAttempts() {
  const auto &attempts = session().attempts;
  bool plan = planning();
  table_->setRowCount(static_cast<int>(attempts.size()));
  for (std::size_t i = 0; i < attempts.size(); ++i) {
    const InspectionAttempt &a = attempts[i];
    int row = static_cast<int>(attempts.size() - 1 - i);
    auto set = [&](int col, const QString &text, const QColor &colour = {}) {
      auto *item = new QTableWidgetItem(text);
      item->setData(Qt::UserRole, static_cast<int>(i));
      if (col > 0) {
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      }
      if (colour.isValid()) {
        item->setForeground(colour);
      }
      table_->setItem(row, col, item);
    };
    set(0, QString::number(i + 1));
    QStringList tip;
    if (!a.comment.empty()) {
      tip << qs(a.comment);
    }
    tip << tr("Scramble: %1").arg(qs(a.scramble));
    if (plan) {
      set(1,
          a.dnf ? QString("DNF(%1)").arg(a.moves) : QString::number(a.moves));
      set(2, a.optimal ? QString::number(*a.optimal) : QString("…"),
          a.optimal && !a.dnf && a.moves <= *a.optimal ? kGood : QColor());
      set(3, secondsText(a.ms));
      if (!a.solution.empty()) {
        tip << tr("Solution: %1").arg(qs(a.solution));
      }
      if (a.solvedIn) {
        tip << tr("Done held %1 top, %2 front")
                   .arg(colourName(a.solvedIn->top).toLower(),
                        colourName(a.solvedIn->front).toLower());
      }
      if (a.pairSolved) {
        tip << (*a.pairSolved
                    ? tr("Pair solved in %1 more moves").arg(a.pairMoves)
                    : tr("Pair not solved"));
      }
    } else {
      for (int axis = 0; axis < 2; ++axis) {
        auto k = static_cast<std::size_t>(axis);
        if (a.correct[k] < 0) {
          set(1 + axis, "-");
        } else if (a.answer[k] == a.correct[k]) {
          set(1 + axis, QString::number(a.answer[k]), kGood);
        } else {
          set(1 + axis, tr("%1 (%2)").arg(a.answer[k]).arg(a.correct[k]), kBad);
        }
      }
      set(3, QString::fromStdString(formatTime(a.ms)));
      tip << tr("Held %1 top, %2 front")
                 .arg(colourName(a.hold.top).toLower(),
                      colourName(a.hold.front).toLower());
    }
    table_->item(row, 0)->setToolTip(tip.join("\n"));
    table_->item(row, 1)->setToolTip(tip.join("\n"));
  }
  if (plan) {
    showPlanStats();
  } else {
    showEdgesStats();
  }
  showChart();
  updateControls();
}

void InspectionPage::showPlanStats() {
  const auto &attempts = session().attempts;
  std::vector<Solve> solves = planAsSolves(attempts);
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
  html += "</table>";
  PlanSummary all = summarizePlan(attempts);
  html += tr("<p>Attempts: %1%2 &nbsp; Mean: %3</p>")
              .arg(attempts.size())
              .arg(sum.dnfs ? QString(" (%1 DNF)").arg(sum.dnfs) : QString())
              .arg(movesText(sum.mean));
  auto overText = [](const PlanSummary &p) {
    return p.meanOver() ? QString("+%1").arg(*p.meanOver(), 0, 'f', 2)
                        : QString("-");
  };
  auto rateText = [](const PlanSummary &p) {
    return p.optimalRate()
               ? QString("%1%").arg(std::lround(*p.optimalRate() * 100))
               : QString("-");
  };
  if (all.compared > 0) {
    html += tr("<p>Over optimal: %1 &nbsp; At optimal: %2</p>")
                .arg(overText(all), rateText(all));
  }
  if (!attempts.empty()) {
    html += tr("<p>Mean inspection: %1</p>")
                .arg(secondsText(all.inspectionMs /
                                 static_cast<std::int64_t>(attempts.size())));
  }
  if (trackedPair()) {
    int tracked = 0, solved = 0, pairMoves = 0;
    for (const auto &a : attempts) {
      if (a.pairSolved) {
        ++tracked;
        if (*a.pairSolved) {
          ++solved;
          pairMoves += a.pairMoves;
        }
      }
    }
    if (tracked > 0) {
      html +=
          tr("<p>Pair solved: %1 of %2%3</p>")
              .arg(solved)
              .arg(tracked)
              .arg(solved ? tr(", in %1 moves on average")
                                .arg(static_cast<double>(pairMoves) / solved, 0,
                                     'f', 1)
                          : QString());
    }
  }
  // How the attempts went by how hard the scramble was and by how long
  // inspection took.
  auto breakdown =
      [&](const QString &title,
          const std::vector<std::pair<QString, PlanSummary>> &rows) {
        if (rows.size() < 2) {
          return; // nothing to compare
        }
        html += "<p><b>" + title +
                "</b></p><table cellspacing='3' width='100%'><tr><th></th>"
                "<th align='right'>" +
                tr("tries") + "</th><th align='right'>" + tr("at opt.") +
                "</th><th align='right'>" + tr("over") +
                "</th><th align='right'>" + "DNF</th></tr>";
        for (const auto &[name, p] : rows) {
          html += QString("<tr><td>%1</td><td align='right'>%2</td><td "
                          "align='right'>%3</td><td align='right'>%4</td><td "
                          "align='right'>%5</td></tr>")
                      .arg(name)
                      .arg(p.attempts)
                      .arg(rateText(p), overText(p))
                      .arg(p.dnfs);
        }
        html += "</table>";
      };
  std::vector<std::pair<QString, PlanSummary>> byOptimal;
  for (const auto &[length, p] : planByOptimal(attempts)) {
    byOptimal.emplace_back(
        length == 1 ? tr("1 move") : tr("%1 moves").arg(length), p);
  }
  breakdown(tr("By optimal length"), byOptimal);
  std::vector<std::pair<QString, PlanSummary>> byTime;
  for (const InspectionRange &r : planByInspection(attempts)) {
    byTime.emplace_back(r.to < 0 ? tr("%1 s and more").arg(r.from)
                                 : tr("%1–%2 s").arg(r.from).arg(r.to),
                        r.summary);
  }
  breakdown(tr("By inspection time used"), byTime);
  stats_->setText(html);
}

void InspectionPage::showChart() {
  const auto &attempts = session().attempts;
  if (!planning()) {
    chart_->setEmptyText(tr("Times will be charted here"));
    chart_->setSolves(edgesAsSolves(attempts));
    return;
  }
  switch (chartBox_->currentIndex()) {
  case 0:
    chart_->setEmptyText(tr("Moves over optimal will be charted here"));
    chart_->setSolves(overOptimalAsSolves(attempts));
    break;
  case 1:
    chart_->setEmptyText(tr("The share of optimal attempts (in %) will be "
                            "charted here"));
    chart_->setSolves(optimalRateAsSolves(attempts, 12));
    break;
  default:
    chart_->setEmptyText(tr("Move counts will be charted here"));
    chart_->setSolves(planAsSolves(attempts));
    break;
  }
}

void InspectionPage::showEdgesStats() {
  const auto &attempts = session().attempts;
  std::vector<Solve> solves = edgesAsSolves(attempts);
  int right = 0;
  std::int64_t rightMs = 0;
  for (const auto &a : attempts) {
    if (a.right()) {
      ++right;
      rightMs += a.ms;
    }
  }
  auto time = [](StatValue v) { return QString::fromStdString(formatTime(v)); };
  QString html =
      "<table cellspacing='4' width='100%'><tr><th></th><th align='right'>" +
      tr("current") + "</th><th align='right'>" + tr("best") + "</th></tr>";
  auto row = [&](const QString &name, const QString &cur, const QString &best) {
    html += QString("<tr><td>%1</td><td align='right'>%2</td><td "
                    "align='right'><b>%3</b></td></tr>")
                .arg(name, cur, best);
  };
  row("ao5", time(currentAverage(solves, 5)), time(bestAverage(solves, 5)));
  row("ao12", time(currentAverage(solves, 12)), time(bestAverage(solves, 12)));
  html += "</table>";
  if (attempts.empty()) {
    html += tr("<p>No attempts yet.</p>");
  } else {
    html += tr("<p>Attempts: %1 &nbsp; Right: %2%</p>")
                .arg(attempts.size())
                .arg(100 * right / static_cast<int>(attempts.size()));
    if (right > 0) {
      html += tr("<p>Mean time when right: %1</p>")
                  .arg(QString::fromStdString(formatTime(rightMs / right)));
    }
  }
  html +=
      tr("<p style='color:gray'>A wrong count is a DNF in the averages.</p>");
  stats_->setText(html);
}

void InspectionPage::requestReview(int index) {
  const auto &attempts = session().attempts;
  if (!planning() || index < 0 || index >= static_cast<int>(attempts.size())) {
    return;
  }
  const InspectionAttempt &a = attempts[static_cast<std::size_t>(index)];
  emit reviewRequested(qs(a.scramble), qs(a.solution),
                       static_cast<int>(session().step), index);
}

void InspectionPage::deleteAttempt(int index) {
  auto &attempts = session().attempts;
  if (index < 0 || index >= static_cast<int>(attempts.size())) {
    return;
  }
  attempts.erase(attempts.begin() + index);
  save();
  refreshAttempts();
}

void InspectionPage::showAttemptMenu(const QPoint &pos) {
  auto *item = table_->itemAt(pos);
  if (!item) {
    return;
  }
  int index = item->data(Qt::UserRole).toInt();
  auto &attempts = session().attempts;
  if (index < 0 || index >= static_cast<int>(attempts.size())) {
    return;
  }
  InspectionAttempt &a = attempts[static_cast<std::size_t>(index)];
  QMenu menu(this);
  QAction *review = nullptr;
  if (planning()) {
    review = menu.addAction(tr("Review…"));
    menu.addSeparator();
  }
  auto *comment = menu.addAction(tr("Comment…"));
  auto *copy = menu.addAction(tr("Copy scramble"));
  QAction *copySolution = nullptr;
  if (planning() && !a.solution.empty()) {
    copySolution = menu.addAction(tr("Copy solution"));
  }
  menu.addSeparator();
  auto *del = menu.addAction(tr("Delete"));
  QAction *chosen = menu.exec(table_->viewport()->mapToGlobal(pos));
  if (!chosen) {
    return;
  }
  if (chosen == review) {
    requestReview(index);
    return;
  }
  if (chosen == comment) {
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
}

void InspectionPage::addSession() {
  bool plan = planning();
  QString suggested =
      plan ? qs(std::string(stepName(session().step))) : tr("Bad edges");
  bool ok = false;
  QString name = QInputDialog::getText(this, tr("New session"), tr("Name:"),
                                       QLineEdit::Normal, suggested, &ok);
  if (!ok || name.trimmed().isEmpty()) {
    return;
  }
  InspectionSession from = session();
  InspectionSession &s = store_.addSession(ss(name.trimmed()));
  s.drill = from.drill;
  s.step = from.step;
  s.axes = from.axes;
  s.limitSeconds = from.limitSeconds;
  s.wca = from.wca;
  selectSession(store_.currentIndex());
}

void InspectionPage::renameSession() {
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

void InspectionPage::deleteSession() {
  auto answer =
      QMessageBox::question(this, tr("Delete session"),
                            tr("Delete session \"%1\" and its %2 attempts?")
                                .arg(qs(session().name))
                                .arg(session().attempts.size()));
  if (answer != QMessageBox::Yes) {
    return;
  }
  store_.removeSession(store_.currentIndex());
  selectSession(store_.currentIndex());
}

void InspectionPage::save() {
  // Written in the background from a copy, so that saving never holds up
  // the next attempt.
  writeInBackground(
      store_.path(),
      [copy = store_](QString *error) { return copy.save(error); }, this,
      [this](const QString &error) {
        emit message(tr("Could not save inspection sessions: %1").arg(error));
      });
}

} // namespace vcube
