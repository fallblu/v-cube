#include "app/library_page.hpp"

#include "app/case_filter.hpp"
#include "app/cube_painter.hpp"
#include "app/set_properties_dialog.hpp"
#include "train/reduction.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDropEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

#include <set>

namespace vcube {

namespace {

constexpr int kCaseRole = Qt::UserRole;
constexpr int kGroupRole = Qt::UserRole + 1;

const StatusColour kPass{Status::Good};
const StatusColour kWarn{Status::Warning};
const QColor kFail(0xe6, 0x66, 0x66);

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

QToolButton *menuButton(const QString &text, QMenu *menu, QWidget *parent) {
  auto *b = new QToolButton(parent);
  b->setText(text);
  b->setMenu(menu);
  b->setPopupMode(QToolButton::InstantPopup);
  b->setToolButtonStyle(Qt::ToolButtonTextOnly);
  return b;
}

} // namespace

// --- AlgListWidget
// -------------------------------------------------------------

AlgListWidget::AlgListWidget(QWidget *parent) : QTreeWidget(parent) {
  setColumnCount(2);
  setHeaderLabels({tr("Alg"), tr("Check")});
  setRootIsDecorated(false);
  setUniformRowHeights(true);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setDragDropMode(QAbstractItemView::InternalMove);
  setDefaultDropAction(Qt::MoveAction);
  setDragDropOverwriteMode(false);
  setEditTriggers(QAbstractItemView::EditKeyPressed);
  setContextMenuPolicy(Qt::CustomContextMenu);
  header()->setSectionResizeMode(0, QHeaderView::Stretch);
  header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  header()->setStretchLastSection(false);
}

void AlgListWidget::dropEvent(QDropEvent *event) {
  QTreeWidget::dropEvent(event);
  emit reordered();
}

// --- LibraryPage
// -----------------------------------------------------------------

LibraryPage::LibraryPage(AppSettings &settings, AlgLibrary &library,
                         QWidget *parent)
    : ModePage(parent), settings_(settings), library_(library) {
  // --- sets ---
  auto *left = new QWidget(this);
  auto *leftLayout = new QVBoxLayout(left);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  setMenu_ = new QMenu(this);
  auto *setHeader = new QHBoxLayout;
  setHeader->addWidget(new QLabel(tr("<b>Alg sets</b>"), left));
  setHeader->addStretch();
  setHeader->addWidget(menuButton(tr("Set"), setMenu_, left));
  leftLayout->addLayout(setHeader);
  setList_ = new QListWidget(left);
  setList_->setContextMenuPolicy(Qt::CustomContextMenu);
  setList_->setToolTip(tr("Double-click a set to edit its properties"));
  leftLayout->addWidget(setList_, 1);

  // --- cases ---
  auto *middle = new QWidget(this);
  auto *middleLayout = new QVBoxLayout(middle);
  middleLayout->setContentsMargins(0, 0, 0, 0);
  caseTree_ = new QTreeWidget(middle);
  caseTree_->setColumnCount(4);
  caseTree_->setHeaderLabels(
      {tr("Case"), tr("Algs"), tr("Check"), tr("Main alg")});
  caseTree_->setIconSize(QSize(40, 40));
  caseTree_->setIndentation(14);
  caseTree_->setContextMenuPolicy(Qt::CustomContextMenu);
  for (int c = 0; c < 3; ++c) {
    caseTree_->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
  }
  caseTree_->header()->setStretchLastSection(true);
  caseFilter_ = new CaseFilter(caseTree_, middle);
  auto caseOf = [this](const QTreeWidgetItem *item) {
    const AlgSet *s = set();
    return s ? s->findCase(ss(item->data(0, kCaseRole).toString())) : nullptr;
  };
  auto analysis = [this](const QTreeWidgetItem *item) {
    return analysisOf(ss(item->data(0, kCaseRole).toString()));
  };
  caseFilter_->setStatuses({
      {tr("No algs"),
       [caseOf](const QTreeWidgetItem *item) {
         const AlgCase *c = caseOf(item);
         return c && c->algs.empty();
       }},
      {tr("No passing alg"),
       [caseOf, analysis](const QTreeWidgetItem *item) {
         const AlgCase *c = caseOf(item);
         const CaseAnalysis *a = analysis(item);
         return c && !c->algs.empty() && (!a || !a->main);
       }},
      {tr("Failing algs"),
       [analysis](const QTreeWidgetItem *item) {
         const CaseAnalysis *a = analysis(item);
         return a && a->failing() > 0;
       }},
  });
  middleLayout->addWidget(caseFilter_);
  middleLayout->addWidget(caseTree_, 1);
  editMenu_ = new QMenu(this);
  auto *caseButtons = new QHBoxLayout;
  auto *pasteButton = new QPushButton(tr("Paste algs…"), middle);
  pasteButton->setToolTip(
      tr("Paste a list of algs; each is matched to the case it solves"));
  caseButtons->addWidget(menuButton(tr("Edit"), editMenu_, middle));
  caseButtons->addStretch();
  caseButtons->addWidget(pasteButton);
  middleLayout->addLayout(caseButtons);

  // --- the case ---
  caseBox_ = new QGroupBox(tr("Case"), this);
  auto *caseLayout = new QVBoxLayout(caseBox_);
  auto *caseTop = new QHBoxLayout;
  caseDiagram_ = new QLabel(caseBox_);
  caseDiagram_->setFixedSize(120, 120);
  auto *caseForm = new QFormLayout;
  caseNameEdit_ = new QLineEdit(caseBox_);
  caseForm->addRow(tr("Name:"), caseNameEdit_);
  caseSummary_ = new QLabel(caseBox_);
  caseSummary_->setWordWrap(true);
  caseForm->addRow(caseSummary_);
  caseTop->addWidget(caseDiagram_);
  caseTop->addLayout(caseForm, 1);
  caseLayout->addLayout(caseTop);
  auto *algHelp =
      new QLabel(tr("The first passing alg is the main alg. Drag to reorder, "
                    "double-click to edit, right-click for more."),
                 caseBox_);
  algHelp->setWordWrap(true);
  caseLayout->addWidget(algHelp);
  algList_ = new AlgListWidget(caseBox_);
  caseLayout->addWidget(algList_, 1);
  auto *addRow = new QHBoxLayout;
  newAlgEdit_ = new QLineEdit(caseBox_);
  newAlgEdit_->setPlaceholderText(tr("New alg, e.g. R U R' U R U2 R'"));
  auto *addButton = new QPushButton(tr("Add"), caseBox_);
  addRow->addWidget(newAlgEdit_, 1);
  addRow->addWidget(addButton);
  caseLayout->addLayout(addRow);

  auto *split = new QSplitter(this);
  split->addWidget(left);
  split->addWidget(middle);
  split->addWidget(caseBox_);
  split->setStretchFactor(1, 1);
  split->setStretchFactor(2, 1);
  split->setSizes({220, 520, 460});
  auto *layout = new QHBoxLayout(this);
  layout->addWidget(split);

  // --- wiring ---
  connect(setMenu_, &QMenu::aboutToShow, this,
          [this] { fillSetMenu(setMenu_); });
  connect(editMenu_, &QMenu::aboutToShow, this,
          [this] { fillEditMenu(editMenu_); });
  connect(setList_, &QListWidget::currentRowChanged, this, [this](int row) {
    if (row >= 0 && row < static_cast<int>(library_.sets().size())) {
      setId_ = library_.sets()[static_cast<std::size_t>(row)].id;
      caseId_.clear();
      loadSet();
    }
  });
  connect(setList_, &QListWidget::itemDoubleClicked, this,
          &LibraryPage::editProperties);
  connect(setList_, &QListWidget::customContextMenuRequested, this,
          [this](const QPoint &pos) {
            if (QListWidgetItem *item = setList_->itemAt(pos)) {
              setList_->setCurrentItem(item);
            }
            QMenu menu(this);
            fillSetMenu(&menu);
            menu.exec(setList_->viewport()->mapToGlobal(pos));
          });
  connect(caseTree_, &QTreeWidget::customContextMenuRequested, this,
          [this](const QPoint &pos) {
            if (QTreeWidgetItem *item = caseTree_->itemAt(pos)) {
              caseTree_->setCurrentItem(item);
            }
            QMenu menu(this);
            fillEditMenu(&menu);
            menu.exec(caseTree_->viewport()->mapToGlobal(pos));
          });
  connect(caseTree_, &QTreeWidget::currentItemChanged, this,
          [this](QTreeWidgetItem *item) {
            caseId_ =
                item ? ss(item->data(0, kCaseRole).toString()) : std::string();
            loadCase();
          });
  connect(pasteButton, &QPushButton::clicked, this, &LibraryPage::pasteAlgs);
  connect(caseNameEdit_, &QLineEdit::editingFinished, this, [this] {
    if (AlgCase *c = currentCase();
        c && !loading_ && ss(caseNameEdit_->text().trimmed()) != c->name &&
        !caseNameEdit_->text().trimmed().isEmpty()) {
      c->name = ss(caseNameEdit_->text().trimmed());
      changed(true);
    }
  });
  connect(addButton, &QPushButton::clicked, this, &LibraryPage::addAlg);
  connect(newAlgEdit_, &QLineEdit::returnPressed, this, &LibraryPage::addAlg);
  connect(algList_, &QTreeWidget::itemChanged, this,
          [this](QTreeWidgetItem *item, int column) {
            if (!loading_ && column == 0) {
              std::string text = ss(item->text(0).trimmed());
              storeAlgs();
              warnIfFailing(text);
            }
          });
  connect(algList_, &QTreeWidget::itemDoubleClicked, this,
          [this](QTreeWidgetItem *item) { algList_->editItem(item, 0); });
  connect(algList_, &AlgListWidget::reordered, this, &LibraryPage::storeAlgs);
  connect(algList_, &QTreeWidget::customContextMenuRequested, this,
          &LibraryPage::showAlgMenu);

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

const CaseAnalysis *LibraryPage::analysisOf(const std::string &caseId) const {
  auto it = analysis_.find(caseId);
  return it == analysis_.end() ? nullptr : &it->second;
}

CubeLook LibraryPage::look() const {
  CubeLook l;
  l.scheme = settings_.scheme;
  l.map = colourMapOf(settings_.neutrality.main);
  return l;
}

void LibraryPage::applySettings() {
  buildCaseTree();
  loadCase();
}

void LibraryPage::analyze() {
  analysis_.clear();
  if (AlgSet *s = set()) {
    for (const auto &g : s->groups) {
      for (const auto &c : g.cases) {
        analysis_[c.id] = analyzeCase(*s, g, c);
      }
    }
  }
}

void LibraryPage::changed(bool rebuildTree) {
  if (AlgSet *s = set()) {
    QString error;
    if (!library_.save(*s, &error)) {
      emit message(tr("Could not save %1: %2").arg(qs(s->name), error));
    }
  }
  analyze();
  refreshSetList(); // failure counts
  if (rebuildTree) {
    buildCaseTree();
  }
  emit setsChanged(qs(setId_));
}

// --- menus -------------------------------------------------------------------

void LibraryPage::fillSetMenu(QMenu *menu) {
  menu->clear();
  const AlgSet *s = set();
  menu->addAction(tr("New set…"), this, &LibraryPage::newSet);
  menu->addAction(tr("Duplicate"), this, &LibraryPage::duplicateSet)
      ->setEnabled(s);
  menu->addAction(tr("Delete…"), this, &LibraryPage::deleteSet)->setEnabled(s);
  menu->addSeparator();
  menu->addAction(tr("Set properties…"), this, &LibraryPage::editProperties)
      ->setEnabled(s);
  menu->addAction(tr("Check algs…"), this, &LibraryPage::checkAlgs)
      ->setEnabled(s);
  menu->addSeparator();
  menu->addAction(tr("Import…"), this, &LibraryPage::importSetFile);
  menu->addAction(tr("Export…"), this, &LibraryPage::exportSetFile)
      ->setEnabled(s);
  QAction *restore = menu->addAction(tr("Restore built-in cases"), this,
                                     &LibraryPage::restoreSet);
  restore->setEnabled(s && s->builtin);
  restore->setToolTip(tr("Reload this built-in set's cases and settings as "
                         "shipped, keeping your algs"));
  menu->setToolTipsVisible(true);
}

void LibraryPage::fillEditMenu(QMenu *menu) {
  menu->clear();
  const AlgSet *s = set();
  QTreeWidgetItem *item = caseTree_->currentItem();
  menu->addAction(tr("New group…"), this, &LibraryPage::newGroup)
      ->setEnabled(s);
  menu->addAction(tr("Rename group…"), this, &LibraryPage::renameGroup)
      ->setEnabled(s && item);
  menu->addAction(item && item->parent() ? tr("Delete case…")
                                         : tr("Delete group…"),
                  this, &LibraryPage::deleteSelected)
      ->setEnabled(s && item);
  menu->addSeparator();
  menu->addAction(tr("New case from alg…"), this, &LibraryPage::newCaseFromAlg)
      ->setEnabled(s);
  menu->addAction(tr("Paste algs…"), this, &LibraryPage::pasteAlgs)
      ->setEnabled(s);
}

// --- sets and cases
// --------------------------------------------------------------

void LibraryPage::refreshSetList() {
  QSignalBlocker block(setList_);
  setList_->clear();
  int current = -1;
  for (std::size_t i = 0; i < library_.sets().size(); ++i) {
    const AlgSet &s = library_.sets()[i];
    int failing = 0;
    if (s.id == setId_) {
      for (const auto &[id, a] : analysis_) {
        failing += a.failing();
      }
    } else {
      for (const auto &g : s.groups) {
        for (const auto &c : g.cases) {
          failing += analyzeCase(s, g, c).failing();
        }
      }
    }
    QString text = QString("%1  (%2)").arg(qs(s.name)).arg(s.caseCount());
    QStringList tips;
    if (s.builtin) {
      tips << tr("Built-in cases; the algs are yours.");
    }
    auto *item = new QListWidgetItem(setList_);
    if (failing > 0) {
      text += "  " + tr("⚠ %1 failing").arg(failing);
      item->setForeground(kWarn);
      tips << (failing == 1
                   ? tr("1 alg doesn't solve its case.")
                   : tr("%1 algs don't solve their cases.").arg(failing))
           << tr("Set > Check algs lists them.");
    }
    item->setText(text);
    item->setToolTip(tips.join(" "));
    if (s.id == setId_) {
      current = static_cast<int>(i);
    }
  }
  setList_->setCurrentRow(current);
}

void LibraryPage::loadSet() {
  analyze();
  refreshSetList();
  buildCaseTree();
  loadCase();
}

void LibraryPage::buildCaseTree() {
  QSignalBlocker block(caseTree_);
  // Keep the scroll position and collapsed groups of the same set.
  bool sameSet = caseTreeSet_ == setId_;
  caseTreeSet_ = setId_;
  int scroll = sameSet ? caseTree_->verticalScrollBar()->value() : 0;
  QSet<QString> collapsed;
  for (int g = 0; sameSet && g < caseTree_->topLevelItemCount(); ++g) {
    if (!caseTree_->topLevelItem(g)->isExpanded()) {
      collapsed.insert(caseTree_->topLevelItem(g)->text(0));
    }
  }
  caseTree_->clear();
  AlgSet *s = set();
  if (!s) {
    return;
  }
  CubeLook l = look();
  qreal dpr = devicePixelRatioF();
  QColor muted = palette().color(QPalette::PlaceholderText);
  QTreeWidgetItem *current = nullptr;
  for (std::size_t gi = 0; gi < s->groups.size(); ++gi) {
    const CaseGroup &g = s->groups[gi];
    auto *groupItem = new QTreeWidgetItem(caseTree_, {qs(g.name)});
    groupItem->setData(0, kGroupRole, static_cast<int>(gi));
    groupItem->setText(1, QString::number(g.cases.size()));
    if (g.spec) {
      groupItem->setToolTip(0, tr("This group has its own step settings"));
    }
    const StepSpec &spec = s->specFor(g);
    int groupFailing = 0;
    for (const auto &c : g.cases) {
      const CaseAnalysis *a = analysisOf(c.id);
      auto *item = new QTreeWidgetItem(groupItem, {qs(c.name)});
      item->setData(0, kCaseRole, qs(c.id));
      item->setIcon(
          0, QIcon(cube_painter::cachedDiagram(
                 maskedFacelets(a ? a->display : c.state, spec.mask, spec.free),
                 spec.view, 40, l, dpr)));
      item->setText(1, QString::number(c.algs.size()));
      int failing = a ? a->failing() : 0;
      groupFailing += failing;
      if (c.algs.empty()) {
        item->setText(2, tr("no algs"));
        item->setForeground(2, muted);
        item->setText(3, tr("(none yet)"));
        item->setForeground(3, muted);
      } else if (!a || !a->main) {
        item->setText(2, tr("✗ no passing alg"));
        item->setForeground(2, kFail);
        item->setText(3, tr("(no alg solves this case)"));
        item->setForeground(3, kFail);
      } else {
        if (failing > 0) {
          item->setText(2, tr("⚠ %1 failing").arg(failing));
          item->setForeground(2, kWarn);
        } else {
          item->setText(2, "✓");
          item->setForeground(2, kPass);
        }
        item->setText(3, qs(c.algs[*a->main]));
      }
      if (c.id == caseId_) {
        current = item;
      }
    }
    if (groupFailing > 0) {
      groupItem->setText(2, tr("⚠ %1 failing").arg(groupFailing));
      groupItem->setForeground(2, kWarn);
    }
    groupItem->setExpanded(!collapsed.contains(qs(g.name)));
  }
  caseFilter_->apply();
  caseTree_->verticalScrollBar()->setValue(scroll);
  if (current) {
    caseTree_->setCurrentItem(current);
  }
}

void LibraryPage::deactivated() { caseFilter_->release(); }

void LibraryPage::loadCase() {
  AlgCase *c = currentCase();
  caseBox_->setEnabled(c != nullptr);
  loading_ = true;
  caseNameEdit_->setText(c ? qs(c->name) : QString());
  loading_ = false;
  if (!c) {
    caseDiagram_->clear();
    caseSummary_->clear();
    QSignalBlocker block(algList_);
    algList_->clear();
    caseBox_->setTitle(tr("Case"));
    return;
  }
  caseBox_->setTitle(tr("Case %1").arg(qs(c->id)));
  AlgSet *s = set();
  const StepSpec &spec = s->specFor(*s->groupOf(c->id));
  const CaseAnalysis *a = analysisOf(c->id);
  caseDiagram_->setPixmap(cube_painter::diagram(
      maskedFacelets(a ? a->display : c->state, spec.mask, spec.free),
      spec.view, 120, look(), devicePixelRatioF()));
  if (a && a->main && s->reduction) {
    // Every step from this case to the end, by the set's main algs.
    Chain chain = solveChain(*s, c->state);
    QStringList steps;
    for (const ChainStep &step : chain.steps) {
      steps << qs(step.alg);
    }
    caseSummary_->setText(
        chain.complete
            ? (steps.size() == 1 ? tr("Finishes the step: %1")
                                 : tr("To the end, a step at a time: %1"))
                  .arg(steps.join("  ·  "))
            : tr("Its steps don't reach the end: %1").arg(steps.join("  ·  ")));
  } else if (a && a->main) {
    caseSummary_->setText(
        tr("Shown as the main alg starts: %1").arg(qs(a->mainAlg)));
  } else {
    caseSummary_->setText(c->algs.empty() ? tr("No algs yet.")
                                          : tr("No alg solves this case yet."));
  }
  refreshAlgList();
}

void LibraryPage::refreshAlgList() {
  AlgCase *c = currentCase();
  QSignalBlocker block(algList_);
  loading_ = true;
  int row = algList_->currentItem()
                ? algList_->indexOfTopLevelItem(algList_->currentItem())
                : -1;
  algList_->clear();
  if (c) {
    const CaseAnalysis *a = analysisOf(c->id);
    for (std::size_t i = 0; i < c->algs.size(); ++i) {
      auto *item = new QTreeWidgetItem(algList_, {qs(c->algs[i])});
      // Items can be dragged and edited but not dropped onto (no nesting).
      item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled |
                     Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
      if (!a || i >= a->checks.size()) {
        continue;
      }
      const AlgCheck &check = a->checks[i];
      QString result = qs(describeCheck(check));
      if (check.passes()) {
        bool isMain = a->main == i;
        item->setText(1, (isMain ? tr("✓ main · %1") : tr("✓ %1")).arg(result));
        item->setForeground(1, kPass);
        if (isMain) {
          QFont f = item->font(0);
          f.setBold(true);
          item->setFont(0, f);
        }
      } else {
        item->setText(1, tr("✗ %1").arg(result));
        item->setForeground(1, kFail);
        item->setForeground(0, kFail);
      }
      if (check.parseError) {
        item->setToolTip(1, qs(check.parseError->message));
      }
    }
  }
  if (row >= 0 && row < algList_->topLevelItemCount()) {
    algList_->setCurrentItem(algList_->topLevelItem(row));
  }
  loading_ = false;
}

void LibraryPage::storeAlgs() {
  AlgCase *c = currentCase();
  if (!c) {
    return;
  }
  c->algs.clear();
  for (int i = 0; i < algList_->topLevelItemCount(); ++i) {
    std::string text = cleanAlgText(ss(algList_->topLevelItem(i)->text(0)));
    if (!text.empty()) {
      c->algs.push_back(text);
    }
  }
  changed(true);
  loadCase();
}

void LibraryPage::warnIfFailing(const std::string &alg) {
  const AlgCase *c = currentCase();
  const CaseAnalysis *a = c ? analysisOf(c->id) : nullptr;
  if (!a) {
    return;
  }
  for (std::size_t i = 0; i < c->algs.size() && i < a->checks.size(); ++i) {
    if (c->algs[i] == alg && !a->checks[i].passes()) {
      emit message(tr("Kept \"%1\", but it doesn't pass: %2")
                       .arg(qs(alg), qs(describeCheck(a->checks[i]))));
      return;
    }
  }
}

void LibraryPage::showAlgMenu(const QPoint &pos) {
  QTreeWidgetItem *item = algList_->itemAt(pos);
  if (!item) {
    return;
  }
  int row = algList_->indexOfTopLevelItem(item);
  algList_->setCurrentItem(item);
  QMenu menu(this);
  menu.addAction(tr("Make main"), this, [this, row] { makeMain(row); })
      ->setEnabled(row > 0);
  menu.addAction(tr("Edit"), this,
                 [this, item] { algList_->editItem(item, 0); });
  menu.addSeparator();
  menu.addAction(tr("Remove"), this, [this, row] { removeAlg(row); });
  menu.exec(algList_->viewport()->mapToGlobal(pos));
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
    s.spec.mask = {StickerMode::Hidden, StickerMode::Hidden, StickerMode::Full,
                   true};
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
  emit setsChanged({});
  emit message(tr("Created \"%1\". Add cases with Edit > New case from alg, "
                  "or Paste algs.")
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
  emit setsChanged({});
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
  emit setsChanged({});
}

void LibraryPage::restoreSet() {
  AlgSet *s = set();
  if (!s || !s->builtin) {
    return;
  }
  auto answer = QMessageBox::question(
      this, tr("Restore built-in cases"),
      tr("Restore the built-in cases of %1? Cases you deleted come back, and "
         "the shipped names and states replace yours. Your algs, the cases "
         "and groups you added and the set's settings are kept.")
          .arg(qs(s->name)));
  if (answer != QMessageBox::Yes) {
    return;
  }
  QString error;
  if (!library_.restoreBuiltin(setId_, &error)) {
    QMessageBox::warning(this, tr("Restore"), error);
    return;
  }
  loadSet();
  refreshSetList();
  emit setsChanged(qs(setId_));
  emit message(tr("Restored the built-in cases of %1; your algs, cases and "
                  "settings were kept.")
                   .arg(qs(set()->name)));
}

void LibraryPage::editProperties() {
  AlgSet *s = set();
  if (!s) {
    return;
  }
  // Start on the selected group if it has its own settings.
  int group = -1;
  if (QTreeWidgetItem *item = caseTree_->currentItem()) {
    QTreeWidgetItem *groupItem = item->parent() ? item->parent() : item;
    int gi = groupItem->data(0, kGroupRole).toInt();
    if (gi >= 0 && gi < static_cast<int>(s->groups.size()) &&
        (item == groupItem ||
         s->groups[static_cast<std::size_t>(gi)].spec.has_value())) {
      group = gi;
    }
  }
  SetPropertiesDialog dialog(*s, group, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  const AlgSet &r = dialog.result();
  s->name = r.name;
  s->description = r.description;
  s->aufBefore = r.aufBefore;
  s->aufAfter = r.aufAfter;
  s->targetSeconds = r.targetSeconds;
  s->guided = r.guided;
  s->families = r.families;
  s->reduction = r.reduction;
  s->mirrors = r.mirrors;
  s->spec = r.spec;
  for (std::size_t i = 0; i < s->groups.size() && i < r.groups.size(); ++i) {
    s->groups[i].spec = r.groups[i].spec;
  }
  changed(true);
  loadCase();
}

void LibraryPage::checkAlgs() {
  AlgSet *s = set();
  if (!s) {
    return;
  }
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Check algs: %1").arg(qs(s->name)));
  auto *layout = new QVBoxLayout(&dialog);
  auto *tree = new QTreeWidget(&dialog);
  tree->setColumnCount(3);
  tree->setHeaderLabels({tr("Case"), tr("Alg"), tr("Problem")});
  tree->setRootIsDecorated(false);
  int total = 0, failing = 0, noPassing = 0;
  for (const auto &g : s->groups) {
    for (const auto &c : g.cases) {
      const CaseAnalysis *a = analysisOf(c.id);
      if (!a) {
        continue;
      }
      total += static_cast<int>(c.algs.size());
      noPassing += !c.algs.empty() && !a->main ? 1 : 0;
      for (std::size_t i = 0; i < c.algs.size(); ++i) {
        if (a->checks[i].passes()) {
          continue;
        }
        ++failing;
        auto *item = new QTreeWidgetItem(
            tree, {qs(c.name), qs(c.algs[i]), qs(describeCheck(a->checks[i]))});
        item->setData(0, kCaseRole, qs(c.id));
        item->setForeground(2, kFail);
      }
    }
  }
  for (int c = 0; c < 3; ++c) {
    tree->resizeColumnToContents(c);
  }
  QString summary =
      failing == 0
          ? tr("All %1 algs solve their cases under the current step "
               "settings.")
                .arg(total)
          : tr("%1 of %2 algs don't solve their cases under the current step "
               "settings%3. Double-click one to open its case.")
                .arg(failing)
                .arg(total)
                .arg(noPassing
                         ? tr("; %1 cases have no passing alg").arg(noPassing)
                         : QString());
  auto *label = new QLabel(summary, &dialog);
  label->setWordWrap(true);
  layout->addWidget(label);
  layout->addWidget(tree, 1);
  tree->setVisible(failing > 0);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  connect(tree, &QTreeWidget::itemDoubleClicked, &dialog,
          [this, &dialog](QTreeWidgetItem *item) {
            caseId_ = ss(item->data(0, kCaseRole).toString());
            buildCaseTree();
            loadCase();
            dialog.accept();
          });
  dialog.resize(failing > 0 ? 760 : 420, failing > 0 ? 420 : 120);
  dialog.exec();
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
  if (library_.idTaken(imported->id)) {
    imported->id = library_.uniqueId(imported->id);
    imported->name += " (imported)";
  }
  imported->builtin = false;
  setId_ = library_.add(std::move(*imported)).id;
  refreshSetList();
  loadSet();
  emit setsChanged({});
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

// --- group, case and alg actions ---------------------------------------------

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
    std::vector<std::string> ids;
    for (const AlgCase &c : s->groups[gi].cases) {
      ids.push_back(c.id);
    }
    s->groups.erase(s->groups.begin() + static_cast<std::ptrdiff_t>(gi));
    library_.forgetProgress(setId_, ids);
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
    library_.forgetProgress(setId_, {id});
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
      existing.algs.push_back(cleanAlgText(ss(text)));
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
  library_.forgetProgress(setId_, {c.id});
  c.state = stateSolvedBy(parsed.alg);
  c.algs.push_back(cleanAlgText(ss(text)));
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
  std::set<std::string> before;
  for (const auto &g : s->groups) {
    for (const auto &c : g.cases) {
      before.insert(c.id);
    }
  }
  ImportReport report =
      importAlgs(*s, ss(edit->toPlainText()), create->isChecked());
  std::vector<std::string> created;
  for (const auto &g : s->groups) {
    for (const auto &c : g.cases) {
      if (!before.contains(c.id)) {
        created.push_back(c.id);
      }
    }
  }
  library_.forgetProgress(setId_, created);
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
  std::string text = cleanAlgText(ss(newAlgEdit_->text()));
  if (!c || text.empty()) {
    return;
  }
  // Failing algs are kept, with a warning: the check shows what's wrong.
  c->algs.push_back(text);
  newAlgEdit_->clear();
  changed(true);
  loadCase();
  warnIfFailing(text);
}

void LibraryPage::removeAlg(int row) {
  AlgCase *c = currentCase();
  if (!c || row < 0 || row >= static_cast<int>(c->algs.size())) {
    return;
  }
  c->algs.erase(c->algs.begin() + row);
  changed(true);
  loadCase();
}

void LibraryPage::makeMain(int row) {
  AlgCase *c = currentCase();
  if (!c || row <= 0 || row >= static_cast<int>(c->algs.size())) {
    return;
  }
  const CaseAnalysis *a = analysisOf(c->id);
  if (a && static_cast<std::size_t>(row) < a->checks.size() &&
      !a->checks[static_cast<std::size_t>(row)].passes()) {
    auto answer = QMessageBox::question(
        this, tr("Make main"),
        tr("This alg doesn't pass the check (%1). It moves to the top of the "
           "list but only becomes the main alg once it passes. Move it "
           "anyway?")
            .arg(qs(describeCheck(a->checks[static_cast<std::size_t>(row)]))));
    if (answer != QMessageBox::Yes) {
      return;
    }
  }
  std::string alg = c->algs[static_cast<std::size_t>(row)];
  c->algs.erase(c->algs.begin() + row);
  c->algs.insert(c->algs.begin(), alg);
  changed(true);
  loadCase();
  algList_->setCurrentItem(algList_->topLevelItem(0));
}

} // namespace vcube
