#include "app/eo_cross_page.hpp"

#include "app/session_panel.hpp"
#include "app/sounds.hpp"
#include "app/ui_state.hpp"
#include "app/ui_text.hpp"

#include <QComboBox>
#include <QDir>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace vcube {

namespace {

// A Space this soon after the time limit hid the cube was meant to hide
// it.
constexpr std::int64_t kHideGraceMs = 600;

// The inspection times offered, in seconds (0: none).
constexpr std::array<int, 7> kLimits = {0, 5, 8, 10, 15, 20, 30};

const StatusColour kWarn{Status::Warning};

bool hasStep(EoDrill d) {
  return d == EoDrill::Axis || d == EoDrill::Plan || d == EoDrill::Solve;
}
bool hasLimit(EoDrill d) { return d == EoDrill::Axis || d == EoDrill::Plan; }

// A hold in plain words, for the hint line: "yellow cross, green front",
// or for EO alone "EO on green / blue".
QString holdWords(Hold h, bool eoOnly) {
  int bottom = (h.top + 3) % 6, back = (h.front + 3) % 6;
  if (eoOnly) {
    return QObject::tr("EO on %1 / %2")
        .arg(colourName(std::min(h.front, back)).toLower(),
             colourName(std::max(h.front, back)).toLower());
  }
  return QObject::tr("%1 cross, %2 front")
      .arg(colourName(bottom).toLower(), colourName(h.front).toLower());
}

// The rotations that turn a cube held as `from` to `to`.
Alg rotationBetween(Hold from, Hold to) {
  Alg r = invert(holdRotation(from));
  Alg on = holdRotation(to);
  r.insert(r.end(), on.begin(), on.end());
  return simplify(r);
}

} // namespace

EoCrossPage::EoCrossPage(AppSettings &settings, const QString &dataDir,
                         QWidget *parent)
    : ModePage(parent), settings_(settings),
      store_(QDir(dataDir).filePath("eocross.json")), rng_(makeRng()) {
  QString error;
  if (!store_.load(&error)) {
    QString kept =
        store_.setAsidePath().isEmpty()
            ? tr("It is left where it is, and nothing will be saved "
                 "over it.")
            : tr("It was kept as %1.")
                  .arg(QDir::toNativeSeparators(store_.setAsidePath()));
    QMessageBox::warning(this, "v-cube",
                         tr("Could not read your EO Cross statistics (%1), so "
                            "EO Cross starts afresh.")
                                 .arg(error) +
                             " " + kept);
  }
  buildSide();

  auto *right = new QWidget(this);
  auto *rightLayout = new QVBoxLayout(right);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  options_ = new OptionBar(right);
  cubeStack_ = new QStackedWidget(right);
  cubeView_ = new CubeView(cubeStack_);
  cubeView_->setMinimumSize(260, 200);
  hiddenLabel_ = new QLabel(cubeStack_);
  hiddenLabel_->setAlignment(Qt::AlignCenter);
  hiddenLabel_->setWordWrap(true);
  hiddenLabel_->setForegroundRole(QPalette::PlaceholderText);
  QFont hf = hiddenLabel_->font();
  hf.setPointSizeF(hf.pointSizeF() * 1.3);
  hiddenLabel_->setFont(hf);
  hiddenLabel_->setText(
      tr("The cube is hidden.\nDo your plan, then press Space."));
  cubeStack_->addWidget(cubeView_);
  cubeStack_->addWidget(hiddenLabel_);
  buildOptions();
  rightLayout->addWidget(options_);

  prompt_ = new QLabel(right);
  prompt_->setAlignment(Qt::AlignCenter);
  prompt_->setWordWrap(true);
  prompt_->setTextFormat(Qt::RichText);
  QFont pf = prompt_->font();
  pf.setPointSizeF(pf.pointSizeF() * 1.3);
  prompt_->setFont(pf);
  display_ = new QLabel(right);
  display_->setAlignment(Qt::AlignCenter);
  display_->setTextFormat(Qt::RichText);
  QFont df = display_->font();
  df.setPointSizeF(df.pointSizeF() * 1.8);
  display_->setFont(df);
  hint_ = new HintBar(right);
  result_ = new QLabel(right);
  result_->setObjectName("eoCrossResult");
  result_->setTextFormat(Qt::RichText);
  result_->setWordWrap(true);
  result_->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
  result_->setTextInteractionFlags(Qt::TextBrowserInteraction);
  result_->setFocusPolicy(Qt::NoFocus);
  result_->setOpenExternalLinks(false);
  auto *scroll = new QScrollArea(right);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidgetResizable(true);
  scroll->setFocusPolicy(Qt::NoFocus);
  scroll->setWidget(result_);

  rightLayout->addWidget(prompt_);
  rightLayout->addWidget(cubeStack_, 4);
  rightLayout->addWidget(display_);
  rightLayout->addWidget(hint_);
  rightLayout->addWidget(scroll, 5);

  auto *split = new QSplitter(this);
  split->setObjectName("main");
  split->addWidget(side_);
  split->addWidget(right);
  split->setStretchFactor(0, 0);
  split->setStretchFactor(1, 1);
  split->setSizes({380, 800});

  tabs_ = new QTabBar(this);
  tabs_->setFocusPolicy(Qt::NoFocus);
  tabs_->setExpanding(false);
  for (int d = 0; d < kEoDrills; ++d) {
    tabs_->addTab(drillName(static_cast<Drill>(d)));
  }
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(tabs_);
  layout->addWidget(split, 1);

  connect(tabs_, &QTabBar::currentChanged, this, [this](int index) {
    if (index >= 0 && static_cast<Drill>(index) != drill_) {
      selectDrill(static_cast<Drill>(index));
    }
  });
  connect(result_, &QLabel::linkActivated, this, [this](const QString &link) {
    if (link.startsWith("sol:")) {
      playSolution(static_cast<std::size_t>(link.mid(4).toInt()));
    }
  });
  clock_.setInterval(37);
  connect(&clock_, &QTimer::timeout, this, &EoCrossPage::showClock);

  loadChoices();
  applySettings();
  showOptions();
}

EoCrossPage::~EoCrossPage() = default;

// --- building and options ----------------------------------------------------

QString EoCrossPage::drillName(Drill d) const {
  switch (d) {
  case Drill::Edges:
    return tr("Bad edges");
  case Drill::Axis:
    return tr("Axis choice");
  case Drill::Plan:
    return tr("Plan blind");
  case Drill::Solve:
    return tr("Step solutions");
  case Drill::Full:
    return tr("Full EO Cross");
  }
  return {};
}

QString EoCrossPage::stepText(SolveStep step) const {
  return qs(stepName(step));
}

void EoCrossPage::buildSide() {
  side_ = new QWidget(this);
  auto *layout = new QVBoxLayout(side_);
  layout->setContentsMargins(0, 0, 0, 0);
  summary_ = new QLabel(side_);
  summary_->setWordWrap(true);
  summary_->setTextFormat(Qt::RichText);
  tree_ = new QTreeWidget(side_);
  tree_->setObjectName("eoCrossKinds");
  tree_->setColumnCount(4);
  tree_->setHeaderLabels({tr("Kind"), tr("Tries"), tr("Extra"), tr("Time")});
  tree_->setRootIsDecorated(false);
  tree_->setFocusPolicy(Qt::NoFocus);
  tree_->header()->setStretchLastSection(false);
  tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  for (int c = 1; c < 4; ++c) {
    tree_->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
  }
  clearButton_ = flatButton(tr("Clear statistics…"), side_);
  layout->addWidget(summary_);
  layout->addWidget(tree_, 1);
  layout->addWidget(helpLabel(
      tr("How each kind of state has gone lately, by drill: a kind is the "
         "number of edges bad for the front-back axis as the cube is first "
         "shown. Extra is your moves over the optimum of your holds (Bad "
         "edges: how far off the counts were; Axis choice: what the hold "
         "you picked costs over the best one), Time the mean (Plan blind: "
         "of the inspection). Kinds that go badly are marked and come up "
         "more often. Double-click a kind to practise only it."),
      side_));
  layout->addWidget(clearButton_, 0, Qt::AlignLeft);
  connect(tree_, &QTreeWidget::itemDoubleClicked, this,
          [this](QTreeWidgetItem *item) { treeActivated(item); });
  connect(clearButton_, &QPushButton::clicked, this, &EoCrossPage::clearStats);
}

void EoCrossPage::buildOptions() {
  auto box = [this] {
    auto *b = new QComboBox(options_);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
  };
  stepBox_ = box();
  for (SolveStep s : eoCrossSteps()) {
    stepBox_->addItem(stepText(s), static_cast<int>(s));
  }
  stepItem_ = options_->addPicker(tr("Step:"), stepBox_);

  badBox_ = box();
  badBox_->addItem(tr("Any, weak kinds more often"), -1);
  for (int bad : kBadEdgeKinds) {
    badBox_->addItem(QString::number(bad), bad);
  }
  badItem_ = options_->addPicker(tr("Bad edges:"), badBox_);

  axesBox_ = box();
  axesBox_->addItem(tr("F/B"), false);
  axesBox_->addItem(tr("F/B and R/L"), true);
  axesItem_ = options_->addPicker(tr("Axes:"), axesBox_);

  limitBox_ = box();
  for (int seconds : kLimits) {
    limitBox_->addItem(seconds == 0    ? tr("No limit")
                       : seconds == 15 ? tr("WCA (15 s)")
                                       : tr("%1 s").arg(seconds),
                       seconds);
  }
  limitItem_ = options_->addPicker(tr("Inspection:"), limitBox_);

  options_->addViewPicker(cubeView_, "eocross");
  options_->addStretch();

  connect(stepBox_, &QComboBox::activated, this, [this] {
    setStep(static_cast<SolveStep>(stepBox_->currentData().toInt()));
  });
  connect(badBox_, &QComboBox::activated, this,
          [this] { setPinnedBad(badBox_->currentData().toInt()); });
  connect(axesBox_, &QComboBox::activated, this,
          [this] { setBothAxes(axesBox_->currentData().toBool()); });
  connect(limitBox_, &QComboBox::activated, this,
          [this] { setLimitSeconds(limitBox_->currentData().toInt()); });
}

void EoCrossPage::loadChoices() {
  drill_ = static_cast<Drill>(std::clamp(
      saved::eoCross("drill", static_cast<int>(Drill::Solve)).get().toInt(), 0,
      kEoDrills - 1));
  std::optional<SolveStep> step = stepFromId(
      saved::eoCross("step", "eocross").get().toString().toStdString());
  const std::vector<SolveStep> &steps = eoCrossSteps();
  step_ = step && std::find(steps.begin(), steps.end(), *step) != steps.end()
              ? *step
              : SolveStep::EOCross;
  int bad = saved::eoCross("bad", -1).get().toInt();
  pinned_ = std::find(kBadEdgeKinds.begin(), kBadEdgeKinds.end(), bad) !=
                    kBadEdgeKinds.end()
                ? bad
                : -1;
  bothAxes_ = saved::eoCross("axes", false).get().toBool();
  int limit = saved::eoCross("limit", 15).get().toInt();
  limit_ = std::find(kLimits.begin(), kLimits.end(), limit) != kLimits.end()
               ? limit
               : 15;
  QSignalBlocker block(tabs_);
  tabs_->setCurrentIndex(static_cast<int>(drill_));
}

void EoCrossPage::saveChoices() const {
  saved::eoCross("drill").set(static_cast<int>(drill_));
  saved::eoCross("step").set(qs(stepId(step_)));
  saved::eoCross("bad").set(pinned_);
  saved::eoCross("axes").set(bothAxes_);
  saved::eoCross("limit").set(limit_);
}

void EoCrossPage::showOptions() {
  stepBox_->setCurrentIndex(stepBox_->findData(static_cast<int>(step_)));
  badBox_->setCurrentIndex(std::max(0, badBox_->findData(pinned_)));
  axesBox_->setCurrentIndex(bothAxes_ ? 1 : 0);
  limitBox_->setCurrentIndex(std::max(0, limitBox_->findData(limit_)));
  stepItem_->setVisible(hasStep(drill_));
  badItem_->setVisible(drill_ != Drill::Axis);
  axesItem_->setVisible(drill_ == Drill::Edges);
  limitItem_->setVisible(hasLimit(drill_));
}

void EoCrossPage::optionChanged() {
  ready_ = false;
  saveChoices();
  showOptions();
  showSummary();
  if (active_) {
    newRound();
  }
}

void EoCrossPage::selectDrill(Drill d) {
  drill_ = d;
  {
    QSignalBlocker block(tabs_);
    tabs_->setCurrentIndex(static_cast<int>(d));
  }
  optionChanged();
}

void EoCrossPage::setStep(SolveStep step) {
  const std::vector<SolveStep> &steps = eoCrossSteps();
  if (std::find(steps.begin(), steps.end(), step) == steps.end()) {
    return;
  }
  bool changed = step != step_;
  step_ = step;
  if (changed) {
    fillTree(); // the list is the step's
  }
  optionChanged();
}

void EoCrossPage::setPinnedBad(int bad) {
  pinned_ = std::find(kBadEdgeKinds.begin(), kBadEdgeKinds.end(), bad) !=
                    kBadEdgeKinds.end()
                ? bad
                : -1;
  optionChanged();
}

void EoCrossPage::setBothAxes(bool on) {
  bothAxes_ = on;
  optionChanged();
}

void EoCrossPage::setLimitSeconds(int seconds) {
  limit_ = std::max(0, seconds);
  optionChanged();
}

std::vector<Hold> EoCrossPage::allowedHolds() const {
  return eoHolds(settings_.neutrality);
}

bool EoCrossPage::cubeHidden() const {
  return cubeStack_->currentWidget() == hiddenLabel_;
}

void EoCrossPage::applySettings() {
  CubeLook look = cubeView_->look();
  look.scheme = settings_.scheme;
  cubeView_->setLook(look);
  cubeView_->setAnimationMs(settings_.animationMs);
  fillTree();
  // The holds may have changed: a state half done isn't taken up again.
  ready_ = false;
  if (active_ && phase_ != Phase::Done) {
    newRound();
  } else if (active_) {
    showPrompt();
    showResult();
  }
}

void EoCrossPage::activated() {
  active_ = true;
  ready_ = false; // a state left half done isn't taken up again
  if (round_.id == 0 || phase_ != Phase::Done) {
    newRound();
  }
}

void EoCrossPage::deactivated() {
  active_ = false;
  clock_.stop();
  cubeView_->finishAnimations();
  if (phase_ != Phase::Done && phase_ != Phase::Ready) {
    // The clock was running: the state is given up, unrecorded, and the
    // ready page is there on the way back.
    showReady();
  }
}

void EoCrossPage::focusModeChanged() {
  options_->setVisible(!focusMode());
  tabs_->setVisible(!focusMode());
}

// --- rounds ------------------------------------------------------------------

void EoCrossPage::showCube(bool shown) {
  cubeStack_->setCurrentWidget(shown ? static_cast<QWidget *>(cubeView_)
                                     : hiddenLabel_);
}

void EoCrossPage::showReady() {
  round_ = {};
  round_.id = ++rounds_;
  phase_ = Phase::Ready;
  clock_.stop();
  display_->setVisible(false);
  cube_ = FaceletCube();
  cube_.apply(holdRotation(settings_.neutrality.main));
  cubeView_->clearMarks();
  cubeView_->setState(cube_);
  showCube(true);
  showPrompt();
  showKeys();
  showResult();
}

void EoCrossPage::newRound(bool sameState) {
  if (!sameState && !ready_) {
    showReady();
    return;
  }
  if (sameState && round_.id != 0) {
    const CubieCube again = round_.scrambled; // startRound resets round_
    startRound(round_.kind, again, round_.shown, true);
    return;
  }
  EoKind kind = normalized({drill_, step_, -1});
  const std::vector<Hold> allowed = allowedHolds();
  // Bad edges shows the cube in any of the neutrality's holds; the others
  // in the main one, to be turned as the solve needs.
  Hold shown = drill_ == Drill::Edges ? settings_.neutrality.pick(rng_)
                                      : settings_.neutrality.main;
  CubieCube scrambled;
  for (int tries = 0; tries < 60; ++tries) {
    if (drill_ != Drill::Axis) {
      // A kind pinned that the step can't have (EO with no bad edges) gives
      // way to the others.
      kind.bad = pinned_ >= 0 && tries < 30
                     ? pinned_
                     : pickBadEdges(store_.stats, drill_, kind.step, rng_);
    }
    scrambled = heldState(randomEoState(kind.bad, rng_), shown);
    // Not a state with the step already done in one of the holds.
    if (drill_ == Drill::Edges ||
        !doneHold(scrambled.toFacelets(), kind.step, allowed)) {
      break;
    }
  }
  startRound(kind, scrambled, shown, false);
}

void EoCrossPage::startRound(EoKind kind, CubieCube scrambled, Hold shown,
                             bool retry) {
  round_ = {};
  round_.id = ++rounds_;
  round_.kind = kind;
  round_.scrambled = scrambled;
  round_.shown = shown;
  round_.retry = retry;
  round_.start = scrambled.toFacelets();
  round_.start.apply(holdRotation(shown));
  cube_ = round_.start;
  cubeView_->clearMarks();
  cubeView_->setState(cube_);
  showCube(true);
  switch (kind.drill) {
  case Drill::Edges: {
    phase_ = Phase::Counting;
    round_.both = bothAxes_;
    CubieCube held = solverState(scrambled, shown);
    round_.correct[0] = badEdgeCount(held, 0);
    if (round_.both) {
      round_.correct[1] = badEdgeCount(held, 1);
    }
    round_.analysed = true;
    break;
  }
  case Drill::Axis:
    phase_ = Phase::Choosing;
    break;
  case Drill::Plan:
    phase_ = Phase::Inspecting;
    break;
  case Drill::Solve:
  case Drill::Full:
    phase_ = Phase::Solving;
    break;
  }
  if (kind.drill != Drill::Edges) {
    requestAnalysis();
  }
  round_.began = monotonicMs();
  display_->setVisible(true);
  clock_.start();
  showPrompt();
  showKeys();
  showClock();
  showResult();
}

void EoCrossPage::turn(Move m) {
  const bool rotation = isRotation(m.type);
  switch (phase_) {
  case Phase::Ready:
    return;
  case Phase::Counting:
  case Phase::Choosing:
  case Phase::Inspecting:
    if (!rotation) {
      emit message(phase_ == Phase::Choosing
                       ? tr("Only rotations here: turn the cube to the hold "
                            "you would solve from")
                       : tr("Only rotations while you look at the cube"));
      return;
    }
    cube_.apply(m);
    cubeView_->animateMove(m);
    if (phase_ == Phase::Counting) {
      round_.lookTurns.push_back(m);
    }
    return;
  case Phase::Executing:
    cube_.apply(m); // not shown: the cube is hidden
    round_.moves.push_back(m);
    showClock();
    return;
  case Phase::Solving:
    cube_.apply(m);
    cubeView_->animateMove(m);
    round_.moves.push_back(m);
    if (std::optional<Hold> h = doneHold(cube_, roundStep(), allowedHolds())) {
      finish(false);
    }
    return;
  case Phase::Done:
    if (drill_ == Drill::Edges) {
      // The rings stay on their edges only while the cube turns whole.
      if (!rotation) {
        emit message(tr("Only rotations here: turn the cube as a whole to "
                        "look at it"));
        return;
      }
      round_.lookTurns.push_back(m);
      cubeView_->setMarks(movedMarks(cubeView_->marks(), m));
    }
    cube_.apply(m);
    cubeView_->animateMove(m);
    return;
  }
}

void EoCrossPage::finish(bool gaveUp) {
  if (phase_ != Phase::Solving && phase_ != Phase::Executing &&
      phase_ != Phase::Inspecting && phase_ != Phase::Choosing) {
    return;
  }
  const std::int64_t now = monotonicMs();
  if (phase_ == Phase::Inspecting) {
    round_.usedMs = now - round_.began;
  }
  round_.ms = drill_ == Drill::Plan ? round_.usedMs : now - round_.began;
  round_.solvedIn = doneHold(cube_, roundStep(), allowedHolds());
  // A plan that didn't do the step is as good as given up.
  round_.gaveUp = gaveUp || (drill_ != Drill::Axis && !round_.solvedIn) ||
                  (drill_ == Drill::Axis && !round_.picked);
  clock_.stop();
  ensureAnalysed();
  if (drill_ == Drill::Full && round_.solvedIn) {
    round_.breakdown =
        eoBreakdown(round_.start, round_.moves, *round_.solvedIn);
  }
  phase_ = Phase::Done;
  cubeView_->finishAnimations();
  cubeView_->setState(cube_);
  showCube(true);
  QString shown;
  if (gaveUp) {
    shown = tr("Gave up");
  } else if (round_.gaveUp) {
    shown = drill_ == Drill::Axis ? tr("No hold") : tr("DNF");
  }
  if (!shown.isEmpty()) {
    display_->setText(QString("<span style='color:%1'>%2</span>")
                          .arg(kFailColour.name(), shown));
  } else if (drill_ == Drill::Plan) {
    display_->setText(
        counted(solutionHtm(round_.moves), tr("move"), tr("moves")) +
        QString(" <span style='font-size:medium'>· %1</span>")
            .arg(tr("%1 s looking").arg(clockText(round_.ms))));
  } else {
    display_->setText(clockText(round_.ms));
  }
  record();
  showPrompt();
  showKeys();
  showResult();
}

void EoCrossPage::resetCube() {
  if (phase_ != Phase::Solving && phase_ != Phase::Executing) {
    return;
  }
  cubeView_->finishAnimations();
  cube_ = round_.start;
  cubeView_->setState(cube_);
  round_.moves.clear();
  showClock();
}

void EoCrossPage::showHint() {
  if (phase_ != Phase::Solving) {
    return;
  }
  std::optional<Hold> now = holdOf(cube_);
  if (!now) {
    return;
  }
  const SolveStep step = roundStep();
  // The hold nearest the step from here; the one the cube is in if it is
  // as near as any.
  std::vector<Hold> holds = allowedHolds();
  if (std::any_of(holds.begin(), holds.end(),
                  [&](Hold h) { return sameStepProblem(h, *now, step); })) {
    holds.insert(holds.begin(), *now);
  }
  std::optional<Hold> best;
  int bestLength = 0;
  for (Hold h : holds) {
    std::optional<CubieCube> held = heldCubie(cube_, h);
    std::optional<int> d = held ? stepDistance(*held, step) : std::nullopt;
    if (d && (!best || *d < bestLength)) {
      best = h;
      bestLength = *d;
    }
  }
  if (!best) {
    return;
  }
  round_.hint = true;
  QString text;
  if (*best != *now) {
    text = tr("Hint: turn to %1 (%2), %3 from there")
               .arg(holdWords(*best, step == SolveStep::EO),
                    qs(toString(rotationBetween(*now, *best))),
                    counted(bestLength, tr("move"), tr("moves")));
  } else if (auto optimum = solveStep(*heldCubie(cube_, *best), step, 1);
             optimum && !optimum->solutions.front().empty()) {
    text = tr("Hint: %1 next, %2 to go")
               .arg(qs(toString(optimum->solutions.front().front())),
                    counted(bestLength, tr("move"), tr("moves")));
  }
  showKeys();
  hint_->setHint(text, hint_->keys());
}

// --- Bad edges ---------------------------------------------------------------

QString EoCrossPage::axisText(int axis, bool html) const {
  std::array<int, 6> colours = holdColours(round_.shown);
  // The faces are U R F D L B.
  int a = colours[axis == 0 ? 2 : 1], b = colours[axis == 0 ? 5 : 4];
  auto name = [&](int slot) {
    return html ? colourHtml(slot, settings_.scheme)
                : colourName(slot).toLower();
  };
  return tr("%1 (%2 / %3)")
      .arg(axis == 0 ? tr("F/B") : tr("R/L"), name(a), name(b));
}

void EoCrossPage::typeDigit(int digit) {
  if (round_.entry.size() >= 2) {
    return;
  }
  round_.entry += static_cast<char>('0' + digit);
  showClock();
}

void EoCrossPage::enterCount() {
  if (round_.entry.empty()) {
    emit message(tr("Type the number of bad edges first"));
    return;
  }
  round_.answers[static_cast<std::size_t>(round_.axis)] =
      std::stoi(round_.entry);
  round_.entry.clear();
  if (round_.axis == 0 && round_.both) {
    round_.axis = 1;
    showPrompt();
    showClock();
    return;
  }
  finishEdges();
}

void EoCrossPage::finishEdges() {
  round_.ms = monotonicMs() - round_.began;
  clock_.stop();
  phase_ = Phase::Done;
  bool right = true;
  round_.ringAxis = 0;
  for (int axis = 1; axis >= 0; --axis) {
    auto i = static_cast<std::size_t>(axis);
    if (round_.correct[i] >= 0 && round_.answers[i] != round_.correct[i]) {
      right = false;
      round_.ringAxis = axis; // the first one that was wrong
    }
  }
  display_->setText(QString("<span style='color:%1'>%2</span> "
                            "<span style='font-size:medium'>· %3 s</span>")
                        .arg(right ? kPassColour.name() : kFailColour.name(),
                             right ? tr("Right") : tr("Wrong"),
                             clockText(round_.ms)));
  record();
  showRings();
  showPrompt();
  showKeys();
  showResult();
}

void EoCrossPage::showRings() {
  CubieCube held = solverState(round_.scrambled, round_.shown);
  std::array<bool, kEdges> bad = badEdges(held, round_.ringAxis);
  StickerMarks marks;
  for (std::size_t e = 0; e < kEdges; ++e) {
    if (bad[e]) {
      for (std::uint8_t facelet : kEdgeFacelets[e]) {
        marks.rings.push_back(facelet);
      }
    }
  }
  // Moved with the cube if it was turned to look at it.
  for (Move m : round_.lookTurns) {
    marks = movedMarks(marks, m);
  }
  cubeView_->setMarks(marks);
}

// --- Axis choice and Plan blind
// ------------------------------------------------

void EoCrossPage::confirmHold(bool timeUp) {
  if (phase_ != Phase::Choosing) {
    return;
  }
  ensureAnalysed();
  std::optional<Hold> now = holdOf(cube_);
  round_.picked =
      now ? rankOfHold(round_.ranked, *now, roundStep()) : std::nullopt;
  if (!round_.picked && !timeUp) {
    emit message(tr("That isn't one of your holds (see Settings, Colour "
                    "neutrality): turn to one of them"));
    return;
  }
  finish(false);
}

void EoCrossPage::hideCube(bool timeUp) {
  if (phase_ != Phase::Inspecting) {
    return;
  }
  const std::int64_t now = monotonicMs();
  round_.usedMs = now - round_.began;
  round_.hiddenAt = timeUp ? now : -1;
  phase_ = Phase::Executing;
  cubeView_->finishAnimations();
  // What was done so far was looking: the plan starts from here.
  round_.start = cube_;
  showCube(false);
  showPrompt();
  showKeys();
  showClock();
}

void EoCrossPage::reveal() {
  if (phase_ != Phase::Executing) {
    return;
  }
  if (round_.hiddenAt >= 0 && monotonicMs() - round_.hiddenAt < kHideGraceMs) {
    return; // that Space was meant to hide the cube
  }
  if (solutionHtm(round_.moves) == 0 &&
      !doneHold(cube_, roundStep(), allowedHolds())) {
    emit message(tr("No moves done yet: do your plan, then press Space "
                    "(Backspace gives up)"));
    return;
  }
  finish(false);
}

// --- the solutions listed
// ------------------------------------------------------

std::vector<EoCrossPage::Solution> EoCrossPage::shownSolutions() const {
  std::vector<Solution> out;
  if (phase_ != Phase::Done || drill_ == Drill::Edges) {
    return out;
  }
  // A few that differ per hold, fewer when there are many holds.
  const std::size_t perHold = round_.ranked.size() <= 2   ? 4
                              : round_.ranked.size() <= 4 ? 3
                                                          : 2;
  for (const HeldOptimum &r : round_.ranked) {
    for (std::size_t i : distinctSolutions(r.optimum.solutions, perHold)) {
      out.push_back({r.hold, r.optimum.solutions[i]});
    }
  }
  return out;
}

void EoCrossPage::playSolution(std::size_t index) {
  std::vector<Solution> solutions = shownSolutions();
  if (index >= solutions.size()) {
    return;
  }
  const Solution &s = solutions[index];
  cubeView_->finishAnimations();
  cube_ = round_.scrambled.toFacelets();
  cube_.apply(holdRotation(s.hold));
  cubeView_->clearMarks();
  cubeView_->setState(cube_);
  cubeView_->animateSequence(s.alg);
  cube_.apply(s.alg);
}

// --- prompt, clock and keys
// ----------------------------------------------------

QString EoCrossPage::promptHtml() const { return prompt_->text(); }
QString EoCrossPage::displayText() const { return display_->text(); }

QString EoCrossPage::clockText(std::int64_t ms) const {
  return QString::number(static_cast<double>(ms) / 1000.0, 'f',
                         settings_.decimals);
}

void EoCrossPage::showClock() {
  const std::int64_t elapsed = monotonicMs() - round_.began;
  const QString muted = palette().color(QPalette::PlaceholderText).name();
  switch (phase_) {
  case Phase::Ready:
  case Phase::Done:
    break;
  case Phase::Solving:
    display_->setText(clockText(elapsed));
    break;
  case Phase::Counting:
    display_->setText(QString("%1 &nbsp;<b>%2</b>")
                          .arg(round_.axis == 0 ? tr("F/B") : tr("R/L"),
                               round_.entry.empty() ? QStringLiteral("_")
                                                    : qs(round_.entry)));
    break;
  case Phase::Executing:
    display_->setText(QString("<span style='color:%1'>%2</span>")
                          .arg(muted, counted(solutionHtm(round_.moves),
                                              tr("move"), tr("moves"))));
    break;
  case Phase::Choosing:
  case Phase::Inspecting: {
    if (limit_ <= 0) {
      display_->setText(QString::number(elapsed / 1000));
      break;
    }
    const std::int64_t left = limit_ * 1000LL - elapsed;
    if (left <= 0) {
      if (phase_ == Phase::Inspecting) {
        hideCube(true);
      } else {
        confirmHold(true);
      }
      break;
    }
    QString colour;
    if (limit_ == 15) {
      // The WCA calls, at 8 and at 12 seconds.
      for (int call : {8, 12}) {
        int made = call == 8 ? 1 : 2;
        if (elapsed >= call * 1000 && round_.calls < made) {
          round_.calls = made;
          if (settings_.inspectionSounds) {
            playInspectionCall(call);
          }
        }
      }
      colour = elapsed >= 12000  ? kFailColour.name()
               : elapsed >= 8000 ? kWarn.name()
                                 : QString();
    } else if (left <= 3000) {
      colour = kFailColour.name();
    }
    QString shown = QString::number((left + 999) / 1000);
    display_->setText(
        colour.isEmpty()
            ? shown
            : QString("<span style='color:%1'>%2</span>").arg(colour, shown));
    break;
  }
  }
}

QString EoCrossPage::whereText(SolveStep step) const {
  const std::vector<Hold> holds = allowedHolds();
  if (step == SolveStep::EO) {
    // EO alone has only an axis.
    std::vector<int> axes;
    for (Hold h : holds) {
      if (std::find(axes.begin(), axes.end(), h.front % 3) == axes.end()) {
        axes.push_back(h.front % 3);
      }
    }
    return axes.size() == 2 ? tr("on either of your EO axes")
                            : tr("on any of your %1 EO axes").arg(axes.size());
  }
  QStringList crosses;
  for (Hold h : holds) {
    QString name = colourName((h.top + 3) % 6).toLower();
    if (!crosses.contains(name)) {
      crosses << name;
    }
  }
  return crosses.size() > 3
             ? tr("with any of your %1 cross colours, on either EO axis")
                   .arg(crosses.size())
             : tr("with a %1 cross, on either EO axis")
                   .arg(crosses.join(tr(" or ")));
}

void EoCrossPage::showPrompt() {
  auto small = [](const QString &t) {
    return t.isEmpty() ? QString()
                       : "<br><span style='font-size:small'>" + t + "</span>";
  };
  const SolveStep theStep = phase_ == Phase::Ready
                                ? normalized({drill_, step_, -1}).step
                                : roundStep();
  const QString step = stepText(theStep);
  const QString where = whereText(theStep);
  if (phase_ == Phase::Ready) {
    QString what;
    switch (drill_) {
    case Drill::Edges:
      what = bothAxes_ ? tr("Count the bad edges for the F/B axis, then for "
                            "the R/L axis")
                       : tr("Count the bad edges for the F/B axis");
      break;
    case Drill::Axis:
      what = tr("Turn the cube to the hold %1 is shortest from, %2")
                 .arg(step, where);
      break;
    case Drill::Plan:
      what = tr("Plan %1 while you look at the cube, then do it without "
                "seeing it")
                 .arg(step);
      break;
    case Drill::Solve:
      what = tr("Solve %1 %2, then compare with the optimal solutions")
                 .arg(step, where);
      break;
    case Drill::Full:
      what = tr("Solve EO Cross %1, then see where the moves went").arg(where);
      break;
    }
    prompt_->setText(drillName(drill_) +
                     small(tr("%1. Timed from the moment the cube is shown: "
                              "press Space when you are ready.")
                               .arg(what)));
    return;
  }
  switch (phase_) {
  case Phase::Counting:
    prompt_->setText(
        tr("How many bad edges?") +
        small(tr("for the %1 axis").arg(axisText(round_.axis, true))));
    break;
  case Phase::Choosing:
    prompt_->setText(tr("Which hold for %1?").arg(step) +
                     small(tr("turn the cube to it: %1").arg(where)));
    break;
  case Phase::Inspecting:
    prompt_->setText(tr("Plan %1").arg(step) + small(where));
    break;
  case Phase::Executing:
    prompt_->setText(tr("Do your plan for %1").arg(step) +
                     small(tr("the cube is hidden")));
    break;
  case Phase::Solving:
    prompt_->setText(tr("Solve %1").arg(step) + small(where));
    break;
  case Phase::Done:
    prompt_->setText(verdictHtml());
    break;
  case Phase::Ready:
    break;
  }
}

void EoCrossPage::showKeys() {
  QList<KeyHint> keys;
  QString message;
  switch (phase_) {
  case Phase::Ready:
    message = tr("Ready when you are.");
    keys = {{"Space", tr("start")}};
    break;
  case Phase::Counting:
    keys = {{"0-9", tr("the count")},
            {"Enter", round_.axis == 0 && round_.both ? tr("the next axis")
                                                      : tr("answer")},
            {"Backspace", tr("correct")},
            {tr("rotations"), tr("look at the cube")}};
    break;
  case Phase::Choosing:
    keys = {{tr("rotations"), tr("turn to your hold")},
            {"Space", tr("this hold")}};
    break;
  case Phase::Inspecting:
    keys = {{tr("rotations"), tr("look at the cube")},
            {"Space", tr("hide it and do the plan")},
            {"Backspace", tr("give up")}};
    break;
  case Phase::Executing:
    keys = {{"Space", tr("done: show the cube")},
            {"Esc", tr("start the moves again")},
            {"Backspace", tr("give up")}};
    break;
  case Phase::Solving:
    keys = {{"Tab", tr("a hint")},
            {"Backspace", tr("give up")},
            {"Esc", tr("start again")}};
    break;
  case Phase::Done:
    keys << KeyHint{"Space", tr("next")};
    if (drill_ == Drill::Edges) {
      message = tr("Ringed: the bad edges for the %1 axis.")
                    .arg(axisText(round_.ringAxis, false));
      if (round_.both) {
        keys << KeyHint{"Tab", tr("the other axis")};
      }
      keys << KeyHint{tr("rotations"), tr("look at the cube")};
    } else {
      keys << KeyHint{"Enter", tr("play the best solution")}
           << KeyHint{"1-9", tr("play that solution")};
    }
    keys << KeyHint{"Esc", tr("try it again (not recorded)")};
    break;
  }
  hint_->setHint(message, keys);
}

// --- keys --------------------------------------------------------------------

bool EoCrossPage::handleKey(QKeyEvent *e, bool press) {
  if (e->modifiers() &
      (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
    return false;
  }
  if (!press || round_.id == 0) {
    return false;
  }
  const int key = e->key();
  if (phase_ == Phase::Ready) {
    // Only Space starts.
    if (key != Qt::Key_Space) {
      return false;
    }
    ready_ = true;
    newRound();
    return true;
  }
  const bool digit = key >= Qt::Key_0 && key <= Qt::Key_9;
  if (phase_ == Phase::Counting) {
    // The count comes before a turning key on a digit or on Enter.
    if (digit) {
      typeDigit(key - Qt::Key_0);
      return true;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
      enterCount();
      return true;
    }
    if (key == Qt::Key_Backspace) {
      if (!round_.entry.empty()) {
        round_.entry.pop_back();
        showClock();
      }
      return true;
    }
  }
  switch (key) {
  case Qt::Key_Space:
    switch (phase_) {
    case Phase::Done:
      ready_ = true;
      newRound();
      break;
    case Phase::Choosing:
      confirmHold(false);
      break;
    case Phase::Inspecting:
      hideCube(false);
      break;
    case Phase::Executing:
      reveal();
      break;
    default:
      break;
    }
    return true;
  case Qt::Key_Tab:
    if (phase_ == Phase::Done && drill_ == Drill::Edges && round_.both) {
      round_.ringAxis = 1 - round_.ringAxis;
      showRings();
      showKeys();
    } else {
      showHint();
    }
    return true;
  case Qt::Key_Backspace:
    finish(true);
    return true;
  case Qt::Key_Escape:
    if (phase_ == Phase::Done) {
      newRound(true);
    } else {
      resetCube();
    }
    return true;
  case Qt::Key_Return:
  case Qt::Key_Enter:
    if (phase_ == Phase::Done) {
      playSolution(0);
    }
    return true;
  default:
    break;
  }
  if (phase_ == Phase::Done && drill_ != Drill::Edges && key >= Qt::Key_1 &&
      key <= Qt::Key_9) {
    playSolution(static_cast<std::size_t>(key - Qt::Key_1));
    return true;
  }
  if (auto m = settings_.keymap.lookup(e)) {
    turn(*m);
    return true;
  }
  return false;
}

} // namespace vcube
