#include "app/tsle_page.hpp"

#include "app/session_panel.hpp"
#include "app/ui_state.hpp"
#include "app/ui_text.hpp"
#include "train/f2l.hpp"

#include <QComboBox>
#include <QDir>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace vcube {

namespace {

QString slotText(Slot s) { return qs(slotName(s)); }

// Whether the moves turn anything but the top layer and the whole cube.
bool turnsTheSlot(const Alg &moves) {
  Alg simple = simplify(moves);
  return std::any_of(simple.begin(), simple.end(), [](Move m) {
    return m.type != MoveType::U && !isRotation(m.type);
  });
}

} // namespace

TslePage::TslePage(AppSettings &settings, const QString &dataDir,
                   QWidget *parent)
    : ModePage(parent), settings_(settings),
      store_(QDir(dataDir).filePath("tsle.json")), rng_(makeRng()) {
  QString error;
  if (!store_.load(&error)) {
    QString kept =
        store_.setAsidePath().isEmpty()
            ? tr("It is left where it is, and nothing will be saved "
                 "over it.")
            : tr("It was kept as %1.")
                  .arg(QDir::toNativeSeparators(store_.setAsidePath()));
    QMessageBox::warning(this, "v-cube",
                         tr("Could not read your TSLE progress (%1), so TSLE "
                            "starts afresh.")
                                 .arg(error) +
                             " " + kept);
  }
  rebuildSolvers();
  buildSide();

  auto *right = new QWidget(this);
  auto *rightLayout = new QVBoxLayout(right);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  options_ = new OptionBar(right);
  cubeView_ = new CubeView(right);
  cubeView_->setMinimumSize(260, 200);
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
  result_->setObjectName("tsleResult");
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
  rightLayout->addWidget(cubeView_, 4);
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
  for (const QString &name : {tr("Guided intro"), tr("Next trigger"),
                              tr("Full TSLE"), tr("Case explorer")}) {
    tabs_->addTab(name);
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
    if (link.startsWith("opt:")) {
      playOption(static_cast<std::size_t>(link.mid(4).toInt()));
    }
  });
  clock_.setInterval(37);
  connect(&clock_, &QTimer::timeout, this, &TslePage::showClock);

  loadChoices();
  applySettings();
  showOptions();
}

TslePage::~TslePage() = default;

// --- building and options ----------------------------------------------------

void TslePage::buildSide() {
  side_ = new QWidget(this);
  auto *layout = new QVBoxLayout(side_);
  layout->setContentsMargins(0, 0, 0, 0);
  summary_ = new QLabel(side_);
  summary_->setWordWrap(true);
  summary_->setTextFormat(Qt::RichText);
  tree_ = new QTreeWidget(side_);
  tree_->setObjectName("tsleCases");
  tree_->setColumnCount(4);
  tree_->setHeaderLabels({tr("Case"), tr("Tries"), tr("Extra"), tr("Time")});
  tree_->setRootIsDecorated(false);
  tree_->setFocusPolicy(Qt::NoFocus);
  tree_->header()->setStretchLastSection(false);
  tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  for (int c = 1; c < 4; ++c) {
    tree_->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
  }
  clearButton_ = flatButton(tr("Clear progress…"), side_);
  layout->addWidget(summary_);
  layout->addWidget(tree_, 1);
  layout->addWidget(helpLabel(
      tr("The cases by steps left (a step is a trigger, or the OCLL that "
         "ends a chain), drawn for the front-right slot with the edge at UF. "
         "A name gives the edge, the slot's corner place (N oriented, CW, "
         "CCW) and the twists at URF, UFL, ULB, UBR (0 oriented, 1 "
         "clockwise, 2 anticlockwise). Extra is your moves over the best "
         "way lately (a trigger that isn't good counts as 3). Double-click "
         "a case to explore it."),
      side_));
  layout->addWidget(clearButton_, 0, Qt::AlignLeft);
  connect(tree_, &QTreeWidget::currentItemChanged, this,
          [this] { treeSelected(); });
  connect(tree_, &QTreeWidget::itemDoubleClicked, this,
          [this](QTreeWidgetItem *item) {
            if (auto c = tsleFromKey(
                    item->data(0, Qt::UserRole).toString().toStdString())) {
              exploreCase(*c);
            }
          });
  connect(clearButton_, &QPushButton::clicked, this, &TslePage::clearProgress);
}

void TslePage::buildOptions() {
  slotsButton_ = new QToolButton(options_);
  slotsButton_->setFocusPolicy(Qt::NoFocus);
  slotsButton_->setPopupMode(QToolButton::InstantPopup);
  auto *menu = new QMenu(slotsButton_);
  for (Slot s : kF2LSlots) {
    QAction *a = menu->addAction(slotText(s));
    a->setCheckable(true);
    a->setData(static_cast<int>(s));
    connect(a, &QAction::toggled, this, [this, s](bool on) {
      std::array<bool, kSlots> chosen = slots_;
      chosen[static_cast<std::size_t>(s)] = on;
      setLastSlots(chosen);
    });
  }
  slotsButton_->setMenu(menu);
  options_->addPicker(tr("Last slot:"), slotsButton_);

  casesBox_ = new QComboBox(options_);
  casesBox_->setFocusPolicy(Qt::NoFocus);
  casesBox_->addItem(tr("Introduced"), false);
  casesBox_->addItem(tr("All"), true);
  casesItem_ = options_->addPicker(tr("Cases:"), casesBox_);

  slotBox_ = new QComboBox(options_);
  slotBox_->setFocusPolicy(Qt::NoFocus);
  slotBox_->addItem(tr("Any slot"), -1);
  for (Slot s : kF2LSlots) {
    slotBox_->addItem(slotText(s), static_cast<int>(s));
  }
  slotItem_ = options_->addPicker(tr("Slot:"), slotBox_);

  options_->addViewPicker(cubeView_, "tsle");
  options_->addStretch();
  ocllButton_ = flatButton(tr("OCLL algs…"), options_);
  options_->addWidget(ocllButton_);

  connect(casesBox_, &QComboBox::activated, this,
          [this] { setAllCases(casesBox_->currentData().toBool()); });
  connect(slotBox_, &QComboBox::activated, this, [this] {
    saveChoices();
    if (active_) {
      newRound();
    }
  });
  connect(ocllButton_, &QPushButton::clicked, this, &TslePage::editOcllAlgs);
}

void TslePage::loadChoices() {
  QStringList chosen =
      saved::tsle("slots", QStringList{"FR", "FL"}).get().toStringList();
  bool any = false;
  for (Slot s : kF2LSlots) {
    bool on = chosen.contains(slotText(s));
    slots_[static_cast<std::size_t>(s)] = on;
    any = any || on;
  }
  if (!any) {
    slots_ = {true, true, false, false};
  }
  allCases_ = saved::tsle("allCases", false).get().toBool();
  drill_ = static_cast<Drill>(
      std::clamp(saved::tsle("drill", 0).get().toInt(), 0, 3));
  slotBox_->setCurrentIndex(
      std::max(0, slotBox_->findData(saved::tsle("slot", -1).get().toInt())));
  QSignalBlocker block(tabs_);
  tabs_->setCurrentIndex(static_cast<int>(drill_));
}

void TslePage::saveChoices() const {
  QStringList chosen;
  for (Slot s : kF2LSlots) {
    if (slots_[static_cast<std::size_t>(s)]) {
      chosen << slotText(s);
    }
  }
  saved::tsle("slots").set(chosen);
  saved::tsle("allCases").set(allCases_);
  saved::tsle("drill").set(static_cast<int>(drill_));
  saved::tsle("slot").set(slotBox_->currentData());
}

void TslePage::showOptions() {
  QStringList names;
  for (QAction *a : slotsButton_->menu()->actions()) {
    QSignalBlocker block(a);
    bool on = slots_[static_cast<std::size_t>(a->data().toInt())];
    a->setChecked(on);
    if (on) {
      names << a->text();
    }
  }
  slotsButton_->setText(names.join(" / "));
  {
    QSignalBlocker block(casesBox_);
    casesBox_->setCurrentIndex(allCases_ ? 1 : 0);
  }
  casesItem_->setVisible(drill_ == Drill::Next || drill_ == Drill::Full);
  slotItem_->setVisible(drill_ == Drill::Explore);
}

void TslePage::setLastSlots(const std::array<bool, kSlots> &chosen) {
  if (std::none_of(chosen.begin(), chosen.end(), [](bool b) { return b; })) {
    showOptions(); // one slot at least
    return;
  }
  slots_ = chosen;
  saveChoices();
  showOptions();
  if (active_) {
    newRound();
  }
}

void TslePage::setAllCases(bool on) {
  allCases_ = on;
  saveChoices();
  showOptions();
  if (active_ && (drill_ == Drill::Next || drill_ == Drill::Full)) {
    newRound();
  }
}

void TslePage::rebuildSolvers() {
  std::vector<OcllAlg> algs = store_.progress.ocllAlgs();
  for (Slot s : kF2LSlots) {
    solvers_[static_cast<std::size_t>(s)] =
        std::make_unique<TsleSolver>(s, algs);
  }
}

const TsleSolver &TslePage::solver(Slot slot) const {
  return *solvers_[static_cast<std::size_t>(slot)];
}

Slot TslePage::randomSlot() {
  std::vector<Slot> allowed;
  for (Slot s : kF2LSlots) {
    if (slots_[static_cast<std::size_t>(s)]) {
      allowed.push_back(s);
    }
  }
  return allowed[std::uniform_int_distribution<std::size_t>(0, allowed.size() -
                                                                   1)(rng_)];
}

void TslePage::selectDrill(Drill d) {
  drill_ = d;
  {
    QSignalBlocker block(tabs_);
    tabs_->setCurrentIndex(static_cast<int>(d));
  }
  saveChoices();
  showOptions();
  if (active_) {
    newRound();
  }
}

void TslePage::exploreCase(TsleState c) {
  explored_ = tsleCase(c);
  exploreAuf_ = 0;
  if (drill_ != Drill::Explore) {
    selectDrill(Drill::Explore);
  } else if (active_) {
    newRound();
  }
  selectInTree(explored_);
}

void TslePage::applySettings() {
  CubeLook look = cubeView_->look();
  look.scheme = settings_.scheme;
  cubeView_->setLook(look);
  cubeView_->setAnimationMs(settings_.animationMs);
  fillTree();
  if (active_ && round_.id != 0) {
    showPrompt();
  }
}

void TslePage::activated() {
  active_ = true;
  if (round_.id == 0 || phase_ != Phase::Done) {
    newRound();
  }
}

void TslePage::deactivated() {
  active_ = false;
  clock_.stop();
  cubeView_->finishAnimations();
}

void TslePage::focusModeChanged() {
  options_->setVisible(!focusMode());
  tabs_->setVisible(!focusMode());
}

// --- rounds ------------------------------------------------------------------

std::optional<TsleState> TslePage::currentState() const {
  auto c = centredCubie(cube_);
  return c ? tsleStateOf(*c, round_.slot) : std::nullopt;
}

TsleState TslePage::pickCase(bool deep) {
  const TsleProgress &p = store_.progress;
  double sum = 0.0;
  int n = 0;
  for (const auto &[key, attempts] : p.cases) {
    for (const PairAttempt &a : attempts) {
      if (!a.gaveUp) {
        sum += static_cast<double>(a.ms);
        ++n;
      }
    }
  }
  const double overall = n ? sum / n : 0.0;
  std::vector<TsleState> pool;
  std::vector<double> weights;
  for (int pass = 0; pass < 2 && pool.empty(); ++pass) {
    for (TsleState c : tsleCases()) {
      if (!(allCases_ || p.isIntroduced(c)) ||
          (deep && pass == 0 && tsleStepsLeft(c) < 2)) {
        continue;
      }
      auto it = p.cases.find(tsleKey(c));
      double w = summarizeCase(it == p.cases.end() ? std::vector<PairAttempt>{}
                                                   : it->second,
                               overall)
                     .weight;
      // The OCLLs are known algs: they come up, but not often.
      pool.push_back(c);
      weights.push_back(c.ocll() ? w * 0.3 : w);
    }
  }
  return pool[pickWeighted(weights, rng_)];
}

void TslePage::newRound(bool sameState) {
  if (sameState && round_.id != 0) {
    CubieCube cube = round_.startCube;
    startRound(round_.start, round_.slot, &cube, true);
    return;
  }
  switch (drill_) {
  case Drill::Intro:
    if (!introCase_) {
      introBegin();
    } else {
      introSetUp();
    }
    break;
  case Drill::Next: {
    TsleState c = pickCase(false);
    startRound(tsleTurned(c, static_cast<int>(rng_() % 4)), randomSlot());
    break;
  }
  case Drill::Full: {
    TsleState c = pickCase(true);
    startRound(tsleTurned(c, static_cast<int>(rng_() % 4)), randomSlot());
    break;
  }
  case Drill::Explore: {
    if (explored_.done()) {
      explored_ = tsleCases().front();
    }
    int chosen = slotBox_->currentData().toInt();
    startRound(tsleTurned(explored_, exploreAuf_),
               chosen >= 0 ? static_cast<Slot>(chosen) : randomSlot());
    break;
  }
  }
}

void TslePage::startRound(TsleState state, Slot slot, const CubieCube *cube,
                          bool retry) {
  round_ = {};
  round_.id = ++rounds_;
  round_.slot = slot;
  round_.start = state;
  round_.stepState = state;
  round_.startCube = cube ? *cube : randomTsleCube(state, slot, rng_);
  round_.hold = settings_.neutrality.pick(rng_);
  round_.retry = retry;
  anchor_ = state;
  phase_ = drill_ == Drill::Explore ? Phase::Explore : Phase::Solving;
  cubeView_->setColourMap(colourMapOf(round_.hold));
  cubeView_->clearMarks();
  cube_ = round_.startCube.toFacelets();
  cubeView_->setState(cube_);
  round_.began = monotonicMs();
  bool timed = drill_ == Drill::Next || drill_ == Drill::Full;
  display_->setVisible(timed);
  if (timed) {
    display_->setText(clockText(0));
    clock_.start();
  } else {
    clock_.stop();
  }
  if (drill_ == Drill::Explore) {
    selectInTree(tsleCase(state));
  } else if (drill_ == Drill::Intro && introCase_) {
    selectInTree(*introCase_);
  }
  showPrompt();
  showKeys();
  showResult();
}

void TslePage::turn(Move m) {
  cube_.apply(m);
  cubeView_->animateMove(m);
  if (phase_ == Phase::Done || phase_ == Phase::Idle) {
    return;
  }
  if (phase_ == Phase::Solving) {
    round_.moves.push_back(m);
    round_.moveMs.push_back(monotonicMs() - round_.began);
  }
  std::optional<TsleState> state = currentState();
  if (!state) {
    return; // in the middle of a trigger
  }
  if (phase_ == Phase::Explore) {
    // The table follows the cube.
    if (*state != anchor_) {
      anchor_ = *state;
      showPrompt();
      showResult();
    }
    return;
  }
  Alg since(round_.moves.begin() +
                static_cast<std::ptrdiff_t>(round_.stepBegin),
            round_.moves.end());
  if (!turnsTheSlot(since)) {
    return; // only U turns so far
  }
  if (round_.stepState.ocll() && !state->done()) {
    return; // an OCLL alg passes through states on its way
  }
  stepDone(*state);
}

TslePage::Step TslePage::judged(TsleState before, TsleState after,
                                const Alg &moves) const {
  const TsleSolver &s = solver(round_.slot);
  Step step;
  step.before = before;
  step.after = after;
  step.moves = htmCount(simplify(moves));
  if (before.ocll()) {
    step.good = after.done();
    step.extra = std::max(0, step.moves - s.movesLeft(before));
    return step;
  }
  step.good = tsleStepsLeft(after) < tsleStepsLeft(before);
  // The way on from here, as the trigger that leaves this state has it
  // (it knows what cancels into an OCLL alg).
  int total = step.moves + s.movesLeft(after);
  for (const TsleOption &o : s.options(before)) {
    if (o.next == after && step.moves >= o.moves) {
      total = std::min(total, o.total + step.moves - o.moves);
    }
  }
  step.extra = std::max(0, total - s.movesLeft(before));
  return step;
}

void TslePage::stepDone(TsleState after) {
  Alg since(round_.moves.begin() +
                static_cast<std::ptrdiff_t>(round_.stepBegin),
            round_.moves.end());
  Step step = judged(round_.stepState, after, since);
  step.begin = round_.stepBegin;
  step.end = round_.moves.size();
  step.hint = round_.stepHint;
  round_.steps.push_back(step);
  switch (drill_) {
  case Drill::Intro:
    introStepDone(step);
    break;
  case Drill::Next:
    finish(false);
    break;
  case Drill::Full:
    if (after.done()) {
      finish(false);
    } else {
      round_.stepBegin = round_.moves.size();
      round_.stepState = after;
      round_.stepHint = false;
      showKeys();
    }
    break;
  case Drill::Explore:
    break;
  }
}

void TslePage::finish(bool gaveUp) {
  if (phase_ != Phase::Solving || drill_ == Drill::Intro) {
    return;
  }
  round_.gaveUp = gaveUp;
  round_.ms = monotonicMs() - round_.began;
  clock_.stop();
  phase_ = Phase::Done;
  display_->setText(gaveUp ? QString("<span style='color:%1'>%2</span>")
                                 .arg(kFailColour.name(), tr("Gave up"))
                           : clockText(round_.ms));
  record();
  showPrompt();
  showKeys();
  showResult();
}

void TslePage::resetCube() {
  cubeView_->finishAnimations();
  cube_ = round_.startCube.toFacelets();
  cubeView_->setState(cube_);
  anchor_ = round_.start;
  if (phase_ == Phase::Solving) {
    round_.moves.clear();
    round_.moveMs.clear();
    round_.steps.clear();
    round_.stepBegin = 0;
    round_.stepState = round_.start;
  }
}

void TslePage::showHint() {
  if (phase_ != Phase::Solving || drill_ == Drill::Intro) {
    return;
  }
  round_.stepHint = true;
  showKeys();
}

std::vector<TsleOption> TslePage::listedOptions() const {
  return solver(round_.slot).options(listedState());
}

TsleState TslePage::listedState() const {
  return phase_ == Phase::Explore ? anchor_ : round_.start;
}

void TslePage::playOption(std::size_t index) {
  if (phase_ != Phase::Explore && phase_ != Phase::Done) {
    return;
  }
  TsleState from = listedState();
  Alg alg;
  if (from.ocll()) {
    if (auto f = solver(round_.slot).finish(from); f && index == 0) {
      alg = f->alg;
    }
  } else {
    std::vector<TsleOption> options = listedOptions();
    if (index < options.size()) {
      alg = options[index].alg;
    }
  }
  if (alg.empty()) {
    return;
  }
  cubeView_->finishAnimations();
  if (phase_ == Phase::Done || currentState() != from) {
    // From where the list is for.
    if (phase_ == Phase::Done) {
      cube_ = round_.startCube.toFacelets();
      cubeView_->setState(cube_);
    } else {
      return;
    }
  }
  cubeView_->animateSequence(alg);
  cube_.apply(alg);
  if (phase_ == Phase::Explore) {
    if (auto now = currentState()) {
      anchor_ = *now;
      showPrompt();
      showResult();
    }
  }
}

QString TslePage::clockText(std::int64_t ms) const {
  return QString::number(static_cast<double>(ms) / 1000.0, 'f',
                         settings_.decimals);
}

void TslePage::showClock() {
  if (phase_ == Phase::Solving) {
    display_->setText(clockText(monotonicMs() - round_.began));
  }
}

// --- Guided intro ------------------------------------------------------------

void TslePage::introBegin() {
  introCase_ = tsleNextNew(store_.progress);
  introStage_ = 0;
  introNote_.clear();
  introGood_.clear();
  if (!introCase_) {
    round_ = {};
    round_.id = ++rounds_;
    phase_ = Phase::Idle;
    clock_.stop();
    display_->setVisible(false);
    cube_ = FaceletCube();
    cubeView_->setColourMap(colourMapOf(settings_.neutrality.main));
    cubeView_->setState(cube_);
    showPrompt();
    showKeys();
    showResult();
    return;
  }
  for (const TsleOption &o : solver(Slot::FR).options(*introCase_)) {
    if (o.good) {
      introGood_.push_back(o);
    }
  }
  introSetUp();
}

void TslePage::introSetUp() {
  if (!introCase_) {
    introBegin();
    return;
  }
  // A trigger to show: turned so that it needs no AUF. A pick: any AUF.
  int auf = introStage_ < introTriggers()
                ? introGood_[static_cast<std::size_t>(introStage_)].auf
                : static_cast<int>(rng_() % 4);
  startRound(tsleTurned(*introCase_, auf), randomSlot());
}

Alg TslePage::introAlg() const {
  if (!introCase_ || introStage_ >= introTriggers() ||
      phase_ != Phase::Solving) {
    return {};
  }
  int turn = introGood_[static_cast<std::size_t>(introStage_)].turn;
  for (const TsleOption &o : solver(round_.slot).options(round_.start)) {
    if (o.auf == 0 && o.turn == turn) {
      return o.alg;
    }
  }
  return {};
}

void TslePage::introStepDone(const Step &step) {
  if (introStage_ < introTriggers()) {
    int turn = introGood_[static_cast<std::size_t>(introStage_)].turn;
    if (step.after == tsleAfter(round_.start, 0, turn)) {
      ++introStage_;
      introNote_.clear();
      introSetUp();
    } else {
      introNote_ = tr("That left %1: not this trigger. Again.")
                       .arg(caseText(step.after));
      CubieCube cube = round_.startCube;
      startRound(round_.start, round_.slot, &cube);
    }
    return;
  }
  // A pick is a try of the case like any other.
  PairAttempt tried;
  tried.timestamp = unixNow();
  tried.ms = monotonicMs() - round_.began;
  tried.moves = step.moves;
  tried.bestMoves = solver(round_.slot).options(round_.start).front().moves;
  tried.excess = step.good ? step.extra : kWrongTrigger;
  store_.progress.add(tsleKey(*introCase_), tried);
  save();
  refreshTree();
  if (step.good && step.extra == 0) {
    ++introStage_;
    introNote_.clear();
    if (introStage_ >= introTriggers() + kIntroPicks) {
      store_.progress.introduced.insert(tsleKey(*introCase_));
      save();
      refreshTree();
      phase_ = Phase::Done;
      showPrompt();
      showKeys();
      showResult();
      return;
    }
  } else {
    const TsleOption best = solver(round_.slot).options(round_.start).front();
    introNote_ = step.good ? tr("Good, but from that AUF %1 was %2 shorter. "
                                "Again, from another AUF.")
                                 .arg(qs(toString(best.alg)))
                                 .arg(step.extra)
                           : tr("Not a good trigger there: the best was %1. "
                                "Again, from another AUF.")
                                 .arg(qs(toString(best.alg)));
  }
  introSetUp();
}

void TslePage::introSkip() {
  if (introCase_) {
    store_.progress.introduced.insert(tsleKey(*introCase_));
    save();
    refreshTree();
  }
  introCase_.reset();
  newRound();
}

// --- prompt and keys ---------------------------------------------------------

QString TslePage::promptHtml() const { return prompt_->text(); }

QString TslePage::caseText(TsleState state) const {
  if (state.done()) {
    return tr("TSLE done");
  }
  QString name = qs(tsleName(tsleCase(state))).toHtmlEscaped();
  if (state.ocll()) {
    return tr("%1 (an OCLL)").arg(name);
  }
  int steps = tsleStepsLeft(state);
  return steps == 1 ? tr("%1 (1 step left)").arg(name)
                    : tr("%1 (%2 steps left)").arg(name).arg(steps);
}

void TslePage::showPrompt() {
  auto small = [](const QString &t) {
    return t.isEmpty() ? QString()
                       : "<br><span style='font-size:small'>" + t + "</span>";
  };
  const QString slot = tr("last slot: %1").arg(slotText(round_.slot));
  switch (drill_) {
  case Drill::Intro: {
    if (phase_ == Phase::Idle) {
      bool all = std::all_of(
          tsleCases().begin(), tsleCases().end(),
          [this](TsleState c) { return store_.progress.isIntroduced(c); });
      int open = tsleOpenLevel(store_.progress);
      prompt_->setText(
          all ? tr("Every case has been introduced")
              : tr("Nothing new for now") +
                    small(tr("The cases with %1 steps left open once those "
                             "with %2 are solid: three quarters of them "
                             "tried and not weak (weak ones are marked in "
                             "the list). Practise them in Next trigger.")
                              .arg(open + 1)
                              .arg(open)));
      break;
    }
    QString note = introNote_.isEmpty()
                       ? QString()
                       : QString("<br><span style='font-size:small; color:%1'>"
                                 "%2</span>")
                             .arg(kFailColour.name(), introNote_);
    if (phase_ == Phase::Done) {
      prompt_->setText(tr("Introduced: %1").arg(caseText(*introCase_)));
    } else if (introStage_ < introTriggers()) {
      prompt_->setText(tr("New case: %1").arg(caseText(*introCase_)) +
                       small(tr("Good trigger %1 of %2, %3: do <b>%4</b>")
                                 .arg(introStage_ + 1)
                                 .arg(introTriggers())
                                 .arg(slot, qs(toString(introAlg())))) +
                       note);
    } else {
      prompt_->setText(tr("Now choose: the best trigger from this AUF") +
                       small(tr("pick %1 of %2; %3")
                                 .arg(introStage_ - introTriggers() + 1)
                                 .arg(kIntroPicks)
                                 .arg(slot)) +
                       note);
    }
    break;
  }
  case Drill::Next: {
    bool onlyOclls = !allCases_ && store_.progress.introduced.empty();
    prompt_->setText(
        tr("Do the next step") +
        small(onlyOclls ? slot + "; " +
                              tr("only the OCLLs so far: meet the cases in "
                                 "Guided intro, or set Cases to All")
                        : slot));
    break;
  }
  case Drill::Full:
    prompt_->setText(tr("Solve TSLE") + small(slot));
    break;
  case Drill::Explore:
    prompt_->setText(caseText(anchor_) + small(slot));
    break;
  }
}

void TslePage::showKeys() {
  QList<KeyHint> keys;
  QString message;
  const TsleSolver &s = solver(round_.slot);
  switch (phase_) {
  case Phase::Idle:
    break;
  case Phase::Explore:
    keys = {{"Enter", tr("do the best trigger")},
            {"1-9", tr("do that trigger")},
            {QStringLiteral("↑ ↓"), tr("case")},
            {"Space", tr("the next AUF")},
            {"Esc", tr("back to the case")}};
    break;
  case Phase::Done:
    keys << KeyHint{"Space",
                    drill_ == Drill::Intro ? tr("the next case") : tr("next")};
    if (drill_ != Drill::Intro) {
      keys << KeyHint{"Enter", tr("play the best trigger")}
           << KeyHint{"1-9", tr("play that trigger")}
           << KeyHint{"Esc", tr("try it again (not recorded)")};
    }
    break;
  case Phase::Solving:
    if (drill_ == Drill::Intro) {
      if (introStage_ < introTriggers()) {
        keys << KeyHint{"Enter", tr("show it")};
      }
      keys << KeyHint{"Esc", tr("start again")}
           << KeyHint{"Space", tr("skip: I know this case")};
      break;
    }
    if (round_.stepHint) {
      if (round_.stepState.ocll()) {
        if (auto f = s.finish(round_.stepState)) {
          message = tr("Hint: %1, %2").arg(qs(f->name), qs(toString(f->alg)));
        }
      } else {
        message =
            tr("Hint: %1")
                .arg(qs(toString(s.options(round_.stepState).front().alg)));
      }
    }
    keys << KeyHint{"Tab", tr("show the best step")}
         << KeyHint{"Backspace", tr("give up")}
         << KeyHint{"Esc", tr("start again")};
    break;
  }
  hint_->setHint(message, keys);
}

// --- keys --------------------------------------------------------------------

bool TslePage::handleKey(QKeyEvent *e, bool press) {
  if (e->modifiers() &
      (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
    return false;
  }
  if (!press || round_.id == 0) {
    return false;
  }
  switch (e->key()) {
  case Qt::Key_Space:
    if (phase_ == Phase::Done) {
      if (drill_ == Drill::Intro) {
        introCase_.reset();
      }
      newRound();
    } else if (phase_ == Phase::Explore) {
      exploreAuf_ = (exploreAuf_ + 1) % 4;
      newRound();
    } else if (phase_ == Phase::Solving && drill_ == Drill::Intro) {
      introSkip();
    } else if (phase_ == Phase::Idle) {
      newRound();
    }
    return true;
  case Qt::Key_Tab:
    showHint();
    return true;
  case Qt::Key_Backspace:
    finish(true);
    return true;
  case Qt::Key_Escape:
    if (phase_ == Phase::Done && drill_ != Drill::Intro) {
      newRound(true);
    } else if (phase_ == Phase::Explore) {
      newRound();
    } else if (phase_ == Phase::Solving) {
      resetCube();
    }
    return true;
  case Qt::Key_Return:
  case Qt::Key_Enter:
    if (phase_ == Phase::Solving && drill_ == Drill::Intro) {
      // Shows the trigger, then puts the cube back to do it.
      Alg alg = introAlg();
      if (!alg.empty()) {
        resetCube();
        cubeView_->animateSequence(alg);
        const int id = round_.id;
        QTimer::singleShot(
            static_cast<int>(alg.size()) * settings_.animationMs * 2 + 900,
            this, [this, id] {
              if (id == round_.id && round_.moves.empty()) {
                cubeView_->setState(cube_);
              }
            });
      }
    } else {
      playOption(0);
    }
    return true;
  case Qt::Key_Up:
  case Qt::Key_Down:
    if (phase_ == Phase::Explore) {
      const std::vector<TsleState> &cases = tsleCases();
      auto at = std::find(cases.begin(), cases.end(), explored_);
      if (at != cases.end()) {
        std::ptrdiff_t i =
            at - cases.begin() + (e->key() == Qt::Key_Down ? 1 : -1);
        if (i >= 0 && i < static_cast<std::ptrdiff_t>(cases.size())) {
          exploreCase(cases[static_cast<std::size_t>(i)]);
        }
      }
    }
    return true;
  default:
    break;
  }
  if ((phase_ == Phase::Done || phase_ == Phase::Explore) &&
      e->key() >= Qt::Key_1 && e->key() <= Qt::Key_9) {
    playOption(static_cast<std::size_t>(e->key() - Qt::Key_1));
    return true;
  }
  if (phase_ != Phase::Idle) {
    if (auto m = settings_.keymap.lookup(e)) {
      turn(*m);
      return true;
    }
  }
  return false;
}

} // namespace vcube
