#include "app/inspection_page.hpp"
#include "app/page_parts.hpp"

#include "app/flow_layout.hpp"
#include "app/session_panel.hpp"
#include "app/ui_state.hpp"

#include "io/session_store.hpp"

#include <QComboBox>
#include <QLineEdit>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDialog>
#include <QDir>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <string_view>

namespace vcube {

namespace {

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

// The flow's settings for a session.
InspectionFlow::Config flowConfig(const InspectionSession &s) {
  InspectionFlow::Config c;
  c.plan = s.drill == InspectionDrill::Plan;
  c.limitSeconds = s.limitSeconds;
  c.wca = s.wca;
  c.axes = {asksAxis(s.axes, 0), asksAxis(s.axes, 1)};
  return c;
}

// The attempt table's columns, titled: "#" (fitted to its numbers), then
// three sharing the width.
std::vector<AttemptColumn> attemptColumns(const QStringList &titles) {
  std::vector<AttemptColumn> columns;
  for (const QString &title : titles) {
    AttemptColumn c;
    c.id = title;
    c.title = title;
    c.fit = columns.empty();
    columns.push_back(c);
  }
  return columns;
}

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

  // --- left column: sessions, stats, attempts (no delete row: Delete last
  // is with the result) ---
  panel_ = new SessionPanel(this);
  panel_->setColumns(
      attemptColumns({"#", tr("Moves"), tr("Optimal"), tr("Inspection")}));

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
  skipButton_->setToolTip(tr("Go on to the next scramble (Ctrl+→)"));
  newSessionButton_ = flatButton(tr("New session…"), optionsRow_);
  newSessionButton_->setToolTip(
      tr("This session's drill and step are fixed now that it has attempts: "
         "a new session practises something else"));
  options->addWidget(new QLabel(tr("Drill:"), optionsRow_));
  options->addWidget(drillBox_);
  options->addSpacing(12);
  options->addWidget(stepPicker_);
  options->addWidget(pairPicker_);
  options->addWidget(axesPicker_);
  options->addWidget(newSessionButton_);
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
  keysLabel_ = new QLabel(right);
  keysLabel_->setAlignment(Qt::AlignCenter);
  keysLabel_->setWordWrap(true);
  keysLabel_->setTextFormat(Qt::RichText);
  keysLabel_->setForegroundRole(QPalette::PlaceholderText);
  keysLabel_->setProperty("vcubeHint", true);
  rightLayout->addWidget(keysLabel_);
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
  deleteButton_->setToolTip(
      tr("Delete the last recorded attempt (Delete; Ctrl+Z puts it back)"));
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
      std::clamp(saved::inspectionChart().get(), 0, chartBox_->count() - 1));
  chart_ = panel_->chart();
  chart_->setVisible(true);
  auto *chartRow = new QHBoxLayout;
  chartRow->addWidget(chart_, 1);
  chartRow->addWidget(chartBox_, 0, Qt::AlignTop);
  rightLayout->addLayout(chartRow, 1);

  auto *split = new QSplitter(this);
  split->addWidget(panel_);
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
  connect(newSessionButton_, &QPushButton::clicked, this,
          &InspectionPage::addSession);
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
    saved::inspectionChart().set(index);
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
  connect(panel_, &SessionPanel::menuAboutToShow, this,
          &InspectionPage::refreshSessionMenu);
  connect(panel_, &SessionPanel::sessionChosen, this,
          &InspectionPage::selectSession);
  connect(panel_, &SessionPanel::newSessionRequested, this,
          &InspectionPage::addSession);
  connect(panel_, &SessionPanel::renameSessionRequested, this,
          &InspectionPage::renameSession);
  connect(panel_, &SessionPanel::deleteSessionRequested, this,
          &InspectionPage::deleteSession);
  connect(panel_, &SessionPanel::attemptMenuRequested, this,
          &InspectionPage::showAttemptMenu);
  connect(panel_, &SessionPanel::attemptActivated, this,
          &InspectionPage::requestReview);

  applySettings();
  selectSession(store_.currentIndex());
  feed_.setType("333");
}

bool InspectionPage::isBusy() const { return flow_.isBusy(); }

void InspectionPage::deactivated() {
  if (flow_.leave()) {
    showCancelled();
  }
  cubeView_->finishAnimations();
}

void InspectionPage::windowDeactivated() {
  if (flow_.leave()) {
    showCancelled();
  }
}

QWidget *InspectionPage::sidePanel() const { return panel_; }

void InspectionPage::focusModeChanged() {
  const bool on = focusMode();
  panel_->setVisible(!on);
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

void InspectionPage::showPhase() {
  Phase p = flow_.phase();
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
  showDisplay();
}

void InspectionPage::showCube(bool shown) {
  if (!shown) {
    hiddenLabel_->setText(
        flow_.phase() == Phase::Executing
            ? (trackedPair()
                   ? tr("The cube is hidden: do your plan, then the %1 "
                        "pair.\nSpace when you are done.")
                         .arg(qs(std::string(slotName(*trackedPair()))))
                   : tr("The cube is hidden: do your plan.\nSpace when you "
                        "are done."))
        : !flow_.retry() && !feed_.current() ? tr("Generating scramble…")
                                             : readyText());
  }
  cubeStack_->setCurrentWidget(shown ? static_cast<QWidget *>(cubeView_)
                                     : hiddenLabel_);
}

QString InspectionPage::readyText() const {
  // What the session is for and how the last attempt went, while the
  // cube waits to be shown.
  const InspectionSession &s = session();
  QStringList lines;
  if (planning()) {
    QString what = tr("Plan %1").arg(qs(std::string(stepName(s.step))));
    if (std::optional<Slot> pair = trackedPair()) {
      what += tr(", then the %1 pair").arg(qs(std::string(slotName(*pair))));
    }
    lines << what;
  } else {
    lines << tr("Count bad edges");
  }
  if (!s.attempts.empty() && planning()) {
    const InspectionAttempt &a = s.attempts.back();
    QString last = a.dnf ? tr("Last: DNF") : tr("Last: %n move(s)", "", a.moves);
    if (!a.dnf && a.optimal) {
      int over = a.moves - *a.optimal;
      last += over > 0 ? tr(", %1 over the optimal %2").arg(over).arg(*a.optimal)
                       : tr(", optimal");
    }
    lines << last;
  }
  lines << (planning() ? tr("Space: start inspection")
                       : tr("Space: show the cube"));
  return lines.join("\n");
}

void InspectionPage::showHint() {
  QString text;
  QList<KeyHint> keys;
  const KeyHint putBack{tr("Esc"), tr("put the scramble back")};
  switch (flow_.phase()) {
  case Phase::Ready:
    keys = {{tr("Space"), planning() ? tr("show the cube and start inspection")
                                     : tr("show the cube")},
            {tr("Ctrl+→"), tr("skip")}};
    if (!planning()) {
      text = tr("Count the cube's bad edges.");
    }
    if (!session().attempts.empty()) {
      keys << KeyHint{tr("Delete"), tr("delete last")};
      if (planning()) {
        keys << KeyHint{tr("Alt+R"), tr("review last")};
      }
    }
    if (std::optional<std::size_t> retry = flow_.retry()) {
      text = tr("Retry of #%1, not recorded. ").arg(*retry + 1) + text;
      keys = {{tr("Space"), tr("show the cube and start inspection")},
              {tr("Esc"), tr("end the retry")}};
    }
    break;
  case Phase::Inspecting:
    text = tr("Plan %1, turning the cube only as a whole.")
               .arg(qs(std::string(stepName(session().step))));
    keys = {{tr("Space"), tr("hide the cube (the time limit does too)")},
            putBack};
    break;
  case Phase::Executing:
    text = tr("Do your plan with the turning keys.");
    keys = {{tr("Space"), tr("see the cube")}, putBack};
    break;
  case Phase::Counting:
    break; // showDisplay() shows what to type and the time
  case Phase::Result:
    text = tr("Rotations turn the cube to look at it.");
    keys = {{tr("Space"), tr("next scramble")}};
    if (!planning() && session().axes == EdgeAxes::Both) {
      keys << KeyHint{tr("Tab"), tr("ring the other axis's bad edges")};
    }
    break;
  }
  if (flow_.phase() != Phase::Counting) {
    hintLabel_->setStyleSheet({});
    hintLabel_->setText(text);
  }
  keysLabel_->setText(keyHintsHtml(keys, palette()));
  keysLabel_->setVisible(!keys.isEmpty() && !focusMode());
}

void InspectionPage::updateControls() {
  bool busy = isBusy();
  bool any = !session().attempts.empty();
  for (QWidget *w : std::initializer_list<QWidget *>{panel_->sessionButton(),
                                                     limitBox_, skipButton_}) {
    w->setEnabled(!busy);
  }
  // What the session is for is fixed once it has attempts: the pickers
  // show it, and a new session changes it.
  for (QWidget *w : std::initializer_list<QWidget *>{drillBox_, stepBox_,
                                                     axesBox_, pairBox_}) {
    w->setEnabled(!busy && !any);
  }
  newSessionButton_->setVisible(any);
  newSessionButton_->setEnabled(!busy);
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
  QStringList titles =
      plan ? QStringList{"#", tr("Moves"), tr("Optimal"), tr("Inspection")}
           : QStringList{"#", tr("F/B"), tr("R/L"), tr("Time")};
  if (panel_->model()->headerData(1, Qt::Horizontal, Qt::DisplayRole) !=
      titles[1]) {
    panel_->setColumns(attemptColumns(titles));
  }
  flow_.setConfig(flowConfig(s));
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
  if (flow_.phase() != Phase::Ready) {
    return; // the attempt keeps its own scramble
  }
  std::optional<std::size_t> retry = flow_.retry();
  scrambleLabel_->setText(retry             ? tr("Retry of #%1").arg(*retry + 1)
                          : feed_.current() ? tr("Scramble ready")
                                            : tr("Generating scramble…"));
  showCube(false);
}

void InspectionPage::tick() {
  InspectionFlow::Effects e = flow_.tick(monotonicMs());
  if (e.startExecution) {
    apply(e); // the limit hid the cube
    return;
  }
  showDisplay();
}

void InspectionPage::showDisplay() {
  std::int64_t now = monotonicMs();
  switch (flow_.phase()) {
  case Phase::Inspecting: {
    InspectionFlow::Countdown c = flow_.countdown(now);
    QColor colour;
    QString call;
    switch (c.alert) {
    case InspectionFlow::Alert::Twelve:
      colour = kBad;
      call = tr("12 seconds!");
      break;
    case InspectionFlow::Alert::Eight:
      colour = kCall;
      call = tr("8 seconds!");
      break;
    case InspectionFlow::Alert::Ending:
      colour = kBad;
      break;
    case InspectionFlow::Alert::None:
      break;
    }
    display_->showText(QString::number(c.shown), colour);
    if (!call.isEmpty()) {
      hintLabel_->setStyleSheet(QString("color: %1").arg(colour.name()));
      hintLabel_->setText(call);
    }
    break;
  }
  case Phase::Counting: {
    int axis = flow_.axis();
    QString typed = flow_.entry().empty() ? QString("_") : qs(flow_.entry());
    display_->showText(QString("%1  %2").arg(tr(axisName(axis)), typed));
    hintLabel_->setStyleSheet({});
    hintLabel_->setText(
        tr("Type how many edges are bad for the %1 axis (%2), then Enter. "
           "Backspace corrects, Esc puts the scramble back. %3")
            .arg(tr(axisName(axis)), axisColours(hold_, axis),
                 secondsText(flow_.elapsed(now))));
    break;
  }
  case Phase::Executing:
    display_->showText(QString::number(attempt_.count()),
                       palette().color(QPalette::PlaceholderText));
    break;
  case Phase::Ready:
    display_->showText("–", palette().color(QPalette::PlaceholderText));
    break;
  case Phase::Result:
    break; // the outcome, as shown when it came
  }
}

void InspectionPage::apply(const InspectionFlow::Effects &e,
                           std::optional<Move> move) {
  if (e.waitScramble) {
    emit message(tr("Wait for the scramble before starting"));
  }
  if (e.startInspection) {
    startInspection();
  }
  if (e.startCounting) {
    startCounting();
  }
  if (e.startExecution) {
    startExecution();
  }
  if (e.finishPlan) {
    finishPlan();
  }
  if (e.entryChanged) {
    showDisplay();
  }
  if (e.needEntry) {
    emit message(tr("Type the number of bad edges first"));
  }
  if (e.finishEdges) {
    finishEdges();
  }
  if (e.rings) {
    showRings();
  }
  if (e.cancel) {
    showCancelled();
  }
  if (e.next) {
    showNext();
  }
  if (e.turn != InspectionFlow::Turn::None && move) {
    turn(e.turn, *move);
  }
}

// --- Plan ------------------------------------------------------------------

void InspectionPage::startInspection() {
  if (flow_.retry()) {
    GeneratedScramble again;
    again.text = qs(retryOf_.scramble);
    scramble_ = again;
  } else {
    scramble_ = *feed_.current(); // the flow waited for it
  }
  // Scrambled holding white on top and green in front, as in Move Count.
  attempt_.load(parseAlgOrEmpty(ss(scramble_->text)));
  cubeView_->clearMarks();
  cubeView_->setState(attempt_.cube());
  scrambleLabel_->setText(scramble_->text.toHtmlEscaped());
  showPhase();
}

void InspectionPage::startExecution() {
  attempt_.start();
  showPhase();
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
  a.ms = flow_.usedMs();
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
  std::optional<std::size_t> retry = flow_.retry();
  if (retry) {
    const InspectionAttempt &was = retryOf_;
    head += "<br>" + tr("Retry of #%1 (not recorded); it was %2")
                         .arg(*retry + 1)
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
  if (retry) {
    index = *retry;
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
  flow_.planDone();
  showPhase();
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
  if (flow_.phase() != Phase::Result || !planning() || !scramble_) {
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
  scramble_ = *feed_.current(); // the flow waited for it
  hold_ = scramble_->hold;
  // Scrambled holding white top and green front, then turned to the hold.
  FaceletCube f;
  f.apply(parseAlgOrEmpty(ss(scramble_->text)));
  f.apply(holdRotation(hold_));
  attempt_.load(f);
  cubeView_->clearMarks();
  cubeView_->setState(attempt_.cube());
  scrambleLabel_->setText(scramble_->text.toHtmlEscaped());
  showPhase();
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
  a.ms = flow_.elapsed(monotonicMs());
  a.hold = hold_;
  for (int axis = 0; axis < 2; ++axis) {
    if (asksAxis(session().axes, axis)) {
      auto i = static_cast<std::size_t>(axis);
      a.answer[i] = flow_.answers()[i];
      a.correct[i] = badEdgeCount(held, axis);
    }
  }
  session().attempts.push_back(a);
  save();
  refreshAttempts();
  feed_.advance(); // this scramble has been seen: the next one is new

  QStringList lines;
  for (int axis = 0; axis < 2; ++axis) {
    auto i = static_cast<std::size_t>(axis);
    if (a.correct[i] < 0) {
      continue;
    }
    bool right = a.answer[i] == a.correct[i];
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
  // Back to the hold it was shown in, with the bad edges ringed (those of
  // the first axis asked, or the first one that was wrong).
  attempt_.restart();
  lookTurns_.clear();
  cubeView_->setState(attempt_.cube());
  flow_.edgesDone(a.correct);
  showPhase();
  display_->showText(a.right() ? tr("Right") : tr("Wrong"),
                     a.right() ? kGood : kBad);
  showRings();
}

void InspectionPage::showRings() {
  int ring = flow_.ringAxis();
  if (!scramble_ || ring < 0) {
    cubeView_->clearMarks();
    return;
  }
  FaceletCube f;
  f.apply(parseAlgOrEmpty(ss(scramble_->text)));
  CubieCube held =
      solverState(CubieCube::fromFacelets(f).value_or(CubieCube{}), hold_);
  std::array<bool, kEdges> bad = badEdges(held, ring);
  StickerMarks marks;
  marks.ringColour = kRing[ring];
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
          .arg(tr(axisName(ring)), axisColours(hold_, ring),
               session().axes == EdgeAxes::Both
                   ? tr("Tab: the other axis. Space: next scramble.")
                   : tr("Space: next scramble.")));
}

// --- attempts --------------------------------------------------------------

bool InspectionPage::handleKey(QKeyEvent *e, bool press) {
  using Key = InspectionFlow::Key;
  int key = e->key();
  // The keys the solve modes use for the last attempt and the scramble.
  if (press && !e->isAutoRepeat() && !isBusy()) {
    Qt::KeyboardModifiers mods =
        e->modifiers() & (Qt::ControlModifier | Qt::AltModifier |
                          Qt::ShiftModifier | Qt::MetaModifier);
    if (mods == Qt::NoModifier && key == Qt::Key_Delete) {
      deleteButton_->click();
      return true;
    }
    if (mods == Qt::ControlModifier && key == Qt::Key_Z) {
      undoDelete();
      return true;
    }
    if (mods == Qt::ControlModifier && key == Qt::Key_Right) {
      skip();
      return true;
    }
    if (mods == Qt::AltModifier && key == Qt::Key_R && planning() &&
        !session().attempts.empty()) {
      reviewButton_->click();
      return true;
    }
  }
  std::optional<Move> move = settings_.keymap.lookup(e);
  InspectionFlow::KeyInput k;
  if (key == Qt::Key_Space) {
    k.key = Key::Space;
  } else if (key == Qt::Key_Escape) {
    k.key = Key::Escape;
  } else if (key == Qt::Key_Tab) {
    k.key = Key::Tab;
  } else if (key >= Qt::Key_0 && key <= Qt::Key_9) {
    k.key = Key::Digit;
    k.digit = key - Qt::Key_0;
  } else if (key == Qt::Key_Backspace) {
    k.key = Key::Backspace;
  } else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
    k.key = Key::Enter;
  }
  k.turn = move.has_value();
  k.press = press;
  k.autoRepeat = e->isAutoRepeat();
  k.modifiers = (e->modifiers() & (Qt::ControlModifier | Qt::AltModifier |
                                   Qt::MetaModifier)) != 0;
  InspectionFlow::Effects effects =
      flow_.key(k, monotonicMs(), feed_.current().has_value());
  apply(effects, move);
  return effects.used;
}

void InspectionPage::turn(InspectionFlow::Turn how, Move m) {
  switch (how) {
  case InspectionFlow::Turn::Inspect:
    if (!attempt_.turn(m)) {
      emit message(tr("Only rotations while you look at the cube"));
      return;
    }
    cubeView_->animateMove(m);
    break;
  case InspectionFlow::Turn::Hidden:
    attempt_.turn(m); // not shown: the cube is hidden
    showDisplay();
    break;
  case InspectionFlow::Turn::Look:
    look(m);
    break;
  case InspectionFlow::Turn::None:
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

void InspectionPage::showCancelled() {
  attempt_.restart();
  cubeView_->clearMarks();
  showPhase();
  showScramble();
}

void InspectionPage::showNext() {
  // The feed moved on when the attempt was recorded (a retry doesn't use
  // it), so its scramble is a new one.
  scramble_.reset();
  shownMoves_.reset();
  cubeView_->clearMarks();
  showPhase();
  showScramble();
}

void InspectionPage::skip() {
  InspectionFlow::Skip s = flow_.skip();
  if (!s.done) {
    return;
  }
  if (s.advanceFeed) {
    feed_.advance(); // the scramble waiting is skipped
  }
  showNext();
}

void InspectionPage::startRetry(int index) {
  const auto &attempts = session().attempts;
  if (isBusy() || !planning() || index < 0 ||
      index >= static_cast<int>(attempts.size())) {
    return;
  }
  auto i = static_cast<std::size_t>(index);
  if (!flow_.startRetry(i)) {
    return;
  }
  retryOf_ = attempts[i];
  scramble_.reset();
  shownMoves_.reset();
  cubeView_->clearMarks();
  showPhase();
  showScramble();
}

void InspectionPage::refreshSessionMenu() {
  std::vector<SessionPanel::MenuSession> list;
  for (const InspectionSession &s : store_.sessions()) {
    list.push_back({qs(s.name), s.attempts.size()});
  }
  panel_->fillSessionMenu(list, store_.currentIndex());
}

void InspectionPage::selectSession(std::size_t index) {
  if (isBusy()) {
    return;
  }
  store_.setCurrentIndex(index);
  deleted_.reset(); // undo is for the session it was deleted from
  panel_->setSessionName(qs(session().name));
  showDrill();
  flow_.next();
  shownMoves_.reset();
  scramble_.reset();
  cubeView_->clearMarks();
  showPhase();
  showScramble();
  refreshAttempts();
  backfillOptima();
  save();
}

void InspectionPage::refreshAttempts() {
  const auto &attempts = session().attempts;
  bool plan = planning();
  std::vector<AttemptRow> rows;
  rows.reserve(attempts.size());
  for (std::size_t i = attempts.size(); i-- > 0;) {
    const InspectionAttempt &a = attempts[i];
    AttemptRow row;
    row.index = static_cast<int>(i);
    row.cells.resize(4);
    auto set = [&](int col, const QString &text, const QColor &colour = {}) {
      AttemptCell &cell = row.cells[static_cast<std::size_t>(col)];
      cell.text = text;
      cell.colour = colour;
      cell.alignRight = col > 0;
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
    }
    // The tooltip is on the number and the first value only.
    row.cells[0].tip = tip.join("\n");
    row.cells[1].tip = tip.join("\n");
    rows.push_back(std::move(row));
  }
  panel_->setRows(std::move(rows));
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
  static constexpr std::string_view kStats[] = {"single", "mo3", "ao5", "ao12"};
  SessionStats stats = sessionStats(solves, kStats);
  const SessionSummary &sum = stats.summary;
  // A single is a whole number of moves; the means and averages have two
  // decimals.
  QString html = statsTableHtml(
      stats,
      [](const std::string &id) {
        return id == "single" ? tr("single") : qs(id);
      },
      [](const std::string &id, StatValue v) {
        return movesText(v, id == "single" ? 0 : 2);
      });
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
  panel_->setStatsHtml(html);
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
  static constexpr std::string_view kStats[] = {"ao5", "ao12"};
  QString html = statsTableHtml(
      sessionStats(solves, kStats),
      [](const std::string &id) { return qs(id); },
      [](const std::string &, StatValue v) {
        return QString::fromStdString(formatTime(v));
      });
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
  panel_->setStatsHtml(html);
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
  // Kept for Ctrl+Z, until the session changes.
  deleted_ = {index, attempts[static_cast<std::size_t>(index)]};
  attempts.erase(attempts.begin() + index);
  save();
  refreshAttempts();
  updateControls();
  emit message(tr("Attempt deleted (Ctrl+Z puts it back)"));
}

void InspectionPage::undoDelete() {
  if (!deleted_) {
    return;
  }
  auto &attempts = session().attempts;
  int at = std::clamp(deleted_->first, 0, static_cast<int>(attempts.size()));
  attempts.insert(attempts.begin() + at, deleted_->second);
  deleted_.reset();
  save();
  refreshAttempts();
  updateControls();
  emit message(tr("Attempt put back"));
}

void InspectionPage::showAttemptMenu(int index, const QPoint &globalPos) {
  const auto &attempts = session().attempts;
  if (index < 0 || index >= static_cast<int>(attempts.size())) {
    return;
  }
  const InspectionAttempt &a = attempts[static_cast<std::size_t>(index)];
  ActionMenu menu(this);
  if (planning()) {
    menu.add(tr("Review…"), [this, index] { requestReview(index); });
    menu.addSeparator();
  }
  AttemptMenuActions actions;
  actions.comment = [this, index] {
    auto &list = session().attempts;
    if (index >= static_cast<int>(list.size())) {
      return;
    }
    InspectionAttempt &attempt = list[static_cast<std::size_t>(index)];
    if (std::optional<QString> text =
            askComment(this, tr("Comment:"), qs(attempt.comment))) {
      attempt.comment = ss(*text);
      save();
      refreshAttempts();
    }
  };
  actions.scramble = qs(a.scramble);
  if (planning()) {
    actions.solution = qs(a.solution); // none: no Copy solution
  }
  actions.remove = [this, index] { deleteAttempt(index); };
  addAttemptActions(menu, actions);
  menu.run(globalPos);
}

void InspectionPage::addSession() {
  // What the session practises is chosen with it, in one form: it is
  // fixed once the session has attempts.
  const InspectionSession from = session();
  QDialog dialog(this);
  dialog.setWindowTitle(tr("New session"));
  auto *form = new QFormLayout(&dialog);
  auto *drill = new QComboBox(&dialog);
  drill->addItem(tr("Plan a step"));
  drill->addItem(tr("Count bad edges"));
  drill->setCurrentIndex(from.drill == InspectionDrill::Plan ? 0 : 1);
  auto *step = new QComboBox(&dialog);
  for (SolveStep st : planSteps()) {
    step->addItem(qs(std::string(stepName(st))));
  }
  step->setCurrentIndex(stepBox_->currentIndex());
  auto *pair = new QComboBox(&dialog);
  pair->addItem(tr("None"));
  for (Slot slot : kSlots4) {
    pair->addItem(tr("%1 pair (%2)")
                      .arg(qs(std::string(slotName(slot))), slotWords(slot)));
  }
  pair->setCurrentIndex(pairBox_->currentIndex());
  auto *axes = new QComboBox(&dialog);
  axes->addItem(tr("F/B"));
  axes->addItem(tr("R/L"));
  axes->addItem(tr("F/B, then R/L"));
  axes->setCurrentIndex(static_cast<int>(from.axes));
  auto *name = new QLineEdit(&dialog);
  form->addRow(tr("Drill:"), drill);
  form->addRow(tr("Step:"), step);
  form->addRow(tr("Then:"), pair);
  form->addRow(tr("Axes:"), axes);
  form->addRow(tr("Name:"), name);
  form->addRow(
      helpLabel(tr("The drill and what it is for can't be changed once the "
                   "session has attempts. The inspection time limit can."),
                &dialog));
  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  // The name follows the choices until it is typed over.
  auto suggested = [=] {
    if (drill->currentIndex() == 1) {
      return tr("Bad edges");
    }
    QString text = step->currentText();
    if (planSteps()[static_cast<std::size_t>(step->currentIndex())] ==
            SolveStep::EOCross &&
        pair->currentIndex() > 0) {
      text += " + " + qs(std::string(slotName(kSlots4[static_cast<std::size_t>(
                          pair->currentIndex() - 1)])));
    }
    return text;
  };
  auto refresh = [=] {
    bool plan = drill->currentIndex() == 0;
    bool cross = plan && planSteps()[static_cast<std::size_t>(
                             step->currentIndex())] == SolveStep::EOCross;
    form->setRowVisible(step, plan);
    form->setRowVisible(pair, cross);
    form->setRowVisible(axes, !plan);
    if (!name->isModified()) {
      name->setText(suggested());
    }
  };
  for (QComboBox *box : {drill, step, pair, axes}) {
    connect(box, &QComboBox::currentIndexChanged, &dialog, refresh);
  }
  refresh();
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  QString chosen = name->text().trimmed();
  if (chosen.isEmpty()) {
    chosen = suggested();
  }
  InspectionSession &s = store_.addSession(ss(chosen));
  s.drill =
      drill->currentIndex() == 0 ? InspectionDrill::Plan : InspectionDrill::Edges;
  s.step = planSteps()[static_cast<std::size_t>(step->currentIndex())];
  s.axes = static_cast<EdgeAxes>(axes->currentIndex());
  if (s.drill == InspectionDrill::Plan && s.step == SolveStep::EOCross &&
      pair->currentIndex() > 0) {
    s.pair = kSlots4[static_cast<std::size_t>(pair->currentIndex() - 1)];
  }
  s.limitSeconds = from.limitSeconds;
  s.wca = from.wca;
  if (s.drill == InspectionDrill::Plan && s.limitSeconds == 0 &&
      from.drill != InspectionDrill::Plan) {
    // Edges sessions have no limit: a Plan session starts with WCA
    // inspection.
    s.limitSeconds = 15;
    s.wca = true;
  }
  selectSession(store_.currentIndex());
}

void InspectionPage::renameSession() {
  std::optional<QString> name = askSessionName(this, tr("Rename session"),
                                               tr("Name:"), qs(session().name));
  if (!name) {
    return;
  }
  session().name = ss(*name);
  selectSession(store_.currentIndex());
}

void InspectionPage::deleteSession() {
  if (!confirmDeleteSession(this, qs(session().name), session().attempts.size(),
                            tr("attempts"))) {
    return;
  }
  store_.removeSession(store_.currentIndex());
  selectSession(store_.currentIndex());
}

void InspectionPage::save() {
  saveInBackground(store_, this, [this](const QString &error) {
    emit message(tr("Could not save inspection sessions: %1").arg(error));
  });
}

} // namespace vcube