#include "app/inspection_page.hpp"
#include "app/inspection_private.hpp"
#include "app/page_parts.hpp"

#include "app/flow_layout.hpp"
#include "app/session_panel.hpp"
#include "app/ui_state.hpp"

#include "io/session_store.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
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

// The time limits offered, in seconds (0: none); 15 is WCA inspection with
// the 8 and 12 second calls.
constexpr std::array<int, 5> kLimits = {15, 10, 20, 30, 0};

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
  split->setObjectName("main");
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
    QString last =
        a.dnf ? tr("Last: DNF") : tr("Last: %n move(s)", "", a.moves);
    if (!a.dnf && a.optimal) {
      int over = a.moves - *a.optimal;
      last += over > 0
                  ? tr(", %1 over the optimal %2").arg(over).arg(*a.optimal)
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

} // namespace vcube
