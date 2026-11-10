#include "app/blind_page.hpp"

#include "app/flow_layout.hpp"

#include "io/data_file.hpp"

#include "cube/cubie.hpp"

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
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabBar>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace vcube {

namespace {

QPushButton *flatButton(const QString &text, QWidget *parent) {
  auto *b = new QPushButton(text, parent);
  b->setFocusPolicy(Qt::NoFocus);
  return b;
}

QComboBox *comboBox(const QStringList &items, QWidget *parent) {
  auto *box = new QComboBox(parent);
  box->setFocusPolicy(Qt::NoFocus);
  box->addItems(items);
  return box;
}

// A label and a widget kept together, so that both can be hidden.
QWidget *labelled(const QString &label, QWidget *w, QWidget *parent) {
  auto *box = new QWidget(parent);
  auto *layout = new QHBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(new QLabel(label, box));
  w->setParent(box);
  layout->addWidget(w);
  return box;
}

QString timeText(StatValue ms, int decimals = 2) {
  return QString::fromStdString(formatTime(ms, decimals));
}

// A typed or scheme letter as letters are compared (see foldLetter()).
QString lowered(const QString &s) { return qs(foldLetter(ss(s.trimmed()))); }

// A Space this soon after the quiz moved on by itself was meant for the
// letter before.
constexpr std::int64_t kSpaceGraceMs = 500;

const StatusColour kRight{Status::Good};
const StatusColour kWrong{Status::Bad};

constexpr std::array<BldPiece, 2> kKinds = {BldPiece::Edge, BldPiece::Corner};

// Every facelet holding its own index, to follow stickers through moves.
FaceletCube numbered() {
  FaceletCube::Stickers ids{};
  for (int i = 0; i < kFaceletCount; ++i) {
    ids[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
  }
  return FaceletCube(ids);
}

// Where the sticker that started at `facelet` is now.
int whereIs(const FaceletCube &ids, int facelet) {
  for (int i = 0; i < kFaceletCount; ++i) {
    if (ids[i] == facelet) {
      return i;
    }
  }
  return facelet;
}

} // namespace

BlindPage::BlindPage(AppSettings &settings, const QString &dataDir,
                     QWidget *parent)
    : ModePage(parent), settings_(settings),
      store_(QDir(dataDir).filePath("blind.json")),
      scheme_(BlindScheme::load()), noSets_(QString(), QString()),
      feed_(noSets_), rng_(makeRng()) {
  QString error;
  if (!store_.load(&error)) {
    QString kept =
        store_.setAsidePath().isEmpty()
            ? tr("It is left where it is, and nothing will be saved "
                 "over it.")
            : tr("It was kept as %1.")
                  .arg(QDir::toNativeSeparators(store_.setAsidePath()));
    QMessageBox::warning(this, "v-cube",
                         tr("Could not read your blind solves and drill "
                            "statistics (%1), so Blind starts afresh.")
                                 .arg(error) +
                             " " + kept);
  }

  // --- left column: sessions or letters, stats, list ---
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
  table_ = new QTableWidget(0, 3, left);
  table_->verticalHeader()->setVisible(false);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setFocusPolicy(Qt::NoFocus);
  table_->setContextMenuPolicy(Qt::CustomContextMenu);
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  leftLayout->addWidget(table_, 1);
  resetStatsButton_ = flatButton(tr("Reset statistics"), left);
  resetStatsButton_->setToolTip(
      tr("Forget this drill's statistics for every letter"));
  leftLayout->addWidget(resetStatsButton_);

  // --- right column: options, prompt, cube, input, result ---
  auto *right = new QWidget(this);
  auto *rightLayout = new QVBoxLayout(right);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  optionsRow_ = new QWidget(right);
  auto *options = new FlowLayout(optionsRow_);
  options->setContentsMargins(0, 0, 0, 0);
  piecesBox_ = comboBox({tr("Edges"), tr("Corners"), tr("Both")}, optionsRow_);
  piecesBox_->setToolTip(tr("The pieces whose letters are asked"));
  directionBox_ =
      comboBox({tr("Sticker → letter"), tr("Letter → sticker"), tr("Mixed")},
               optionsRow_);
  directionBox_->setToolTip(
      tr("Name the ringed sticker's letter, click the sticker of a letter "
         "on the net, or both in turn"));
  setupModeBox_ =
      comboBox({tr("Do it on the cube"), tr("Type it")}, optionsRow_);
  setupModeBox_->setToolTip(
      tr("Do each setup on the virtual cube, or type it to be checked"));
  cubeBox_ = comboBox({tr("Virtual cube"), tr("Physical cube")}, optionsRow_);
  cubeBox_->setToolTip(tr("The cube this session's solves are done on"));
  cubeView_ = new CubeView(right);
  cubeView_->setMinimumSize(260, 200);
  viewPicker_ = new QWidget(optionsRow_);
  auto *viewLayout = new QHBoxLayout(viewPicker_);
  viewLayout->setContentsMargins(0, 0, 0, 0);
  viewLayout->addWidget(new QLabel(tr("View:"), viewPicker_));
  viewBox_ = createViewSelector(cubeView_, "blind", viewPicker_);
  viewLayout->addWidget(viewBox_);
  schemeButton_ = flatButton(tr("Lettering && setups…"), optionsRow_);
  schemeButton_->setToolTip(
      tr("Change the letters, the setup moves and the hold you solve blind "
         "in"));
  options->addWidget(labelled(tr("Pieces:"), piecesBox_, optionsRow_));
  options->addWidget(labelled(tr("Ask:"), directionBox_, optionsRow_));
  options->addWidget(labelled(tr("Setups:"), setupModeBox_, optionsRow_));
  options->addWidget(cubeBox_);
  options->addWidget(viewPicker_);
  options->addStretch();
  options->addWidget(schemeButton_);
  rightLayout->addWidget(optionsRow_);

  auto *scrambleRow = new QHBoxLayout;
  scrambleLabel_ = new QLabel(right);
  scrambleLabel_->setWordWrap(true);
  scrambleLabel_->setAlignment(Qt::AlignCenter);
  scrambleLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  QFont sf = scrambleLabel_->font();
  sf.setPointSize(17);
  scrambleLabel_->setFont(sf);
  markScrambleLabel(scrambleLabel_);
  net_ = new NetPreview(right);
  scrambleRow->addWidget(scrambleLabel_, 1);
  scrambleRow->addWidget(net_);
  rightLayout->addLayout(scrambleRow);

  promptLabel_ = new QLabel(right);
  promptLabel_->setAlignment(Qt::AlignCenter);
  promptLabel_->setWordWrap(true);
  promptLabel_->setTextFormat(Qt::RichText);
  QFont pf = promptLabel_->font();
  pf.setPointSizeF(pf.pointSizeF() * 1.3);
  promptLabel_->setFont(pf);
  display_ = new TimeDisplay(right);
  display_->setTextFormat(Qt::PlainText); // letters may hold < or &
  hintLabel_ = new QLabel(right);
  hintLabel_->setAlignment(Qt::AlignCenter);
  hintLabel_->setWordWrap(true);
  hintLabel_->setForegroundRole(QPalette::PlaceholderText);

  traceRow_ = new QWidget(right);
  auto *traceLayout = new QHBoxLayout(traceRow_);
  traceLayout->setContentsMargins(0, 0, 0, 0);
  edgesEdit_ = new QLineEdit(traceRow_);
  cornersEdit_ = new QLineEdit(traceRow_);
  edgesEdit_->setPlaceholderText(tr("Edge letters, e.g. AB CD E"));
  cornersEdit_->setPlaceholderText(tr("Corner letters"));
  for (QLineEdit *e : {edgesEdit_, cornersEdit_}) {
    e->setFocusPolicy(Qt::ClickFocus);
    e->installEventFilter(this);
    QFont f = e->font();
    f.setPointSizeF(f.pointSizeF() * 1.3);
    e->setFont(f);
  }
  traceLayout->addWidget(new QLabel(tr("Edges:"), traceRow_));
  traceLayout->addWidget(edgesEdit_, 1);
  traceLayout->addSpacing(12);
  traceLayout->addWidget(new QLabel(tr("Corners:"), traceRow_));
  traceLayout->addWidget(cornersEdit_, 1);

  setupRow_ = new QWidget(right);
  auto *setupLayout = new QHBoxLayout(setupRow_);
  setupLayout->setContentsMargins(0, 0, 0, 0);
  setupEdit_ = new QLineEdit(setupRow_);
  setupEdit_->setPlaceholderText(tr("Type the setup and press Enter"));
  setupEdit_->setFocusPolicy(Qt::ClickFocus);
  setupEdit_->installEventFilter(this);
  setupLayout->addWidget(new QLabel(tr("Setup:"), setupRow_));
  setupLayout->addWidget(setupEdit_, 1);

  resultLabel_ = new QLabel(right);
  resultLabel_->setTextFormat(Qt::RichText);
  resultLabel_->setWordWrap(true);
  resultLabel_->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
  resultLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);

  chart_ = new TrendChart(right);
  chart_->setEmptyText(tr("Blind solve times will be charted here"));

  auto *centre = new QVBoxLayout;
  centre->addWidget(promptLabel_);
  centre->addWidget(cubeView_, 8);
  centre->addWidget(display_);
  centre->addWidget(hintLabel_);
  centre->addWidget(traceRow_);
  centre->addWidget(setupRow_);
  centre->addWidget(resultLabel_, 2);
  rightLayout->addLayout(centre, 3);
  rightLayout->addWidget(chart_, 1);

  auto *split = new QSplitter(this);
  split->addWidget(left);
  split->addWidget(right);
  split->setStretchFactor(0, 0);
  split->setStretchFactor(1, 1);
  split->setSizes({300, 800});
  // The drills, always in view (in focus mode too): the left column and the
  // options belong to the one picked.
  drillTabs_ = new QTabBar(this);
  drillTabs_->setFocusPolicy(Qt::NoFocus);
  drillTabs_->setExpanding(false);
  for (const QString &name : {tr("Letter quiz"), tr("Memo tracing"),
                              tr("Setup moves"), tr("Blind solves")}) {
    drillTabs_->addTab(name);
  }
  drillTabs_->setTabToolTip(0, tr("Name the letter of a sticker, or find the "
                                  "sticker of a letter"));
  drillTabs_->setTabToolTip(
      1, tr("Type the memo of a scrambled cube, checked by doing it"));
  drillTabs_->setTabToolTip(2, tr("Bring a letter's sticker to the swap spot"));
  drillTabs_->setTabToolTip(3, tr("Memo, then solve blind, in sessions"));
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(drillTabs_);
  layout->addWidget(split, 1);

  // --- wiring ---
  nextTimer_.setSingleShot(true);
  connect(&nextTimer_, &QTimer::timeout, this, [this] {
    nextItem();
    graceUntil_ = monotonicMs() + kSpaceGraceMs;
  });
  connect(&tick_, &QTimer::timeout, this, &BlindPage::onTick);
  connect(&feed_, &ScrambleFeed::currentChanged, this, [this] {
    if (waitingTrace_ && feed_.current()) {
      startTracing();
    } else if (drill_ == Drill::Solves && phase_ == Phase::Ready) {
      showScramble();
    }
  });
  connect(drillTabs_, &QTabBar::currentChanged, this, [this](int index) {
    if (index < 0 || static_cast<Drill>(index) == drill_) {
      return;
    }
    if (isBusy()) {
      QSignalBlocker block(drillTabs_);
      drillTabs_->setCurrentIndex(static_cast<int>(drill_));
      emit message(tr("Finish or cancel the solve (Esc) first"));
      return;
    }
    selectDrill(static_cast<Drill>(index));
  });
  auto optionChanged = [this] {
    QSettings q;
    if (drill_ == Drill::Quiz) {
      q.setValue("blind/quizPieces", piecesBox_->currentIndex());
      q.setValue("blind/quizDirection", directionBox_->currentIndex());
    } else if (drill_ == Drill::Setups) {
      q.setValue("blind/setupPieces", piecesBox_->currentIndex());
      q.setValue("blind/setupMode", setupModeBox_->currentIndex());
    }
    selectDrill(drill_);
  };
  for (QComboBox *box : {piecesBox_, directionBox_, setupModeBox_}) {
    connect(box, &QComboBox::activated, this, optionChanged);
  }
  connect(cubeBox_, &QComboBox::activated, this, [this](int index) {
    CubeKind kind = index == 0 ? CubeKind::Virtual : CubeKind::Physical;
    if (isBusy() || session().cube == kind) {
      return;
    }
    session().cube = kind;
    save();
    selectDrill(Drill::Solves);
  });
  connect(viewBox_, &QComboBox::activated, this, &BlindPage::applyView);
  connect(schemeButton_, &QPushButton::clicked, this, &BlindPage::editScheme);
  connect(sessionMenu_, &QMenu::aboutToShow, this,
          &BlindPage::refreshSessionMenu);
  connect(table_, &QTableWidget::customContextMenuRequested, this,
          [this](const QPoint &pos) {
            if (drill_ == Drill::Tracing) {
              showTraceMenu(pos);
            } else {
              showAttemptMenu(pos);
            }
          });
  connect(resetStatsButton_, &QPushButton::clicked, this, [this] {
    bool setups = drill_ == Drill::Setups;
    auto answer = QMessageBox::question(
        this, tr("Reset statistics"),
        setups ? tr("Forget the setup move statistics of every letter?")
               : tr("Forget the letter quiz statistics of every letter?"));
    if (answer != QMessageBox::Yes) {
      return;
    }
    (setups ? store_.data().setups : store_.data().quiz) = LetterStats{};
    save();
    refreshLeft();
  });
  connect(cubeView_, &CubeView::stickerClicked, this,
          &BlindPage::answerSticker);

  feed_.setType("333"); // WCA scrambles held white top, green front
  applySettings();
  int saved = QSettings().value("blind/drill", 0).toInt();
  selectDrill(static_cast<Drill>(std::clamp(saved, 0, 3)));
  pause(); // until the page is shown
}

// --- common
// -------------------------------------------------------------------

bool BlindPage::onVirtualCube() const {
  return store_.data().sessions[store_.currentIndex()].cube ==
         CubeKind::Virtual;
}

BlindPage::Pieces BlindPage::pieces() const {
  return static_cast<Pieces>(std::clamp(piecesBox_->currentIndex(), 0, 2));
}

BlindPage::Direction BlindPage::direction() const {
  return static_cast<Direction>(
      std::clamp(directionBox_->currentIndex(), 0, 2));
}

bool BlindPage::setupsOnCube() const {
  return setupModeBox_->currentIndex() == 0;
}

QString BlindPage::letterOf(BldPiece kind, int sticker) const {
  return qs(scheme_.letters.letter(kind, sticker));
}

QString BlindPage::stickerText(BldPiece kind, int sticker) const {
  return tr("%1 (%2)").arg(letterOf(kind, sticker),
                           qs(bldStickerName(kind, sticker)));
}

QString BlindPage::stickerHtml(BldPiece kind, int sticker) const {
  return stickerText(kind, sticker).toHtmlEscaped();
}

bool BlindPage::isBusy() const {
  return drill_ == Drill::Solves &&
         (phase_ == Phase::Memo || phase_ == Phase::Executing);
}

void BlindPage::activated() {
  resume();
  if ((drill_ == Drill::Quiz || drill_ == Drill::Setups) && !item_) {
    nextItem();
  }
}

void BlindPage::deactivated() {
  pause();
  nextTimer_.stop();
  cubeView_->finishAnimations();
  if (isBusy()) {
    cancelSolve();
  }
  for (QLineEdit *e : {edgesEdit_, cornersEdit_, setupEdit_}) {
    e->clearFocus();
  }
}

void BlindPage::windowDeactivated() {
  // Nothing is held down here, so a running solve keeps going: a long
  // blind solve shouldn't be lost to a window losing the keyboard. A
  // letter's or a trace's clock stops until the window is back.
  pause();
}

void BlindPage::changeEvent(QEvent *event) {
  if (event->type() == QEvent::ActivationChange && isActiveWindow() &&
      isVisible()) {
    resume();
  }
  ModePage::changeEvent(event);
}

void BlindPage::pause() {
  bool letter = (drill_ == Drill::Quiz || drill_ == Drill::Setups) && item_ &&
                !item_->answered;
  bool tracing = drill_ == Drill::Tracing && phase_ == Phase::Memo;
  if (!awayAt_ && (letter || tracing)) {
    awayAt_ = monotonicMs();
  }
}

void BlindPage::resume() {
  if (!awayAt_) {
    return;
  }
  std::int64_t now = monotonicMs();
  std::int64_t away = now - *awayAt_;
  awayAt_.reset();
  if (item_) {
    // A letter shown while away (e.g. moved on to by itself) starts now.
    item_->shownAt = std::min(now, item_->shownAt + away);
  }
  if (drill_ == Drill::Tracing && phase_ == Phase::Memo) {
    startMs_ += away;
  }
}

void BlindPage::look(Move m) {
  if (!isRotation(m.type)) {
    emit message(tr("Only rotations: turn the cube as a whole to look at it"));
    return;
  }
  cubeView_->animateMove(m);
  cubeView_->setMarks(movedMarks(cubeView_->marks(), m));
}

void BlindPage::setFocusMode(bool on) {
  leftPanel_->setVisible(!on);
  optionsRow_->setVisible(!on);
  chart_->setVisible(!on && drill_ == Drill::Solves);
}

void BlindPage::applySettings() {
  net_->setScheme(settings_.scheme);
  CubeLook look = cubeView_->look();
  look.scheme = settings_.scheme;
  cubeView_->setLook(look);
  cubeView_->setColourMap(colourMapOf(scheme_.hold));
  cubeView_->setAnimationMs(settings_.animationMs);
  display_->setDecimals(settings_.decimals);
  refreshLeft();
}

void BlindPage::save() {
  // Written in the background from a copy, so that saving never holds up
  // the next attempt.
  writeInBackground(
      store_.path(),
      [copy = store_](QString *error) { return copy.save(error); }, this,
      [this](const QString &error) {
        emit message(tr("Could not save blind data: %1").arg(error));
      });
}

void BlindPage::selectDrill(Drill d) {
  if (drill_ == Drill::Solves && d != Drill::Solves && isBusy()) {
    return;
  }
  bool changed = drill_ != d;
  drill_ = d;
  QSettings q;
  q.setValue("blind/drill", static_cast<int>(d));
  {
    QSignalBlocker block(drillTabs_);
    drillTabs_->setCurrentIndex(static_cast<int>(d));
  }
  nextTimer_.stop();
  tick_.stop();
  waitingTrace_ = false;
  awayAt_.reset();
  justRecorded_.reset();
  item_.reset();
  typed_.clear();
  phase_ = Phase::Ready;
  cubeView_->clearMarks();
  cubeView_->setColourMap(colourMapOf(scheme_.hold));
  resultLabel_->clear();
  promptLabel_->clear();
  for (QLineEdit *e : {edgesEdit_, cornersEdit_, setupEdit_}) {
    e->clear();
    e->clearFocus();
  }
  if (changed || d == Drill::Quiz || d == Drill::Setups) {
    // Each drill keeps its own choice of pieces.
    if (d == Drill::Quiz) {
      piecesBox_->setCurrentIndex(q.value("blind/quizPieces", 0).toInt());
      directionBox_->setCurrentIndex(q.value("blind/quizDirection", 0).toInt());
    } else if (d == Drill::Setups) {
      piecesBox_->setCurrentIndex(q.value("blind/setupPieces", 0).toInt());
      setupModeBox_->setCurrentIndex(q.value("blind/setupMode", 0).toInt());
    }
  }
  bool letters = d == Drill::Quiz || d == Drill::Setups;
  piecesBox_->parentWidget()->setVisible(letters);
  directionBox_->parentWidget()->setVisible(d == Drill::Quiz);
  setupModeBox_->parentWidget()->setVisible(d == Drill::Setups);
  cubeBox_->setVisible(d == Drill::Solves);
  sessionButton_->setVisible(d == Drill::Solves);
  resetStatsButton_->setVisible(letters);
  chart_->setVisible(d == Drill::Solves && optionsRow_->isVisibleTo(this));
  traceRow_->setVisible(d == Drill::Tracing);
  setupRow_->setVisible(d == Drill::Setups && !setupsOnCube());
  bool physical = d == Drill::Solves && !onVirtualCube();
  cubeView_->setVisible(!physical);
  viewPicker_->setVisible(d != Drill::Quiz && !physical);
  net_->setVisible(physical);
  scrambleLabel_->setVisible(d == Drill::Solves);
  if (d == Drill::Solves) {
    cubeBox_->setCurrentIndex(onVirtualCube() ? 0 : 1);
    sessionButton_->setText(tr("Session: %1").arg(qs(session().name)));
    display_->showTime(0);
    showScramble();
  } else if (d == Drill::Tracing) {
    cubeView_->setState(FaceletCube());
    display_->showText("–", palette().color(QPalette::PlaceholderText));
  } else {
    nextItem();
  }
  applyView();
  refreshLeft();
  updateControls();
  showHint();
}

void BlindPage::applyView() {
  CubeView::Mode chosen =
      CubeView::modeFromName(viewBox_->currentData().toString())
          .value_or(CubeView::Mode::ThreeD);
  CubeView::Mode mode = chosen;
  if (drill_ == Drill::Quiz) {
    mode = CubeView::Mode::Net; // every sticker can be seen and clicked
  } else if (drill_ == Drill::Tracing && phase_ == Phase::Ready) {
    mode = CubeView::Mode::Hidden;
    cubeView_->setHiddenText(tr("Press Space for a scramble"));
  } else if (drill_ == Drill::Solves &&
             (phase_ == Phase::Ready || phase_ == Phase::Executing)) {
    mode = CubeView::Mode::Hidden;
    cubeView_->setHiddenText(phase_ == Phase::Ready
                                 ? tr("Press Space to start the memo")
                                 : tr("Solving blind… press Space when "
                                      "done"));
  }
  if (mode == CubeView::Mode::Hidden && chosen == CubeView::Mode::Hidden) {
    cubeView_->setHiddenText({});
  }
  cubeView_->setMode(mode);
}

void BlindPage::updateControls() {
  bool busy = isBusy();
  for (QWidget *w : std::initializer_list<QWidget *>{
           drillTabs_, sessionButton_, cubeBox_, schemeButton_, viewBox_}) {
    w->setEnabled(!busy);
  }
  // The time only changes during a solve or a trace, so the display is
  // only redrawn then.
  bool ticking = busy || (drill_ == Drill::Tracing && phase_ == Phase::Memo);
  if (ticking != tick_.isActive()) {
    if (ticking) {
      tick_.start(30);
    } else {
      tick_.stop();
    }
  }
}

void BlindPage::showHint() {
  QString text;
  switch (drill_) {
  case Drill::Quiz:
    if (item_ && item_->answered) {
      text = tr("Space: next letter.");
    } else if (item_ && item_->toLetter) {
      text = tr("Type the ringed sticker's letter (Enter for a longer "
                "label, Backspace corrects). Space gives up.");
    } else {
      text = tr("Click the letter's sticker on the net. Space gives up.");
    }
    break;
  case Drill::Setups:
    if (item_ && item_->answered) {
      text = tr("Space: next letter. Esc: try it again.");
    } else if (setupsOnCube()) {
      text = tr("Turn the cube to bring the ringed sticker to ◎ without "
                "moving the buffer. Esc starts again, Space skips.");
    } else {
      text = tr("Type the setup and press Enter. Esc leaves the field; Esc "
                "again gives the letter up.");
    }
    break;
  case Drill::Tracing:
    text = phase_ == Phase::Ready
               ? tr("Space: a new scramble, and the timer starts.")
           : phase_ == Phase::Done
               ? tr("Space: next scramble. Rotations turn the cube to look "
                    "at it.")
               : tr("Turn the cube as a whole to look at it. Enter: type "
                    "the letters (Enter after the edges, Enter again to "
                    "check; Esc returns the keys to the cube). Esc: give "
                    "up this scramble.");
    break;
  case Drill::Solves:
    if (!onVirtualCube()) {
      text = phase_ == Phase::Ready ? tr("Space starts the timer and the memo.")
             : phase_ == Phase::Memo
                 ? tr("Space when the memo is done (put the blindfold on).")
             : phase_ == Phase::Executing
                 ? tr("Any key stops the timer. Esc cancels.")
                 : tr("Space: next scramble. Backspace toggles DNF.");
    } else {
      text = phase_ == Phase::Ready ? tr("Space starts the memo and the timer.")
             : phase_ == Phase::Memo
                 ? tr("Memorise: turn the cube as a whole to look at it. "
                      "Space hides it and starts the execution. Esc cancels.")
             : phase_ == Phase::Executing
                 ? tr("Turn the hidden cube. Space stops the timer. Esc "
                      "cancels.")
                 : tr("Space: next scramble. Rotations turn the cube to look "
                      "at it.");
    }
    break;
  }
  hintLabel_->setText(text);
}

bool BlindPage::eventFilter(QObject *obj, QEvent *event) {
  if (event->type() == QEvent::KeyPress &&
      (obj == edgesEdit_ || obj == cornersEdit_ || obj == setupEdit_)) {
    int key = static_cast<QKeyEvent *>(event)->key();
    bool enter = key == Qt::Key_Return || key == Qt::Key_Enter;
    if (key == Qt::Key_Escape) {
      static_cast<QWidget *>(obj)->clearFocus();
      return true;
    }
    if (enter && obj == edgesEdit_) {
      cornersEdit_->setFocus();
      return true;
    }
    if (enter && obj == cornersEdit_) {
      checkTrace();
      return true;
    }
    if (enter && obj == setupEdit_) {
      checkTypedSetup();
      return true;
    }
  }
  return ModePage::eventFilter(obj, event);
}

bool BlindPage::handleKey(QKeyEvent *e, bool press) {
  int key = e->key();
  if (e->modifiers() &
      (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
    return false;
  }
  bool space = key == Qt::Key_Space;
  bool enter = key == Qt::Key_Return || key == Qt::Key_Enter;
  std::optional<Move> move = settings_.keymap.lookup(e);

  switch (drill_) {
  case Drill::Quiz: {
    QString text = e->text();
    bool letter = !text.isEmpty() && text.at(0).isPrint() && !space;
    bool known = letter || space || enter || key == Qt::Key_Backspace;
    if (!known) {
      return false;
    }
    if (!press || e->isAutoRepeat()) {
      return true;
    }
    if (!item_) {
      nextItem();
      return true;
    }
    if (item_->answered) {
      if (space || enter) {
        nextTimer_.stop();
        nextItem();
      }
      return true;
    }
    if (space) {
      if (monotonicMs() < graceUntil_) {
        return true; // meant for the letter before, which moved on
      }
      finishItem(false, tr("Given up."));
      return true;
    }
    if (!item_->toLetter) {
      return true; // a click answers
    }
    if (key == Qt::Key_Backspace) {
      typed_.chop(1);
    } else if (enter) {
      if (!typed_.isEmpty()) {
        answerLetter(typed_);
      }
      return true;
    } else {
      typed_ += text;
    }
    // A letter that only one sticker's letter starts with, and is all of
    // it, answers at once; one that no letter starts with is wrong.
    int starts = 0;
    bool whole = false;
    for (int i = 0; i < kBldStickers; ++i) {
      QString l = lowered(letterOf(item_->kind, i));
      if (!typed_.isEmpty() && l.startsWith(lowered(typed_))) {
        ++starts;
        whole = whole || l == lowered(typed_);
      }
    }
    if (!typed_.isEmpty() && (starts == 0 || (starts == 1 && whole))) {
      answerLetter(typed_);
    } else {
      display_->showText(typed_.isEmpty() ? "?" : typed_);
    }
    return true;
  }

  case Drill::Setups: {
    bool known = move || space || enter || key == Qt::Key_Escape;
    if (!known) {
      return false;
    }
    if (!press || e->isAutoRepeat()) {
      return true;
    }
    if (!item_) {
      nextItem();
      return true;
    }
    if (item_->answered) {
      if (space || enter) {
        nextTimer_.stop();
        nextItem();
      } else if (key == Qt::Key_Escape) {
        // The same letter again.
        nextTimer_.stop();
        item_->answered = false;
        item_->shownAt = monotonicMs();
        showItem();
      }
      return true;
    }
    if (!setupsOnCube()) {
      // Typed: the keys belong to the field; one pressed outside it goes
      // back to it, and Esc there gives the letter up.
      if (key == Qt::Key_Escape) {
        skipItem();
      } else if (space || enter) {
        setupEdit_->setFocus();
      }
    } else if (space) {
      skipItem();
    } else if (key == Qt::Key_Escape) {
      resetSetupCube();
    } else if (move) {
      turnSetup(*move);
    }
    return true;
  }

  case Drill::Tracing: {
    bool known =
        move || space || enter || key == Qt::Key_Escape || key == Qt::Key_Tab;
    if (!known) {
      return false;
    }
    if (!press || e->isAutoRepeat()) {
      return true;
    }
    if (phase_ == Phase::Ready || phase_ == Phase::Done) {
      if (space) {
        startTracing(); // the feed moved on when the last one was shown
      } else if (move && phase_ == Phase::Done) {
        look(*move);
      }
      return true;
    }
    // Tracing.
    if (key == Qt::Key_Escape) {
      giveUpTrace();
      return true;
    }
    if (enter || key == Qt::Key_Tab) {
      focusTrace();
      return true;
    }
    if (move) {
      if (isRotation(move->type)) {
        traceCube_.apply(*move);
        cubeView_->animateMove(*move);
      } else {
        emit message(tr("Only rotations while tracing"));
      }
    }
    return true;
  }

  case Drill::Solves:
    break;
  }

  // Blind solves.
  if (!onVirtualCube()) {
    if (phase_ == Phase::Executing) {
      // Any key stops the timer, except Esc, which cancels.
      if (!press || e->isAutoRepeat()) {
        return true;
      }
      if (key == Qt::Key_Escape) {
        cancelSolve();
      } else {
        stopSolve();
      }
      return true;
    }
    bool known = space || key == Qt::Key_Escape || key == Qt::Key_Backspace;
    if (!known) {
      return false;
    }
    if (!press || e->isAutoRepeat()) {
      return true;
    }
    if (key == Qt::Key_Escape) {
      if (phase_ == Phase::Memo) {
        cancelSolve();
      }
    } else if (key == Qt::Key_Backspace) {
      // Only the solve just recorded, while it is still the last one.
      const auto &attempts = session().attempts;
      if (phase_ == Phase::Done && justRecorded_ &&
          justRecorded_->first == store_.currentIndex() && !attempts.empty() &&
          attempts.back().timestamp == justRecorded_->second) {
        BlindAttempt &a = session().attempts.back();
        a.dnf = !a.dnf;
        display_->showTime(a.timeMs, a.dnf ? Penalty::Dnf : Penalty::None);
        save();
        refreshLeft();
      }
    } else if (phase_ == Phase::Ready) {
      startMemo();
    } else if (phase_ == Phase::Memo) {
      startExecution();
    } else if (phase_ == Phase::Done) {
      next();
    }
    return true;
  }

  bool known = move || space || key == Qt::Key_Escape;
  if (!known) {
    return false;
  }
  if (!press || e->isAutoRepeat()) {
    return true;
  }
  if (key == Qt::Key_Escape) {
    if (isBusy()) {
      cancelSolve();
    }
    return true;
  }
  if (space) {
    if (phase_ == Phase::Ready) {
      startMemo();
    } else if (phase_ == Phase::Memo) {
      startExecution();
    } else if (phase_ == Phase::Executing) {
      stopSolve();
    } else {
      next();
    }
    return true;
  }
  if (phase_ == Phase::Memo || phase_ == Phase::Executing) {
    if (attempt_.turn(*move)) {
      cubeView_->animateMove(*move);
    } else {
      emit message(tr("Only rotations during the memo"));
    }
  } else if (phase_ == Phase::Done) {
    look(*move);
  }
  return true;
}

// --- left column
// --------------------------------------------------------------

void BlindPage::refreshLeft() {
  table_->clearContents();
  QColor muted = palette().color(QPalette::PlaceholderText);
  auto cell = [&](int row, int col, const QString &text, int index) {
    auto *item = new QTableWidgetItem(text);
    item->setData(Qt::UserRole, index);
    if (col > 0) {
      item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    table_->setItem(row, col, item);
    return item;
  };

  int d = settings_.decimals;
  if (drill_ == Drill::Quiz || drill_ == Drill::Setups) {
    bool setups = drill_ == Drill::Setups;
    const LetterStats &stats =
        setups ? store_.data().setups : store_.data().quiz;
    std::vector<std::pair<BldPiece, int>> rows;
    for (BldPiece kind : kKinds) {
      if ((kind == BldPiece::Edge && pieces() == Pieces::Corners) ||
          (kind == BldPiece::Corner && pieces() == Pieces::Edges)) {
        continue;
      }
      for (int i = 0; i < kBldStickers; ++i) {
        if (setups && (!op::isTarget(kind, i) || i == op::swapSpot(kind))) {
          continue;
        }
        rows.emplace_back(kind, i);
      }
    }
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels(
        {tr("Letter"), tr("Sticker"), tr("Tries"), tr("Right"), tr("Mean")});
    table_->setRowCount(static_cast<int>(rows.size()));
    int attempts = 0, correct = 0;
    std::int64_t correctMs = 0;
    for (std::size_t r = 0; r < rows.size(); ++r) {
      auto [kind, sticker] = rows[r];
      const LetterStat &s = stats.of(kind, sticker);
      attempts += s.attempts;
      correct += s.correct;
      correctMs += s.correctMs;
      int row = static_cast<int>(r);
      cell(row, 0, letterOf(kind, sticker), sticker);
      cell(row, 1, qs(bldStickerName(kind, sticker)), sticker);
      cell(row, 2, QString::number(s.attempts), sticker);
      auto *acc = cell(
          row, 3,
          s.accuracy() ? QString("%1%").arg(std::lround(*s.accuracy() * 100))
                       : QString("-"),
          sticker);
      if (s.accuracy() && *s.accuracy() < 0.8) {
        acc->setForeground(kWrong);
      }
      cell(row, 4,
           s.meanMs() ? timeText(static_cast<std::int64_t>(*s.meanMs()), d)
                      : QString("-"),
           sticker);
      if (s.attempts == 0) {
        for (int c = 2; c < 5; ++c) {
          table_->item(row, c)->setForeground(muted);
        }
      }
    }
    stats_->setText(
        (setups ? tr("<p><b>Setup moves</b>: %1 tries, %2 right%3, mean "
                     "%4.</p>")
                : tr("<p><b>Letter quiz</b>: %1 tries, %2 right%3, mean "
                     "%4.</p>"))
            .arg(attempts)
            .arg(correct)
            .arg(attempts ? QString(" (%1%)").arg(
                                std::lround(100.0 * correct / attempts))
                          : QString())
            .arg(correct ? timeText(correctMs / correct, d) : QString("-")));
    return;
  }

  if (drill_ == Drill::Tracing) {
    const auto &traces = store_.data().traces;
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels(
        {"#", tr("Time"), tr("Edges"), tr("Corners")});
    std::size_t shown = std::min<std::size_t>(traces.size(), 100);
    table_->setRowCount(static_cast<int>(shown));
    int edges = 0, corners = 0, both = 0;
    std::int64_t bothMs = 0;
    StatValue best;
    for (const auto &t : traces) {
      edges += t.edgesRight;
      corners += t.cornersRight;
      if (t.edgesRight && t.cornersRight) {
        ++both;
        bothMs += t.timeMs;
        best = best ? std::min(*best, t.timeMs) : t.timeMs;
      }
    }
    for (std::size_t r = 0; r < shown; ++r) {
      std::size_t i = traces.size() - 1 - r;
      const TraceAttempt &t = traces[i];
      int row = static_cast<int>(r);
      cell(row, 0, QString::number(i + 1), static_cast<int>(i));
      cell(row, 1, timeText(t.timeMs, d), static_cast<int>(i));
      cell(row, 2, t.edgesRight ? "✓" : "✗", static_cast<int>(i))
          ->setForeground(t.edgesRight ? kRight : kWrong);
      cell(row, 3, t.cornersRight ? "✓" : "✗", static_cast<int>(i))
          ->setForeground(t.cornersRight ? kRight : kWrong);
    }
    int n = static_cast<int>(traces.size());
    auto pct = [n](int k) {
      return n ? QString("%1%").arg(std::lround(100.0 * k / n)) : QString("-");
    };
    stats_->setText(tr("<p><b>Memo tracing</b>: %1 traced. Edges right %2, "
                       "corners right %3, both %4.</p><p>Mean time (both "
                       "right): %5 &nbsp; Best: %6</p>")
                        .arg(n)
                        .arg(pct(edges), pct(corners), pct(both),
                             both ? timeText(bothMs / both, d) : QString("-"),
                             timeText(best, d)));
    return;
  }

  // Blind solves.
  const auto &attempts = session().attempts;
  std::vector<Solve> solves = blindSolves(session());
  table_->setColumnCount(3);
  table_->setHorizontalHeaderLabels({"#", tr("Time"), tr("Memo")});
  table_->setRowCount(static_cast<int>(attempts.size()));
  int successes = 0;
  std::int64_t memoTotal = 0;
  for (std::size_t i = 0; i < attempts.size(); ++i) {
    const BlindAttempt &a = attempts[i];
    int row = static_cast<int>(attempts.size() - 1 - i);
    int index = static_cast<int>(i);
    cell(row, 0, QString::number(i + 1), index);
    cell(row, 1, qs(formatSolve(solves[i], d)), index);
    cell(row, 2, timeText(a.memoMs, d), index);
    QStringList tip;
    if (!a.comment.empty()) {
      tip << qs(a.comment);
    }
    tip << tr("Scramble: %1").arg(qs(a.scramble));
    if (!a.solution.empty()) {
      tip << tr("Solution: %1").arg(qs(a.solution));
    }
    table_->item(row, 1)->setToolTip(tip.join("\n"));
    if (!a.dnf) {
      ++successes;
      memoTotal += a.memoMs;
    }
  }
  SessionSummary sum = summarize(solves);
  QString html =
      "<table cellspacing='4' width='100%'><tr><th></th><th align='right'>" +
      tr("current") + "</th><th align='right'>" + tr("best") + "</th></tr>";
  auto row = [&](const QString &name, StatValue cur, StatValue best) {
    html += QString("<tr><td>%1</td><td align='right'>%2</td><td "
                    "align='right'><b>%3</b></td></tr>")
                .arg(name, timeText(cur, d), timeText(best, d));
  };
  row(tr("time"),
      solves.empty() ? StatValue{} : StatValue{effectiveTime(solves.back())},
      sum.best);
  row("mo3", currentMean(solves, 3), bestMean(solves, 3));
  row("ao5", currentAverage(solves, 5), bestAverage(solves, 5));
  row("ao12", currentAverage(solves, 12), bestAverage(solves, 12));
  html += "</table>";
  int n = static_cast<int>(attempts.size());
  html +=
      tr("<p>Successes: %1 of %2%3<br>Mean of successes: %4 &nbsp; Mean "
         "memo: %5</p>")
          .arg(successes)
          .arg(n)
          .arg(n ? QString(" (%1%)").arg(std::lround(100.0 * successes / n))
                 : QString())
          .arg(timeText(sum.mean, d),
               successes ? timeText(memoTotal / successes, d) : QString("-"));
  stats_->setText(html);
  chart_->setSolves(solves);
}

// --- letter quiz and setup moves
// ----------------------------------------------

BlindPage::Item BlindPage::pickItem(bool setups) {
  const LetterStats &stats = setups ? store_.data().setups : store_.data().quiz;
  struct Choice {
    BldPiece kind;
    int sticker;
  };
  std::vector<Choice> pool;
  for (BldPiece kind : kKinds) {
    if ((kind == BldPiece::Edge && pieces() == Pieces::Corners) ||
        (kind == BldPiece::Corner && pieces() == Pieces::Edges)) {
      continue;
    }
    for (int i = 0; i < kBldStickers; ++i) {
      // The swap spot needs no setup, and the buffer can't be a target.
      if (setups && (!op::isTarget(kind, i) || i == op::swapSpot(kind))) {
        continue;
      }
      pool.push_back({kind, i});
    }
  }
  // Slow letters are compared with the mean over all letters.
  double total = 0;
  int timed = 0;
  for (const Choice &c : pool) {
    if (auto m = stats.of(c.kind, c.sticker).meanMs()) {
      total += *m;
      ++timed;
    }
  }
  double overall = timed ? total / timed : 0.0;
  std::vector<double> weights;
  for (const Choice &c : pool) {
    const LetterStat &s = stats.of(c.kind, c.sticker);
    double w = 1.0;
    if (s.attempts == 0) {
      w = 3.0; // never asked yet
    } else {
      w += 4.0 * (1.0 - s.accuracy().value_or(0.0));
      if (auto m = s.meanMs(); m && overall > 0) {
        w += 2.0 * std::clamp(*m / overall - 1.0, 0.0, 3.0);
      }
    }
    if (last_ && last_->kind == c.kind && last_->sticker == c.sticker &&
        pool.size() > 1) {
      w = 0.0; // never the same letter twice in a row
    }
    weights.push_back(w);
  }
  std::discrete_distribution<std::size_t> pick(weights.begin(), weights.end());
  Choice c = pool[pick(rng_)];
  Item item;
  item.kind = c.kind;
  item.sticker = c.sticker;
  Direction dir = direction();
  item.toLetter = setups || dir == Direction::ToLetter ||
                  (dir == Direction::Mixed &&
                   std::uniform_int_distribution<int>(0, 1)(rng_) == 0);
  item.shownAt = monotonicMs();
  return item;
}

void BlindPage::nextItem() {
  nextTimer_.stop();
  if (drill_ != Drill::Quiz && drill_ != Drill::Setups) {
    return;
  }
  item_ = pickItem(drill_ == Drill::Setups);
  last_ = item_;
  if (!awayAt_ && (!isVisible() || !isActiveWindow())) {
    awayAt_ = monotonicMs(); // shown while away: its clock starts on return
  }
  showItem();
}

void BlindPage::showItem() {
  if (!item_) {
    return;
  }
  typed_.clear();
  resultLabel_->clear();
  const Item &it = *item_;
  QString kind = pieceName(it.kind, false).toLower();
  if (drill_ == Drill::Quiz) {
    cubeView_->setState(FaceletCube());
    StickerMarks marks;
    if (it.toLetter) {
      marks.rings.push_back(bldFacelet(it.kind, it.sticker));
      promptLabel_->setText(
          tr("Which letter is the ringed %1 sticker?").arg(kind));
      display_->showText("?", palette().color(QPalette::PlaceholderText));
    } else {
      promptLabel_->setText(
          tr("Click the %1 sticker with this letter:").arg(kind));
      display_->showText(letterOf(it.kind, it.sticker));
    }
    cubeView_->setMarks(marks);
  } else {
    promptLabel_->setText(
        tr("Set up %1 %2: bring it to %3 (◎)")
            .arg(kind, stickerHtml(it.kind, it.sticker),
                 qs(bldStickerName(it.kind, op::swapSpot(it.kind)))));
    display_->showText(letterOf(it.kind, it.sticker));
    resetSetupCube();
    if (!setupsOnCube()) {
      setupEdit_->clear();
      setupEdit_->setEnabled(true);
      setupEdit_->setFocus();
    }
  }
  showHint();
}

void BlindPage::answerLetter(const QString &text) {
  if (!item_ || item_->answered) {
    return;
  }
  QString right = letterOf(item_->kind, item_->sticker);
  bool ok = lowered(text) == lowered(right);
  finishItem(ok,
             ok ? tr("Right: %1.").arg(stickerHtml(item_->kind, item_->sticker))
                : tr("“%1” is wrong: it's %2.")
                      .arg(text.toHtmlEscaped(),
                           stickerHtml(item_->kind, item_->sticker)));
}

void BlindPage::answerSticker(int facelet) {
  if (drill_ != Drill::Quiz || !item_ || item_->answered || item_->toLetter) {
    return;
  }
  std::optional<int> clicked = bldSticker(item_->kind, facelet);
  if (clicked && *clicked == item_->sticker) {
    finishItem(true,
               tr("Right: %1.").arg(stickerHtml(item_->kind, item_->sticker)));
    return;
  }
  QString what;
  if (clicked) {
    what = tr("that's %1").arg(stickerHtml(item_->kind, *clicked));
  } else {
    BldPiece other =
        item_->kind == BldPiece::Edge ? BldPiece::Corner : BldPiece::Edge;
    what = bldSticker(other, facelet)
               ? (other == BldPiece::Edge ? tr("that's an edge sticker")
                                          : tr("that's a corner sticker"))
               : tr("that's a centre");
  }
  finishItem(false, tr("Wrong, %1. %2 is ringed.")
                        .arg(what, stickerHtml(item_->kind, item_->sticker)));
}

void BlindPage::finishItem(bool right, const QString &feedback) {
  if (!item_ || item_->answered) {
    return;
  }
  item_->answered = true;
  item_->right = right;
  std::int64_t ms = monotonicMs() - item_->shownAt;
  bool setups = drill_ == Drill::Setups;
  (setups ? store_.data().setups : store_.data().quiz)
      .of(item_->kind, item_->sticker)
      .record(right, ms);
  save();
  refreshLeft();
  QColor colour = right ? kRight : kWrong;
  if (drill_ == Drill::Quiz) {
    // The right sticker, ringed and lettered.
    StickerMarks marks;
    int f = bldFacelet(item_->kind, item_->sticker);
    marks.rings.push_back(f);
    marks.ringColour = colour;
    marks.labels[static_cast<std::size_t>(f)] =
        letterOf(item_->kind, item_->sticker);
    cubeView_->setMarks(marks);
    display_->showText(item_->toLetter
                           ? (typed_.isEmpty() ? QString("–") : typed_)
                           : letterOf(item_->kind, item_->sticker),
                       colour);
  } else {
    display_->showText(right ? timeText(ms, settings_.decimals)
                             : letterOf(item_->kind, item_->sticker),
                       colour);
    setupEdit_->clearFocus();
  }
  resultLabel_->setText(
      QString("<p style='color:%1'>%2</p>").arg(colour.name(), feedback));
  if (right && drill_ == Drill::Quiz) {
    nextTimer_.start(700); // or Space now
  }
  showHint();
}

void BlindPage::skipItem() {
  if (!item_ || item_->answered) {
    return;
  }
  std::optional<Alg> setup = scheme_.setup(item_->kind, item_->sticker);
  finishItem(false, tr("Skipped. The setup: %1")
                        .arg(setup ? qs(toString(*setup)).toHtmlEscaped()
                                   : QString()));
}

void BlindPage::resetSetupCube() {
  setupCube_ = FaceletCube();
  setupIds_ = numbered();
  setupMoves_.clear();
  cubeView_->setState(setupCube_);
  showSetupCube();
}

void BlindPage::showSetupCube() {
  if (!item_) {
    return;
  }
  StickerMarks marks;
  int swap = bldFacelet(item_->kind, op::swapSpot(item_->kind));
  marks.labels[static_cast<std::size_t>(swap)] = "◎";
  marks.rings.push_back(
      whereIs(setupIds_, bldFacelet(item_->kind, item_->sticker)));
  if (item_->answered) {
    marks.ringColour = item_->right ? kRight : kWrong;
  }
  cubeView_->setMarks(marks);
}

void BlindPage::turnSetup(Move m) {
  if (!item_ || item_->answered) {
    return;
  }
  if (isRotation(m.type)) {
    emit message(tr("Rotations aren't part of a setup: turn layers only"));
    return;
  }
  setupCube_.apply(m);
  setupIds_.apply(m);
  setupMoves_.push_back(m);
  cubeView_->animateMove(m);
  showSetupCube();
  if (op::isValidSetup(item_->kind, item_->sticker, setupMoves_)) {
    std::optional<Alg> scheme = scheme_.setup(item_->kind, item_->sticker);
    Alg done = simplify(setupMoves_);
    int moves = htmCount(done);
    finishItem(
        true,
        tr("Set up in %1: %2. The setup in your scheme: %3")
            .arg(setupMovesText(moves), qs(toString(done)).toHtmlEscaped(),
                 scheme ? qs(toString(*scheme)).toHtmlEscaped() : QString()));
    showSetupCube();
    return;
  }
  int swap = bldFacelet(item_->kind, op::swapSpot(item_->kind));
  if (whereIs(setupIds_, bldFacelet(item_->kind, item_->sticker)) == swap) {
    resultLabel_->setText(
        QString("<p style='color:%1'>%2</p>")
            .arg(statusColour(Status::Warning).name(),
                 tr("It's at the swap spot, but this setup %1. Esc starts "
                    "again.")
                     .arg(setupProblem(item_->kind, item_->sticker,
                                       toString(setupMoves_))
                              .toHtmlEscaped())));
  } else {
    resultLabel_->clear();
  }
}

void BlindPage::checkTypedSetup() {
  if (!item_ || item_->answered || drill_ != Drill::Setups) {
    return;
  }
  std::string text = cleanAlgText(ss(setupEdit_->text()));
  if (text.empty()) {
    // Not an answer: nothing is recorded.
    resultLabel_->setText(
        QString("<p style='color:%1'>%2</p>")
            .arg(kWrong.name(), tr("Type the setup first (Esc outside the "
                                   "field gives the letter up)")));
    return;
  }
  ParseResult parsed = parseAlg(text);
  if (!parsed) {
    resultLabel_->setText(
        QString("<p style='color:%1'>%2</p>")
            .arg(kWrong.name(),
                 tr("Can't read this at character %1: %2")
                     .arg(parsed.error->position + 1)
                     .arg(qs(parsed.error->message).toHtmlEscaped())));
    return; // keep the field to fix it
  }
  // Show where the typed setup takes the sticker.
  setupCube_ = FaceletCube();
  setupIds_ = numbered();
  setupCube_.apply(parsed.alg);
  setupIds_.apply(parsed.alg);
  setupMoves_ = parsed.alg;
  cubeView_->setState(setupCube_);
  std::optional<Alg> scheme = scheme_.setup(item_->kind, item_->sticker);
  QString schemeText =
      scheme ? qs(toString(*scheme)).toHtmlEscaped() : QString();
  if (op::isValidSetup(item_->kind, item_->sticker, parsed.alg)) {
    finishItem(true, tr("Right. The setup in your scheme: %1").arg(schemeText));
  } else {
    finishItem(
        false,
        tr("That setup %1. The setup in your scheme: %2")
            .arg(
                setupProblem(item_->kind, item_->sticker, text).toHtmlEscaped(),
                schemeText));
  }
  showSetupCube();
}

// --- memo tracing
// -------------------------------------------------------------

void BlindPage::startTracing() {
  const auto &current = feed_.current();
  if (!current) {
    waitingTrace_ = true;
    resultLabel_->setText(tr("Generating scramble…"));
    return;
  }
  waitingTrace_ = false;
  phase_ = Phase::Memo;
  used_ = *current;
  feed_.advance(); // seen now, so never traced or solved again
  CubieCube scrambled = cubieOf(parseAlgOrEmpty(ss(used_->text)));
  heldCubie_ = solverState(scrambled, scheme_.hold);
  heldStart_ = heldCubie_.toFacelets();
  traceCube_ = heldStart_;
  cubeView_->clearMarks();
  cubeView_->setState(traceCube_);
  edgesEdit_->clear();
  cornersEdit_->clear();
  resultLabel_->clear();
  promptLabel_->setText(tr("Trace the memo: edges from UR, corners from UBL."));
  startMs_ = monotonicMs();
  display_->showText(timeText(0, 1));
  applyView();
  updateControls();
  showHint();
}

void BlindPage::giveUpTrace() {
  phase_ = Phase::Ready;
  cubeView_->clearMarks();
  display_->showText("–", palette().color(QPalette::PlaceholderText));
  resultLabel_->setText(tr("Scramble given up; nothing was recorded."));
  applyView();
  updateControls();
  showHint();
}

void BlindPage::showTraceMenu(const QPoint &pos) {
  auto *item = table_->itemAt(pos);
  if (!item) {
    return;
  }
  auto index = static_cast<std::size_t>(item->data(Qt::UserRole).toInt());
  if (index >= store_.data().traces.size()) {
    return;
  }
  QMenu menu(this);
  auto *del = menu.addAction(tr("Delete"));
  if (menu.exec(table_->viewport()->mapToGlobal(pos)) == del) {
    store_.removeTrace(index);
    save();
    refreshLeft();
  }
}

void BlindPage::focusTrace() {
  if (phase_ != Phase::Memo) {
    return;
  }
  (edgesEdit_->text().isEmpty() || !cornersEdit_->text().isEmpty()
       ? edgesEdit_
       : cornersEdit_)
      ->setFocus();
}

void BlindPage::checkTrace() {
  if (drill_ != Drill::Tracing || phase_ != Phase::Memo) {
    return;
  }
  // Letters that can't be read are a typing slip, not a wrong memo: they
  // are shown, and nothing is graded until they are fixed.
  QStringList slips;
  QLineEdit *slipEdit = nullptr;
  for (BldPiece kind : kKinds) {
    QLineEdit *edit = kind == BldPiece::Edge ? edgesEdit_ : cornersEdit_;
    LetterScheme::Parsed parsed = scheme_.letters.parse(kind, ss(edit->text()));
    if (parsed.errorAt) {
      slips << tr("%1: can't read “%2” at character %3.")
                   .arg(pieceName(kind), edit->text().toHtmlEscaped())
                   .arg(*parsed.errorAt + 1);
      if (!slipEdit) {
        slipEdit = edit;
      }
    }
  }
  if (slipEdit) {
    resultLabel_->setText(
        QString("<p style='color:%1'>%2</p>")
            .arg(kWrong.name(),
                 slips.join("<br>") + "<br>" +
                     tr("Fix the letters and press Enter to check.")));
    slipEdit->setFocus();
    return;
  }
  std::int64_t ms = monotonicMs() - startMs_;
  BldMemo mine = traceMemo(heldCubie_);
  QString html;
  std::optional<std::pair<BldPiece, std::vector<int>>> ringed;
  std::array<bool, 2> right{};
  for (BldPiece kind : kKinds) {
    QLineEdit *edit = kind == BldPiece::Edge ? edgesEdit_ : cornersEdit_;
    QString name = pieceName(kind);
    LetterScheme::Parsed parsed = scheme_.letters.parse(kind, ss(edit->text()));
    bool ok = false;
    QString line;
    if (parsed.errorAt) {
      line = tr("%1: can't read “%2” at character %3.")
                 .arg(name, edit->text().toHtmlEscaped())
                 .arg(*parsed.errorAt + 1);
    } else {
      MemoCheck check = checkMemo(heldCubie_, kind, parsed.stickers);
      ok = check.solves && !check.firstWrong;
      // The typed letters in pairs, the first wrong one marked.
      QStringList pairs;
      QString pair;
      for (std::size_t i = 0; i < parsed.stickers.size(); ++i) {
        QString l = letterOf(kind, parsed.stickers[i]).toHtmlEscaped();
        if (check.firstWrong == i) {
          l = "<span style='color:" + kWrong.name() + "'><u>" + l +
              "</u></span>";
        }
        pair += l;
        if (i % 2 == 1) {
          pairs << pair;
          pair.clear();
        }
      }
      if (!pair.isEmpty()) {
        pairs << pair;
      }
      QString typed = pairs.isEmpty() ? tr("(none)") : pairs.join(' ');
      auto lettersOf = [&](const std::vector<int> &stickers) {
        QStringList out;
        for (std::size_t i = 0; i < stickers.size() && i < 8; ++i) {
          out << letterOf(kind, stickers[i]).toHtmlEscaped();
        }
        if (stickers.size() > 8) {
          out << "…";
        }
        return out.join(", ");
      };
      if (ok) {
        line = tr("%1 ✓ %2").arg(name, typed);
      } else if (check.firstWrong) {
        std::size_t at = *check.firstWrong;
        QString expected =
            check.expected.empty()
                ? tr("nothing: the pieces were all solved by then")
            : check.expected.size() == 1
                ? tr("%1").arg(lettersOf(check.expected))
                : tr("a new cycle, any of %1").arg(lettersOf(check.expected));
        line = tr("%1 ✗ %2 — letter %3 is wrong; there it should be %4.")
                   .arg(name, typed)
                   .arg(at + 1)
                   .arg(expected);
        if (!ringed && !check.expected.empty()) {
          ringed = {kind, check.expected};
        }
      } else {
        line = tr("%1 ✗ %2 — pieces are left; next would be %3.")
                   .arg(name, typed, lettersOf(check.expected));
        if (!ringed && !check.expected.empty()) {
          ringed = {kind, check.expected};
        }
      }
    }
    right[kind == BldPiece::Edge ? 0 : 1] = ok;
    html += QString("<p style='color:%1'>%2</p>")
                .arg((ok ? kRight : kWrong).name(), line);
  }
  html += "<p>" +
          tr("Traced here: edges %1, corners %2.")
              .arg(qs(scheme_.letters.format(BldPiece::Edge, mine.edges))
                       .toHtmlEscaped(),
                   qs(scheme_.letters.format(BldPiece::Corner, mine.corners))
                       .toHtmlEscaped()) +
          " " +
          (mine.parity()
               ? tr("Parity: an odd number of edge targets, so the parity "
                    "alg goes between the edges and the corners.")
               : tr("No parity.")) +
          "</p>";
  resultLabel_->setText(html);
  store_.addTrace({ms, right[0], right[1], unixNow()});
  save();
  refreshLeft();
  display_->showText(timeText(ms, settings_.decimals),
                     right[0] && right[1] ? kRight : kWrong);
  // Back to the way it was held, with the stickers that were due ringed.
  phase_ = Phase::Done;
  edgesEdit_->clearFocus();
  cornersEdit_->clearFocus();
  cubeView_->setState(heldStart_);
  StickerMarks marks;
  if (ringed) {
    for (int s : ringed->second) {
      marks.rings.push_back(bldFacelet(ringed->first, s));
    }
  }
  cubeView_->setMarks(marks);
  applyView();
  updateControls();
  showHint();
}

// --- blind solves
// -------------------------------------------------------------

void BlindPage::showScramble() {
  if (drill_ != Drill::Solves) {
    return;
  }
  // The solve's own while it lasts and after; the feed's next when ready.
  const std::optional<GeneratedScramble> &current =
      phase_ == Phase::Ready ? feed_.current() : used_;
  if (!current) {
    scrambleLabel_->setText(tr("Generating scramble…"));
    net_->setCube(std::nullopt);
    return;
  }
  scrambleLabel_->setText(current->text);
  net_->setScramble(current->text);
  CubieCube scrambled = cubieOf(parseAlgOrEmpty(ss(current->text)));
  heldCubie_ = solverState(scrambled, scheme_.hold);
  heldStart_ = heldCubie_.toFacelets();
  attempt_.load(heldStart_);
  cubeView_->setState(attempt_.cube());
}

void BlindPage::startMemo() {
  if (!feed_.current()) {
    emit message(tr("Wait for the scramble before starting"));
    return;
  }
  showScramble();
  used_ = feed_.current();
  phase_ = Phase::Memo;
  feed_.advance(); // seen now: cancelled or not, the next solve is new
  justRecorded_.reset();
  resultLabel_->clear();
  startMs_ = monotonicMs();
  memoMs_ = 0;
  applyView();
  updateControls();
  showHint();
  onTick();
}

void BlindPage::startExecution() {
  memoMs_ = monotonicMs() - startMs_;
  phase_ = Phase::Executing;
  if (onVirtualCube()) {
    attempt_.start();
  }
  applyView();
  updateControls();
  showHint();
}

void BlindPage::stopSolve() {
  std::int64_t ms = monotonicMs() - startMs_;
  phase_ = Phase::Done;
  updateControls();
  if (!onVirtualCube()) {
    recordSolve(ms, memoMs_, false, {});
    applyView();
    showHint();
    return;
  }
  bool solved = attempt_.cube().isSolved();
  // The solution from the WCA hold, so that it replays from the scramble.
  Alg turn = holdRotation(scheme_.hold);
  std::string solution = toString(attempt_.solution());
  if (!turn.empty()) {
    solution = toString(turn) + (solution.empty() ? "" : " " + solution);
  }
  recordSolve(ms, memoMs_, !solved, solution);
  cubeView_->finishAnimations();
  applyView(); // reveal the cube as it was left
  if (solved) {
    resultLabel_->setText(
        QString("<p style='color:%1'>%2</p>")
            .arg(kRight.name(),
                 tr("Solved! %1 (memo %2).")
                     .arg(timeText(ms, settings_.decimals),
                          timeText(memoMs_, settings_.decimals))));
  } else {
    FaceletCube end = attempt_.cube();
    end.normalizeOrientation();
    std::optional<CubieCube> left = CubieCube::fromFacelets(end);
    BldMemo memo = traceMemo(heldCubie_);
    QString off;
    if (left) {
      int edges = unsolvedCount(*left, BldPiece::Edge);
      int corners = unsolvedCount(*left, BldPiece::Corner);
      off = tr("%1 and %2 off.")
                .arg(edges == 1 ? tr("1 edge") : tr("%1 edges").arg(edges),
                     corners == 1 ? tr("1 corner")
                                  : tr("%1 corners").arg(corners));
    }
    resultLabel_->setText(
        QString("<p style='color:%1'>%2</p><p>%3</p>")
            .arg(kWrong.name(), tr("DNF: %1").arg(off),
                 tr("The memo traced here: edges %1, corners %2%3.")
                     .arg(qs(scheme_.letters.format(BldPiece::Edge, memo.edges))
                              .toHtmlEscaped(),
                          qs(scheme_.letters.format(BldPiece::Corner,
                                                    memo.corners))
                              .toHtmlEscaped(),
                          memo.parity() ? tr(", with parity") : QString())));
  }
  showHint();
}

void BlindPage::cancelSolve() {
  phase_ = Phase::Ready;
  promptLabel_->clear();
  updateControls();
  display_->showTime(0);
  resultLabel_->setText(tr("Solve cancelled; nothing was recorded. The "
                           "next scramble is a new one."));
  showScramble(); // the memo was seen: on to the feed's next one
  applyView();
  showHint();
}

void BlindPage::recordSolve(std::int64_t timeMs, std::int64_t memoMs, bool dnf,
                            const std::string &solution) {
  BlindAttempt a;
  a.timeMs = timeMs;
  a.memoMs = std::min(memoMs, timeMs);
  a.dnf = dnf;
  a.scramble = used_ ? ss(used_->text) : std::string();
  a.solution = solution;
  a.timestamp = unixNow();
  session().attempts.push_back(a);
  justRecorded_ = {store_.currentIndex(), a.timestamp};
  display_->showTime(timeMs, dnf ? Penalty::Dnf : Penalty::None);
  save();
  refreshLeft();
}

void BlindPage::next() {
  phase_ = Phase::Ready;
  justRecorded_.reset();
  resultLabel_->clear();
  promptLabel_->clear();
  cubeView_->clearMarks();
  showScramble(); // the feed moved on when the memo started
  applyView();
  showHint();
}

void BlindPage::onTick() {
  bool tracing = drill_ == Drill::Tracing && phase_ == Phase::Memo;
  if (!isBusy() && !tracing) {
    updateControls();
    return;
  }
  std::int64_t now = monotonicMs() - startMs_;
  display_->showText(timeText(now, 1));
  if (tracing) {
    return;
  }
  if (phase_ == Phase::Executing) {
    promptLabel_->setText(
        tr("Memo: %1").arg(timeText(memoMs_, settings_.decimals)));
  } else {
    promptLabel_->clear();
  }
}

void BlindPage::refreshSessionMenu() {
  sessionMenu_->clear();
  auto *group = new QActionGroup(sessionMenu_);
  for (std::size_t i = 0; i < store_.sessions().size(); ++i) {
    const BlindSession &s = store_.sessions()[i];
    QAction *a = sessionMenu_->addAction(
        tr("%1 (%2)").arg(qs(s.name)).arg(s.attempts.size()), this,
        [this, i] { selectSession(i); });
    a->setCheckable(true);
    a->setChecked(i == store_.currentIndex());
    group->addAction(a);
  }
  sessionMenu_->addSeparator();
  sessionMenu_->addAction(tr("New session…"), this, &BlindPage::addSession);
  sessionMenu_->addAction(tr("Rename…"), this, &BlindPage::renameSession);
  sessionMenu_->addAction(tr("Delete…"), this, &BlindPage::deleteSession);
}

void BlindPage::selectSession(std::size_t index) {
  if (isBusy()) {
    return;
  }
  store_.setCurrentIndex(index);
  save();
  selectDrill(Drill::Solves);
}

void BlindPage::showAttemptMenu(const QPoint &pos) {
  if (drill_ != Drill::Solves) {
    return;
  }
  auto *item = table_->itemAt(pos);
  if (!item) {
    return;
  }
  int index = item->data(Qt::UserRole).toInt();
  auto &attempts = session().attempts;
  if (index < 0 || index >= static_cast<int>(attempts.size())) {
    return;
  }
  BlindAttempt &a = attempts[static_cast<std::size_t>(index)];
  QMenu menu(this);
  auto *dnf = menu.addAction("DNF");
  dnf->setCheckable(true);
  dnf->setChecked(a.dnf);
  auto *comment = menu.addAction(tr("Comment…"));
  menu.addSeparator();
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
  if (chosen == dnf) {
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
    attempts.erase(attempts.begin() + index);
  }
  save();
  refreshLeft();
}

void BlindPage::addSession() {
  bool ok = false;
  QString name = QInputDialog::getText(
      this, tr("New session"), tr("Name:"), QLineEdit::Normal,
      tr("Session %1").arg(store_.sessions().size() + 1), &ok);
  if (!ok || name.trimmed().isEmpty()) {
    return;
  }
  CubeKind cube = session().cube;
  store_.addSession(ss(name.trimmed())).cube = cube;
  selectSession(store_.currentIndex());
}

void BlindPage::renameSession() {
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

void BlindPage::deleteSession() {
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

void BlindPage::editScheme() {
  if (isBusy()) {
    return;
  }
  BldSchemeDialog dialog(scheme_, settings_.scheme, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  scheme_ = dialog.result();
  scheme_.save();
  // The cube, letters and setups shown may all have changed.
  selectDrill(drill_);
}

} // namespace vcube
