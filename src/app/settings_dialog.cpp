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

#include <type_traits>

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

template <typename Widget, typename T>
Widget *SettingsDialog::bound(Widget *widget, T &field) {
  if constexpr (std::is_same_v<Widget, QCheckBox>) {
    widget->setChecked(field);
    readBacks_.push_back([widget, &field] { field = widget->isChecked(); });
  } else if constexpr (std::is_same_v<Widget, QComboBox>) {
    widget->setCurrentIndex(widget->findData(field));
    readBacks_.push_back([widget, &field] {
      field = widget->currentData().template value<T>();
    });
  } else {
    widget->setValue(field);
    readBacks_.push_back([widget, &field] { field = widget->value(); });
  }
  return widget;
}

SettingsDialog::SettingsDialog(const AppSettings &settings, QWidget *parent)
    : QDialog(parent), settings_(settings) {
  setWindowTitle(tr("Settings"));
  auto *tabs = new QTabWidget(this);
  tabs_ = tabs;
  // A check box with a tooltip.
  auto checkBox = [this](const QString &text, const QString &tip, bool &field,
                         QWidget *page) {
    auto *box = new QCheckBox(text, page);
    box->setToolTip(tip);
    return bound(box, field);
  };

  // General
  auto *general = new QWidget(tabs);
  auto *gForm = wrappingForm(general);
  // A choice of a few sizes, keeping one set by hand in the file.
  auto choice = [this,
                 general](const std::vector<std::pair<QString, int>> &items,
                          int &value, const QString &custom) {
    auto *box = new QComboBox(general);
    for (const auto &[label, v] : items) {
      box->addItem(label, v);
    }
    if (box->findData(value) < 0) {
      box->addItem(custom.arg(value), value);
    }
    return bound(box, value);
  };
  auto *themeBox = new QComboBox(general);
  themeBox->addItem(tr("Dark"), true);
  themeBox->addItem(tr("Light"), false);
  gForm->addRow(tr("Theme:"), bound(themeBox, settings_.darkTheme));
  std::vector<std::pair<QString, int>> textSizes;
  for (int percent : {100, 108, 117, 125, 133}) {
    QString px = tr("%1 px").arg(interfaceTextPixels(percent));
    textSizes.emplace_back(percent == 100 ? tr("Normal (%1)").arg(px) : px,
                           percent);
  }
  auto *interfaceTextBox =
      choice(textSizes, settings_.interfaceTextPercent, tr("%1%"));
  interfaceTextBox->setToolTip(
      tr("The size of the text everywhere: lists, buttons, menus, this dialog "
         "and the help panel. The sizes below are on top of it."));
  gForm->addRow(tr("Interface text size:"), interfaceTextBox);
  auto *animationSpin = new QSpinBox(general);
  animationSpin->setRange(0, 500);
  animationSpin->setSuffix(" ms");
  animationSpin->setSpecialValueText(tr("Off"));
  gForm->addRow(tr("Virtual cube turn speed:"),
                bound(animationSpin, settings_.animationMs));
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
  auto *scrambleFontSpin = new QSpinBox(general);
  scrambleFontSpin->setRange(AppSettings::kMinScrambleFontPt,
                             AppSettings::kMaxScrambleFontPt);
  scrambleFontSpin->setSuffix(" pt");
  gForm->addRow(tr("Scramble text size:"),
                bound(scrambleFontSpin, settings_.scrambleFontPt));
  auto *hintTextBox = choice({{tr("Normal"), 100},
                              {tr("Large"), 120},
                              {tr("Larger"), 145},
                              {tr("Largest"), 175}},
                             settings_.hintTextPercent, tr("%1%"));
  hintTextBox->setToolTip(
      tr("The size of the hints under the timer and cube, and of messages"));
  gForm->addRow(tr("Hint and message text:"), hintTextBox);
  auto *thumbnailBox =
      choice({{tr("Small"), 38}, {tr("Medium"), 56}, {tr("Large"), 76}},
             settings_.thumbnailSize, tr("%1 px"));
  thumbnailBox->setToolTip(
      tr("The size of the case pictures in the Learn and Drill lists"));
  gForm->addRow(tr("Case pictures:"), thumbnailBox);
  tabs->addTab(scrolled(general, tabs), tr("Appearance"));

  tabs->addTab(buildNeutralityTab(tabs), tr("Colour neutrality"));
  refreshColours();

  // Timer
  auto *timer = new QWidget(tabs);
  auto *tForm = wrappingForm(timer);
  auto *holdSpin = new QSpinBox(timer);
  holdSpin->setRange(0, AppSettings::kMaxHoldMs);
  holdSpin->setSingleStep(50);
  holdSpin->setSuffix(" ms");
  holdSpin->setSpecialValueText(tr("None"));
  holdSpin->setToolTip(tr("How long Space must be held before the timer "
                          "can start in Timed, Untimed and Move Count "
                          "(cstimer uses 300 ms, a Stackmat 550 ms)"));
  tForm->addRow(tr("Hold to start:"), bound(holdSpin, settings_.holdMs));
  tForm->addRow(tr("Inspection:"), checkBox(tr("15 s WCA inspection"),
                                            tr("+2 after 15 s, DNF after 17 s"),
                                            settings_.inspection, timer));
  tForm->addRow(tr("Inspection calls:"),
                checkBox(tr("Beep at 8 s and 12 s"),
                         tr("Once at 8 s and twice at 12 s, like a judge's "
                            "calls"),
                         settings_.inspectionSounds, timer));
  auto *decimalsSpin = new QSpinBox(timer);
  decimalsSpin->setRange(1, 3);
  tForm->addRow(tr("Decimal places:"), bound(decimalsSpin, settings_.decimals));
  auto *timerFontSpin = new QSpinBox(timer);
  timerFontSpin->setRange(AppSettings::kMinTimerFontPt,
                          AppSettings::kMaxTimerFontPt);
  timerFontSpin->setSuffix(" pt");
  tForm->addRow(tr("Timer text size:"),
                bound(timerFontSpin, settings_.timerFontPt));
  tForm->addRow(tr("Virtual cube solves:"),
                checkBox(tr("Stop when the cube is solved"),
                         tr("Timed and Untimed. Off: Space ends the solve, "
                            "and a cube that isn't solved then is a DNF"),
                         settings_.autoStop, timer));
  tForm->addRow(QString(),
                checkBox(tr("Hide the scrambled cube until inspection"),
                         tr("Timed, with inspection on: the scramble can't "
                            "be studied before"),
                         settings_.hideBeforeInspection, timer));
  tForm->addRow(tr("Move counts:"),
                checkBox(tr("Count moves simplified"),
                         tr("U D U' is 1 move, R x R' none. Off: every turn "
                            "counts as you did it. The solution is kept as "
                            "you did it either way."),
                         settings_.simplifiedCounts, timer));
  tForm->addRow(tr("Move Count's Opt:"),
                checkBox(tr("The optimum in any hold"),
                         tr("As the step is checked. Off: the optimum in the "
                            "holds of your colour neutrality (either EO "
                            "axis)"),
                         settings_.optimalAnyHold, timer));
  // What is about text and looks goes with the appearance.
  moveRow(tForm, gForm, timerFontSpin);
  moveRow(tForm, gForm, decimalsSpin);
  tabs->addTab(scrolled(timer, tabs), tr("Timing and solves"));

  // Keyboard
  auto *keys = new QWidget(tabs);
  auto *kLayout = new QVBoxLayout(keys);
  auto *cubeForm = new QFormLayout;
  kLayout->addLayout(cubeForm);
  moveRow(gForm, cubeForm, animationSpin);
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
  SrsConfig &srs = settings_.srs;
  auto *newPerDaySpin = new QSpinBox(learning);
  newPerDaySpin->setRange(0, 100);
  newPerDaySpin->setToolTip(tr("Taken from the learn queue"));
  lForm->addRow(tr("New cases per day:"), bound(newPerDaySpin, srs.newPerDay));
  learningEdit_ = new QLineEdit(stepsToText(srs.learningSteps), learning);
  learningEdit_->setToolTip(
      tr("Delays before a new case's next reviews, e.g. \"1m 10m\""));
  lForm->addRow(tr("Learning steps:"), learningEdit_);
  relearningEdit_ = new QLineEdit(stepsToText(srs.relearningSteps), learning);
  lForm->addRow(tr("Relearning steps (after a miss):"), relearningEdit_);
  auto days = [this, learning](double &value) {
    auto *spin = new QDoubleSpinBox(learning);
    spin->setRange(1, 365);
    spin->setDecimals(1);
    spin->setSuffix(tr(" d"));
    return bound(spin, value);
  };
  auto *graduatingSpin = days(srs.graduatingDays);
  graduatingSpin->setToolTip(
      tr("The first review interval of a case that finishes its learning "
         "steps"));
  lForm->addRow(tr("Graduating interval:"), graduatingSpin);
  auto *easySpin = days(srs.easyDays);
  easySpin->setToolTip(tr("The first review interval of a new case graded "
                          "Easy, and of a case marked as learned"));
  lForm->addRow(tr("Easy interval:"), easySpin);
  lapseSpin_ = new QSpinBox(learning);
  lapseSpin_->setRange(0, 100);
  lapseSpin_->setSuffix(" %");
  lapseSpin_->setValue(static_cast<int>(std::lround(srs.lapseFactor * 100)));
  lapseSpin_->setToolTip(tr("How much of its interval a forgotten case keeps "
                            "once it is relearned"));
  lForm->addRow(tr("Interval kept after a miss:"), lapseSpin_);
  auto *easeRecoverySpin = new QDoubleSpinBox(learning);
  easeRecoverySpin->setRange(0, 0.3);
  easeRecoverySpin->setSingleStep(0.05);
  easeRecoverySpin->setDecimals(2);
  easeRecoverySpin->setSpecialValueText(tr("Off"));
  easeRecoverySpin->setToolTip(
      tr("A Good review raises a case's ease by this much, up to where it "
         "started, so that a case once missed can be spaced out as far "
         "again (Off: only Easy raises it)"));
  lForm->addRow(tr("Ease regained per Good review:"),
                bound(easeRecoverySpin, srs.easeRecovery));
  lForm->addRow(tr("Late reviews:"),
                checkBox(tr("A late review counts part of the delay"),
                         tr("A case reviewed days after it was due and still "
                            "remembered gets a longer interval, as in Anki"),
                         srs.creditLateReviews, learning));
  lForm->addRow(tr("Hard:"),
                checkBox(tr("Hard moves on to the next learning step"),
                         tr("Otherwise Hard (the next-move hint) repeats the "
                            "step. Hard never finishes the steps."),
                         srs.hardAdvancesSteps, learning));
  auto *maxLearningSpin = new QSpinBox(learning);
  maxLearningSpin->setRange(0, 50);
  maxLearningSpin->setSpecialValueText(tr("No limit"));
  maxLearningSpin->setToolTip(
      tr("New cases wait while this many are in their learning steps, so "
         "that walkthroughs don't come back to back"));
  lForm->addRow(tr("New cases learning at once:"),
                bound(maxLearningSpin, srs.maxLearning));
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
  auto *leechSpin = new QSpinBox(learning);
  leechSpin->setRange(0, 50);
  leechSpin->setSpecialValueText(tr("Off"));
  leechSpin->setSuffix(tr(" misses"));
  leechSpin->setToolTip(tr("A case forgotten this many times is flagged as "
                           "a leech in the case list"));
  lForm->addRow(tr("Leech after:"), bound(leechSpin, srs.leechLapses));
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
  tabs->addTab(scrolled(learning, tabs), tr("Learn"));

  // Starting and stopping the timer is set in one place for every mode:
  // Drill's rows go under the hold time.
  int holdRow = 0;
  tForm->getWidgetPosition(holdSpin, &holdRow, nullptr);
  drillTab_ = new DrillSettingsTab(settings_.drill, tForm, holdRow + 1, tabs);
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
    // The swatch with the colour's name.
    auto cell = [page, c](QLabel *swatch) {
      auto *row = new QHBoxLayout;
      row->setSpacing(6);
      row->addWidget(swatch);
      row->addWidget(new QLabel(colourName(c), page));
      row->addStretch();
      return row;
    };
    grid->addLayout(cell(front), 0, c + 1);
    grid->addLayout(cell(top), c + 1, 0);
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

  // The preset chosen (but Custom) from the main hold.
  auto applyPreset = [this] {
    if (int p = presetBox_->currentData().toInt(); p != kCustomPreset) {
      settings_.neutrality = ColourNeutrality::preset(
          settings_.neutrality.main, static_cast<Neutrality>(p));
    }
    refreshNeutrality();
  };
  connect(mainTopBox_, &QComboBox::activated, this, [this, applyPreset] {
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
    applyPreset();
  });
  connect(mainFrontBox_, &QComboBox::activated, this, [this, applyPreset] {
    settings_.neutrality.main.front = mainFrontBox_->currentData().toInt();
    applyPreset();
  });
  connect(presetBox_, &QComboBox::activated, this, applyPreset);
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
  for (const auto &readBack : readBacks_) {
    readBack();
  }
  settings_.srs.learningSteps = learn;
  settings_.srs.relearningSteps = relearn;
  settings_.srs.lapseFactor = lapseSpin_->value() / 100.0;
  settings_.srs.learnAheadSeconds = learnAheadSpin_->value() * 60;
  settings_.slowRecognitionMs =
      static_cast<int>(std::lround(slowSpin_->value() * 1000));
  settings_.drill = drillTab_->result();
  return true;
}

} // namespace vcube
