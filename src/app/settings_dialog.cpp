#include "app/settings_dialog.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace vcube {

namespace {
const std::array<const char *, 6> kFaceNames = {
    "U (top)", "R (right)", "F (front)", "D (bottom)", "L (left)", "B (back)"};
}

SettingsDialog::SettingsDialog(const AppSettings &settings, QWidget *parent)
    : QDialog(parent), settings_(settings) {
  setWindowTitle(tr("Settings"));
  auto *tabs = new QTabWidget(this);
  tabs_ = tabs;

  // General
  auto *general = new QWidget(tabs);
  auto *gForm = new QFormLayout(general);
  themeBox_ = new QComboBox(general);
  themeBox_->addItems({tr("Dark"), tr("Light")});
  themeBox_->setCurrentIndex(settings_.darkTheme ? 0 : 1);
  gForm->addRow(tr("Theme:"), themeBox_);
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
    b->setFixedSize(40, 28);
    colourButtons_[i] = b;
    colourRow->addWidget(b);
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
  colourRow->addWidget(resetColours);
  colourRow->addStretch();
  connect(resetColours, &QPushButton::clicked, this, [this] {
    settings_.scheme = ColourScheme::standard();
    refreshColours();
  });
  gForm->addRow(tr("Colour scheme (U R F D L B):"), colourRow);
  tabs->addTab(general, tr("General"));

  tabs->addTab(buildNeutralityTab(tabs), tr("Colour neutrality"));
  refreshColours();

  // Timer
  auto *timer = new QWidget(tabs);
  auto *tForm = new QFormLayout(timer);
  holdBox_ = new QComboBox(timer);
  for (int ms : {0, 300, 550, 1000}) {
    holdBox_->addItem(QString::number(ms / 1000.0, 'g', 3) + " s", ms);
  }
  holdBox_->setCurrentIndex(std::max(0, holdBox_->findData(settings_.holdMs)));
  tForm->addRow(tr("Hold Space before starting:"), holdBox_);
  inspectionBox_ = new QCheckBox(
      tr("15 s WCA inspection (+2 after 15 s, DNF after 17 s)"), timer);
  inspectionBox_->setChecked(settings_.inspection);
  tForm->addRow(tr("Inspection:"), inspectionBox_);
  decimalsSpin_ = new QSpinBox(timer);
  decimalsSpin_->setRange(1, 3);
  decimalsSpin_->setValue(settings_.decimals);
  tForm->addRow(tr("Decimal places:"), decimalsSpin_);
  tabs->addTab(timer, tr("Timer"));

  // Keyboard
  auto *keys = new QWidget(tabs);
  auto *kLayout = new QVBoxLayout(keys);
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
          [this] { setBinding(true); });
  connect(addButton, &QPushButton::clicked, this,
          [this] { setBinding(false); });
  // Enter changes the selected row, or adds a binding when none is.
  connect(moveEdit_, &QLineEdit::returnPressed, this,
          [this] { setBinding(selectedBinding() >= 0); });
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
  tabs->addTab(keys, tr("Keyboard"));

  // Learning
  auto *learning = new QWidget(tabs);
  auto *lForm = new QFormLayout(learning);
  newPerDaySpin_ = new QSpinBox(learning);
  newPerDaySpin_->setRange(0, 100);
  newPerDaySpin_->setValue(settings_.srs.newPerDay);
  lForm->addRow(tr("New cases per day (from the learn queue):"),
                newPerDaySpin_);
  learningEdit_ =
      new QLineEdit(stepsToText(settings_.srs.learningSteps), learning);
  learningEdit_->setToolTip(
      tr("Delays before a new case's next reviews, e.g. \"1m 10m\""));
  lForm->addRow(tr("Learning steps:"), learningEdit_);
  relearningEdit_ =
      new QLineEdit(stepsToText(settings_.srs.relearningSteps), learning);
  lForm->addRow(tr("Relearning steps (after a miss):"), relearningEdit_);
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
  tabs->addTab(learning, tr("Learning"));

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(tabs);
  layout->addWidget(buttons);
  resize(620, 520);
}

void SettingsDialog::refreshColours() {
  for (std::size_t i = 0; i < 6; ++i) {
    colourButtons_[i]->setStyleSheet(
        QString(
            "background-color: %1; border: 1px solid #222; border-radius: 3px;")
            .arg(settings_.scheme.faces[i].name()));
  }
  refreshNeutrality();
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
         "front. Choose the holds you solve in: every scramble in Timer, "
         "Scramble and Virtual, and every case in Learn and Drill, is set up "
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
    frontCell->addWidget(front);
    frontCell->addWidget(frontName);
    frontCell->addStretch();
    grid->addLayout(frontCell, 0, c + 1);
    auto *topCell = new QHBoxLayout;
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

void SettingsDialog::setBinding(bool changeSelected) {
  QKeySequence seq = keyEdit_->keySequence();
  Alg move = parseAlgOrEmpty(moveEdit_->text().toStdString());
  if (seq.isEmpty() || move.size() != 1) {
    QMessageBox::information(this, tr("Keyboard"),
                             tr("Enter one key and a single move."));
    return;
  }
  QKeyCombination combo = seq[0];
  Qt::Key key = combo.key();
  if (key == Qt::Key_Space || key == Qt::Key_Escape ||
      key == Qt::Key_Backspace || key == Qt::Key_Tab || key == Qt::Key_Return ||
      key == Qt::Key_Enter) {
    QMessageBox::information(
        this, tr("Keyboard"),
        tr("Space, Esc, Backspace, Tab and Enter are reserved for the "
           "modes' own controls."));
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

void SettingsDialog::accept() {
  auto learn = stepsFromText(learningEdit_->text());
  auto relearn = stepsFromText(relearningEdit_->text());
  if (learn.empty() || relearn.empty()) {
    QMessageBox::warning(this, tr("Settings"),
                         tr("Learning steps must be durations like \"1m 10m\" "
                            "or \"30s 5m 1h\"."));
    return;
  }
  settings_.darkTheme = themeBox_->currentIndex() == 0;
  settings_.animationMs = animationSpin_->value();
  settings_.holdMs = holdBox_->currentData().toInt();
  settings_.inspection = inspectionBox_->isChecked();
  settings_.decimals = decimalsSpin_->value();
  settings_.srs.newPerDay = newPerDaySpin_->value();
  settings_.srs.learningSteps = learn;
  settings_.srs.relearningSteps = relearn;
  QDialog::accept();
}

} // namespace vcube
