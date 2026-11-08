#include "app/set_properties_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

namespace vcube {

namespace {

struct Preset {
  const char *label;
  const char *free;
};

const std::array<Preset, 5> kPresets = {{
    {"Everything solved (PLL-like)", ""},
    {"Last-layer permutation free (OLL-like)", "LL:p"},
    {"LL permutation and corner orientation free (edge orientation)",
     "LL:p LLC:o"},
    {"LL edge permutation free (corner permutation)", "LLE:p"},
    {"Whole last layer free (F2L, last slot)", "LL:po"},
}};

QComboBox *maskCombo(QWidget *parent) {
  auto *c = new QComboBox(parent);
  c->addItem(QObject::tr("Full colour"), static_cast<int>(StickerMode::Full));
  c->addItem(QObject::tr("Top colour only"),
             static_cast<int>(StickerMode::Orientation));
  c->addItem(QObject::tr("Grey"), static_cast<int>(StickerMode::Hidden));
  return c;
}

QString qs(const std::string &s) { return QString::fromStdString(s); }

} // namespace

SetPropertiesDialog::SetPropertiesDialog(const AlgSet &set, int group,
                                         QWidget *parent)
    : QDialog(parent), set_(set) {
  setWindowTitle(tr("Set properties: %1").arg(qs(set.name)));
  auto *layout = new QVBoxLayout(this);

  auto *general = new QGroupBox(tr("Set"), this);
  auto *form = new QFormLayout(general);
  nameEdit_ = new QLineEdit(qs(set_.name), general);
  descriptionEdit_ = new QLineEdit(qs(set_.description), general);
  form->addRow(tr("Name:"), nameEdit_);
  form->addRow(tr("Description:"), descriptionEdit_);
  aufBeforeBox_ = new QCheckBox(tr("Random U turn before the case"), general);
  aufAfterBox_ = new QCheckBox(tr("Random U turn after the case"), general);
  aufBeforeBox_->setChecked(set_.aufBefore);
  aufAfterBox_->setChecked(set_.aufAfter);
  aufBeforeBox_->setToolTip(
      tr("Learn and Drill turn U randomly before the case, and algs may need "
         "a U turn first"));
  aufAfterBox_->setToolTip(
      tr("Learn and Drill turn U randomly after the case, and algs may need "
         "a U turn at the end"));
  form->addRow(tr("Learn and Drill:"), aufBeforeBox_);
  form->addRow(QString(), aufAfterBox_);
  targetSpin_ = new QDoubleSpinBox(general);
  targetSpin_->setRange(0.3, 60.0);
  targetSpin_->setSingleStep(0.1);
  targetSpin_->setSuffix(" s");
  targetSpin_->setValue(set_.targetSeconds);
  targetSpin_->setToolTip(
      tr("Drill times at or under this are shown in green, and slower cases "
         "come up more often in weighted order"));
  form->addRow(tr("Target time:"), targetSpin_);
  guidedBox_ = new QCheckBox(tr("Guided learning"), general);
  guidedBox_->setChecked(set_.guided);
  guidedBox_->setToolTip(
      tr("New cases start with a walkthrough of the main alg (step through "
         "it with the arrow keys, then do it with each next move shown), and "
         "Tab shows only the next move while a case is in its learning "
         "steps"));
  familiesBox_ = new QCheckBox(tr("Learn by family"), general);
  familiesBox_->setChecked(set_.families);
  familiesBox_->setToolTip(
      tr("Learn each case together with its mirror images (left-right, "
         "front-back and both, e.g. the four F2L slots): one review card "
         "per family, set up as a random one of them, with all of them "
         "shown afterwards"));
  form->addRow(tr("Learn:"), guidedBox_);
  form->addRow(QString(), familiesBox_);
  layout->addWidget(general);

  auto *step = new QGroupBox(tr("Step settings"), this);
  auto *stepLayout = new QVBoxLayout(step);
  auto *scopeRow = new QHBoxLayout;
  scopeBox_ = new QComboBox(step);
  scopeBox_->addItem(tr("Whole set"));
  for (const auto &g : set_.groups) {
    scopeBox_->addItem(tr("Group: %1").arg(qs(g.name)));
  }
  overrideBox_ = new QCheckBox(tr("This group has its own settings"), step);
  scopeRow->addWidget(new QLabel(tr("Settings for:"), step));
  scopeRow->addWidget(scopeBox_, 1);
  scopeRow->addWidget(overrideBox_);
  stepLayout->addLayout(scopeRow);
  specWidgets_ = new QWidget(step);
  auto *specForm = new QFormLayout(specWidgets_);
  specForm->setContentsMargins(0, 0, 0, 0);
  presetBox_ = new QComboBox(specWidgets_);
  for (const auto &p : kPresets) {
    presetBox_->addItem(tr(p.label), QString(p.free));
  }
  presetBox_->addItem(tr("Custom"), QString("?"));
  freeEdit_ = new QLineEdit(specWidgets_);
  freeEdit_->setPlaceholderText(tr("nothing free: everything must be solved"));
  freeEdit_->setToolTip(
      tr("Pieces the algs may leave unsolved, e.g. \"LL:p\" or "
         "\"LL:po DFR:po FR:po\".\nGroups: LL, LLC, LLE, F2L, ALL or a piece "
         "(URF, FR, …); flags: p = permutation, o = orientation."));
  specForm->addRow(tr("Left unsolved:"), presetBox_);
  specForm->addRow(tr("Tokens:"), freeEdit_);
  freeError_ = new QLabel(specWidgets_);
  freeError_->setStyleSheet("color: #e66");
  freeError_->setVisible(false);
  specForm->addRow(QString(), freeError_);
  cornerMaskBox_ = maskCombo(specWidgets_);
  edgeMaskBox_ = maskCombo(specWidgets_);
  restMaskBox_ = maskCombo(specWidgets_);
  restMaskBox_->removeItem(1);
  viewBox_ = new QComboBox(specWidgets_);
  viewBox_->addItem(tr("Last layer from above"),
                    static_cast<int>(DiagramView::TopLayer));
  viewBox_->addItem(tr("3D from the front right"),
                    static_cast<int>(DiagramView::Iso3D));
  viewBox_->addItem(tr("3D from the front left"),
                    static_cast<int>(DiagramView::Iso3DFrontLeft));
  viewBox_->addItem(tr("3D from the back left"),
                    static_cast<int>(DiagramView::Iso3DBackLeft));
  viewBox_->addItem(tr("3D from the back right"),
                    static_cast<int>(DiagramView::Iso3DBackRight));
  byPieceBox_ = new QCheckBox(tr("Follow the pieces"), specWidgets_);
  byPieceBox_->setToolTip(
      tr("Apply the LL settings to the last-layer pieces (and to F2L pieces "
         "left unsolved) wherever they are, rather than to whatever sits in "
         "the last layer, so that F2L pieces in the top layer keep their "
         "colours"));
  specForm->addRow(tr("Diagram LL corners:"), cornerMaskBox_);
  specForm->addRow(tr("Diagram LL edges:"), edgeMaskBox_);
  specForm->addRow(tr("Diagram rest:"), restMaskBox_);
  specForm->addRow(QString(), byPieceBox_);
  specForm->addRow(tr("Diagram view:"), viewBox_);
  stepLayout->addWidget(specWidgets_);
  layout->addWidget(step);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this,
          &SetPropertiesDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  connect(scopeBox_, &QComboBox::currentIndexChanged, this, [this](int i) {
    if (loading_) {
      return;
    }
    if (!storeScope()) {
      // Stay until the tokens can be read.
      QSignalBlocker block(scopeBox_);
      scopeBox_->setCurrentIndex(scope_ + 1);
      return;
    }
    scope_ = i - 1;
    loadScope();
  });
  connect(overrideBox_, &QCheckBox::toggled, this, [this](bool on) {
    if (loading_ || scope_ < 0) {
      return;
    }
    if (!on) {
      set_.groups[static_cast<std::size_t>(scope_)].spec.reset();
    } else if (storeScope()) {
      set_.groups[static_cast<std::size_t>(scope_)].spec = set_.spec;
    }
    loadScope();
  });
  connect(presetBox_, &QComboBox::activated, this, [this] {
    QString free = presetBox_->currentData().toString();
    if (free != "?") {
      freeEdit_->setText(free);
      freeError_->setVisible(false);
    }
  });
  connect(freeEdit_, &QLineEdit::textEdited, this, [this] {
    int preset = presetBox_->findData(freeEdit_->text().trimmed());
    presetBox_->setCurrentIndex(preset >= 0 ? preset : presetBox_->count() - 1);
    freeError_->setVisible(false);
  });

  scope_ =
      group >= 0 && group < static_cast<int>(set_.groups.size()) ? group : -1;
  {
    QSignalBlocker block(scopeBox_);
    scopeBox_->setCurrentIndex(scope_ + 1);
  }
  loadScope();
  resize(560, sizeHint().height());
}

StepSpec *SetPropertiesDialog::scopeSpec() {
  if (scope_ < 0) {
    return &set_.spec;
  }
  auto &g = set_.groups[static_cast<std::size_t>(scope_)];
  return g.spec ? &*g.spec : nullptr;
}

void SetPropertiesDialog::loadScope() {
  loading_ = true;
  overrideBox_->setVisible(scope_ >= 0);
  StepSpec *own = scopeSpec();
  overrideBox_->setChecked(scope_ >= 0 && own);
  // A group without its own settings shows the set's, read-only.
  const StepSpec &spec = own ? *own : set_.spec;
  specWidgets_->setEnabled(own != nullptr);
  QString free = qs(freeToString(spec.free));
  freeEdit_->setText(free);
  freeError_->setVisible(false);
  int preset = presetBox_->findData(free);
  presetBox_->setCurrentIndex(preset >= 0 ? preset : presetBox_->count() - 1);
  cornerMaskBox_->setCurrentIndex(
      cornerMaskBox_->findData(static_cast<int>(spec.mask.llCorners)));
  edgeMaskBox_->setCurrentIndex(
      edgeMaskBox_->findData(static_cast<int>(spec.mask.llEdges)));
  restMaskBox_->setCurrentIndex(
      std::max(0, restMaskBox_->findData(static_cast<int>(spec.mask.rest))));
  byPieceBox_->setChecked(spec.mask.byPiece);
  viewBox_->setCurrentIndex(viewBox_->findData(static_cast<int>(spec.view)));
  loading_ = false;
}

bool SetPropertiesDialog::storeScope() {
  StepSpec *spec = scopeSpec();
  if (!spec) {
    return true; // nothing of its own to store
  }
  auto free = parseFree(freeEdit_->text().toStdString());
  if (!free) {
    freeError_->setText(tr("Can't read \"%1\"; use tokens like LL:p or FR:po.")
                            .arg(freeEdit_->text()));
    freeError_->setVisible(true);
    freeEdit_->setFocus();
    return false;
  }
  spec->free = *free;
  spec->mask.llCorners =
      static_cast<StickerMode>(cornerMaskBox_->currentData().toInt());
  spec->mask.llEdges =
      static_cast<StickerMode>(edgeMaskBox_->currentData().toInt());
  spec->mask.rest =
      static_cast<StickerMode>(restMaskBox_->currentData().toInt());
  spec->mask.byPiece = byPieceBox_->isChecked();
  spec->view = static_cast<DiagramView>(viewBox_->currentData().toInt());
  return true;
}

void SetPropertiesDialog::accept() {
  if (!storeScope()) {
    return;
  }
  QString name = nameEdit_->text().trimmed();
  if (!name.isEmpty()) {
    set_.name = name.toStdString();
  }
  set_.description = descriptionEdit_->text().trimmed().toStdString();
  set_.aufBefore = aufBeforeBox_->isChecked();
  set_.aufAfter = aufAfterBox_->isChecked();
  set_.targetSeconds = targetSpin_->value();
  set_.guided = guidedBox_->isChecked();
  set_.families = familiesBox_->isChecked();
  QDialog::accept();
}

} // namespace vcube
