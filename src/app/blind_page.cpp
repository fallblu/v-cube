#include "app/blind_page.hpp"
#include "app/page_parts.hpp"

#include "app/blind_quiz_drill.hpp"
#include "app/blind_setup_drill.hpp"
#include "app/blind_solves_drill.hpp"
#include "app/blind_tracing_drill.hpp"
#include "app/cube_view.hpp"
#include "app/flow_layout.hpp"
#include "app/session_panel.hpp"
#include "app/ui_state.hpp"
#include "app/widgets.hpp"

#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabBar>
#include <QVBoxLayout>

#include <algorithm>

namespace vcube {

BlindPage::BlindPage(AppSettings &settings, const QString &dataDir,
                     QWidget *parent)
    : ModePage(parent), settings_(settings),
      store_(QDir(dataDir).filePath("blind.json")),
      scheme_(BlindScheme::load()), noSets_(QString(), QString()),
      feed_(noSets_), rng_(makeRng()),
      context_(settings_, store_, scheme_, feed_, rng_, this,
               [this] { save(); }) {
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

  // --- the middle: scramble, prompt, cube, time, hint, result ---
  auto *right = new QWidget(this);
  auto *rightLayout = new QVBoxLayout(right);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  cubeView_ = new CubeView(right);
  cubeView_->setMinimumSize(260, 200);
  scrambleLabel_ = new QLabel(right);
  scrambleLabel_->setWordWrap(true);
  scrambleLabel_->setAlignment(Qt::AlignCenter);
  scrambleLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  QFont sf = scrambleLabel_->font();
  sf.setPointSize(17);
  scrambleLabel_->setFont(sf);
  markScrambleLabel(scrambleLabel_);
  net_ = new NetPreview(right);
  auto *prompt = new QLabel(right);
  prompt->setAlignment(Qt::AlignCenter);
  prompt->setWordWrap(true);
  prompt->setTextFormat(Qt::RichText);
  QFont pf = prompt->font();
  pf.setPointSizeF(pf.pointSizeF() * 1.3);
  prompt->setFont(pf);
  auto *display = new TimeDisplay(right);
  display->setTextFormat(Qt::PlainText); // letters may hold < or &
  auto *hint = new HintBar(right);
  auto *result = new QLabel(right);
  result->setTextFormat(Qt::RichText);
  result->setWordWrap(true);
  result->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
  result->setTextInteractionFlags(Qt::TextSelectableByMouse);
  context_.cube = cubeView_;
  context_.prompt = prompt;
  context_.display = display;
  context_.hint = hint;
  context_.result = result;
  context_.scramble = scrambleLabel_;
  context_.net = net_;

  // --- the drills ---
  auto *quiz = new QuizDrill(context_, this);
  auto *tracing = new TracingDrill(context_, this);
  auto *setups = new SetupDrill(context_, this);
  auto *solves = new SolvesDrill(context_, this);
  drills_ = {quiz, tracing, setups, solves};

  // --- left column: the drill's sessions or letters, stats, list ---
  leftPanel_ = new QWidget(this);
  auto *leftLayout = new QVBoxLayout(leftPanel_);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  for (BlindDrill *d : drills_) {
    leftLayout->addWidget(d->leftPanel());
  }

  // --- right column: options, scramble, middle, chart ---
  auto *options = new OptionBar(right);
  optionsRow_ = options;
  for (BlindDrill *d : drills_) {
    for (QWidget *w : d->options()) {
      options->addWidget(w);
    }
  }
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
  options->addWidget(viewPicker_);
  options->addStretch();
  options->addWidget(schemeButton_);
  rightLayout->addWidget(optionsRow_);

  auto *scrambleRow = new QHBoxLayout;
  scrambleRow->addWidget(scrambleLabel_, 1);
  scrambleRow->addWidget(net_);
  rightLayout->addLayout(scrambleRow);

  auto *centre = new QVBoxLayout;
  centre->addWidget(prompt);
  centre->addWidget(cubeView_, 8);
  centre->addWidget(display);
  centre->addWidget(hint);
  for (BlindDrill *d : drills_) {
    if (QWidget *row = d->inputRow()) {
      centre->addWidget(row);
    }
  }
  centre->addWidget(result, 2);
  rightLayout->addLayout(centre, 3);
  chart_ = solves->chart();
  rightLayout->addWidget(chart_, 1);

  auto *split = new QSplitter(this);
  split->addWidget(leftPanel_);
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
  for (BlindDrill *d : drills_) {
    connect(d, &BlindDrill::message, this, &BlindPage::message);
    connect(d, &BlindDrill::viewChanged, this, &BlindPage::applyView);
    connect(d, &BlindDrill::busyChanged, this, &BlindPage::updateControls);
    connect(d, &BlindDrill::restartRequested, this,
            [this] { selectDrill(drill_); });
  }
  connect(&feed_, &ScrambleFeed::currentChanged, this,
          [this] { current()->scrambleChanged(); });
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
  connect(viewBox_, &QComboBox::activated, this, &BlindPage::applyView);
  connect(schemeButton_, &QPushButton::clicked, this, &BlindPage::editScheme);
  connect(cubeView_, &CubeView::stickerClicked, this,
          [this](int facelet) { current()->stickerClicked(facelet); });

  feed_.setType("333"); // WCA scrambles held white top, green front
  applySettings();
  int drill = saved::blindDrill().get();
  selectDrill(static_cast<Drill>(std::clamp(drill, 0, 3)));
  current()->pause(); // until the page is shown
}

BlindDrill *BlindPage::current() const {
  return drills_[static_cast<std::size_t>(drill_)];
}

bool BlindPage::isBusy() const { return current()->isBusy(); }

void BlindPage::activated() {
  current()->resume();
  current()->activated();
}

void BlindPage::deactivated() {
  current()->pause();
  current()->deactivated();
}

void BlindPage::windowDeactivated() {
  // Nothing is held down here, so a running solve keeps going: a long
  // blind solve shouldn't be lost to a window losing the keyboard. A
  // letter's or a trace's clock stops until the window is back.
  current()->pause();
}

void BlindPage::changeEvent(QEvent *event) {
  if (event->type() == QEvent::ActivationChange && isActiveWindow() &&
      isVisible()) {
    current()->resume();
  }
  ModePage::changeEvent(event);
}

void BlindPage::focusModeChanged() {
  const bool on = focusMode();
  leftPanel_->setVisible(!on);
  optionsRow_->setVisible(!on);
  chart_->setVisible(!on && current()->layout().chart);
}

void BlindPage::applySettings() {
  net_->setScheme(settings_.scheme);
  CubeLook look = cubeView_->look();
  look.scheme = settings_.scheme;
  cubeView_->setLook(look);
  cubeView_->setColourMap(colourMapOf(scheme_.hold));
  cubeView_->setAnimationMs(settings_.animationMs);
  context_.display->setDecimals(settings_.decimals);
  current()->refreshLeft();
}

void BlindPage::save() {
  saveInBackground(store_, this, [this](const QString &error) {
    emit message(tr("Could not save blind data: %1").arg(error));
  });
}

void BlindPage::selectDrill(Drill d) {
  if (d != drill_ && isBusy()) {
    return;
  }
  bool changed = drill_ != d;
  if (changed) {
    current()->leave();
  }
  drill_ = d;
  saved::blindDrill().set(static_cast<int>(d));
  {
    QSignalBlocker block(drillTabs_);
    drillTabs_->setCurrentIndex(static_cast<int>(d));
  }
  cubeView_->clearMarks();
  cubeView_->setColourMap(colourMapOf(scheme_.hold));
  context_.result->clear();
  context_.prompt->clear();
  // Each drill brings its own left column, options and input row.
  for (BlindDrill *drill : drills_) {
    bool on = drill == current();
    drill->leftPanel()->setVisible(on);
    for (QWidget *w : drill->options()) {
      w->setVisible(on);
    }
    if (QWidget *row = drill->inputRow()) {
      row->setVisible(on);
    }
  }
  BlindDrill::Layout shown = current()->layout();
  chart_->setVisible(shown.chart && !focusMode());
  cubeView_->setVisible(shown.cube);
  viewPicker_->setVisible(shown.viewPicker);
  net_->setVisible(shown.net);
  scrambleLabel_->setVisible(shown.scramble);
  current()->enter(changed);
  applyView();
  current()->refreshLeft();
  updateControls();
  current()->showHint();
}

void BlindPage::applyView() {
  CubeView::Mode chosen =
      CubeView::modeFromName(viewBox_->currentData().toString())
          .value_or(CubeView::Mode::Flat);
  CubeView::Mode mode = chosen;
  BlindDrill::View view = current()->view();
  if (view.kind == BlindDrill::View::Kind::Net) {
    mode = CubeView::Mode::Net;
  } else if (view.kind == BlindDrill::View::Kind::Hidden) {
    mode = CubeView::Mode::Hidden;
    cubeView_->setHiddenText(view.hiddenText);
  }
  if (mode == CubeView::Mode::Hidden && chosen == CubeView::Mode::Hidden) {
    cubeView_->setHiddenText({});
  }
  cubeView_->setMode(mode);
}

void BlindPage::updateControls() {
  bool busy = isBusy();
  for (QWidget *w :
       std::initializer_list<QWidget *>{drillTabs_, schemeButton_, viewBox_}) {
    w->setEnabled(!busy);
  }
}

bool BlindPage::handleKey(QKeyEvent *e, bool press) {
  if (e->modifiers() &
      (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
    return false;
  }
  return current()->handleKey(e, press);
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
