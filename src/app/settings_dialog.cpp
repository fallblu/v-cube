#include "app/settings_dialog.hpp"
#include "app/colours.hpp"
#include "app/page_parts.hpp"
#include "app/text_style.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace vcube {

namespace {

// Moves the row of `field` to the end of another form.
void moveRow(QFormLayout *from, QFormLayout *to, QWidget *field) {
  QFormLayout::TakeRowResult row = from->takeRow(field);
  if (row.labelItem && row.labelItem->widget()) {
    to->addRow(row.labelItem->widget(), field);
  } else {
    to->addRow(field);
  }
  delete row.labelItem;
  delete row.fieldItem;
}

// A tab's contents in a scroll area: the forms are longer than the dialog
// on a small screen.
QScrollArea *scrolled(QWidget *page, QWidget *parent) {
  auto *scroll = new QScrollArea(parent);
  scroll->setWidget(page);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  // The rows wrap to the dialog's width instead.
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  return scroll;
}

// A form whose fields go under their labels when a row is too wide.
QFormLayout *wrappingForm(QWidget *page) {
  auto *form = new QFormLayout(page);
  form->setRowWrapPolicy(QFormLayout::WrapLongRows);
  return form;
}

const std::array<const char *, 6> kFaceNames = {
    "U (top)", "R (right)", "F (front)", "D (bottom)", "L (left)", "B (back)"};
const std::array<const char *, 6> kFaceLetters = {"U", "R", "F", "D", "L", "B"};
} // namespace

SettingsDialog::SettingsDialog(const AppSettings &settings, QWidget *parent)
    : QDialog(parent), settings_(settings) {
  setWindowTitle(tr("Settings"));
  auto *tabs = new QTabWidget(this);
  tabs_ = tabs;

  // General
  auto *general = new QWidget(tabs);
  auto *gForm = wrappingForm(general);
  themeBox_ = new QComboBox(general);
  themeBox_->addItems({tr("Dark"), tr("Light")});
  themeBox_->setCurrentIndex(settings_.darkTheme ? 0 : 1);
  gForm->addRow(tr("Theme:"), themeBox_);
  interfaceTextBox_ = new QComboBox(general);
  for (int percent : {100, 108, 117, 125, 133}) {
    QString px = tr("%1 px").arg(interfaceTextPixels(percent));
    interfaceTextBox_->addItem(percent == 100 ? tr("Normal (%1)").arg(px) : px,
                               percent);
  }
  if (interfaceTextBox_->findData(settings_.interfaceTextPercent) < 0) {
    interfaceTextBox_->addItem(tr("%1%").arg(settings_.interfaceTextPercent),
                               settings_.interfaceTextPercent);
  }
  interfaceTextBox_->setCurrentIndex(
      interfaceTextBox_->findData(settings_.interfaceTextPercent));
  interfaceTextBox_->setToolTip(
      tr("The size of the text everywhere: lists, buttons, menus, this dialog "
         "and the help panel. The sizes below are on top of it."));
  gForm->addRow(tr("Interface text size:"), interfaceTextBox_);
  animationSpin_ = new QSpinBox(general);
  animationSpin_->setRange(0, 500);
  animationSpin_->setSuffix(" ms");
  animationSpin_->setSpecialValueText(tr("Off"));
  animationSpin_->setValue(settings_.animationMs);
  gForm->addRow(tr("Virtual cube turn speed:"), animationSpin_);
  auto *colourRow = new QHBoxLayout;
  for (std::size_t i = 0; i < 6; ++i) {
    auto *b = new QPushButton(general);
    b->setToolTip(tr("Colour of the %1 face in the WCA orientation")
                      .arg(tr(kFaceNames[i])));
    b->setFixedSize(scaledToText(40), scaledToText(28));
    b->setAccessibleName(tr("%1 face colour").arg(tr(kFaceNames[i])));
    colourButtons_[i] = b;
    // The face's letter under its swatch.
    auto *cell = new QVBoxLayout;
    cell->setSpacing(2);
    cell->addWidget(b);
    auto *letter = new QLabel(kFaceLetters[i], general);
    letter->setAlignment(Qt::AlignHCenter);
    letter->setForegroundRole(QPalette::PlaceholderText);
    cell->addWidget(letter);
    colourRow->addLayout(cell);
    connect(b, &QPushButton::clicked, this, [this, i] {
      QColor c = QColorDialog::getColor(settings_.scheme.faces[i], this,
                                        tr("Sticker colour"));
      if (c.isValid()) {
        settings_.scheme.faces[i] = c;
        refreshColours();
      }
    });
  }
  auto *resetColours = new QPushButton(tr("Reset"), general);
  colourRow->addWidget(resetColours, 0, Qt::AlignTop);
  colourRow->addStretch();
  connect(resetColours, &QPushButton::clicked, this, [this] {
    settings_.scheme = ColourScheme::standard();
    refreshColours();
  });
  gForm->addRow(tr("Colour scheme:"), colourRow);
  scrambleFontSpin_ = new QSpinBox(general);
  scrambleFontSpin_->setRange(AppSettings::kMinScrambleFontPt,
                              AppSettings::kMaxScrambleFontPt);
  scrambleFontSpin_->setSuffix(" pt");
  scrambleFontSpin_->setValue(settings_.scrambleFontPt);
  gForm->addRow(tr("Scramble text size:"), scrambleFontSpin_);
  // A choice of a few sizes, keeping one set by hand in the file.
  auto choice = [general](const std::vector<std::pair<QString, int>> &items,
                          int value, const QString &custom) {
    auto *box = new QComboBox(general);
    for (const auto &[label, v] : items) {
      box->addItem(label, v);
    }
    if (box->findData(value) < 0) {
      box->addItem(custom.arg(value), value);
    }
    box->setCurrentIndex(box->findData(value));
    return box;
  };
  hintTextBox_ = choice({{tr("Normal"), 100},
                         {tr("Large"), 120},
                         {tr("Larger"), 145},
                         {tr("Largest"), 175}},
                        settings_.hintTextPercent, tr("%1%"));
  hintTextBox_->setToolTip(
      tr("The size of the hints under the timer and cube, and of messages"));
  gForm->addRow(tr("Hint and message text:"), hintTextBox_);
  thumbnailBox_ =
      choice({{tr("Small"), 38}, {tr("Medium"), 56}, {tr("Large"), 76}},
             settings_.thumbnailSize, tr("%1 px"));
  thumbnailBox_->setToolTip(
      tr("The size of the case pictures in the Learn and Drill lists"));
  gForm->addRow(tr("Case pictures:"), thumbnailBox_);
  tabs->addTab(scrolled(general, tabs), tr("Appearance"));

  tabs->addTab(buildNeutralityTab(tabs), tr("Colour neutrality"));
  refreshColours();

  // Timer
  auto *timer = new QWidget(tabs);
  auto *tForm = wrappingForm(timer);
  holdSpin_ = new QSpinBox(timer);
  holdSpin_->setRange(0, AppSettings::kMaxHoldMs);
  holdSpin_->setSingleStep(50);
  holdSpin_->setSuffix(" ms");
  holdSpin_->setSpecialValueText(tr("None"));
  holdSpin_->setValue(settings_.holdMs);
  holdSpin_->setToolTip(tr("How long Space must be held before the timer "
                           "can start in Timed, Untimed and Move Count "
                           "(cstimer uses 300 ms, a Stackmat 550 ms)"));
  tForm->addRow(tr("Hold to start:"), holdSpin_);
  inspectionBox_ = new QCheckBox(tr("15 s WCA inspection"), timer);
  inspectionBox_->setToolTip(tr("+2 after 15 s, DNF after 17 s"));
  inspectionBox_->setChecked(settings_.inspection);
  tForm->addRow(tr("Inspection:"), inspectionBox_);
  soundsBox_ = new QCheckBox(tr("Beep at 8 s and 12 s"), timer);
  soundsBox_->setToolTip(
      tr("Once at 8 s and twice at 12 s, like a judge's calls"));
  soundsBox_->setChecked(settings_.inspectionSounds);
  tForm->addRow(tr("Inspection calls:"), soundsBox_);
  decimalsSpin_ = new QSpinBox(timer);
  decimalsSpin_->setRange(1, 3);
  decimalsSpin_->setValue(settings_.decimals);
  tForm->addRow(tr("Decimal places:"), decimalsSpin_);
  timerFontSpin_ = new QSpinBox(timer);
  timerFontSpin_->setRange(AppSettings::kMinTimerFontPt,
                           AppSettings::kMaxTimerFontPt);
  timerFontSpin_->setSuffix(" pt");
  timerFontSpin_->setValue(settings_.timerFontPt);
  tForm->addRow(tr("Timer text size:"), timerFontSpin_);
  autoStopBox_ = new QCheckBox(tr("Stop when the cube is solved"), timer);
  autoStopBox_->setToolTip(
      tr("Timed and Untimed. Off: Space ends the solve, and a cube that isn't "
         "solved then is a DNF"));
  autoStopBox_->setChecked(settings_.autoStop);
  tForm->addRow(tr("Virtual cube solves:"), autoStopBox_);
  hideCubeBox_ =
      new QCheckBox(tr("Hide the scrambled cube until inspection"), timer);
  hideCubeBox_->setToolTip(
      tr("Timed, with inspection on: the scramble can't be studied before"));
  hideCubeBox_->setChecked(settings_.hideBeforeInspection);
  tForm->addRow(QString(), hideCubeBox_);
  simplifiedBox_ = new QCheckBox(tr("Count moves simplified"), timer);
  simplifiedBox_->setToolTip(
      tr("U D U' is 1 move, R x R' none. Off: every turn counts as you did "
         "it. The solution is kept as you did it either way."));
  simplifiedBox_->setChecked(settings_.simplifiedCounts);
  tForm->addRow(tr("Move counts:"), simplifiedBox_);
  anyHoldBox_ = new QCheckBox(tr("The optimum in any hold"), timer);
  anyHoldBox_->setToolTip(
      tr("As the step is checked. Off: the optimum in the holds of your "
         "colour neutrality (either EO axis)"));
  anyHoldBox_->setChecked(settings_.optimalAnyHold);
  tForm->addRow(tr("Move Count's Opt:"), anyHoldBox_);
  // What is about text and looks goes with the appearance.
  moveRow(tForm, gForm, timerFontSpin_);
  moveRow(tForm, gForm, decimalsSpin_);
  tabs->addTab(scrolled(timer, tabs), tr("Timing and solves"));

  // Keyboard
  auto *keys = new QWidget(tabs);
  auto *kLayout = new QVBoxLayout(keys);
  auto *cubeForm = new QFormLayout;
  kLayout->addLayout(cubeForm);
  moveRow(gForm, cubeForm, animationSpin_);
  kLayout->addWidget(
      new QLabel(tr("Virtual cube turning keys. Select a row to change its "
                    "key or move, or enter a key and a move to add one."),
                 keys));
  keyTable_ = new QTableWidget(0, 2, keys);
  keyTable_->setHorizontalHeaderLabels({tr("Key"), tr("Move")});
  keyTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  keyTable_->verticalHeader()->setVisible(false);
  keyTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  keyTable_->setSelectionMode(QAbstractItemView::SingleSelection);
  keyTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  kLayout->addWidget(keyTable_, 1);
  auto *editRow = new QHBoxLayout;
  keyEdit_ = new QKeySequenceEdit(keys);
  keyEdit_->setMaximumSequenceLength(1);
  moveEdit_ = new QLineEdit(keys);
  moveEdit_->setPlaceholderText(tr("Move, e.g. R' or M2 or y"));
  changeButton_ = new QPushButton(tr("Change"), keys);
  changeButton_->setToolTip(tr("Give the selected row this key and move"));
  auto *addButton = new QPushButton(tr("Add"), keys);
  addButton->setToolTip(
      tr("Add this key and move, replacing any other move on the key"));
  removeButton_ = new QPushButton(tr("Remove"), keys);
  auto *defaultsButton = new QPushButton(tr("Defaults"), keys);
  editRow->addWidget(new QLabel(tr("Key:"), keys));
  editRow->addWidget(keyEdit_);
  editRow->addWidget(new QLabel(tr("Move:"), keys));
  editRow->addWidget(moveEdit_);
  editRow->addWidget(changeButton_);
  editRow->addWidget(addButton);
  editRow->addWidget(removeButton_);
  editRow->addWidget(defaultsButton);
  kLayout->addLayout(editRow);
  connect(keyTable_, &QTableWidget::currentCellChanged, this, [this](int row) {
    if (row >= 0 && row < static_cast<int>(settings_.keymap.bindings.size())) {
      const auto &b = settings_.keymap.bindings[static_cast<std::size_t>(row)];
      keyEdit_->setKeySequence(QKeySequence(Keymap::keyText(b.key, b.shift),
                                            QKeySequence::PortableText));
      moveEdit_->setText(QString::fromStdString(toString(b.move)));
    }
    updateKeyButtons();
  });
  connect(changeButton_, &QPushButton::clicked, this,
          [this] { setBinding(Bind::ChangeSelected); });
  connect(addButton, &QPushButton::clicked, this,
          [this] { setBinding(Bind::AddNew); });
  // Enter changes the selected row, or adds a binding when none is, and
  // must not go on to close the dialog (see eventFilter()).
  moveEdit_->installEventFilter(this);
  connect(removeButton_, &QPushButton::clicked, this, [this] {
    int row = selectedBinding();
    if (row >= 0) {
      settings_.keymap.bindings.erase(settings_.keymap.bindings.begin() + row);
      refreshKeys();
    }
  });
  connect(defaultsButton, &QPushButton::clicked, this, [this] {
    settings_.keymap = Keymap::defaultLayout();
    refreshKeys();
  });
  refreshKeys();
  tabs->addTab(keys, tr("Cube and keys"));

  // Learning
  auto *learning = new QWidget(tabs);
  auto *lForm = wrappingForm(learning);
  newPerDaySpin_ = new QSpinBox(learning);
  newPerDaySpin_->setRange(0, 100);
  newPerDaySpin_->setValue(settings_.srs.newPerDay);
  newPerDaySpin_->setToolTip(tr("Taken from the learn queue"));
  lForm->addRow(tr("New cases per day:"), newPerDaySpin_);
  learningEdit_ =
      new QLineEdit(stepsToText(settings_.srs.learningSteps), learning);
  learningEdit_->setToolTip(
      tr("Delays before a new case's next reviews, e.g. \"1m 10m\""));
  lForm->addRow(tr("Learning steps:"), learningEdit_);
  relearningEdit_ =
      new QLineEdit(stepsToText(settings_.srs.relearningSteps), learning);
  lForm->addRow(tr("Relearning steps (after a miss):"), relearningEdit_);
  const SrsConfig &srs = settings_.srs;
  auto days = [learning](double value) {
    auto *spin = new QDoubleSpinBox(learning);
    spin->setRange(1, 365);
    spin->setDecimals(1);
    spin->setSuffix(tr(" d"));
    spin->setValue(value);
    return spin;
  };
  graduatingSpin_ = days(srs.graduatingDays);
  graduatingSpin_->setToolTip(
      tr("The first review interval of a case that finishes its learning "
         "steps"));
  lForm->addRow(tr("Graduating interval:"), graduatingSpin_);
  easySpin_ = days(srs.easyDays);
  easySpin_->setToolTip(tr("The first review interval of a new case graded "
                           "Easy, and of a case marked as learned"));
  lForm->addRow(tr("Easy interval:"), easySpin_);
  lapseSpin_ = new QSpinBox(learning);
  lapseSpin_->setRange(0, 100);
  lapseSpin_->setSuffix(" %");
  lapseSpin_->setValue(static_cast<int>(std::lround(srs.lapseFactor * 100)));
  lapseSpin_->setToolTip(tr("How much of its interval a forgotten case keeps "
                            "once it is relearned"));
  lForm->addRow(tr("Interval kept after a miss:"), lapseSpin_);
  easeRecoverySpin_ = new QDoubleSpinBox(learning);
  easeRecoverySpin_->setRange(0, 0.3);
  easeRecoverySpin_->setSingleStep(0.05);
  easeRecoverySpin_->setDecimals(2);
  easeRecoverySpin_->setSpecialValueText(tr("Off"));
  easeRecoverySpin_->setValue(srs.easeRecovery);
  easeRecoverySpin_->setToolTip(
      tr("A Good review raises a case's ease by this much, up to where it "
         "started, so that a case once missed can be spaced out as far "
         "again (Off: only Easy raises it)"));
  lForm->addRow(tr("Ease regained per Good review:"), easeRecoverySpin_);
  lateBox_ =
      new QCheckBox(tr("A late review counts part of the delay"), learning);
  lateBox_->setChecked(srs.creditLateReviews);
  lateBox_->setToolTip(tr("A case reviewed days after it was due and still "
                          "remembered gets a longer interval, as in Anki"));
  lForm->addRow(tr("Late reviews:"), lateBox_);
  hardBox_ =
      new QCheckBox(tr("Hard moves on to the next learning step"), learning);
  hardBox_->setChecked(srs.hardAdvancesSteps);
  hardBox_->setToolTip(tr("Otherwise Hard (the next-move hint) repeats the "
                          "step. Hard never finishes the steps."));
  lForm->addRow(tr("Hard:"), hardBox_);
  maxLearningSpin_ = new QSpinBox(learning);
  maxLearningSpin_->setRange(0, 50);
  maxLearningSpin_->setSpecialValueText(tr("No limit"));
  maxLearningSpin_->setValue(srs.maxLearning);
  maxLearningSpin_->setToolTip(
      tr("New cases wait while this many are in their learning steps, so "
         "that walkthroughs don't come back to back"));
  lForm->addRow(tr("New cases learning at once:"), maxLearningSpin_);
  learnAheadSpin_ = new QSpinBox(learning);
  learnAheadSpin_->setRange(0, 60);
  learnAheadSpin_->setSuffix(tr(" min"));
  learnAheadSpin_->setSpecialValueText(tr("Off"));
  learnAheadSpin_->setValue(static_cast<int>(srs.learnAheadSeconds / 60));
  learnAheadSpin_->setToolTip(
      tr("With nothing else to do, cases due this soon in their learning "
         "steps come up early. Space on the idle "
         "screen always takes the next one early."));
  lForm->addRow(tr("Learn ahead:"), learnAheadSpin_);
  leechSpin_ = new QSpinBox(learning);
  leechSpin_->setRange(0, 50);
  leechSpin_->setSpecialValueText(tr("Off"));
  leechSpin_->setSuffix(tr(" misses"));
  leechSpin_->setValue(srs.leechLapses);
  leechSpin_->setToolTip(tr("A case forgotten this many times is flagged as "
                            "a leech in the case list"));
  lForm->addRow(tr("Leech after:"), leechSpin_);
  slowSpin_ = new QDoubleSpinBox(learning);
  slowSpin_->setRange(0.1, 30);
  slowSpin_->setDecimals(1);
  slowSpin_->setSingleStep(0.1);
  slowSpin_->setSuffix(" s");
  slowSpin_->setValue(settings_.slowRecognitionMs / 1000.0);
  slowSpin_->setToolTip(
      tr("Cases whose mean recognition time (from the case being set up to "
         "your first turn off U and D) is longer are slow to recognise"));
  lForm->addRow(tr("Slow to recognise over:"), slowSpin_);
  auto *learningNote = new QLabel(
      tr("Learn mode grades each case by correctness: solved from memory "
         "with one of its algs is Good, while using Tab to see the algs, "
         "giving up or leaving the algs is Again, and keys 1-4 change the "
         "grade afterwards. Graduated cases come back "
         "after days, spaced out further each time they go well. Drill mode "
         "never changes the schedule."),
      learning);
  learningNote->setWordWrap(true);
  lForm->addRow(learningNote);
  // The form is longer than the dialog on small screens.
  auto *learningScroll = new QScrollArea(tabs);
  learningScroll->setWidget(learning);
  learningScroll->setWidgetResizable(true);
  learningScroll->setFrameShape(QFrame::NoFrame);
  learningScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  tabs->addTab(learningScroll, tr("Learn"));

  drillTab_ = new DrillSettingsTab(settings_.drill, tabs);
  // Starting and stopping the timer is set in one place for every mode.
  int holdRow = 0;
  tForm->getWidgetPosition(holdSpin_, &holdRow, nullptr);
  drillTab_->moveTimingRows(tForm, holdRow);
  tabs->addTab(scrolled(drillTab_, tabs), tr("Drill"));

  // Tooltips are never shown (see MainWindow), so each option's
  // explanation is written under it.
  for (QFormLayout *form : {gForm, cubeForm, tForm, lForm, drillTab_->form()}) {
    showTipsAsHelp(form);
  }

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel,
      this);
  connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
  connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
          [this] {
            if (collect()) {
              emit applied(settings_);
            }
          });
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(tabs);
  layout->addWidget(buttons);
  resize(720, 640);
}

void SettingsDialog::refreshColours() {
  styleColourButtons();
  refreshNeutrality();
}

void SettingsDialog::styleColourButtons() {
  for (std::size_t i = 0; i < 6; ++i) {
    // The edge in the field edge colour, and states drawn by hand: the
    // stylesheet takes Fusion's away, and the focus has to show.
    const QColor face = settings_.scheme.faces[i];
    colourButtons_[i]->setStyleSheet(
        QString("QPushButton { background-color: %1; border: 1px solid %2; "
                "border-radius: 3px; }"
                "QPushButton:hover { background-color: %3; }"
                "QPushButton:pressed { background-color: %4; }"
                "QPushButton:focus { border: 2px solid palette(highlight); }")
            .arg(face.name(), roleColour(Role::FieldEdge).name(),
                 face.lighter(112).name(), face.darker(115).name()));
    colourButtons_[i]->setAccessibleDescription(
        settings_.scheme.faces[i].name());
  }
}

void SettingsDialog::showNeutralityTab() { tabs_->setCurrentIndex(1); }

// --- colour neutrality
// --------------------------------------------------------

namespace {

struct PresetLabel {
  Neutrality n;
  const char *text;
};

const PresetLabel kPresets[] = {
    {Neutrality::Fixed,
     QT_TRANSLATE_NOOP("vcube::SettingsDialog", "Fixed: the main hold only")},
    {Neutrality::Y2, QT_TRANSLATE_NOOP("vcube::SettingsDialog",
                                       "y2: front colour or its opposite")},
    {Neutrality::Y,
     QT_TRANSLATE_NOOP("vcube::SettingsDialog", "y: any front colour")},
    {Neutrality::X2, QT_TRANSLATE_NOOP("vcube::SettingsDialog",
                                       "x2: top colour or its opposite")},
    {Neutrality::X2Y2,
     QT_TRANSLATE_NOOP("vcube::SettingsDialog",
                       "x2 y2: top or its opposite, front or its opposite")},
    {Neutrality::X2Y,
     QT_TRANSLATE_NOOP("vcube::SettingsDialog",
                       "x2 y: top or its opposite, any front (dual colour)")},
    {Neutrality::Full, QT_TRANSLATE_NOOP("vcube::SettingsDialog",
                                         "Full: any top and front colour")},
};

constexpr int kCustomPreset = -1;

QPixmap swatch(const QColor &c, int size = 14) {
  QPixmap p(size, size);
  p.fill(c);
  return p;
}

} // namespace

QWidget *SettingsDialog::buildNeutralityTab(QWidget *parent) {
  auto *page = new QWidget(parent);
  auto *layout = new QVBoxLayout(page);
  auto *intro = new QLabel(
      tr("Scrambles are always applied holding white on top and green in "
         "front. Choose the holds you solve in: every scramble in Timed, "
         "Untimed and Move Count, and every case in Learn and Drill, is set "
         "up "
         "for one of the ticked holds, picked at random. For example, an "
         "EOCross scramble held x2 y neutral gives a white or yellow cross "
         "with any front colour."),
      page);
  intro->setWordWrap(true);
  layout->addWidget(intro);

  auto *form = new QFormLayout;
  auto *mainRow = new QHBoxLayout;
  mainTopBox_ = new QComboBox(page);
  mainFrontBox_ = new QComboBox(page);
  for (int c = 0; c < 6; ++c) {
    mainTopBox_->addItem(colourName(c), c);
  }
  mainRow->addWidget(new QLabel(tr("Top:"), page));
  mainRow->addWidget(mainTopBox_);
  mainRow->addSpacing(8);
  mainRow->addWidget(new QLabel(tr("Front:"), page));
  mainRow->addWidget(mainFrontBox_);
  mainRow->addStretch();
  auto *mainLabel = new QLabel(tr("Main hold:"), page);
  mainLabel->setToolTip(tr("Case diagrams are drawn in this hold, and the "
                           "neutrality presets start from it"));
  form->addRow(mainLabel, mainRow);
  presetBox_ = new QComboBox(page);
  for (const auto &p : kPresets) {
    presetBox_->addItem(tr(p.text), static_cast<int>(p.n));
  }
  presetBox_->addItem(tr("Custom: the holds ticked below"), kCustomPreset);
  form->addRow(tr("Neutrality:"), presetBox_);
  layout->addLayout(form);

  // One tick box per hold: rows by top colour, columns by front colour.
  auto *grid = new QGridLayout;
  grid->setHorizontalSpacing(14);
  grid->addWidget(new QLabel(tr("Top \\ front"), page), 0, 0);
  for (int c = 0; c < 6; ++c) {
    auto *front = new QLabel(page);
    auto *top = new QLabel(page);
    holdSwatches_[static_cast<std::size_t>(c)] = front;
    holdSwatches_[static_cast<std::size_t>(6 + c)] = top;
    auto *frontName = new QLabel(colourName(c), page);
    auto *frontCell = new QHBoxLayout;
    frontCell->setSpacing(6);
    frontCell->addWidget(front);
    frontCell->addWidget(frontName);
    frontCell->addStretch();
    grid->addLayout(frontCell, 0, c + 1);
    auto *topCell = new QHBoxLayout;
    topCell->setSpacing(6);
    topCell->addWidget(top);
    topCell->addWidget(new QLabel(colourName(c), page));
    topCell->addStretch();
    grid->addLayout(topCell, c + 1, 0);
  }
  for (Hold h : allHolds()) {
    auto *box = new QCheckBox(page);
    box->setToolTip(tr("%1 top, %2 front")
                        .arg(colourName(h.top), colourName(h.front).toLower()));
    holdBoxes_[static_cast<std::size_t>(h.top * 6 + h.front)] = box;
    grid->addWidget(box, h.top + 1, h.front + 1, Qt::AlignCenter);
    connect(box, &QCheckBox::toggled, this, [this, h](bool on) {
      auto &holds = settings_.neutrality.holds;
      if (on) {
        holds.push_back(h);
      } else {
        std::erase(holds, h);
        if (holds.empty()) {
          holds.push_back(h); // at least one hold
        }
      }
      settings_.neutrality.normalize();
      refreshNeutrality();
    });
  }
  auto *gridBox = new QGroupBox(tr("Holds"), page);
  gridBox->setLayout(grid);
  auto *gridRow = new QHBoxLayout;
  gridRow->addWidget(gridBox);
  gridRow->addStretch();
  layout->addLayout(gridRow);
  holdSummary_ = new QLabel(page);
  holdSummary_->setWordWrap(true);
  holdSummary_->setTextFormat(Qt::RichText);
  layout->addWidget(holdSummary_);
  layout->addStretch();

  connect(mainTopBox_, &QComboBox::activated, this, [this] {
    settings_.neutrality.main.top = mainTopBox_->currentData().toInt();
    if (!isValidHold(settings_.neutrality.main)) {
      // Keep the front if it fits, else the first colour that does.
      for (int f = 0; f < 6; ++f) {
        if (isValidHold({settings_.neutrality.main.top, f})) {
          settings_.neutrality.main.front = f;
          break;
        }
      }
    }
    fillMainFronts();
    if (int p = presetBox_->currentData().toInt(); p != kCustomPreset) {
      settings_.neutrality = ColourNeutrality::preset(
          settings_.neutrality.main, static_cast<Neutrality>(p));
    }
    refreshNeutrality();
  });
  connect(mainFrontBox_, &QComboBox::activated, this, [this] {
    settings_.neutrality.main.front = mainFrontBox_->currentData().toInt();
    if (int p = presetBox_->currentData().toInt(); p != kCustomPreset) {
      settings_.neutrality = ColourNeutrality::preset(
          settings_.neutrality.main, static_cast<Neutrality>(p));
    }
    refreshNeutrality();
  });
  connect(presetBox_, &QComboBox::activated, this, [this] {
    if (int p = presetBox_->currentData().toInt(); p != kCustomPreset) {
      settings_.neutrality = ColourNeutrality::preset(
          settings_.neutrality.main, static_cast<Neutrality>(p));
    }
    refreshNeutrality();
  });
  fillMainFronts();
  return page;
}

void SettingsDialog::fillMainFronts() {
  QSignalBlocker block(mainFrontBox_);
  mainFrontBox_->clear();
  for (int f = 0; f < 6; ++f) {
    if (isValidHold({settings_.neutrality.main.top, f})) {
      mainFrontBox_->addItem(colourName(f), f);
    }
  }
}

void SettingsDialog::refreshNeutrality() {
  const ColourNeutrality &n = settings_.neutrality;
  for (int c = 0; c < 6; ++c) {
    QPixmap p = swatch(settings_.scheme.colourOfSlot(c));
    mainTopBox_->setItemIcon(c, p);
    holdSwatches_[static_cast<std::size_t>(c)]->setPixmap(p);
    holdSwatches_[static_cast<std::size_t>(6 + c)]->setPixmap(p);
  }
  for (int i = 0; i < mainFrontBox_->count(); ++i) {
    mainFrontBox_->setItemIcon(i, swatch(settings_.scheme.colourOfSlot(
                                      mainFrontBox_->itemData(i).toInt())));
  }
  mainTopBox_->setCurrentIndex(mainTopBox_->findData(n.main.top));
  mainFrontBox_->setCurrentIndex(mainFrontBox_->findData(n.main.front));
  std::optional<Neutrality> preset = n.matchingPreset();
  presetBox_->setCurrentIndex(
      presetBox_->findData(preset ? static_cast<int>(*preset) : kCustomPreset));
  for (Hold h : allHolds()) {
    QCheckBox *box = holdBoxes_[static_cast<std::size_t>(h.top * 6 + h.front)];
    QSignalBlocker block(box);
    box->setChecked(std::find(n.holds.begin(), n.holds.end(), h) !=
                    n.holds.end());
  }
  QString summary = n.isFixed()
                        ? tr("Every scramble is set up for %1.")
                              .arg(holdHtml(n.holds.front(), settings_.scheme))
                        : tr("Each scramble is set up for one of %1 holds.")
                              .arg(n.holds.size());
  if (std::find(n.holds.begin(), n.holds.end(), n.main) == n.holds.end()) {
    summary += " " + tr("The main hold isn't ticked, so it is used for "
                        "diagrams only.");
  }
  holdSummary_->setText(summary);
}

int SettingsDialog::selectedBinding() const {
  int row = keyTable_->currentRow();
  bool selected = row >= 0 &&
                  row < static_cast<int>(settings_.keymap.bindings.size()) &&
                  keyTable_->selectionModel()->isRowSelected(row);
  return selected ? row : -1;
}

void SettingsDialog::updateKeyButtons() {
  bool selected = selectedBinding() >= 0;
  changeButton_->setEnabled(selected);
  removeButton_->setEnabled(selected);
}

void SettingsDialog::refreshKeys() {
  keyTable_->setRowCount(static_cast<int>(settings_.keymap.bindings.size()));
  for (std::size_t i = 0; i < settings_.keymap.bindings.size(); ++i) {
    const auto &b = settings_.keymap.bindings[i];
    keyTable_->setItem(static_cast<int>(i), 0,
                       new QTableWidgetItem(Keymap::keyText(b.key, b.shift)));
    keyTable_->setItem(
        static_cast<int>(i), 1,
        new QTableWidgetItem(QString::fromStdString(toString(b.move))));
  }
  updateKeyButtons();
}

void SettingsDialog::setBinding(Bind bind) {
  const bool changeSelected = bind == Bind::ChangeSelected;
  QKeySequence seq = keyEdit_->keySequence();
  Alg move = parseAlgOrEmpty(moveEdit_->text().toStdString());
  if (seq.isEmpty() || move.size() != 1) {
    QMessageBox::information(this, tr("Keyboard"),
                             tr("Enter one key and a single move."));
    return;
  }
  QKeyCombination combo = seq[0];
  Qt::Key key = combo.key();
  if (QString why = Keymap::reservedReason(key); !why.isEmpty()) {
    QMessageBox::information(
        this, tr("Keyboard"),
        tr("%1 can't turn the cube: %2").arg(Keymap::keyText(key, false), why));
    return;
  }
  if (combo.keyboardModifiers() &
      (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
    QMessageBox::information(
        this, tr("Keyboard"),
        tr("Turning keys can be held with Shift, but not with Ctrl, Alt or "
           "Meta, which are left for shortcuts."));
    return;
  }
  // A key turns its layer once, so R3 is bound as R'.
  KeyBinding b{key,
               combo.keyboardModifiers().testFlag(Qt::ShiftModifier),
               {move.front().type, move.front().amount}};
  auto &list = settings_.keymap.bindings;
  int row = changeSelected ? selectedBinding() : -1;
  if (changeSelected && row < 0) {
    return;
  }
  // A key turns one move: any other binding for it goes.
  for (std::size_t i = list.size(); i-- > 0;) {
    if (static_cast<int>(i) != row && list[i].key == b.key &&
        list[i].shift == b.shift) {
      if (row < 0) {
        row = static_cast<int>(i); // adding: take that binding's place
        continue;
      }
      list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
      if (static_cast<int>(i) < row) {
        --row;
      }
    }
  }
  if (row >= 0) {
    list[static_cast<std::size_t>(row)] = b;
  } else {
    list.push_back(b);
    row = static_cast<int>(list.size()) - 1;
  }
  refreshKeys();
  keyTable_->setCurrentCell(row, 0);
}

void SettingsDialog::changeEvent(QEvent *event) {
  QDialog::changeEvent(event);
  // The swatch edges follow a theme applied with the dialog open.
  if (event->type() == QEvent::PaletteChange && colourButtons_[5]) {
    styleColourButtons();
  }
}

bool SettingsDialog::eventFilter(QObject *watched, QEvent *event) {
  if (watched == moveEdit_ && event->type() == QEvent::KeyPress) {
    auto *key = static_cast<QKeyEvent *>(event);
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
      setBinding(selectedBinding() >= 0 ? Bind::ChangeSelected : Bind::AddNew);
      return true; // used here: the OK button doesn't see it
    }
  }
  return QDialog::eventFilter(watched, event);
}

void SettingsDialog::accept() {
  if (collect()) {
    QDialog::accept();
  }
}

bool SettingsDialog::collect() {
  auto learn = stepsFromText(learningEdit_->text());
  auto relearn = stepsFromText(relearningEdit_->text());
  if (learn.empty() || relearn.empty()) {
    QMessageBox::warning(this, tr("Settings"),
                         tr("Learning steps must be durations like \"1m 10m\" "
                            "or \"30s 5m 1h\"."));
    return false;
  }
  settings_.darkTheme = themeBox_->currentIndex() == 0;
  settings_.interfaceTextPercent = interfaceTextBox_->currentData().toInt();
  settings_.animationMs = animationSpin_->value();
  settings_.scrambleFontPt = scrambleFontSpin_->value();
  settings_.hintTextPercent = hintTextBox_->currentData().toInt();
  settings_.thumbnailSize = thumbnailBox_->currentData().toInt();
  settings_.holdMs = holdSpin_->value();
  settings_.inspection = inspectionBox_->isChecked();
  settings_.inspectionSounds = soundsBox_->isChecked();
  settings_.decimals = decimalsSpin_->value();
  settings_.timerFontPt = timerFontSpin_->value();
  settings_.autoStop = autoStopBox_->isChecked();
  settings_.hideBeforeInspection = hideCubeBox_->isChecked();
  settings_.simplifiedCounts = simplifiedBox_->isChecked();
  settings_.optimalAnyHold = anyHoldBox_->isChecked();
  settings_.srs.newPerDay = newPerDaySpin_->value();
  settings_.srs.learningSteps = learn;
  settings_.srs.relearningSteps = relearn;
  settings_.srs.graduatingDays = graduatingSpin_->value();
  settings_.srs.easyDays = easySpin_->value();
  settings_.srs.lapseFactor = lapseSpin_->value() / 100.0;
  settings_.srs.easeRecovery = easeRecoverySpin_->value();
  settings_.srs.creditLateReviews = lateBox_->isChecked();
  settings_.srs.hardAdvancesSteps = hardBox_->isChecked();
  settings_.srs.maxLearning = maxLearningSpin_->value();
  settings_.srs.learnAheadSeconds = learnAheadSpin_->value() * 60;
  settings_.srs.leechLapses = leechSpin_->value();
  settings_.slowRecognitionMs =
      static_cast<int>(std::lround(slowSpin_->value() * 1000));
  settings_.drill = drillTab_->result();
  return true;
}

} // namespace vcube
