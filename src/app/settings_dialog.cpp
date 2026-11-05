#include "app/settings_dialog.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
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
  refreshColours();
  tabs->addTab(general, tr("General"));

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
      new QLabel(tr("Virtual cube turning keys. Select a row to change it, or "
                    "enter a key and a move to add one."),
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
  auto *setButton = new QPushButton(tr("Set"), keys);
  auto *removeButton = new QPushButton(tr("Remove"), keys);
  auto *defaultsButton = new QPushButton(tr("cstimer defaults"), keys);
  editRow->addWidget(new QLabel(tr("Key:"), keys));
  editRow->addWidget(keyEdit_);
  editRow->addWidget(new QLabel(tr("Move:"), keys));
  editRow->addWidget(moveEdit_);
  editRow->addWidget(setButton);
  editRow->addWidget(removeButton);
  editRow->addWidget(defaultsButton);
  kLayout->addLayout(editRow);
  connect(keyTable_, &QTableWidget::currentCellChanged, this, [this](int row) {
    if (row >= 0 && row < static_cast<int>(settings_.keymap.bindings.size())) {
      const auto &b = settings_.keymap.bindings[static_cast<std::size_t>(row)];
      keyEdit_->setKeySequence(QKeySequence(Keymap::keyText(b.key, b.shift),
                                            QKeySequence::PortableText));
      moveEdit_->setText(QString::fromStdString(toString(b.move)));
    }
  });
  connect(setButton, &QPushButton::clicked, this, &SettingsDialog::setBinding);
  connect(moveEdit_, &QLineEdit::returnPressed, this,
          &SettingsDialog::setBinding);
  connect(removeButton, &QPushButton::clicked, this, [this] {
    int row = keyTable_->currentRow();
    if (row >= 0 && row < static_cast<int>(settings_.keymap.bindings.size())) {
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
  lForm->addRow(new QLabel(
      tr("Learn mode grades each case by correctness: solved from memory "
         "is Good, using Tab to see the algs or giving up is Again, and "
         "keys 1-4 change the grade afterwards. Graduated cases come back "
         "after days, spaced out further each time they go well. Drill mode "
         "never changes the schedule."),
      learning));
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
}

void SettingsDialog::setBinding() {
  QKeySequence seq = keyEdit_->keySequence();
  Alg move = parseAlgOrEmpty(moveEdit_->text().toStdString());
  if (seq.isEmpty() || move.size() != 1) {
    QMessageBox::information(this, tr("Keyboard"),
                             tr("Enter one key and a single move."));
    return;
  }
  QKeyCombination combo = seq[0];
  if (combo.key() == Qt::Key_Space || combo.key() == Qt::Key_Escape ||
      combo.key() == Qt::Key_Backspace || combo.key() == Qt::Key_Tab ||
      combo.key() == Qt::Key_Return) {
    QMessageBox::information(
        this, tr("Keyboard"),
        tr("Space, Esc, Backspace, Tab and Enter are reserved for the "
           "modes' own controls."));
    return;
  }
  KeyBinding b{combo.key(),
               combo.keyboardModifiers().testFlag(Qt::ShiftModifier),
               move.front()};
  auto &list = settings_.keymap.bindings;
  // Replace any binding for the same key.
  auto it = std::find_if(list.begin(), list.end(), [&b](const KeyBinding &x) {
    return x.key == b.key && x.shift == b.shift;
  });
  int row;
  if (it != list.end()) {
    *it = b;
    row = static_cast<int>(it - list.begin());
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
