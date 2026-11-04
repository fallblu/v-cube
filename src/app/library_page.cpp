#include "app/library_page.hpp"

#include "app/cube_painter.hpp"
#include "train/case_logic.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace vcube {

namespace {

constexpr int kCaseRole = Qt::UserRole;
constexpr int kGroupRole = Qt::UserRole + 1;

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

QString reportText(const ImportReport &r) {
  QString text = QObject::tr("Added %1 algs to cases").arg(r.added);
  if (r.created) {
    text += QObject::tr(", created %1 new cases").arg(r.created);
  }
  if (r.duplicates) {
    text += QObject::tr(", skipped %1 already there").arg(r.duplicates);
  }
  text += ".";
  auto list = [](const std::vector<std::string> &items) {
    QStringList out;
    for (const auto &i : items) {
      out << QString::fromStdString(i).toHtmlEscaped();
    }
    return out.join("<br>");
  };
  if (!r.unmatched.empty()) {
    text += QObject::tr("<p><b>Matched no case (%1):</b><br>%2</p>")
                .arg(r.unmatched.size())
                .arg(list(r.unmatched));
  }
  if (!r.invalid.empty()) {
    text += QObject::tr("<p><b>Could not be read (%1):</b><br>%2</p>")
                .arg(r.invalid.size())
                .arg(list(r.invalid));
  }
  return text;
}

} // namespace

LibraryPage::LibraryPage(AppSettings &settings, AlgLibrary &library,
                         QWidget *parent)
    : QWidget(parent), settings_(settings), library_(library) {
  // --- sets ---
  auto *left = new QWidget(this);
  auto *leftLayout = new QVBoxLayout(left);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->addWidget(new QLabel(tr("<b>Alg sets</b>"), left));
  setList_ = new QListWidget(left);
  leftLayout->addWidget(setList_, 1);
  auto *row1 = new QHBoxLayout;
  auto *newButton = new QPushButton(tr("New…"), left);
  auto *dupButton = new QPushButton(tr("Duplicate"), left);
  auto *delButton = new QPushButton(tr("Delete"), left);
  row1->addWidget(newButton);
  row1->addWidget(dupButton);
  row1->addWidget(delButton);
  auto *row2 = new QHBoxLayout;
  auto *importButton = new QPushButton(tr("Import…"), left);
  auto *exportButton = new QPushButton(tr("Export…"), left);
  row2->addWidget(importButton);
  row2->addWidget(exportButton);
  restoreButton_ = new QPushButton(tr("Restore built-in cases"), left);
  restoreButton_->setToolTip(
      tr("Reload this built-in set's cases and settings as shipped, "
         "keeping your algs"));
  leftLayout->addLayout(row1);
  leftLayout->addLayout(row2);
  leftLayout->addWidget(restoreButton_);

  // --- set settings ---
  auto *right = new QWidget(this);
  auto *rightLayout = new QVBoxLayout(right);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  auto *setBox = new QGroupBox(tr("Set"), right);
  auto *setForm = new QFormLayout(setBox);
  nameEdit_ = new QLineEdit(setBox);
  descriptionEdit_ = new QLineEdit(setBox);
  setForm->addRow(tr("Name:"), nameEdit_);
  setForm->addRow(tr("Description:"), descriptionEdit_);
  auto *drillRow = new QHBoxLayout;
  aufBeforeBox_ = new QCheckBox(tr("Random U turn before the case"), setBox);
  aufAfterBox_ = new QCheckBox(tr("and after it"), setBox);
  targetSpin_ = new QDoubleSpinBox(setBox);
  targetSpin_->setRange(0.3, 60.0);
  targetSpin_->setSingleStep(0.1);
  targetSpin_->setSuffix(" s");
  targetSpin_->setToolTip(
      tr("Time to beat for a \"Good\" grade (60% of it is \"Easy\")"));
  drillRow->addWidget(aufBeforeBox_);
  drillRow->addWidget(aufAfterBox_);
  drillRow->addStretch();
  drillRow->addWidget(new QLabel(tr("Target time:"), setBox));
  drillRow->addWidget(targetSpin_);
  setForm->addRow(tr("Drills:"), drillRow);

  auto *scopeRow = new QHBoxLayout;
  scopeBox_ = new QComboBox(setBox);
  overrideBox_ = new QCheckBox(tr("Group has its own settings"), setBox);
  scopeRow->addWidget(scopeBox_, 1);
  scopeRow->addWidget(overrideBox_);
  setForm->addRow(tr("Step settings for:"), scopeRow);
  specWidgets_ = new QWidget(setBox);
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
  auto *freeRow = new QHBoxLayout;
  freeRow->addWidget(presetBox_, 1);
  freeRow->addWidget(freeEdit_, 1);
  specForm->addRow(tr("Left unsolved:"), freeRow);
  cornerMaskBox_ = maskCombo(specWidgets_);
  edgeMaskBox_ = maskCombo(specWidgets_);
  restMaskBox_ = maskCombo(specWidgets_);
  restMaskBox_->removeItem(1);
  viewBox_ = new QComboBox(specWidgets_);
  viewBox_->addItem(tr("Last layer from above"),
                    static_cast<int>(DiagramView::TopLayer));
  viewBox_->addItem(tr("3D"), static_cast<int>(DiagramView::Iso3D));
  auto *maskRow = new QHBoxLayout;
  maskRow->addWidget(new QLabel(tr("LL corners"), specWidgets_));
  maskRow->addWidget(cornerMaskBox_);
  maskRow->addWidget(new QLabel(tr("LL edges"), specWidgets_));
  maskRow->addWidget(edgeMaskBox_);
  maskRow->addWidget(new QLabel(tr("Rest"), specWidgets_));
  maskRow->addWidget(restMaskBox_);
  maskRow->addWidget(viewBox_);
  specForm->addRow(tr("Diagrams:"), maskRow);
  setForm->addRow(specWidgets_);
  rightLayout->addWidget(setBox);

  // --- cases ---
  auto *caseSplit = new QSplitter(right);
  auto *treeWidget = new QWidget(caseSplit);
  auto *treeLayout = new QVBoxLayout(treeWidget);
  treeLayout->setContentsMargins(0, 0, 0, 0);
  caseTree_ = new QTreeWidget(treeWidget);
  caseTree_->setColumnCount(3);
  caseTree_->setHeaderLabels({tr("Case"), tr("Algs"), tr("Main alg")});
  caseTree_->setIconSize(QSize(40, 40));
  caseTree_->setIndentation(14);
  caseTree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  caseTree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  caseTree_->header()->setStretchLastSection(true);
  treeLayout->addWidget(caseTree_, 1);
  auto *caseButtons = new QHBoxLayout;
  auto *newGroupButton = new QPushButton(tr("New group"), treeWidget);
  auto *renameGroupButton = new QPushButton(tr("Rename"), treeWidget);
  auto *deleteButton = new QPushButton(tr("Delete"), treeWidget);
  auto *newCaseButton = new QPushButton(tr("New case from alg…"), treeWidget);
  auto *pasteButton = new QPushButton(tr("Paste algs…"), treeWidget);
  pasteButton->setToolTip(
      tr("Paste a list of algs; each is matched to the case it solves"));
  for (auto *b : {newGroupButton, renameGroupButton, deleteButton,
                  newCaseButton, pasteButton}) {
    caseButtons->addWidget(b);
  }
  treeLayout->addLayout(caseButtons);

  caseBox_ = new QGroupBox(tr("Case"), caseSplit);
  auto *caseLayout = new QVBoxLayout(caseBox_);
  auto *caseTop = new QHBoxLayout;
  caseDiagram_ = new QLabel(caseBox_);
  caseDiagram_->setFixedSize(120, 120);
  auto *caseForm = new QFormLayout;
  caseNameEdit_ = new QLineEdit(caseBox_);
  caseForm->addRow(tr("Name:"), caseNameEdit_);
  caseTop->addWidget(caseDiagram_);
  caseTop->addLayout(caseForm, 1);
  caseLayout->addLayout(caseTop);
  caseLayout->addWidget(new QLabel(
      tr("Algs (the first is the main alg; double-click to edit):"), caseBox_));
  algList_ = new QListWidget(caseBox_);
  algList_->setEditTriggers(QAbstractItemView::DoubleClicked |
                            QAbstractItemView::EditKeyPressed);
  caseLayout->addWidget(algList_, 1);
  auto *addRow = new QHBoxLayout;
  newAlgEdit_ = new QLineEdit(caseBox_);
  newAlgEdit_->setPlaceholderText(tr("New alg, e.g. R U R' U R U2 R'"));
  auto *addButton = new QPushButton(tr("Add"), caseBox_);
  addRow->addWidget(newAlgEdit_, 1);
  addRow->addWidget(addButton);
  caseLayout->addLayout(addRow);
  auto *algButtons = new QHBoxLayout;
  auto *mainButton = new QPushButton(tr("Make main"), caseBox_);
  auto *upButton = new QPushButton(tr("Up"), caseBox_);
  auto *downButton = new QPushButton(tr("Down"), caseBox_);
  auto *removeButton = new QPushButton(tr("Remove"), caseBox_);
  for (auto *b : {mainButton, upButton, downButton, removeButton}) {
    algButtons->addWidget(b);
  }
  caseLayout->addLayout(algButtons);
  caseSplit->addWidget(treeWidget);
  caseSplit->addWidget(caseBox_);
  caseSplit->setSizes({520, 420});
  rightLayout->addWidget(caseSplit, 1);

  auto *split = new QSplitter(this);
  split->addWidget(left);
  split->addWidget(right);
  split->setStretchFactor(1, 1);
  split->setSizes({230, 970});
  auto *layout = new QHBoxLayout(this);
  layout->addWidget(split);

  // --- wiring ---
  connect(setList_, &QListWidget::currentRowChanged, this, [this](int row) {
    if (row >= 0 && row < static_cast<int>(library_.sets().size())) {
      setId_ = library_.sets()[static_cast<std::size_t>(row)].id;
      caseId_.clear();
      loadSet();
    }
  });
  connect(newButton, &QPushButton::clicked, this, &LibraryPage::newSet);
  connect(dupButton, &QPushButton::clicked, this, &LibraryPage::duplicateSet);
  connect(delButton, &QPushButton::clicked, this, &LibraryPage::deleteSet);
  connect(importButton, &QPushButton::clicked, this,
          &LibraryPage::importSetFile);
  connect(exportButton, &QPushButton::clicked, this,
          &LibraryPage::exportSetFile);
  connect(restoreButton_, &QPushButton::clicked, this,
          &LibraryPage::restoreSet);
  connect(nameEdit_, &QLineEdit::editingFinished, this, [this] {
    if (AlgSet *s = set(); s && !loading_ && ss(nameEdit_->text()) != s->name) {
      s->name = ss(nameEdit_->text().trimmed());
      changed();
      refreshSetList();
    }
  });
  connect(descriptionEdit_, &QLineEdit::editingFinished, this, [this] {
    if (AlgSet *s = set();
        s && !loading_ && ss(descriptionEdit_->text()) != s->description) {
      s->description = ss(descriptionEdit_->text());
      changed();
    }
  });
  connect(aufBeforeBox_, &QCheckBox::toggled, this, [this](bool on) {
    if (AlgSet *s = set(); s && !loading_) {
      s->aufBefore = on;
      changed();
    }
  });
  connect(aufAfterBox_, &QCheckBox::toggled, this, [this](bool on) {
    if (AlgSet *s = set(); s && !loading_) {
      s->aufAfter = on;
      changed();
    }
  });
  connect(targetSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
    if (AlgSet *s = set(); s && !loading_) {
      s->targetSeconds = v;
      changed();
    }
  });
  connect(scopeBox_, &QComboBox::currentIndexChanged, this,
          [this] { loadSpec(); });
  connect(overrideBox_, &QCheckBox::toggled, this, [this](bool on) {
    if (loading_) {
      return;
    }
    if (CaseGroup *g = scopeGroup()) {
      g->spec = on ? std::optional<StepSpec>(set()->spec) : std::nullopt;
      loadSpec();
      changed(true);
    }
  });
  connect(presetBox_, &QComboBox::activated, this, [this] {
    QString free = presetBox_->currentData().toString();
    if (free != "?") {
      freeEdit_->setText(free);
      storeSpec();
    }
  });
  connect(freeEdit_, &QLineEdit::editingFinished, this,
          &LibraryPage::storeSpec);
  for (QComboBox *c : {cornerMaskBox_, edgeMaskBox_, restMaskBox_, viewBox_}) {
    connect(c, &QComboBox::activated, this, &LibraryPage::storeSpec);
  }
  connect(caseTree_, &QTreeWidget::currentItemChanged, this,
          [this](QTreeWidgetItem *item) {
            caseId_ =
                item ? ss(item->data(0, kCaseRole).toString()) : std::string();
            // Show the step settings that apply to the selection: its group's
            // own settings if it has them, else the whole set's.
            QTreeWidgetItem *groupItem =
                item && item->parent() ? item->parent() : item;
            if (AlgSet *s = set(); s && groupItem) {
              int gi = groupItem->data(0, kGroupRole).toInt();
              bool own =
                  gi >= 0 && gi < static_cast<int>(s->groups.size()) &&
                  s->groups[static_cast<std::size_t>(gi)].spec.has_value();
              scopeBox_->setCurrentIndex(item == groupItem || own ? gi + 1 : 0);
            }
            loadCase();
          });
  connect(newGroupButton, &QPushButton::clicked, this, &LibraryPage::newGroup);
  connect(renameGroupButton, &QPushButton::clicked, this,
          &LibraryPage::renameGroup);
  connect(deleteButton, &QPushButton::clicked, this,
          &LibraryPage::deleteSelected);
  connect(newCaseButton, &QPushButton::clicked, this,
          &LibraryPage::newCaseFromAlg);
  connect(pasteButton, &QPushButton::clicked, this, &LibraryPage::pasteAlgs);
  connect(caseNameEdit_, &QLineEdit::editingFinished, this, [this] {
    if (AlgCase *c = currentCase();
        c && !loading_ && ss(caseNameEdit_->text()) != c->name) {
      c->name = ss(caseNameEdit_->text().trimmed());
      changed(true);
    }
  });
  connect(addButton, &QPushButton::clicked, this, &LibraryPage::addAlg);
  connect(newAlgEdit_, &QLineEdit::returnPressed, this, &LibraryPage::addAlg);
  connect(algList_, &QListWidget::itemChanged, this, [this] {
    if (!loading_) {
      storeAlgs();
    }
  });
  connect(mainButton, &QPushButton::clicked, this, &LibraryPage::makeMain);
  connect(upButton, &QPushButton::clicked, this, [this] { moveAlg(-1); });
  connect(downButton, &QPushButton::clicked, this, [this] { moveAlg(1); });
  connect(removeButton, &QPushButton::clicked, this, &LibraryPage::removeAlg);

  refreshSetList();
  if (!library_.sets().empty()) {
    setList_->setCurrentRow(0);
  }
}

// --- helpers ---------------------------------------------------------------

AlgSet *LibraryPage::set() { return library_.find(setId_); }

AlgCase *LibraryPage::currentCase() {
  AlgSet *s = set();
  return s && !caseId_.empty() ? s->findCase(caseId_) : nullptr;
}

CaseGroup *LibraryPage::scopeGroup() {
  AlgSet *s = set();
  int i = scopeBox_->currentIndex() - 1;
  if (!s || i < 0 || i >= static_cast<int>(s->groups.size())) {
    return nullptr;
  }
  return &s->groups[static_cast<std::size_t>(i)];
}

CubeLook LibraryPage::look() const {
  CubeLook l;
  l.scheme = settings_.scheme;
  QSettings q;
  QString key = QString("trainer/sets/%1/").arg(qs(setId_));
  l.map = colourMapFor(q.value(key + "top", 0).toInt(),
                       q.value(key + "front", 2).toInt())
              .value_or(kIdentityColourMap);
  return l;
}

void LibraryPage::applySettings() {
  buildCaseTree();
  loadCase();
}

void LibraryPage::changed(bool rebuildTree) {
  if (AlgSet *s = set()) {
    QString error;
    if (!library_.save(*s, &error)) {
      emit message(tr("Could not save %1: %2").arg(qs(s->name), error));
    }
  }
  if (rebuildTree) {
    buildCaseTree();
  }
  emit setsChanged();
}

void LibraryPage::refreshSetList() {
  QSignalBlocker block(setList_);
  setList_->clear();
  int current = -1;
  for (std::size_t i = 0; i < library_.sets().size(); ++i) {
    const AlgSet &s = library_.sets()[i];
    auto *item = new QListWidgetItem(
        QString("%1  (%2)").arg(qs(s.name)).arg(s.caseCount()), setList_);
    if (s.builtin) {
      item->setToolTip(tr("Built-in cases; the algs are yours"));
    }
    if (s.id == setId_) {
      current = static_cast<int>(i);
    }
  }
  setList_->setCurrentRow(current);
}

void LibraryPage::loadSet() {
  AlgSet *s = set();
  loading_ = true;
  nameEdit_->setText(s ? qs(s->name) : QString());
  descriptionEdit_->setText(s ? qs(s->description) : QString());
  aufBeforeBox_->setChecked(s && s->aufBefore);
  aufAfterBox_->setChecked(s && s->aufAfter);
  targetSpin_->setValue(s ? s->targetSeconds : 3.0);
  restoreButton_->setEnabled(s && s->builtin);
  scopeBox_->clear();
  scopeBox_->addItem(tr("Whole set"));
  if (s) {
    for (const auto &g : s->groups) {
      scopeBox_->addItem(tr("Group: %1").arg(qs(g.name)));
    }
  }
  loading_ = false;
  loadSpec();
  buildCaseTree();
  loadCase();
}

void LibraryPage::loadSpec() {
  AlgSet *s = set();
  if (!s) {
    return;
  }
  loading_ = true;
  CaseGroup *g = scopeGroup();
  overrideBox_->setVisible(g != nullptr);
  overrideBox_->setChecked(g && g->spec);
  const StepSpec &spec = g ? s->specFor(*g) : s->spec;
  specWidgets_->setEnabled(!g || g->spec.has_value());
  QString free = qs(freeToString(spec.free));
  freeEdit_->setText(free);
  int preset = presetBox_->findData(free);
  presetBox_->setCurrentIndex(preset >= 0 ? preset : presetBox_->count() - 1);
  cornerMaskBox_->setCurrentIndex(
      cornerMaskBox_->findData(static_cast<int>(spec.mask.llCorners)));
  edgeMaskBox_->setCurrentIndex(
      edgeMaskBox_->findData(static_cast<int>(spec.mask.llEdges)));
  restMaskBox_->setCurrentIndex(
      std::max(0, restMaskBox_->findData(static_cast<int>(spec.mask.rest))));
  viewBox_->setCurrentIndex(viewBox_->findData(static_cast<int>(spec.view)));
  loading_ = false;
}

void LibraryPage::storeSpec() {
  AlgSet *s = set();
  if (!s || loading_) {
    return;
  }
  CaseGroup *g = scopeGroup();
  if (g && !g->spec) {
    return;
  }
  StepSpec &spec = g ? *g->spec : s->spec;
  auto free = parseFree(ss(freeEdit_->text()));
  if (!free) {
    freeEdit_->setStyleSheet("color: #e66");
    emit message(tr("Could not read \"%1\"; use tokens like LL:p or FR:po")
                     .arg(freeEdit_->text()));
    return;
  }
  freeEdit_->setStyleSheet(QString());
  spec.free = *free;
  spec.mask.llCorners =
      static_cast<StickerMode>(cornerMaskBox_->currentData().toInt());
  spec.mask.llEdges =
      static_cast<StickerMode>(edgeMaskBox_->currentData().toInt());
  spec.mask.rest =
      static_cast<StickerMode>(restMaskBox_->currentData().toInt());
  spec.view = static_cast<DiagramView>(viewBox_->currentData().toInt());
  loadSpec();
  changed(true);
  loadCase();
}

void LibraryPage::buildCaseTree() {
  QSignalBlocker block(caseTree_);
  caseTree_->clear();
  AlgSet *s = set();
  if (!s) {
    return;
  }
  CubeLook l = look();
  qreal dpr = devicePixelRatioF();
  QTreeWidgetItem *current = nullptr;
  for (std::size_t gi = 0; gi < s->groups.size(); ++gi) {
    const CaseGroup &g = s->groups[gi];
    auto *groupItem = new QTreeWidgetItem(caseTree_, {qs(g.name)});
    groupItem->setData(0, kGroupRole, static_cast<int>(gi));
    groupItem->setText(1, QString::number(g.cases.size()));
    const StepSpec &spec = s->specFor(g);
    for (const auto &c : g.cases) {
      auto *item = new QTreeWidgetItem(groupItem, {qs(c.name)});
      item->setData(0, kCaseRole, qs(c.id));
      item->setIcon(
          0, QIcon(cube_painter::diagram(maskedFacelets(c.state, spec.mask),
                                         spec.view, 40, l, dpr)));
      item->setText(1, QString::number(c.algs.size()));
      item->setText(2, c.algs.empty() ? tr("(none yet)") : qs(c.algs.front()));
      if (c.id == caseId_) {
        current = item;
      }
    }
    groupItem->setExpanded(true);
  }
  if (current) {
    caseTree_->setCurrentItem(current);
  }
}

void LibraryPage::loadCase() {
  AlgCase *c = currentCase();
  caseBox_->setEnabled(c != nullptr);
  loading_ = true;
  caseNameEdit_->setText(c ? qs(c->name) : QString());
  loading_ = false;
  if (!c) {
    caseDiagram_->clear();
    algList_->clear();
    caseBox_->setTitle(tr("Case"));
    return;
  }
  caseBox_->setTitle(tr("Case %1").arg(qs(c->id)));
  AlgSet *s = set();
  const StepSpec &spec = s->specFor(*s->groupOf(c->id));
  caseDiagram_->setPixmap(
      cube_painter::diagram(maskedFacelets(c->state, spec.mask), spec.view, 120,
                            look(), devicePixelRatioF()));
  refreshAlgList();
}

void LibraryPage::refreshAlgList() {
  AlgCase *c = currentCase();
  QSignalBlocker block(algList_);
  loading_ = true;
  int row = algList_->currentRow();
  algList_->clear();
  if (c) {
    AlgSet *s = set();
    const PieceMask &free = s->specFor(*s->groupOf(c->id)).free;
    for (std::size_t i = 0; i < c->algs.size(); ++i) {
      auto *item = new QListWidgetItem(qs(c->algs[i]), algList_);
      item->setFlags(item->flags() | Qt::ItemIsEditable);
      ParseResult parsed = parseAlg(c->algs[i]);
      if (!parsed) {
        item->setForeground(QColor(0xe6, 0x66, 0x66));
        item->setToolTip(tr("Can't read this alg: %1 (at character %2)")
                             .arg(qs(parsed.error->message))
                             .arg(parsed.error->position + 1));
      } else if (auto fit = fitAlg(c->state, parsed.alg, free)) {
        item->setToolTip(tr("Solves the case as %1 (%2 moves)")
                             .arg(qs(withAufs(c->algs[i], *fit)))
                             .arg(moveCount(parsed.alg)));
        if (i == 0) {
          QFont f = item->font();
          f.setBold(true);
          item->setFont(f);
        }
      } else {
        item->setForeground(QColor(0xe6, 0x66, 0x66));
        item->setToolTip(tr("Doesn't solve this case"));
      }
    }
  }
  algList_->setCurrentRow(std::min(row, algList_->count() - 1));
  loading_ = false;
}

void LibraryPage::storeAlgs() {
  AlgCase *c = currentCase();
  if (!c) {
    return;
  }
  c->algs.clear();
  for (int i = 0; i < algList_->count(); ++i) {
    QString text = algList_->item(i)->text().trimmed();
    if (!text.isEmpty()) {
      c->algs.push_back(ss(text));
    }
  }
  refreshAlgList();
  changed(true);
}

void LibraryPage::selectCase(const QString &setId, const QString &caseId) {
  for (std::size_t i = 0; i < library_.sets().size(); ++i) {
    if (library_.sets()[i].id == ss(setId)) {
      setList_->setCurrentRow(static_cast<int>(i));
      break;
    }
  }
  caseId_ = ss(caseId);
  buildCaseTree();
  loadCase();
  newAlgEdit_->setFocus();
}

// --- set actions -------------------------------------------------------------

void LibraryPage::newSet() {
  bool ok = false;
  QString name = QInputDialog::getText(this, tr("New alg set"), tr("Name:"),
                                       QLineEdit::Normal, QString(), &ok);
  if (!ok || name.trimmed().isEmpty()) {
    return;
  }
  QStringList kinds = {
      tr("Last layer, everything solved (like PLL, ZBLL, 1LLL)"),
      tr("Last layer, permutation left (like OLL, COLL-free OLLCP)"),
      tr("First two layers / last slot (LL left, 3D diagrams)"),
      tr("Anything (everything solved, 3D diagrams)")};
  QString kind = QInputDialog::getItem(this, tr("New alg set"),
                                       tr("What do the algs solve?"), kinds, 0,
                                       false, &ok);
  if (!ok) {
    return;
  }
  AlgSet s;
  s.name = ss(name.trimmed());
  s.id = library_.uniqueId(s.name);
  int k = static_cast<int>(kinds.indexOf(kind));
  if (k == 1) {
    s.spec.free = *parseFree("LL:p");
    s.spec.mask = {StickerMode::Orientation, StickerMode::Orientation,
                   StickerMode::Full};
  } else if (k == 2) {
    s.spec.free = *parseFree("LL:po");
    s.spec.mask = {StickerMode::Hidden, StickerMode::Hidden, StickerMode::Full};
    s.spec.view = DiagramView::Iso3D;
    s.aufBefore = true;
    s.aufAfter = false;
  } else if (k == 3) {
    s.spec.view = DiagramView::Iso3D;
    s.aufBefore = false;
    s.aufAfter = false;
  }
  s.groups.push_back({"Cases", std::nullopt, {}});
  setId_ = library_.add(std::move(s)).id;
  refreshSetList();
  loadSet();
  emit setsChanged();
  emit message(tr("Created \"%1\". Add cases with \"New case from alg\" or "
                  "\"Paste algs\".")
                   .arg(name));
}

void LibraryPage::duplicateSet() {
  AlgSet *s = set();
  if (!s) {
    return;
  }
  AlgSet copy = *s;
  copy.builtin = false;
  copy.name += " (copy)";
  copy.id = library_.uniqueId(s->id + "-copy");
  setId_ = library_.add(std::move(copy)).id;
  refreshSetList();
  loadSet();
  emit setsChanged();
}

void LibraryPage::deleteSet() {
  AlgSet *s = set();
  if (!s) {
    return;
  }
  auto answer = QMessageBox::question(
      this, tr("Delete alg set"),
      tr("Delete \"%1\", its algs and your drill progress for it? This can't "
         "be undone.%2")
          .arg(qs(s->name))
          .arg(s->builtin ? tr("\n\nBuilt-in sets don't come back by "
                               "themselves; export it "
                               "first if you may want it again.")
                          : QString()));
  if (answer != QMessageBox::Yes) {
    return;
  }
  library_.remove(setId_);
  setId_ = library_.sets().empty() ? std::string() : library_.sets().front().id;
  refreshSetList();
  loadSet();
  emit setsChanged();
}

void LibraryPage::restoreSet() {
  AlgSet *s = set();
  if (!s || !s->builtin) {
    return;
  }
  QString error;
  if (!library_.restoreBuiltin(setId_, &error)) {
    QMessageBox::warning(this, tr("Restore"), error);
    return;
  }
  loadSet();
  refreshSetList();
  emit setsChanged();
  emit message(tr("Restored the built-in cases of %1; your algs were kept.")
                   .arg(qs(set()->name)));
}

void LibraryPage::importSetFile() {
  QString path = QFileDialog::getOpenFileName(
      this, tr("Import alg set"), QString(), tr("v-cube alg set (*.json)"));
  if (path.isEmpty()) {
    return;
  }
  QString error;
  auto imported = readAlgSetFile(path, &error);
  if (!imported) {
    QMessageBox::warning(this, tr("Import alg set"),
                         tr("Could not import: %1").arg(error));
    return;
  }
  if (library_.find(imported->id)) {
    imported->id = library_.uniqueId(imported->id);
    imported->name += " (imported)";
  }
  imported->builtin = false;
  setId_ = library_.add(std::move(*imported)).id;
  refreshSetList();
  loadSet();
  emit setsChanged();
}

void LibraryPage::exportSetFile() {
  AlgSet *s = set();
  if (!s) {
    return;
  }
  QString path = QFileDialog::getSaveFileName(this, tr("Export alg set"),
                                              qs(s->id) + ".json",
                                              tr("v-cube alg set (*.json)"));
  if (path.isEmpty()) {
    return;
  }
  QString error;
  if (!writeAlgSetFile(*s, path, &error)) {
    QMessageBox::warning(this, tr("Export alg set"), error);
    return;
  }
  emit message(tr("Exported %1 to %2").arg(qs(s->name), path));
}

// --- group and case actions
// ----------------------------------------------------

void LibraryPage::newGroup() {
  AlgSet *s = set();
  if (!s) {
    return;
  }
  bool ok = false;
  QString name = QInputDialog::getText(this, tr("New group"), tr("Group name:"),
                                       QLineEdit::Normal, QString(), &ok);
  if (!ok || name.trimmed().isEmpty()) {
    return;
  }
  s->groups.push_back({ss(name.trimmed()), std::nullopt, {}});
  changed(true);
  loadSet();
}

void LibraryPage::renameGroup() {
  AlgSet *s = set();
  QTreeWidgetItem *item = caseTree_->currentItem();
  if (!s || !item) {
    return;
  }
  if (item->parent()) {
    item = item->parent();
  }
  auto gi = static_cast<std::size_t>(item->data(0, kGroupRole).toInt());
  bool ok = false;
  QString name =
      QInputDialog::getText(this, tr("Rename group"), tr("Group name:"),
                            QLineEdit::Normal, qs(s->groups[gi].name), &ok);
  if (!ok || name.trimmed().isEmpty()) {
    return;
  }
  s->groups[gi].name = ss(name.trimmed());
  changed(true);
  loadSet();
}

void LibraryPage::deleteSelected() {
  AlgSet *s = set();
  QTreeWidgetItem *item = caseTree_->currentItem();
  if (!s || !item) {
    return;
  }
  if (!item->parent()) {
    auto gi = static_cast<std::size_t>(item->data(0, kGroupRole).toInt());
    auto answer =
        QMessageBox::question(this, tr("Delete group"),
                              tr("Delete group \"%1\" and its %2 cases?")
                                  .arg(qs(s->groups[gi].name))
                                  .arg(s->groups[gi].cases.size()));
    if (answer != QMessageBox::Yes) {
      return;
    }
    s->groups.erase(s->groups.begin() + static_cast<std::ptrdiff_t>(gi));
  } else {
    std::string id = ss(item->data(0, kCaseRole).toString());
    auto answer = QMessageBox::question(
        this, tr("Delete case"),
        tr("Delete case \"%1\" and its algs?").arg(item->text(0)));
    if (answer != QMessageBox::Yes) {
      return;
    }
    for (auto &g : s->groups) {
      std::erase_if(g.cases, [&id](const AlgCase &c) { return c.id == id; });
    }
    caseId_.clear();
  }
  changed(true);
  loadSet();
  refreshSetList();
}

void LibraryPage::newCaseFromAlg() {
  AlgSet *s = set();
  if (!s) {
    return;
  }
  bool ok = false;
  QString text = QInputDialog::getText(this, tr("New case"),
                                       tr("An alg that solves the new case:"),
                                       QLineEdit::Normal, QString(), &ok);
  if (!ok || text.trimmed().isEmpty()) {
    return;
  }
  ParseResult parsed = parseAlg(ss(text));
  if (!parsed || parsed.alg.empty()) {
    QMessageBox::warning(
        this, tr("New case"),
        tr("Can't read that alg: %1")
            .arg(parsed.error ? qs(parsed.error->message) : tr("empty")));
    return;
  }
  if (auto ref = matchCase(*s, parsed.alg)) {
    AlgCase &existing = s->groups[ref->group].cases[ref->index];
    auto answer =
        QMessageBox::question(this, tr("New case"),
                              tr("That alg solves the existing case \"%1\". "
                                 "Add it to that case instead?")
                                  .arg(qs(existing.name)));
    if (answer == QMessageBox::Yes) {
      existing.algs.push_back(ss(text.trimmed()));
      caseId_ = existing.id;
      changed(true);
      loadCase();
    }
    return;
  }
  QString name = QInputDialog::getText(
      this, tr("New case"), tr("Case name:"), QLineEdit::Normal,
      tr("Case %1").arg(s->caseCount() + 1), &ok);
  if (!ok) {
    return;
  }
  if (s->groups.empty()) {
    s->groups.push_back({"Cases", std::nullopt, {}});
  }
  // Add to the selected group, or the last one.
  std::size_t gi = s->groups.size() - 1;
  if (QTreeWidgetItem *item = caseTree_->currentItem()) {
    QTreeWidgetItem *groupItem = item->parent() ? item->parent() : item;
    gi = static_cast<std::size_t>(groupItem->data(0, kGroupRole).toInt());
  }
  AlgCase c;
  c.name = ss(name.trimmed().isEmpty() ? tr("Case %1").arg(s->caseCount() + 1)
                                       : name.trimmed());
  c.id = s->uniqueCaseId(c.name);
  c.state = stateSolvedBy(parsed.alg);
  c.algs.push_back(ss(text.trimmed()));
  caseId_ = c.id;
  s->groups[gi].cases.push_back(std::move(c));
  if (stepSolved(s->groups[gi].cases.back().state,
                 s->specFor(s->groups[gi]).free)) {
    emit message(tr("Note: with this set's \"left unsolved\" setting, that "
                    "case already counts as solved."));
  }
  changed(true);
  refreshSetList();
  loadCase();
}

void LibraryPage::pasteAlgs() {
  AlgSet *s = set();
  if (!s) {
    return;
  }
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Paste algs into %1").arg(qs(s->name)));
  auto *layout = new QVBoxLayout(&dialog);
  layout->addWidget(new QLabel(tr("One case per line. Separate several algs "
                                  "for a case with \"/\"; a line may start "
                                  "with \"Name:\". Each alg is matched to the "
                                  "case it solves (any AUF or angle), "
                                  "so the order doesn't matter."),
                               &dialog));
  auto *edit = new QPlainTextEdit(&dialog);
  edit->setPlaceholderText(
      "Ua: M2 U M U2 M' U M2 / R U' R U R U R U' R' U' R2\n"
      "T: R U R' U' R' F R2 U' R' U' R U R' F'");
  layout->addWidget(edit, 1);
  auto *create = new QCheckBox(
      tr("Create new cases for algs that match no case"), &dialog);
  create->setChecked(!s->builtin);
  layout->addWidget(create);
  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->button(QDialogButtonBox::Ok)->setText(tr("Import"));
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  dialog.resize(640, 420);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  ImportReport report =
      importAlgs(*s, ss(edit->toPlainText()), create->isChecked());
  changed(true);
  refreshSetList();
  loadCase();
  QMessageBox box(this);
  box.setWindowTitle(tr("Paste algs"));
  box.setTextFormat(Qt::RichText);
  box.setText(reportText(report));
  box.exec();
}

void LibraryPage::addAlg() {
  AlgCase *c = currentCase();
  QString text = newAlgEdit_->text().trimmed();
  if (!c || text.isEmpty()) {
    return;
  }
  ParseResult parsed = parseAlg(ss(text));
  if (!parsed) {
    emit message(tr("Can't read that alg: %1").arg(qs(parsed.error->message)));
    return;
  }
  AlgSet *s = set();
  if (!fitAlg(c->state, parsed.alg, s->specFor(*s->groupOf(c->id)).free)) {
    auto answer = QMessageBox::question(
        this, tr("Add alg"),
        tr("That alg doesn't solve this case. Add it anyway?"));
    if (answer != QMessageBox::Yes) {
      return;
    }
  }
  c->algs.push_back(ss(text));
  newAlgEdit_->clear();
  refreshAlgList();
  changed(true);
}

void LibraryPage::moveAlg(int delta) {
  AlgCase *c = currentCase();
  int row = algList_->currentRow();
  int to = row + delta;
  if (!c || row < 0 || to < 0 || to >= static_cast<int>(c->algs.size())) {
    return;
  }
  std::swap(c->algs[static_cast<std::size_t>(row)],
            c->algs[static_cast<std::size_t>(to)]);
  refreshAlgList();
  algList_->setCurrentRow(to);
  changed(true);
}

void LibraryPage::removeAlg() {
  AlgCase *c = currentCase();
  int row = algList_->currentRow();
  if (!c || row < 0 || row >= static_cast<int>(c->algs.size())) {
    return;
  }
  c->algs.erase(c->algs.begin() + row);
  refreshAlgList();
  changed(true);
}

void LibraryPage::makeMain() {
  AlgCase *c = currentCase();
  int row = algList_->currentRow();
  if (!c || row <= 0 || row >= static_cast<int>(c->algs.size())) {
    return;
  }
  std::string alg = c->algs[static_cast<std::size_t>(row)];
  c->algs.erase(c->algs.begin() + row);
  c->algs.insert(c->algs.begin(), alg);
  refreshAlgList();
  algList_->setCurrentRow(0);
  changed(true);
}

} // namespace vcube
