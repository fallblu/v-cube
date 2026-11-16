#include "app/f2l_page.hpp"

#include "app/session_panel.hpp"
#include "app/ui_state.hpp"
#include "app/ui_text.hpp"
#include "train/f2l_stats.hpp"

#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
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

// The stickers of a slot's corner and edge, wherever they are on a cube
// held any way.
std::vector<int> pairFacelets(const FaceletCube &cube, Slot slot) {
  std::vector<int> out;
  auto colours = [](auto facelets) {
    std::vector<int> c;
    for (std::uint8_t f : facelets) {
      c.push_back(f / 9);
    }
    std::sort(c.begin(), c.end());
    return c;
  };
  auto shown = [&cube](auto facelets) {
    std::vector<int> c;
    for (std::uint8_t f : facelets) {
      c.push_back(cube[f]);
    }
    std::sort(c.begin(), c.end());
    return c;
  };
  std::vector<int> corner = colours(kCornerFacelets[slotCorner(slot)]);
  for (const auto &facelets : kCornerFacelets) {
    if (shown(facelets) == corner) {
      out.insert(out.end(), facelets.begin(), facelets.end());
    }
  }
  std::vector<int> edge = colours(kEdgeFacelets[slotEdge(slot)]);
  for (const auto &facelets : kEdgeFacelets) {
    if (shown(facelets) == edge) {
      out.insert(out.end(), facelets.begin(), facelets.end());
    }
  }
  return out;
}

} // namespace

F2LPage::F2LPage(AppSettings &settings, AlgLibrary &library, AlgLabStore &lab,
                 const QString &dataDir, QWidget *parent)
    : ModePage(parent), settings_(settings), library_(library), lab_(lab),
      store_(QDir(dataDir).filePath("f2l.json")), rng_(makeRng()) {
  QString error;
  if (!store_.load(&error)) {
    QString kept =
        store_.setAsidePath().isEmpty()
            ? tr("It is left where it is, and nothing will be saved "
                 "over it.")
            : tr("It was kept as %1.")
                  .arg(QDir::toNativeSeparators(store_.setAsidePath()));
    QMessageBox::warning(this, "v-cube",
                         tr("Could not read your F2L statistics (%1), so F2L "
                            "starts afresh.")
                                 .arg(error) +
                             " " + kept);
  }

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
  result_->setObjectName("f2lResult");
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
  split->setSizes({400, 800});

  tabs_ = new QTabBar(this);
  tabs_->setFocusPolicy(Qt::NoFocus);
  tabs_->setExpanding(false);
  for (const QString &name : {tr("Pair solutions"), tr("Pair choice"),
                              tr("Full F2L"), tr("Case explorer")}) {
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
    if (link.startsWith("sol:")) {
      selectSolution(static_cast<std::size_t>(link.mid(4).toInt()), true);
    }
  });
  clock_.setInterval(37);
  connect(&clock_, &QTimer::timeout, this, &F2LPage::showClock);

  loadChoices();
  applySettings();
  fillTree();
  showOptions();
}

F2LPage::~F2LPage() {
  if (stop_) {
    stop_->store(true);
  }
}

// --- building and options ----------------------------------------------------

void F2LPage::buildSide() {
  side_ = new QWidget(this);
  auto *layout = new QVBoxLayout(side_);
  layout->setContentsMargins(0, 0, 0, 0);
  summary_ = new QLabel(side_);
  summary_->setWordWrap(true);
  summary_->setTextFormat(Qt::RichText);
  tree_ = new QTreeWidget(side_);
  tree_->setObjectName("f2lCases");
  tree_->setColumnCount(4);
  tree_->setHeaderLabels(
      {tr("Case"), tr("Tries"), tr("Over best"), tr("Time")});
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
  layout->addWidget(
      helpLabel(tr("How each pair case went lately, in every drill: your "
                   "solution's cost over the best one's, and your time. "
                   "Weak cases are marked and come up more often in Pair "
                   "solutions. Double-click a case to explore it."),
                side_));
  layout->addWidget(clearButton_, 0, Qt::AlignLeft);
  connect(tree_, &QTreeWidget::currentItemChanged, this,
          [this] { treeSelected(); });
  connect(tree_, &QTreeWidget::itemDoubleClicked, this,
          [this](QTreeWidgetItem *item) {
            if (auto c = caseFromKey(
                    item->data(0, Qt::UserRole).toString().toStdString())) {
              exploreCase(*c);
            }
          });
  connect(clearButton_, &QPushButton::clicked, this, &F2LPage::clearStats);
}

void F2LPage::buildOptions() {
  methodBox_ = new QComboBox(options_);
  methodBox_->setFocusPolicy(Qt::NoFocus);
  for (int m = 0; m < kF2LMethods; ++m) {
    auto method = static_cast<F2LMethod>(m);
    methodBox_->addItem(qs(methodName(method)), qs(methodId(method)));
  }
  options_->addPicker(tr("F2L:"), methodBox_);

  leaveButton_ = new QToolButton(options_);
  leaveButton_->setFocusPolicy(Qt::NoFocus);
  leaveButton_->setPopupMode(QToolButton::InstantPopup);
  auto *leaveMenu = new QMenu(leaveButton_);
  for (Slot s : kF2LSlots) {
    QAction *a = leaveMenu->addAction(slotText(s));
    a->setCheckable(true);
    a->setData(static_cast<int>(s));
    connect(a, &QAction::toggled, this, [this, s, a](bool on) {
      config_.lsSlots[static_cast<std::size_t>(s)] = on;
      if (std::none_of(config_.lsSlots.begin(), config_.lsSlots.end(),
                       [](bool b) { return b; })) {
        // One slot at least can be left.
        config_.lsSlots[static_cast<std::size_t>(s)] = true;
        QSignalBlocker block(a);
        a->setChecked(true);
        return;
      }
      configChanged();
    });
  }
  leaveButton_->setMenu(leaveMenu);
  leaveItem_ = options_->addPicker(tr("Last slot:"), leaveButton_);

  movesButton_ = new QToolButton(options_);
  movesButton_->setFocusPolicy(Qt::NoFocus);
  movesButton_->setPopupMode(QToolButton::InstantPopup);
  auto *movesMenu = new QMenu(movesButton_);
  auto addMove = [&](const QString &text, const char *name,
                     bool F2LConfig::*f) {
    QAction *a = movesMenu->addAction(text);
    a->setCheckable(true);
    a->setObjectName(name);
    connect(a, &QAction::toggled, this, [this, f](bool on) {
      config_.*f = on;
      configChanged();
    });
  };
  addMove(tr("D turns"), "d", &F2LConfig::dMoves);
  addMove(tr("F2 and B2"), "halfFB", &F2LConfig::halfFB);
  addMove(tr("Wide turns (r, l, u, d)"), "wide", &F2LConfig::wide);
  movesButton_->setMenu(movesMenu);
  options_->addPicker(tr("Solutions use:"), movesButton_);

  stageBox_ = new QComboBox(options_);
  stageBox_->setFocusPolicy(Qt::NoFocus);
  stageItem_ = options_->addPicker(tr("Pair:"), stageBox_);

  slotBox_ = new QComboBox(options_);
  slotBox_->setFocusPolicy(Qt::NoFocus);
  slotBox_->addItem(tr("Any slot"), -1);
  for (Slot s : kF2LSlots) {
    slotBox_->addItem(slotText(s), static_cast<int>(s));
  }
  slotItem_ = options_->addPicker(tr("Slot:"), slotBox_);
  contextBox_ = new QComboBox(options_);
  contextBox_->setFocusPolicy(Qt::NoFocus);
  contextBox_->addItem(tr("Solved"), 1);
  contextBox_->addItem(tr("Open"), 0);
  contextItem_ = options_->addPicker(tr("Other slots:"), contextBox_);

  options_->addViewPicker(cubeView_, "f2l");
  options_->addStretch();

  connect(methodBox_, &QComboBox::activated, this, [this] {
    if (auto m =
            methodFromId(methodBox_->currentData().toString().toStdString())) {
      applyMethod(config_, *m);
      configChanged();
    }
  });
  for (QComboBox *box : {stageBox_, slotBox_, contextBox_}) {
    connect(box, &QComboBox::activated, this, [this] {
      ready_ = false;
      saveChoices();
      if (active_) {
        newRound();
      }
    });
  }
}

void F2LPage::loadChoices() {
  if (auto m = methodFromId(
          saved::f2l("method", "zz-1").get().toString().toStdString())) {
    applyMethod(config_, *m);
  }
  QStringList leave =
      saved::f2l("leave", QStringList{"FR", "FL"}).get().toStringList();
  bool any = false;
  for (Slot s : kF2LSlots) {
    bool on = leave.contains(slotText(s));
    config_.lsSlots[static_cast<std::size_t>(s)] = on;
    any = any || on;
  }
  if (!any) {
    config_.lsSlots = F2LConfig().lsSlots;
  }
  config_.dMoves = saved::f2l("dMoves", true).get().toBool();
  config_.halfFB = saved::f2l("halfFB", true).get().toBool();
  config_.wide = saved::f2l("wide", true).get().toBool();
  drill_ = static_cast<Drill>(
      std::clamp(saved::f2l("drill", 0).get().toInt(), 0, 3));
  fillStages();
  stageBox_->setCurrentIndex(
      std::max(0, stageBox_->findData(saved::f2l("stage", 0).get().toInt())));
  slotBox_->setCurrentIndex(
      std::max(0, slotBox_->findData(saved::f2l("slot", -1).get().toInt())));
  contextBox_->setCurrentIndex(std::max(
      0, contextBox_->findData(saved::f2l("context", 1).get().toInt())));
  QSignalBlocker block(tabs_);
  tabs_->setCurrentIndex(static_cast<int>(drill_));
}

void F2LPage::saveChoices() const {
  saved::f2l("method").set(qs(methodId(methodOf(config_))));
  QStringList leave;
  for (Slot s : kF2LSlots) {
    if (config_.lsSlots[static_cast<std::size_t>(s)]) {
      leave << slotText(s);
    }
  }
  saved::f2l("leave").set(leave);
  saved::f2l("dMoves").set(config_.dMoves);
  saved::f2l("halfFB").set(config_.halfFB);
  saved::f2l("wide").set(config_.wide);
  saved::f2l("drill").set(static_cast<int>(drill_));
  saved::f2l("stage").set(stageBox_->currentData());
  saved::f2l("slot").set(slotBox_->currentData());
  saved::f2l("context").set(contextBox_->currentData());
}

void F2LPage::fillStages() {
  QVariant was = stageBox_->currentData();
  QSignalBlocker block(stageBox_);
  stageBox_->clear();
  stageBox_->addItem(tr("Any"), 0);
  const QStringList names = {tr("1st"), tr("2nd"), tr("3rd"), tr("4th")};
  int stages = stageCount(config_);
  for (int s = 1; s <= stages; ++s) {
    stageBox_->addItem(
        s == stages ? tr("%1 (last)").arg(names[s - 1]) : names[s - 1], s);
  }
  stageBox_->setCurrentIndex(std::max(0, stageBox_->findData(was)));
}

void F2LPage::showOptions() {
  {
    QSignalBlocker block(methodBox_);
    methodBox_->setCurrentIndex(
        methodBox_->findData(qs(methodId(methodOf(config_)))));
  }
  QStringList leave;
  for (QAction *a : leaveButton_->menu()->actions()) {
    QSignalBlocker block(a);
    bool on = config_.lsSlots[static_cast<std::size_t>(a->data().toInt())];
    a->setChecked(on);
    if (on) {
      leave << a->text();
    }
  }
  leaveButton_->setText(leave.join(" / "));
  leaveItem_->setVisible(config_.lastSlot);
  for (QAction *a : movesButton_->menu()->actions()) {
    QSignalBlocker block(a);
    if (a->objectName() == "d") {
      a->setChecked(config_.dMoves);
    } else if (a->objectName() == "halfFB") {
      // Without EO every F and B turn is used.
      a->setChecked(config_.eo ? config_.halfFB : true);
      a->setEnabled(config_.eo);
    } else {
      a->setChecked(!config_.eo && config_.wide);
      a->setEnabled(!config_.eo);
    }
  }
  PairMoves moves = config_.moves();
  QString text = "R U L";
  text += moves.d ? " D" : "";
  text += moves.quarterFB ? " F B" : moves.halfFB ? " F2 B2" : "";
  text += moves.wide ? " r l u d" : "";
  movesButton_->setText(text);
  stageItem_->setVisible(drill_ == Drill::Pair);
  slotItem_->setVisible(drill_ == Drill::Explore);
  contextItem_->setVisible(drill_ == Drill::Explore);
}

void F2LPage::configChanged() {
  ready_ = false;
  saveChoices();
  fillStages();
  showOptions();
  fillTree();
  if (active_) {
    newRound();
  }
}

void F2LPage::setConfig(const F2LConfig &config) {
  config_ = config;
  configChanged();
}

void F2LPage::selectDrill(Drill d) {
  drill_ = d;
  ready_ = false;
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

void F2LPage::exploreCase(PairCase c) {
  explored_ = c;
  if (drill_ != Drill::Explore) {
    selectDrill(Drill::Explore);
  } else if (active_) {
    newRound();
  }
  selectInTree(c);
}

void F2LPage::applySettings() {
  CubeLook look = cubeView_->look();
  look.scheme = settings_.scheme;
  cubeView_->setLook(look);
  cubeView_->setAnimationMs(settings_.animationMs);
  fillTree();
  if (active_ && round_.id != 0) {
    showPrompt();
  }
}

void F2LPage::reloadSets() {
  reference_.reset();
  const AlgSet *set = library_.find(saved::reviewPairSet().get().toStdString());
  if (set && isPairSet(*set)) {
    reference_ = std::make_shared<const AlgSet>(*set);
  }
}

void F2LPage::activated() {
  active_ = true;
  ready_ = false; // a state left half done isn't taken up again
  reloadSets();
  if (round_.id == 0 || phase_ != Phase::Done) {
    newRound();
  }
}

void F2LPage::deactivated() {
  active_ = false;
  clock_.stop();
  cubeView_->finishAnimations();
  if (phase_ == Phase::Solving) {
    // The clock was running: the state is given up, unrecorded, and the
    // ready page is there on the way back.
    showReady();
  }
}

void F2LPage::focusModeChanged() {
  options_->setVisible(!focusMode());
  tabs_->setVisible(!focusMode());
}

// --- rounds ------------------------------------------------------------------

std::optional<Slot> F2LPage::target() const { return round_.target; }

void F2LPage::showReady() {
  if (stop_) {
    stop_->store(true);
  }
  round_ = {};
  round_.id = ++rounds_;
  phase_ = Phase::Ready;
  clock_.stop();
  display_->setVisible(false);
  cube_ = FaceletCube();
  cubeView_->setColourMap(colourMapOf(settings_.neutrality.main));
  cubeView_->clearMarks();
  cubeView_->setState(cube_);
  showPrompt();
  showKeys();
  showResult();
}

void F2LPage::newRound(bool sameState) {
  if (!sameState && drill_ != Drill::Explore && !ready_) {
    showReady();
    return;
  }
  if (sameState && round_.id != 0) {
    Round again;
    again.start = round_.start;
    again.target = round_.target;
    again.pcase = round_.pcase;
    again.hold = round_.hold;
    again.retry = true;
    if (round_.analysed && drill_ != Drill::Full) {
      again.analysis = round_.analysis;
      again.analysed = true;
    }
    round_ = std::move(again);
    round_.id = ++rounds_;
    round_.analysis.round = round_.id;
    startRound(round_.start, round_.target);
    return;
  }
  round_ = {};
  round_.id = ++rounds_;
  round_.hold = settings_.neutrality.pick(rng_);
  switch (drill_) {
  case Drill::Pair: {
    // Weak cases, and cases not tried yet, come up more often.
    const std::vector<PairCase> &cases = pairCases(config_.eo);
    double overall = meanPairMs(store_.data);
    std::vector<double> weights;
    for (PairCase c : cases) {
      auto it = store_.data.cases.find(caseKey(c));
      weights.push_back(summarizeCase(it == store_.data.cases.end()
                                          ? std::vector<PairAttempt>{}
                                          : it->second,
                                      overall)
                            .weight);
    }
    int stage = stageBox_->currentData().toInt();
    auto setup = randomPairSetup(config_, stage,
                                 cases[pickWeighted(weights, rng_)], rng_);
    if (!setup) {
      setup = randomPairSetup(config_, stage, std::nullopt, rng_);
    }
    startRound(setup->state, setup->target);
    break;
  }
  case Drill::Choice:
    startRound(randomChoiceState(config_, rng_), std::nullopt);
    break;
  case Drill::Full:
    startRound(randomFullState(config_, rng_), std::nullopt);
    break;
  case Drill::Explore: {
    const std::vector<PairCase> &cases = pairCases(config_.eo);
    if (std::find(cases.begin(), cases.end(), explored_) == cases.end()) {
      explored_ = cases.front();
    }
    int chosen = slotBox_->currentData().toInt();
    Slot slot = chosen >= 0
                    ? static_cast<Slot>(chosen)
                    : kF2LSlots[std::uniform_int_distribution<std::size_t>(
                          0, kSlots - 1)(rng_)];
    auto setup = casePairSetup(config_, explored_, slot,
                               contextBox_->currentData().toInt() == 1, rng_);
    if (!setup) {
      emit message(
          tr("That case can't be set up in the %1 slot").arg(slotText(slot)));
      setup = randomPairSetup(config_, 0, explored_, rng_);
    }
    if (setup) {
      startRound(setup->state, setup->target);
    }
    break;
  }
  }
}

void F2LPage::startRound(const CubieCube &state, std::optional<Slot> target) {
  if (stop_ && !round_.analysed) {
    stop_->store(true);
  }
  round_.start = state;
  round_.target = target;
  if (target) {
    round_.pcase = pairCaseOf(state, *target);
  }
  phase_ = drill_ == Drill::Explore ? Phase::Explore : Phase::Solving;
  shown_ = 0;
  stepped_ = 0;
  stepping_ = true;
  cubeView_->setColourMap(colourMapOf(round_.hold));
  cube_ = state.toFacelets();
  showCube();
  round_.began = monotonicMs();
  display_->setVisible(phase_ == Phase::Solving);
  if (phase_ == Phase::Solving) {
    display_->setText(clockText(0));
    clock_.start();
  } else {
    clock_.stop();
    selectInTree(round_.pcase);
  }
  showPrompt();
  showKeys();
  if (drill_ != Drill::Full && !round_.analysed) {
    requestAnalysis();
  }
  showResult();
}

void F2LPage::showCube() {
  cubeView_->setState(cube_);
  markPair();
}

void F2LPage::markPair() {
  if (!round_.target) {
    cubeView_->clearMarks();
    return;
  }
  StickerMarks marks;
  marks.rings = pairFacelets(cube_, *round_.target);
  cubeView_->setMarks(marks);
}

bool F2LPage::initiallySolvedKept(const CubieCube &c) const {
  return std::all_of(kF2LSlots.begin(), kF2LSlots.end(), [&](Slot s) {
    return !slotSolved(round_.start, s) || slotSolved(c, s);
  });
}

void F2LPage::turn(Move m) {
  cube_.apply(m);
  cubeView_->animateMove(m);
  markPair();
  if (phase_ != Phase::Solving) {
    stepping_ = false; // the cube is turned freely now
    return;
  }
  round_.moves.push_back(m);
  round_.moveMs.push_back(monotonicMs() - round_.began);
  checkDone();
}

void F2LPage::checkDone() {
  auto c = centredCubie(cube_);
  if (!c || !f2lBase(*c, config_)) {
    return;
  }
  bool done = false;
  switch (drill_) {
  case Drill::Pair:
    done = slotSolved(*c, *round_.target) && initiallySolvedKept(*c);
    break;
  case Drill::Choice:
    if (initiallySolvedKept(*c)) {
      for (Slot s : kF2LSlots) {
        if (!slotSolved(round_.start, s) && slotSolved(*c, s)) {
          round_.picked = s;
          done = true;
          break;
        }
      }
    }
    break;
  case Drill::Full:
    done = f2lDone(*c, config_);
    break;
  case Drill::Explore:
    break;
  }
  if (done) {
    finish(false);
  }
}

void F2LPage::finish(bool gaveUp) {
  if (phase_ != Phase::Solving) {
    return;
  }
  round_.gaveUp = gaveUp;
  round_.ms = monotonicMs() - round_.began;
  clock_.stop();
  phase_ = Phase::Done;
  if (gaveUp) {
    display_->setText(QString("<span style='color:%1'>%2</span>")
                          .arg(kFailColour.name(), tr("Gave up")));
  } else {
    display_->setText(clockText(round_.ms));
  }
  stepping_ = false;
  if (drill_ == Drill::Full) {
    round_.segments = f2lSegments(round_.start, round_.moves, config_);
    requestAnalysis();
  } else if (round_.analysed) {
    record();
  }
  showPrompt();
  showKeys();
  showResult();
}

void F2LPage::resetCube() {
  cubeView_->finishAnimations();
  cube_ = round_.start.toFacelets();
  showCube();
  stepped_ = 0;
  stepping_ = true;
  if (phase_ == Phase::Solving) {
    round_.moves.clear();
    round_.moveMs.clear();
  }
}

void F2LPage::showHintMove() {
  if (phase_ != Phase::Solving || drill_ == Drill::Full) {
    return;
  }
  if (!round_.analysed) {
    emit message(tr("Still working out the solutions"));
    return;
  }
  if (drill_ == Drill::Choice) {
    if (!round_.analysis.ranks.empty()) {
      round_.hint = true;
      showKeys();
    }
    return;
  }
  const PairAnalysis &pair = *round_.analysis.pair;
  if (!pair.best()) {
    return;
  }
  // The best solution's next move, done from the start.
  const Alg &best = pair.best()->alg;
  round_.hint = true;
  round_.hintMoves =
      std::min(round_.hintMoves + 1, static_cast<int>(best.size()));
  resetCube();
  Alg shown(best.begin(), best.begin() + round_.hintMoves);
  cubeView_->animateSequence(shown);
  for (Move m : shown) {
    cube_.apply(m);
    round_.moves.push_back(m);
    round_.moveMs.push_back(monotonicMs() - round_.began);
  }
  markPair();
  showKeys();
  checkDone();
}

void F2LPage::showPrompt() {
  if (phase_ == Phase::Ready) {
    QString what = drill_ == Drill::Pair ? tr("One pair to solve at a time")
                   : drill_ == Drill::Choice
                       ? tr("Solve the pair you would do first")
                       : tr("The whole F2L");
    prompt_->setText(
        tabs_->tabText(static_cast<int>(drill_)) +
        "<br><span style='font-size:small'>" +
        tr("%1, timed from the moment the cube is shown. Press Space when "
           "you are ready.")
            .arg(what) +
        "</span>");
    return;
  }
  QString leaving;
  if (config_.lastSlot) {
    QStringList names;
    for (Slot s : kF2LSlots) {
      if (config_.leaves(s)) {
        names << slotText(s);
      }
    }
    leaving = names.size() == 1
                  ? tr("the %1 slot is left for the last-slot step")
                        .arg(names.front())
                  : tr("one of %1 is left for the last-slot step")
                        .arg(names.join(", "));
  }
  auto small = [](const QString &t) {
    return t.isEmpty() ? QString()
                       : "<br><span style='font-size:small'>" + t + "</span>";
  };
  QString pairColours;
  if (round_.target) {
    for (std::uint8_t f : kEdgeFacelets[slotEdge(*round_.target)]) {
      pairColours +=
          QString("<span style='color:%1'>■</span>")
              .arg(cubeView_->look()
                       .stickerColour(static_cast<std::uint8_t>(f / 9))
                       .name());
    }
  }
  switch (drill_) {
  case Drill::Pair: {
    int left = pairsLeft(round_.start, config_);
    QString stage =
        left <= 1 ? tr("the last pair") : tr("%1 pairs to go").arg(left);
    prompt_->setText(
        tr("Solve the %1 pair %2")
            .arg("<b>" + slotText(*round_.target) + "</b>", pairColours) +
        small(leaving.isEmpty() ? stage : stage + "; " + leaving));
    break;
  }
  case Drill::Choice:
    prompt_->setText(
        tr("Solve the pair you would do first") +
        small(
            (pairsLeft(round_.start, config_) == 1
                 ? tr("1 pair to go")
                 : tr("%1 pairs to go").arg(pairsLeft(round_.start, config_))) +
            (leaving.isEmpty() ? QString() : "; " + leaving)));
    break;
  case Drill::Full:
    prompt_->setText(tr("Solve the F2L") + small(leaving));
    break;
  case Drill::Explore:
    prompt_->setText(
        tr("%1 — %2 slot %3")
            .arg(qs(caseName(round_.pcase)).toHtmlEscaped(),
                 round_.target ? slotText(*round_.target) : QString(),
                 pairColours));
    break;
  }
}

void F2LPage::showKeys() {
  const QString arrows = QStringLiteral("← →");
  if (phase_ == Phase::Ready) {
    hint_->setHint(tr("Ready when you are."), {{"Space", tr("start")}});
    return;
  }
  if (phase_ == Phase::Explore) {
    hint_->setHint({}, {{"Enter", tr("play the solution")},
                        {arrows, tr("step through it")},
                        {"1-9", tr("pick a solution")},
                        {QStringLiteral("↑ ↓"), tr("case")},
                        {"Space", tr("another example")},
                        {"Esc", tr("reset the cube")}});
    return;
  }
  if (phase_ == Phase::Done) {
    QList<KeyHint> keys{{"Space", tr("next")}};
    if (drill_ != Drill::Full) {
      keys << KeyHint{"Enter", tr("play the solution")}
           << KeyHint{arrows, tr("step through it")}
           << KeyHint{"1-9", tr("pick a solution")};
    }
    keys << KeyHint{"Esc", tr("try it again (not recorded)")};
    hint_->setHint({}, keys);
    return;
  }
  QString message;
  QList<KeyHint> keys;
  if (drill_ == Drill::Pair) {
    if (round_.hint && round_.analysed && round_.analysis.pair->best()) {
      const Alg &best = round_.analysis.pair->best()->alg;
      Alg shown(best.begin(), best.begin() + round_.hintMoves);
      message = tr("Hint: %1 (%2 of %3 moves)")
                    .arg(qs(toString(shown)))
                    .arg(round_.hintMoves)
                    .arg(best.size());
    }
    keys << KeyHint{"Tab", tr("show the next move")};
  } else if (drill_ == Drill::Choice) {
    if (round_.hint && !round_.analysis.ranks.empty()) {
      message = tr("The best pair to start with: %1")
                    .arg(slotText(round_.analysis.ranks.front().pair.slot));
    }
    keys << KeyHint{"Tab", tr("show the best pair")};
  }
  keys << KeyHint{"Backspace", tr("give up")}
       << KeyHint{"Esc", tr("start again")};
  hint_->setHint(message, keys);
}

QString F2LPage::clockText(std::int64_t ms) const {
  return QString::number(static_cast<double>(ms) / 1000.0, 'f',
                         settings_.decimals);
}

void F2LPage::showClock() {
  if (phase_ == Phase::Solving) {
    display_->setText(clockText(monotonicMs() - round_.began));
  }
}

// --- stepping through a solution ---------------------------------------------

void F2LPage::selectSolution(std::size_t index, bool play) {
  if (phase_ == Phase::Solving || index >= shownSolutions().size()) {
    return;
  }
  shown_ = index;
  resetCube();
  if (play) {
    playRest();
  }
  showResult();
}

void F2LPage::step(int by) {
  std::vector<RankedSolution> solutions = shownSolutions();
  if (phase_ == Phase::Solving || shown_ >= solutions.size()) {
    return;
  }
  const Alg &alg = solutions[shown_].alg;
  if (!stepping_) {
    resetCube();
  }
  if (by > 0 && stepped_ < alg.size()) {
    Move m = alg[stepped_++];
    cube_.apply(m);
    cubeView_->animateMove(m);
  } else if (by < 0 && stepped_ > 0) {
    Move m = invert({alg[--stepped_]}).front();
    cube_.apply(m);
    cubeView_->animateMove(m);
  }
  markPair();
}

void F2LPage::playRest() {
  std::vector<RankedSolution> solutions = shownSolutions();
  if (phase_ == Phase::Solving || shown_ >= solutions.size()) {
    return;
  }
  const Alg &alg = solutions[shown_].alg;
  if (!stepping_ || stepped_ >= alg.size()) {
    resetCube();
  }
  Alg rest(alg.begin() + static_cast<std::ptrdiff_t>(stepped_), alg.end());
  cubeView_->animateSequence(rest);
  cube_.apply(rest);
  stepped_ = alg.size();
  markPair();
}

// --- keys --------------------------------------------------------------------

bool F2LPage::handleKey(QKeyEvent *e, bool press) {
  if (e->modifiers() &
      (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
    return false;
  }
  if (!press || round_.id == 0) {
    return false;
  }
  if (phase_ == Phase::Ready) {
    // Only Space starts.
    if (e->key() != Qt::Key_Space) {
      return false;
    }
    ready_ = true;
    newRound();
    return true;
  }
  const bool solving = phase_ == Phase::Solving;
  switch (e->key()) {
  case Qt::Key_Space:
    if (!solving) {
      ready_ = true;
      newRound();
    }
    return true;
  case Qt::Key_Tab:
    showHintMove();
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
    playRest();
    return true;
  case Qt::Key_Left:
    step(-1);
    return true;
  case Qt::Key_Right:
    step(1);
    return true;
  case Qt::Key_Up:
  case Qt::Key_Down:
    if (phase_ == Phase::Explore) {
      const std::vector<PairCase> &cases = pairCases(config_.eo);
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
  if (!solving && e->key() >= Qt::Key_1 && e->key() <= Qt::Key_9) {
    selectSolution(static_cast<std::size_t>(e->key() - Qt::Key_1), false);
    return true;
  }
  if (auto m = settings_.keymap.lookup(e)) {
    turn(*m);
    return true;
  }
  return false;
}

} // namespace vcube
