#include "app/set_properties_dialog.hpp"
#include "app/page_parts.hpp"
#include "app/ui_text.hpp"

#include "train/case_logic.hpp"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
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

constexpr int kPreviewSize = 150;

QComboBox *maskCombo(QWidget *parent) {
  auto *c = new QComboBox(parent);
  c->addItem(QObject::tr("Full colour"), static_cast<int>(StickerMode::Full));
  c->addItem(QObject::tr("Top colour only"),
             static_cast<int>(StickerMode::Orientation));
  c->addItem(QObject::tr("Grey"), static_cast<int>(StickerMode::Hidden));
  return c;
}

} // namespace

SetPropertiesDialog::SetPropertiesDialog(const AlgSet &set, int group,
                                         const CubeLook &look,
                                         const std::string &caseId,
                                         QWidget *parent)
    : QDialog(parent), set_(set), look_(look), previewCase_(caseId) {
  setWindowTitle(tr("Set properties: %1").arg(qs(set.name)));
  for (const auto &g : set_.groups) {
    for (const auto &c : g.cases) {
      auto &passing = passingNow_[c.id];
      for (const auto &alg : c.algs) {
        passing.push_back(checkCaseAlg(set_, g, c, alg).passes());
      }
    }
  }
  auto *layout = new QVBoxLayout(this);
  auto *tabs = new QTabWidget(this);
  layout->addWidget(tabs, 1);

  // --- the set ---
  auto *general = new QWidget(tabs);
  auto *form = new QFormLayout(general);
  nameEdit_ = new QLineEdit(qs(set_.name), general);
  descriptionEdit_ = new QLineEdit(qs(set_.description), general);
  form->addRow(tr("Name:"), nameEdit_);
  form->addRow(tr("Description:"), descriptionEdit_);
  targetSpin_ = new QDoubleSpinBox(general);
  targetSpin_->setRange(0.1, 600.0);
  targetSpin_->setDecimals(2);
  targetSpin_->setSingleStep(0.1);
  targetSpin_->setSuffix(" s");
  targetSpin_->setValue(set_.targetSeconds);
  form->addRow(tr("Target time:"), targetSpin_);
  form->addRow(QString(),
               helpLabel(tr("Drill times at or under this show in green, and "
                            "slower cases come up more often in weighted "
                            "order."),
                         general));
  tabs->addTab(general, tr("Set"));

  // --- Learn and Drill ---
  auto *training = new QWidget(tabs);
  auto *trainingForm = new QFormLayout(training);
  aufBeforeBox_ = new QCheckBox(tr("Random U turn before the case"), training);
  aufAfterBox_ = new QCheckBox(tr("Random U turn after the case"), training);
  aufBeforeBox_->setChecked(set_.aufBefore);
  aufAfterBox_->setChecked(set_.aufAfter);
  trainingForm->addRow(tr("Set up:"), aufBeforeBox_);
  trainingForm->addRow(QString(), aufAfterBox_);
  trainingForm->addRow(
      QString(),
      helpLabel(tr("Cases come up from any side, and algs may need a U turn "
                   "first (or at the end) to pass."),
                training));
  guidedBox_ = new QCheckBox(tr("Guided learning"), training);
  guidedBox_->setChecked(set_.guided);
  familiesBox_ = new QCheckBox(tr("Learn by family"), training);
  familiesBox_->setChecked(set_.families);
  reductionBox_ = new QCheckBox(tr("Solve a step at a time"), training);
  reductionBox_->setChecked(set_.reduction);
  trainingForm->addRow(tr("Learn:"), guidedBox_);
  trainingForm->addRow(
      QString(),
      helpLabel(tr("New cases start with a walkthrough of the main alg, and "
                   "Tab shows only the next move while a case is being "
                   "learned."),
                training));
  trainingForm->addRow(QString(), familiesBox_);
  trainingForm->addRow(
      QString(),
      helpLabel(tr("A case and its mirror images (e.g. the four F2L slots) "
                   "share one review card; a random one comes up."),
                training));
  trainingForm->addRow(QString(), reductionBox_);
  trainingForm->addRow(
      QString(),
      helpLabel(tr("An alg may stop at another case of the set, as a "
                   "trigger does; Learn asks for one step per case and "
                   "follows the cases through to the end."),
                training));
  auto hasMirror = [this](Mirror m) {
    return std::find(set_.mirrors.begin(), set_.mirrors.end(), m) !=
           set_.mirrors.end();
  };
  mirrorLeftRightBox_ = new QCheckBox(tr("Left-right"), training);
  mirrorFrontBackBox_ = new QCheckBox(tr("Front-back"), training);
  mirrorBothBox_ = new QCheckBox(tr("Both"), training);
  mirrorLeftRightBox_->setChecked(hasMirror({true, false}));
  mirrorFrontBackBox_->setChecked(hasMirror({false, true}));
  mirrorBothBox_->setChecked(hasMirror({true, true}));
  auto *mirrorRow = new QHBoxLayout;
  for (QCheckBox *box :
       {mirrorLeftRightBox_, mirrorFrontBackBox_, mirrorBothBox_}) {
    mirrorRow->addWidget(box);
  }
  mirrorRow->addStretch();
  trainingForm->addRow(tr("Also mirrored:"), mirrorRow);
  trainingForm->addRow(
      QString(),
      helpLabel(tr("Learn and Drill also set each case up mirrored, with its "
                   "algs mirrored to match (R U R' becomes L' U' L "
                   "left-right), so a set for the front-right slot is "
                   "practised for the other slots too."),
                training));
  tabs->addTab(training, tr("Learn and Drill"));

  // --- step settings ---
  auto *step = new QWidget(tabs);
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
  auto *specRow = new QHBoxLayout;
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
  specForm->addRow(tr("Left unsolved:"), presetBox_);
  specForm->addRow(tr("Tokens:"), freeEdit_);
  specForm->addRow(
      QString(),
      helpLabel(tr("Pieces the algs may leave unsolved, e.g. \"LL:p\" or "
                   "\"LL:po DFR:po FR:po\": LL, LLC (LL corners), LLE (LL "
                   "edges), F2L, ALL or a piece (URF, FR, …), with p for "
                   "place and o for orientation."),
                specWidgets_));
  freeError_ = new QLabel(specWidgets_);
  freeError_->setStyleSheet("color: #e66");
  freeError_->setWordWrap(true);
  freeError_->setVisible(false);
  specForm->addRow(QString(), freeError_);
  freeNote_ = new QLabel(specWidgets_);
  freeNote_->setWordWrap(true);
  freeNote_->setStyleSheet("color: #ffb030");
  freeNote_->setVisible(false);
  specForm->addRow(QString(), freeNote_);
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
  specForm->addRow(tr("Diagram LL corners:"), cornerMaskBox_);
  specForm->addRow(tr("Diagram LL edges:"), edgeMaskBox_);
  specForm->addRow(tr("Diagram rest:"), restMaskBox_);
  specForm->addRow(QString(), byPieceBox_);
  specForm->addRow(
      QString(),
      helpLabel(tr("The LL settings apply to the last-layer pieces (and F2L "
                   "pieces left unsolved) wherever they are, so F2L pieces "
                   "in the top layer keep their colours."),
                specWidgets_));
  specForm->addRow(tr("Diagram view:"), viewBox_);
  specRow->addWidget(specWidgets_, 1);
  // The diagram of a case of the scope, as these settings draw it.
  auto *previewBox = new QVBoxLayout;
  preview_ = new QLabel(step);
  preview_->setFixedSize(kPreviewSize, kPreviewSize);
  preview_->setAlignment(Qt::AlignCenter);
  previewName_ = new QLabel(step);
  previewName_->setAlignment(Qt::AlignCenter);
  previewName_->setWordWrap(true);
  auto *previewButtons = new QHBoxLayout;
  auto *previous = new QToolButton(step);
  previous->setArrowType(Qt::LeftArrow);
  previous->setToolTip(tr("Show the case before"));
  auto *next = new QToolButton(step);
  next->setArrowType(Qt::RightArrow);
  next->setToolTip(tr("Show the next case"));
  previewButtons->addStretch();
  previewButtons->addWidget(previous);
  previewButtons->addWidget(next);
  previewButtons->addStretch();
  previewBox->addWidget(preview_, 0, Qt::AlignHCenter);
  previewBox->addWidget(previewName_);
  previewBox->addLayout(previewButtons);
  previewBox->addStretch();
  specRow->addLayout(previewBox);
  stepLayout->addLayout(specRow, 1);
  tabs->addTab(step, tr("Step settings"));

  // --- what the changes do ---
  impact_ = new QLabel(this);
  impact_->setWordWrap(true);
  layout->addWidget(impact_);
  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this,
          &SetPropertiesDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  updateTimer_ = new QTimer(this);
  updateTimer_->setSingleShot(true);
  updateTimer_->setInterval(150);
  connect(updateTimer_, &QTimer::timeout, this,
          &SetPropertiesDialog::updatePreview);
  auto changed = [this] {
    if (!loading_) {
      updateTimer_->start();
    }
  };
  for (QCheckBox *box :
       {aufBeforeBox_, aufAfterBox_, reductionBox_, byPieceBox_}) {
    connect(box, &QCheckBox::toggled, this, changed);
  }
  for (QComboBox *box :
       {cornerMaskBox_, edgeMaskBox_, restMaskBox_, viewBox_}) {
    connect(box, &QComboBox::currentIndexChanged, this, changed);
  }
  connect(freeEdit_, &QLineEdit::textChanged, this, changed);
  connect(targetSpin_, &QDoubleSpinBox::valueChanged, this,
          [this] { targetEdited_ = true; });
  connect(previous, &QToolButton::clicked, this, [this] {
    if (!previewCases_.empty()) {
      previewIndex_ =
          (previewIndex_ + previewCases_.size() - 1) % previewCases_.size();
      previewCase_ = previewCases_[previewIndex_];
      updatePreview();
    }
  });
  connect(next, &QToolButton::clicked, this, [this] {
    if (!previewCases_.empty()) {
      previewIndex_ = (previewIndex_ + 1) % previewCases_.size();
      previewCase_ = previewCases_[previewIndex_];
      updatePreview();
    }
  });

  connect(scopeBox_, &QComboBox::currentIndexChanged, this, [this](int i) {
    if (loading_) {
      return;
    }
    if (!storeScope(set_, true)) {
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
    } else if (storeScope(set_, true)) {
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
  if (scope_ >= 0) {
    tabs->setCurrentWidget(step);
  }
  loadScope();
  targetEdited_ = false;
  resize(640, sizeHint().height());
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
  choosePreviewCases();
  updatePreview();
}

bool SetPropertiesDialog::storeScope(AlgSet &into, bool showErrors) {
  StepSpec *spec = nullptr;
  if (scope_ < 0) {
    spec = &into.spec;
  } else if (auto &g = into.groups[static_cast<std::size_t>(scope_)]; g.spec) {
    spec = &*g.spec;
  }
  if (!spec) {
    return true; // nothing of its own to store
  }
  auto free = parseFree(freeEdit_->text().toStdString());
  if (!free) {
    if (showErrors) {
      freeError_->setText(
          tr("Can't read \"%1\"; use tokens like LL:p or FR:po.")
              .arg(freeEdit_->text()));
      freeError_->setVisible(true);
      freeEdit_->setFocus();
    }
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

bool SetPropertiesDialog::readWidgets(AlgSet &into, bool showErrors) {
  if (!storeScope(into, showErrors)) {
    return false;
  }
  QString name = nameEdit_->text().trimmed();
  if (!name.isEmpty()) {
    into.name = name.toStdString();
  }
  into.description = descriptionEdit_->text().trimmed().toStdString();
  into.aufBefore = aufBeforeBox_->isChecked();
  into.aufAfter = aufAfterBox_->isChecked();
  // The spin box can't hold every stored time: keep it unless edited.
  if (targetEdited_) {
    into.targetSeconds = targetSpin_->value();
  }
  into.guided = guidedBox_->isChecked();
  into.families = familiesBox_->isChecked();
  into.reduction = reductionBox_->isChecked();
  into.mirrors.clear();
  if (mirrorLeftRightBox_->isChecked()) {
    into.mirrors.push_back({true, false});
  }
  if (mirrorFrontBackBox_->isChecked()) {
    into.mirrors.push_back({false, true});
  }
  if (mirrorBothBox_->isChecked()) {
    into.mirrors.push_back({true, true});
  }
  return true;
}

void SetPropertiesDialog::choosePreviewCases() {
  previewCases_.clear();
  for (std::size_t g = 0; g < set_.groups.size(); ++g) {
    const CaseGroup &group = set_.groups[g];
    // The set's own settings show on the groups that use them.
    bool inScope =
        scope_ < 0 ? !group.spec.has_value() : static_cast<int>(g) == scope_;
    if (!inScope) {
      continue;
    }
    for (const auto &c : group.cases) {
      previewCases_.push_back(c.id);
    }
  }
  if (previewCases_.empty() && scope_ < 0) {
    for (const auto &g : set_.groups) {
      for (const auto &c : g.cases) {
        previewCases_.push_back(c.id);
      }
    }
  }
  auto it = std::find(previewCases_.begin(), previewCases_.end(), previewCase_);
  previewIndex_ = it == previewCases_.end()
                      ? 0
                      : static_cast<std::size_t>(it - previewCases_.begin());
  if (!previewCases_.empty()) {
    previewCase_ = previewCases_[previewIndex_];
  }
}

void SetPropertiesDialog::updatePreview() {
  AlgSet edited = set_;
  bool widened = false;
  if (auto free = parseFree(freeEdit_->text().toStdString(), &widened);
      free && widened) {
    freeNote_->setText(
        tr("Pieces that trade places share what may be left: this is "
           "checked as \"%1\".")
            .arg(qs(freeToString(*free))));
    freeNote_->setVisible(true);
  } else {
    freeNote_->setVisible(false);
  }
  if (!readWidgets(edited, false)) {
    impact_->setText(tr("Fix the tokens to see what the settings do."));
    preview_->clear();
    return;
  }
  // The case, as the settings in force draw it.
  const AlgCase *c = edited.findCase(previewCase_);
  if (const CaseGroup *g = c ? edited.groupOf(c->id) : nullptr) {
    const StepSpec &spec = edited.specFor(*g);
    CaseAnalysis a = analyzeCase(edited, *g, *c);
    preview_->setPixmap(cube_painter::diagram(
        maskedFacelets(a.display, spec.mask, spec.free), spec.view,
        kPreviewSize, look_, devicePixelRatioF()));
    previewName_->setText(previewCases_.size() > 1
                              ? tr("%1 (%2 of %3)")
                                    .arg(qs(c->name))
                                    .arg(previewIndex_ + 1)
                                    .arg(previewCases_.size())
                              : qs(c->name));
  } else {
    preview_->clear();
    previewName_->setText(tr("No case to show"));
  }
  // Which algs would stop or start passing.
  int stop = 0, start = 0, total = 0;
  for (const auto &g : edited.groups) {
    for (const auto &cs : g.cases) {
      const auto &now = passingNow_[cs.id];
      for (std::size_t i = 0; i < cs.algs.size() && i < now.size(); ++i) {
        bool passes = checkCaseAlg(edited, g, cs, cs.algs[i]).passes();
        stop += now[i] && !passes ? 1 : 0;
        start += !now[i] && passes ? 1 : 0;
        ++total;
      }
    }
  }
  QString text;
  if (stop == 0 && start == 0) {
    text =
        total == 0
            ? tr("The set has no algs yet.")
            : (total == 1 ? tr("The set's alg checks as it does now.")
                          : tr("All %1 algs check as they do now.").arg(total));
  } else {
    QStringList parts;
    if (stop > 0) {
      parts << (stop == 1 ? tr("<b>1 alg that passes now would stop "
                               "passing</b>")
                          : tr("<b>%1 algs that pass now would stop "
                               "passing</b>")
                                .arg(stop));
    }
    if (start > 0) {
      parts << tr("%1 would pass")
                   .arg(counted(start, tr("failing alg"), tr("failing algs")));
    }
    text = tr("With these settings, %1.").arg(parts.join(tr(" and ")));
  }
  impact_->setText(text);
}

void SetPropertiesDialog::accept() {
  if (!readWidgets(set_, true)) {
    return;
  }
  QDialog::accept();
}

} // namespace vcube
