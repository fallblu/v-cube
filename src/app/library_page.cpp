#include "app/library_page.hpp"
#include "app/ui_text.hpp"

#include "app/alglab_common.hpp"
#include "app/case_filter.hpp"
#include "app/cube_painter.hpp"
#include "app/grip_view.hpp"
#include "app/paste_dialog.hpp"
#include "app/set_properties_dialog.hpp"
#include "app/ui_state.hpp"
#include "train/reduction.hpp"

#include <QAction>
#include <QApplication>
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
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QSplitter>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>

namespace vcube {

namespace {

constexpr int kCaseRole = Qt::UserRole;
constexpr int kGroupRole = Qt::UserRole + 1;

const auto &kPass = kPassColour;
const auto &kWarn = kWarnColour;
const auto &kFail = kFailColour;

constexpr int kDiagramSize = 160;

QString drillText(const CaseProgress &p, AttemptMode mode) {
  std::size_t n = p.drillCount(mode);
  if (n == 0) {
    return {};
  }
  QString text =
      counted(static_cast<int>(n), QObject::tr("drill"), QObject::tr("drills"));
  if (auto best = p.best(mode)) {
    text += QObject::tr(", best %1 s")
                .arg(static_cast<double>(*best) / 1000.0, 0, 'f', 2);
  }
  if (auto mean = p.recentMean(mode)) {
    text += QObject::tr(", recent mean %1 s").arg(*mean / 1000.0, 0, 'f', 2);
  }
  return text;
}

} // namespace

// --- AlgListWidget
// -------------------------------------------------------------

AlgListWidget::AlgListWidget(QWidget *parent) : QTreeWidget(parent) {
  setColumnCount(3);
  setHeaderLabels({tr("Alg"), tr("Check"), tr("Alg Lab")});
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

// --- CaseTreeWidget
// ------------------------------------------------------------

CaseTreeWidget::CaseTreeWidget(QWidget *parent) : QTreeWidget(parent) {
  setSelectionMode(QAbstractItemView::ExtendedSelection);
  setDragDropMode(QAbstractItemView::InternalMove);
  setDefaultDropAction(Qt::MoveAction);
  setDragDropOverwriteMode(false);
}

void CaseTreeWidget::dropEvent(QDropEvent *event) {
  QPersistentModelIndex target = indexAt(event->position().toPoint());
  int position = dropIndicatorPosition() == OnItem      ? DropOn
                 : dropIndicatorPosition() == AboveItem ? DropAbove
                 : dropIndicatorPosition() == BelowItem ? DropBelow
                                                        : DropElsewhere;
  // The page moves the cases and rebuilds the tree; the view itself must
  // move nothing, and the drag must not remove the dragged items.
  event->setDropAction(Qt::IgnoreAction);
  event->accept();
  stopAutoScroll();
  setState(NoState);
  viewport()->update();
  QMetaObject::invokeMethod(
      this,
      [this, target, position] {
        emit dropped(target.isValid() ? itemFromIndex(target) : nullptr,
                     position);
      },
      Qt::QueuedConnection);
}

// --- LibraryPage
// -----------------------------------------------------------------

LibraryPage::LibraryPage(AppSettings &settings, AlgLibrary &library,
                         QWidget *parent)
    : ModePage(parent), settings_(settings), library_(library),
      undo_(new QUndoStack(this)),
      editor_(
          library, undo_,
          [this](const LibraryEditor::Change &c) { refresh(c); },
          [this](const QString &text) { emit message(text); }) {
  undo_->setUndoLimit(200); // each step keeps two copies of its set
  buildActions();

  // --- sets ---
  auto *left = new QWidget(this);
  auto *leftLayout = new QVBoxLayout(left);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->addWidget(new QLabel(tr("<b>Alg sets</b>"), left));
  setList_ = new QListWidget(left);
  setList_->setContextMenuPolicy(Qt::CustomContextMenu);
  setList_->setToolTip(tr("Double-click a set to edit its properties"));
  setList_->setSpacing(3);
  leftLayout->addWidget(setList_, 1);

  // --- cases ---
  auto *middle = new QWidget(this);
  auto *middleLayout = new QVBoxLayout(middle);
  middleLayout->setContentsMargins(0, 0, 0, 0);
  caseTree_ = new CaseTreeWidget(middle);
  caseTree_->setColumnCount(4);
  caseTree_->setHeaderLabels(
      {tr("Case"), tr("Algs"), tr("Check"), tr("Main alg")});
  caseTree_->setIconSize(QSize(40, 40));
  caseTree_->setIndentation(14);
  caseTree_->setContextMenuPolicy(Qt::CustomContextMenu);
  caseTree_->setToolTip(
      tr("Ctrl- or Shift-click to select several cases; drag them to "
         "another place or group"));
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
    return editor_.analysisOf(ss(item->data(0, kCaseRole).toString()));
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
  auto *caseButtons = new QHBoxLayout;
  auto *newCaseButton = new QPushButton(tr("New case…"), middle);
  newCaseButton->setToolTip(
      tr("Make a case from an alg that solves it (%1)")
          .arg(newCaseAction_->shortcut().toString(QKeySequence::NativeText)));
  auto *pasteButton = new QPushButton(tr("Paste algs…"), middle);
  pasteButton->setToolTip(
      tr("Paste a list of algs; each is filed under the case its line names "
         "or the case it solves (%1)")
          .arg(pasteAction_->shortcut().toString(QKeySequence::NativeText)));
  caseButtons->addWidget(newCaseButton);
  caseButtons->addStretch();
  caseButtons->addWidget(pasteButton);
  middleLayout->addLayout(caseButtons);

  // --- the case ---
  caseBox_ = new QGroupBox(tr("Case"), this);
  auto *caseLayout = new QVBoxLayout(caseBox_);
  auto *caseTop = new QHBoxLayout;
  caseDiagram_ = new QLabel(caseBox_);
  caseDiagram_->setFixedSize(kDiagramSize, kDiagramSize);
  auto *caseForm = new QFormLayout;
  caseNameEdit_ = new QLineEdit(caseBox_);
  caseForm->addRow(tr("Name:"), caseNameEdit_);
  caseGroup_ = new QLabel(caseBox_);
  caseForm->addRow(tr("Group:"), caseGroup_);
  caseAlgCount_ = new QLabel(caseBox_);
  caseForm->addRow(tr("Algs:"), caseAlgCount_);
  caseProgress_ = new QLabel(caseBox_);
  caseProgress_->setWordWrap(true);
  caseForm->addRow(tr("Progress:"), caseProgress_);
  caseSummary_ = new QLabel(caseBox_);
  caseSummary_->setWordWrap(true);
  caseSummary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  caseForm->addRow(caseSummary_);
  caseTop->addWidget(caseDiagram_, 0, Qt::AlignTop);
  caseTop->addLayout(caseForm, 1);
  caseLayout->addLayout(caseTop);
  auto *algHelp =
      new QLabel(tr("The first passing alg is the main alg. Drag to reorder "
                    "(or Alt+Up and Alt+Down), F2 or double-click to edit, "
                    "Delete to remove, right-click for more."),
                 caseBox_);
  algHelp->setWordWrap(true);
  algHelp->setForegroundRole(QPalette::PlaceholderText);
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
  gripTitle_ = new QLabel(tr("<b>Fingertricks</b>"), caseBox_);
  caseLayout->addWidget(gripTitle_);
  grips_ = new GripView(caseBox_);
  caseLayout->addWidget(grips_);

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
  connect(setList_, &QListWidget::currentRowChanged, this, [this](int row) {
    if (row >= 0 && row < static_cast<int>(library_.sets().size())) {
      editor_.setId = library_.sets()[static_cast<std::size_t>(row)].id;
      editor_.caseId.clear();
      newAlgEdit_->clear();
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
            fillContextMenu(&menu, true);
            menu.exec(setList_->viewport()->mapToGlobal(pos));
          });
  connect(caseTree_, &QTreeWidget::customContextMenuRequested, this,
          [this](const QPoint &pos) {
            QTreeWidgetItem *item = caseTree_->itemAt(pos);
            if (item && !item->isSelected()) {
              caseTree_->setCurrentItem(item);
            }
            QMenu menu(this);
            fillContextMenu(&menu, false);
            menu.exec(caseTree_->viewport()->mapToGlobal(pos));
          });
  connect(caseTree_, &QTreeWidget::currentItemChanged, this,
          [this](QTreeWidgetItem *item) {
            std::string id =
                item ? ss(item->data(0, kCaseRole).toString()) : std::string();
            if (id.empty() && item) {
              return; // a group: keep showing the case
            }
            if (id != editor_.caseId) {
              newAlgEdit_->clear(); // it was meant for the other case
            }
            editor_.caseId = id;
            loadCase();
          });
  connect(caseTree_, &QTreeWidget::itemSelectionChanged, this,
          &LibraryPage::updateActions);
  connect(caseTree_, &CaseTreeWidget::dropped, this, &LibraryPage::dropOnTree);
  connect(pasteButton, &QPushButton::clicked, this, &LibraryPage::pasteAlgs);
  connect(newCaseButton, &QPushButton::clicked, this,
          &LibraryPage::newCaseFromAlg);
  connect(caseNameEdit_, &QLineEdit::editingFinished, this, [this] {
    QString name = caseNameEdit_->text().trimmed();
    AlgCase *c = currentCase();
    if (!c || loading_ || name.isEmpty() || ss(name) == c->name) {
      return;
    }
    auto pending = editor_.begin();
    c->name = ss(name);
    editor_.commit(tr("Rename case"), *pending);
  });
  connect(addButton, &QPushButton::clicked, this, &LibraryPage::addAlg);
  connect(newAlgEdit_, &QLineEdit::returnPressed, this, &LibraryPage::addAlg);
  connect(algList_, &QTreeWidget::itemChanged, this,
          [this](QTreeWidgetItem *item, int column) {
            if (!loading_ && column == 0) {
              int row = algList_->indexOfTopLevelItem(item);
              // After the item's signal: storing rebuilds the list.
              QMetaObject::invokeMethod(
                  this, [this, row] { storeAlgs(tr("Edit alg"), row); },
                  Qt::QueuedConnection);
            }
          });
  connect(algList_, &QTreeWidget::itemDoubleClicked, this,
          [this](QTreeWidgetItem *item) { algList_->editItem(item, 0); });
  connect(algList_, &AlgListWidget::reordered, this,
          [this] { storeAlgs(tr("Reorder algs")); });
  connect(algList_, &QTreeWidget::customContextMenuRequested, this,
          &LibraryPage::showAlgMenu);
  connect(algList_, &QTreeWidget::currentItemChanged, this,
          &LibraryPage::updateActions);
  connect(algList_, &QTreeWidget::currentItemChanged, this,
          &LibraryPage::updateGrips);
  connect(qApp, &QApplication::focusChanged, this, [this] {
    if (active_) {
      updateActions(); // the keys act on the alg list or the case tree
    }
  });

  refreshSetList();
  if (!library_.sets().empty()) {
    setList_->setCurrentRow(0);
  }
  updateActions();
  for (QAction *a : editActions()) {
    a->setVisible(false);
  }
  for (QMenu *m : menus()) {
    m->menuAction()->setVisible(false);
  }
}

// --- actions and menus -----------------------------------------------------

void LibraryPage::buildActions() {
  auto action = [this](const QString &text, auto slot, QKeySequence key = {}) {
    auto *a = new QAction(text, this);
    a->setShortcut(key);
    // The page is hidden in other modes, and so the keys work only here.
    a->setShortcutContext(Qt::WindowShortcut);
    connect(a, &QAction::triggered, this, slot);
    addAction(a);
    return a;
  };
  undoAction_ = undo_->createUndoAction(this, tr("&Undo"));
  undoAction_->setShortcuts(QKeySequence::Undo);
  redoAction_ = undo_->createRedoAction(this, tr("&Redo"));
  QList<QKeySequence> redoKeys = QKeySequence::keyBindings(QKeySequence::Redo);
  if (!redoKeys.contains(QKeySequence(Qt::CTRL | Qt::Key_Y))) {
    redoKeys << QKeySequence(Qt::CTRL | Qt::Key_Y);
  }
  redoAction_->setShortcuts(redoKeys);
  addAction(undoAction_);
  addAction(redoAction_);
  editSeparator_ = new QAction(this);
  editSeparator_->setSeparator(true);

  newSetAction_ = action(tr("&New set…"), &LibraryPage::newSet,
                         QKeySequence(Qt::CTRL | Qt::Key_N));
  duplicateSetAction_ =
      action(tr("D&uplicate set"), &LibraryPage::duplicateSet);
  deleteSetAction_ = action(tr("&Delete set…"), &LibraryPage::deleteSet);
  propertiesAction_ =
      action(tr("Set &properties…"), &LibraryPage::editProperties,
             QKeySequence(Qt::ALT | Qt::Key_Return));
  checkAction_ = action(tr("&Check algs…"), &LibraryPage::checkAlgs,
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K));
  importAction_ = action(tr("&Import set…"), &LibraryPage::importSetFile);
  exportAction_ = action(tr("&Export set…"), &LibraryPage::exportSetFile);
  restoreCasesAction_ =
      action(tr("&Restore built-in cases…"), &LibraryPage::restoreSet);
  restoreCasesAction_->setToolTip(
      tr("Bring back this built-in set's cases as shipped, keeping your "
         "algs"));
  newGroupAction_ = action(tr("New &group…"), &LibraryPage::newGroup,
                           QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G));
  newCaseAction_ =
      action(tr("New &case from alg…"), &LibraryPage::newCaseFromAlg,
             QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A));
  pasteAction_ = action(tr("&Paste algs…"), &LibraryPage::pasteAlgs,
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
  renameAction_ = action(tr("&Rename…"), &LibraryPage::renameSelected,
                         QKeySequence(Qt::Key_F2));
  deleteAction_ = action(tr("&Delete…"), &LibraryPage::deleteSelected,
                         QKeySequence(QKeySequence::Delete));
  moveUpAction_ = action(
      tr("Move &up"), [this] { moveSelected(-1); },
      QKeySequence(Qt::ALT | Qt::Key_Up));
  moveDownAction_ = action(
      tr("Move do&wn"), [this] { moveSelected(1); },
      QKeySequence(Qt::ALT | Qt::Key_Down));

  setMenu_ = new QMenu(tr("&Set"), this);
  setMenu_->setToolTipsVisible(true);
  setMenu_->addAction(newSetAction_);
  setMenu_->addAction(duplicateSetAction_);
  setMenu_->addAction(deleteSetAction_);
  setMenu_->addSeparator();
  setMenu_->addAction(propertiesAction_);
  setMenu_->addAction(checkAction_);
  setMenu_->addSeparator();
  setMenu_->addAction(restoreCasesAction_);
  restoreSetMenu_ = setMenu_->addMenu(tr("Restore a &built-in set"));
  connect(restoreSetMenu_, &QMenu::aboutToShow, this,
          &LibraryPage::fillRestoreSetMenu);
  connect(setMenu_, &QMenu::aboutToShow, this, &LibraryPage::updateActions);

  caseMenu_ = new QMenu(tr("&Cases"), this);
  caseMenu_->addAction(newCaseAction_);
  caseMenu_->addAction(pasteAction_);
  caseMenu_->addAction(newGroupAction_);
  caseMenu_->addSeparator();
  caseMenu_->addAction(renameAction_);
  moveToMenu_ = caseMenu_->addMenu(tr("&Move to group"));
  connect(moveToMenu_, &QMenu::aboutToShow, this, &LibraryPage::fillMoveToMenu);
  caseMenu_->addAction(moveUpAction_);
  caseMenu_->addAction(moveDownAction_);
  caseMenu_->addSeparator();
  caseMenu_->addAction(deleteAction_);
  connect(caseMenu_, &QMenu::aboutToShow, this, &LibraryPage::updateActions);
}

QList<QMenu *> LibraryPage::menus() const { return {setMenu_, caseMenu_}; }

QList<QAction *> LibraryPage::editActions() const {
  return {undoAction_, redoAction_, editSeparator_};
}

void LibraryPage::fillContextMenu(QMenu *menu, bool forSets) {
  updateActions();
  menu->setToolTipsVisible(true);
  for (QAction *a : (forSets ? setMenu_ : caseMenu_)->actions()) {
    menu->addAction(a);
  }
}

void LibraryPage::updateActions() {
  if (!caseTree_ || !algList_) {
    return; // still being built
  }
  const AlgSet *s = set();
  bool alg = algList_->hasFocus() && algList_->currentItem();
  bool cases = !selectedCases().empty();
  bool groups = !selectedGroups().empty();
  duplicateSetAction_->setEnabled(s);
  deleteSetAction_->setEnabled(s);
  propertiesAction_->setEnabled(s);
  checkAction_->setEnabled(s);
  exportAction_->setEnabled(s);
  restoreCasesAction_->setEnabled(s &&
                                  library_.builtinIds().contains(qs(s->id)));
  newGroupAction_->setEnabled(s);
  newCaseAction_->setEnabled(s);
  pasteAction_->setEnabled(s);
  renameAction_->setEnabled(s && (alg || caseTree_->currentItem()));
  deleteAction_->setEnabled(s && (alg || cases || groups));
  moveUpAction_->setEnabled(s && (alg || cases || groups));
  moveDownAction_->setEnabled(s && (alg || cases || groups));
  moveToMenu_->setEnabled(s && cases && s->groups.size() > 1);
  deleteAction_->setText(alg      ? tr("&Remove alg")
                         : groups ? tr("&Delete group…")
                                  : tr("&Delete…"));
  renameAction_->setText(alg ? tr("&Edit alg") : tr("&Rename…"));
}

void LibraryPage::activated() {
  active_ = true;
  refreshAlgList(); // Alg Lab's times may have changed
  for (QAction *a : editActions()) {
    a->setVisible(true);
  }
  for (QMenu *m : menus()) {
    m->menuAction()->setVisible(true);
  }
  updateActions();
}

void LibraryPage::deactivated() {
  active_ = false;
  caseFilter_->release();
  // Hidden, their keys don't reach the other modes.
  for (QAction *a : editActions()) {
    a->setVisible(false);
  }
  for (QMenu *m : menus()) {
    m->menuAction()->setVisible(false);
  }
}

void LibraryPage::fillRestoreSetMenu() {
  restoreSetMenu_->clear();
  std::vector<AlgSet> missing = library_.missingBuiltins();
  if (missing.empty()) {
    restoreSetMenu_->addAction(tr("(every built-in set is here)"))
        ->setEnabled(false);
  }
  for (const AlgSet &s : missing) {
    std::string id = s.id;
    restoreSetMenu_->addAction(
        tr("%1 (%2 cases)").arg(qs(s.name)).arg(s.caseCount()), this,
        [this, id] { restoreBuiltinSet(id); });
  }
}

void LibraryPage::fillMoveToMenu() {
  moveToMenu_->clear();
  const AlgSet *s = set();
  if (!s) {
    return;
  }
  for (std::size_t g = 0; g < s->groups.size(); ++g) {
    moveToMenu_->addAction(qs(s->groups[g].name), this,
                           [this, g] { moveCasesTo(g); });
  }
}

// --- helpers ---------------------------------------------------------------

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

// --- edits and undo ----------------------------------------------------------

void LibraryPage::refresh(const LibraryEditor::Change &change) {
  const std::string &setId = change.setId;
  AlgSet *s = library_.find(setId);
  refreshSetList();
  if (change.reload || !change.cases || !s || setId != editor_.setId ||
      caseTreeSet_ != editor_.setId) {
    buildCaseTree();
    loadCase();
  } else {
    // Only these cases' algs changed: update their rows in place.
    const std::vector<std::string> &cases = *change.cases;
    {
      QSignalBlocker block(caseTree_);
      for (int gi = 0; gi < caseTree_->topLevelItemCount(); ++gi) {
        QTreeWidgetItem *groupItem = caseTree_->topLevelItem(gi);
        auto g =
            static_cast<std::size_t>(groupItem->data(0, kGroupRole).toInt());
        if (g >= s->groups.size()) {
          continue;
        }
        bool touched = false;
        for (int ci = 0; ci < groupItem->childCount(); ++ci) {
          QTreeWidgetItem *item = groupItem->child(ci);
          std::string id = ss(item->data(0, kCaseRole).toString());
          if (std::find(cases.begin(), cases.end(), id) == cases.end()) {
            continue;
          }
          if (const AlgCase *c = s->findCase(id)) {
            fillCaseItem(item, *s, s->groups[g], *c);
            touched = true;
          }
        }
        if (touched) {
          fillGroupItem(groupItem, *s, s->groups[g]);
        }
      }
    }
    caseFilter_->apply();
    loadCase();
  }
  if (change.announce) {
    emit setsChanged(*change.announce);
  }
}

// --- sets and cases
// --------------------------------------------------------------

void LibraryPage::refreshSetList() {
  QSignalBlocker block(setList_);
  setList_->clear();
  int current = -1;
  for (std::size_t i = 0; i < library_.sets().size(); ++i) {
    const AlgSet &s = library_.sets()[i];
    int failing = editor_.failingIn(s);
    QString details = counted(s.caseCount(), tr("case"), tr("cases"));
    QStringList tips;
    if (s.builtin) {
      tips << tr("Built-in cases; the algs are yours.");
    }
    auto *item = new QListWidgetItem(setList_);
    if (failing > 0) {
      details += "  " + tr("⚠ %1 failing").arg(failing);
      item->setForeground(kWarn);
      tips << (failing == 1
                   ? tr("1 alg doesn't solve its case.")
                   : tr("%1 algs don't solve their cases.").arg(failing))
           << tr("Set > Check algs lists them.");
    }
    item->setText(qs(s.name) + "\n" + details);
    item->setToolTip(tips.join(" "));
    if (s.id == editor_.setId) {
      current = static_cast<int>(i);
    }
  }
  setList_->setCurrentRow(current);
}

void LibraryPage::loadSet() {
  refreshSetList();
  buildCaseTree();
  loadCase();
}

void LibraryPage::fillCaseItem(QTreeWidgetItem *item, const AlgSet &s,
                               const CaseGroup &g, const AlgCase &c) {
  const StepSpec &spec = s.specFor(g);
  const CaseAnalysis *a = editor_.analysisOf(c.id);
  QColor muted = palette().color(QPalette::PlaceholderText);
  QColor normal = palette().color(QPalette::Text);
  item->setText(0, qs(c.name));
  item->setData(0, kCaseRole, qs(c.id));
  item->setIcon(
      0, QIcon(cube_painter::cachedDiagram(
             maskedFacelets(a ? a->display : c.state, spec.mask, spec.free),
             spec.view, 40, look(), devicePixelRatioF())));
  item->setText(1, QString::number(c.algs.size()));
  int failing = a ? a->failing() : 0;
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
    item->setForeground(3, normal);
  }
  // Cases can be dragged, but nothing is dropped onto them.
  item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled |
                 Qt::ItemIsDragEnabled);
}

void LibraryPage::fillGroupItem(QTreeWidgetItem *item, const AlgSet &s,
                                const CaseGroup &g) {
  (void)s;
  item->setText(0, qs(g.name));
  item->setText(1, QString::number(g.cases.size()));
  item->setToolTip(0, g.spec ? tr("This group has its own step settings")
                             : QString());
  int failing = 0;
  for (const auto &c : g.cases) {
    if (const CaseAnalysis *a = editor_.analysisOf(c.id)) {
      failing += a->failing();
    }
  }
  item->setText(2, failing > 0 ? tr("⚠ %1 failing").arg(failing) : QString());
  item->setForeground(2, kWarn);
  item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled |
                 Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
}

void LibraryPage::buildCaseTree() {
  std::vector<std::string> selected;
  {
    QSignalBlocker block(caseTree_);
    // Keep the scroll position, the selection and collapsed groups of the
    // same set.
    bool sameSet = caseTreeSet_ == editor_.setId;
    caseTreeSet_ = editor_.setId;
    int scroll = sameSet ? caseTree_->verticalScrollBar()->value() : 0;
    QSet<QString> collapsed;
    for (int g = 0; sameSet && g < caseTree_->topLevelItemCount(); ++g) {
      if (!caseTree_->topLevelItem(g)->isExpanded()) {
        collapsed.insert(caseTree_->topLevelItem(g)->text(0));
      }
    }
    if (sameSet) {
      selected = selectedCases();
    }
    caseTree_->clear();
    AlgSet *s = set();
    if (!s) {
      updateActions();
      return;
    }
    QTreeWidgetItem *current = nullptr;
    for (std::size_t gi = 0; gi < s->groups.size(); ++gi) {
      const CaseGroup &g = s->groups[gi];
      auto *groupItem = new QTreeWidgetItem(caseTree_);
      groupItem->setData(0, kGroupRole, static_cast<int>(gi));
      fillGroupItem(groupItem, *s, g);
      for (const auto &c : g.cases) {
        auto *item = new QTreeWidgetItem(groupItem);
        fillCaseItem(item, *s, g, c);
        if (c.id == editor_.caseId) {
          current = item;
        }
        if (std::find(selected.begin(), selected.end(), c.id) !=
            selected.end()) {
          item->setSelected(true);
        }
      }
      groupItem->setExpanded(!collapsed.contains(qs(g.name)));
    }
    caseFilter_->apply();
    caseTree_->verticalScrollBar()->setValue(scroll);
    if (current) {
      caseTree_->setCurrentItem(current, 0,
                                selected.size() > 1
                                    ? QItemSelectionModel::Current
                                    : QItemSelectionModel::ClearAndSelect);
      current->setSelected(true);
    }
  }
  updateActions();
}

void LibraryPage::loadCase() {
  AlgCase *c = currentCase();
  caseBox_->setEnabled(c != nullptr);
  loading_ = true;
  caseNameEdit_->setText(c ? qs(c->name) : QString());
  loading_ = false;
  if (!c) {
    caseDiagram_->clear();
    caseSummary_->clear();
    caseGroup_->clear();
    caseAlgCount_->clear();
    caseProgress_->clear();
    QSignalBlocker block(algList_);
    algList_->clear();
    caseBox_->setTitle(tr("Case"));
    updateActions();
    return;
  }
  caseBox_->setTitle(qs(c->name));
  AlgSet *s = set();
  const CaseGroup &group = *s->groupOf(c->id);
  const StepSpec &spec = s->specFor(group);
  const CaseAnalysis *a = editor_.analysisOf(c->id);
  caseDiagram_->setPixmap(cube_painter::diagram(
      maskedFacelets(a ? a->display : c->state, spec.mask, spec.free),
      spec.view, kDiagramSize, look(), devicePixelRatioF()));
  caseGroup_->setText(qs(group.name) +
                      (group.spec ? tr(" (its own step settings)") : ""));
  int passing = a ? static_cast<int>(a->passing.size()) : 0;
  caseAlgCount_->setText(
      c->algs.empty() ? tr("none yet")
                      : tr("%1, %2 passing").arg(c->algs.size()).arg(passing));
  // What Learn and Drill have recorded for it.
  QStringList progress;
  const SetProgress &p = library_.progress(s->id);
  if (auto it = p.find(c->id); it != p.end()) {
    switch (statusOf(&it->second)) {
    case CaseStatus::Unstarted:
      progress << tr("not learned yet");
      break;
    case CaseStatus::Learning:
      progress << tr("learning");
      break;
    case CaseStatus::Learned:
      progress << tr("learned");
      break;
    }
    const std::pair<AttemptMode, QString> modes[] = {
        {AttemptMode::PhysicalDrill, tr("physical cube")},
        {AttemptMode::VirtualDrill, tr("virtual cube")}};
    for (const auto &[mode, name] : modes) {
      QString text = drillText(it->second, mode);
      if (!text.isEmpty()) {
        progress << tr("%1 (%2)").arg(text, name);
      }
    }
  } else {
    progress << tr("not learned yet, no drills");
  }
  caseProgress_->setText(progress.join("; "));
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
  updateActions();
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
    const CaseAnalysis *a = editor_.analysisOf(c->id);
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
    if (lab_) {
      // Each alg's times from Alg Lab, on each cube it was timed on.
      LabCaseKey key{editor_.setId, c->id};
      WarmupOf warm = [this](std::string_view id) {
        return lab_->warmupOf(id);
      };
      for (int i = 0; i < algList_->topLevelItemCount(); ++i) {
        std::string alg = labAlgKey(c->algs[static_cast<std::size_t>(i)]);
        QStringList parts;
        for (CubeKind cube : {CubeKind::Physical, CubeKind::Virtual}) {
          LabStats st = labStats(lab_->attempts(), key, alg, cube, {}, 0, warm);
          if (st.mean) {
            parts << tr("%1 mean of %2%3")
                         .arg(qs(formatTime(*st.mean, settings_.decimals)))
                         .arg(st.used)
                         .arg(cube == CubeKind::Virtual ? tr(" (virtual)")
                                                        : QString());
          }
        }
        algList_->topLevelItem(i)->setText(2, parts.join(" · "));
      }
    }
  }
  if (row >= 0 && row < algList_->topLevelItemCount()) {
    algList_->setCurrentItem(algList_->topLevelItem(row));
  }
  loading_ = false;
  updateGrips();
}

void LibraryPage::updateGrips() {
  const ErgoProfile *g = lab_ ? labGuideProfile(*lab_) : nullptr;
  gripTitle_->setVisible(lab_ != nullptr);
  grips_->setVisible(lab_ != nullptr);
  QTreeWidgetItem *item = algList_->currentItem();
  if (!item) {
    grips_->clear(currentCase() && !currentCase()->algs.empty()
                      ? tr("Select an alg to see how to turn it.")
                      : QString());
    return;
  }
  grips_->setAlg(item->text(0), g ? &*g->grips : nullptr,
                 g ? tr("Can't read this alg.")
                   : tr("No grip profile: make one in Alg Lab (Profile…)."));
}

void LibraryPage::storeAlgs(const QString &text, std::optional<int> edited) {
  AlgCase *c = currentCase();
  if (!c) {
    return;
  }
  std::vector<std::string> algs;
  std::optional<std::size_t> editedIndex;
  for (int i = 0; i < algList_->topLevelItemCount(); ++i) {
    std::string alg = cleanAlgText(ss(algList_->topLevelItem(i)->text(0)));
    if (alg.empty()) {
      continue;
    }
    if (std::find(algs.begin(), algs.end(), alg) != algs.end()) {
      emit message(tr("%1 already has \"%2\"").arg(qs(c->name), qs(alg)));
      refreshAlgList(); // back to what it was
      return;
    }
    if (edited == i) {
      editedIndex = algs.size();
    }
    algs.push_back(alg);
  }
  if (algs == c->algs) {
    refreshAlgList(); // e.g. only spaces were tidied
    return;
  }
  auto pending = editor_.begin();
  c->algs = std::move(algs);
  editor_.commit(text, *pending, std::vector<std::string>{editor_.caseId});
  // The edited alg as stored, which may have been tidied up.
  if (editedIndex) {
    warnIfFailing(*editedIndex);
  }
}

void LibraryPage::warnIfFailing(std::size_t index) {
  const AlgCase *c = currentCase();
  const CaseAnalysis *a = c ? editor_.analysisOf(c->id) : nullptr;
  if (!a || index >= a->checks.size() || index >= c->algs.size() ||
      a->checks[index].passes()) {
    return;
  }
  emit message(
      tr("Kept \"%1\", but it doesn't pass: %2")
          .arg(qs(c->algs[index]), qs(describeCheck(a->checks[index]))));
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
  menu.addAction(tr("Edit"), QKeySequence(Qt::Key_F2), this,
                 [this, item] { algList_->editItem(item, 0); });
  menu.addAction(tr("Move up"), QKeySequence(Qt::ALT | Qt::Key_Up), this,
                 [this, row] { moveAlg(row, -1); })
      ->setEnabled(row > 0);
  menu.addAction(tr("Move down"), QKeySequence(Qt::ALT | Qt::Key_Down), this,
                 [this, row] { moveAlg(row, 1); })
      ->setEnabled(row + 1 < algList_->topLevelItemCount());
  menu.addAction(tr("Open in Alg Lab"), this, [this] {
    if (currentCase()) {
      emit algLabRequested(qs(editor_.setId), qs(currentCase()->id));
    }
  });
  menu.addSeparator();
  menu.addAction(tr("Remove"), QKeySequence(QKeySequence::Delete), this,
                 [this, row] { removeAlg(row); });
  menu.exec(algList_->viewport()->mapToGlobal(pos));
}

void LibraryPage::selectCase(const QString &setId, const QString &caseId) {
  for (std::size_t i = 0; i < library_.sets().size(); ++i) {
    if (library_.sets()[i].id == ss(setId)) {
      setList_->setCurrentRow(static_cast<int>(i));
      break;
    }
  }
  editor_.caseId = ss(caseId);
  buildCaseTree();
  loadCase();
  newAlgEdit_->setFocus();
}

bool LibraryPage::setCaseAlgs(const QString &setId, const QString &caseId,
                              std::vector<std::string> algs,
                              const QString &text) {
  selectCase(setId, caseId);
  AlgCase *c = currentCase();
  if (!c || editor_.setId != ss(setId) || c->id != ss(caseId)) {
    return false;
  }
  if (algs == c->algs) {
    return true;
  }
  auto pending = editor_.begin();
  if (!pending) {
    return false;
  }
  c->algs = std::move(algs);
  return editor_.commit(text, *pending, std::vector<std::string>{c->id});
}

// --- selection ---------------------------------------------------------------

std::vector<std::string> LibraryPage::selectedCases() const {
  std::vector<std::string> out;
  for (int g = 0; g < caseTree_->topLevelItemCount(); ++g) {
    QTreeWidgetItem *groupItem = caseTree_->topLevelItem(g);
    for (int c = 0; c < groupItem->childCount(); ++c) {
      QTreeWidgetItem *item = groupItem->child(c);
      if (item->isSelected() && !item->isHidden()) {
        out.push_back(ss(item->data(0, kCaseRole).toString()));
      }
    }
  }
  return out;
}

std::vector<std::size_t> LibraryPage::selectedGroups() const {
  std::vector<std::size_t> out;
  for (int g = 0; g < caseTree_->topLevelItemCount(); ++g) {
    QTreeWidgetItem *groupItem = caseTree_->topLevelItem(g);
    if (groupItem->isSelected() && !groupItem->isHidden()) {
      out.push_back(
          static_cast<std::size_t>(groupItem->data(0, kGroupRole).toInt()));
    }
  }
  return out;
}

std::optional<std::size_t> LibraryPage::currentGroup() const {
  QTreeWidgetItem *item = caseTree_->currentItem();
  if (!item) {
    return std::nullopt;
  }
  QTreeWidgetItem *groupItem = item->parent() ? item->parent() : item;
  return static_cast<std::size_t>(groupItem->data(0, kGroupRole).toInt());
}

void LibraryPage::selectItems(const std::vector<std::string> &cases,
                              const std::vector<std::size_t> &groups) {
  {
    QSignalBlocker block(caseTree_);
    caseTree_->clearSelection();
    QTreeWidgetItem *first = nullptr;
    for (int g = 0; g < caseTree_->topLevelItemCount(); ++g) {
      QTreeWidgetItem *groupItem = caseTree_->topLevelItem(g);
      auto gi =
          static_cast<std::size_t>(groupItem->data(0, kGroupRole).toInt());
      if (std::find(groups.begin(), groups.end(), gi) != groups.end()) {
        groupItem->setSelected(true);
        first = first ? first : groupItem;
      }
      for (int c = 0; c < groupItem->childCount(); ++c) {
        QTreeWidgetItem *item = groupItem->child(c);
        std::string id = ss(item->data(0, kCaseRole).toString());
        if (std::find(cases.begin(), cases.end(), id) != cases.end()) {
          item->setSelected(true);
          // Keep the shown case when it is one of them.
          if (!first || id == editor_.caseId) {
            first = item;
          }
        }
      }
    }
    if (first) {
      caseTree_->setCurrentItem(first, 0, QItemSelectionModel::Current);
      caseTree_->scrollToItem(first);
    }
  }
  updateActions();
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
  QString error;
  AlgSet *added = library_.add(std::move(s), &error);
  if (!added) {
    QMessageBox::warning(this, tr("New alg set"),
                         tr("Could not create the set: %1").arg(error));
    return;
  }
  editor_.added(*added, tr("New set"));
  emit message(tr("Created \"%1\". Add cases with Cases > New case from "
                  "alg, or Paste algs.")
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
  QString error;
  AlgSet *added = library_.add(std::move(copy), &error);
  if (!added) {
    QMessageBox::warning(this, tr("Duplicate alg set"),
                         tr("Could not save the copy: %1").arg(error));
    return;
  }
  editor_.added(*added, tr("Duplicate set"), true);
}

void LibraryPage::deleteSet() {
  AlgSet *s = set();
  if (!s) {
    return;
  }
  SetProgress progress = library_.progress(s->id);
  int algs = 0;
  for (const auto &g : s->groups) {
    for (const auto &c : g.cases) {
      algs += static_cast<int>(c.algs.size());
    }
  }
  QString text = tr("Delete \"%1\" and its %2?")
                     .arg(qs(s->name), counted(algs, tr("alg"), tr("algs")));
  if (!progress.empty()) {
    text += " " + tr("Your Learn and Drill progress on %n of its cases goes "
                     "too.",
                     nullptr, static_cast<int>(progress.size()));
  }
  text += "\n\n" + tr("Edit > Undo brings it back while v-cube is open, and "
                      "its files are kept in the alg sets' deleted folder.");
  if (s->builtin) {
    text += " " + tr("After that, Set > Restore a built-in set brings back "
                     "its cases, but not your algs.");
  }
  if (QMessageBox::question(this, tr("Delete alg set"), text) !=
      QMessageBox::Yes) {
    return;
  }
  Edit edit{s->id,
            *s,
            std::nullopt,
            {},
            std::move(progress),
            saved::setSettings(s->id),
            editor_.caseId,
            {}};
  QString error;
  if (!library_.remove(edit.setId, &error)) {
    QMessageBox::warning(this, tr("Delete alg set"),
                         tr("Could not delete the set: %1").arg(error));
    return;
  }
  if (!error.isEmpty()) {
    emit message(tr("Deleted the set, but %1").arg(error));
  }
  editor_.removed(std::move(edit), tr("Delete set"));
}

void LibraryPage::restoreSet() {
  AlgSet *s = set();
  if (!s || !library_.builtinIds().contains(qs(s->id))) {
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
  auto pending = editor_.begin();
  QString error;
  if (!library_.restoreBuiltin(editor_.setId, &error)) {
    QMessageBox::warning(this, tr("Restore"), error);
    return;
  }
  if (editor_.commit(tr("Restore built-in cases"), *pending)) {
    emit message(tr("Restored the built-in cases of %1; your algs, cases and "
                    "settings were kept.")
                     .arg(qs(set()->name)));
  }
}

void LibraryPage::restoreBuiltinSet(const std::string &id) {
  QString error;
  if (!library_.restoreBuiltin(id, &error)) {
    QMessageBox::warning(this, tr("Restore a built-in set"), error);
    return;
  }
  AlgSet *s = library_.find(id);
  if (!s) {
    return;
  }
  editor_.added(*s, tr("Restore %1").arg(qs(s->name)));
  emit message(tr("Added the built-in set %1 again, with no algs yet.")
                   .arg(qs(s->name)));
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
  SetPropertiesDialog dialog(*s, group, look(), editor_.caseId, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  const AlgSet &r = dialog.result();
  auto pending = editor_.begin();
  bool renamed = r.name != s->name;
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
  if (algChanges(pending->before, *s)) {
    return; // nothing changed
  }
  editor_.commit(tr("Set properties"), *pending);
  if (renamed) {
    emit setsChanged({});
  }
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
      const CaseAnalysis *a = editor_.analysisOf(c.id);
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
            editor_.caseId = ss(item->data(0, kCaseRole).toString());
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
  // A built-in set exported and imported again stays a built-in one, so
  // that its shipped cases can still be restored.
  imported->builtin =
      imported->builtin && library_.builtinIds().contains(qs(imported->id));
  AlgSet *added = library_.add(std::move(*imported), &error);
  if (!added) {
    QMessageBox::warning(this, tr("Import alg set"),
                         tr("Could not import: %1").arg(error));
    return;
  }
  editor_.added(*added, tr("Import set"));
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

// --- group and case actions --------------------------------------------------

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
  auto pending = editor_.begin();
  s->groups.push_back({ss(name.trimmed()), std::nullopt, {}});
  std::size_t added = s->groups.size() - 1;
  if (editor_.commit(tr("New group"), *pending)) {
    selectItems({}, {added});
  }
}

void LibraryPage::renameSelected() {
  if (algList_->hasFocus() && algList_->currentItem()) {
    algList_->editItem(algList_->currentItem(), 0);
    return;
  }
  AlgSet *s = set();
  QTreeWidgetItem *item = caseTree_->currentItem();
  if (!s || !item) {
    return;
  }
  if (item->parent()) {
    caseNameEdit_->setFocus();
    caseNameEdit_->selectAll();
    return;
  }
  auto gi = static_cast<std::size_t>(item->data(0, kGroupRole).toInt());
  if (gi >= s->groups.size()) {
    return;
  }
  bool ok = false;
  QString name =
      QInputDialog::getText(this, tr("Rename group"), tr("Group name:"),
                            QLineEdit::Normal, qs(s->groups[gi].name), &ok);
  if (!ok || name.trimmed().isEmpty() ||
      ss(name.trimmed()) == s->groups[gi].name) {
    return;
  }
  auto pending = editor_.begin();
  s->groups[gi].name = ss(name.trimmed());
  editor_.commit(tr("Rename group"), *pending);
}

void LibraryPage::deleteSelected() {
  if (algList_->hasFocus() && algList_->currentItem()) {
    removeAlg(algList_->indexOfTopLevelItem(algList_->currentItem()));
    return;
  }
  AlgSet *s = set();
  if (!s) {
    return;
  }
  std::vector<std::size_t> groups = selectedGroups();
  std::vector<std::string> cases = selectedCases();
  // Cases of the deleted groups go with them.
  std::vector<std::string> ids;
  for (std::size_t gi : groups) {
    for (const AlgCase &c : s->groups[gi].cases) {
      ids.push_back(c.id);
    }
  }
  for (const auto &id : cases) {
    if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
      ids.push_back(id);
    }
  }
  if (ids.empty() && groups.empty()) {
    return;
  }
  const SetProgress &progress = library_.progress(s->id);
  int withProgress = 0, algs = 0;
  for (const auto &id : ids) {
    withProgress += progress.contains(id) ? 1 : 0;
    algs += static_cast<int>(s->findCase(id)->algs.size());
  }
  QString what;
  if (!groups.empty()) {
    what = groups.size() == 1
               ? tr("group \"%1\"").arg(qs(s->groups[groups.front()].name))
               : tr("%1 groups").arg(groups.size());
    what += " " + tr("with %1").arg(counted(static_cast<int>(ids.size()),
                                            tr("case"), tr("cases")));
  } else {
    what = ids.size() == 1
               ? tr("case \"%1\"").arg(qs(s->findCase(ids.front())->name))
               : tr("%1 cases").arg(ids.size());
  }
  QString text =
      tr("Delete %1 and %2?").arg(what, counted(algs, tr("alg"), tr("algs")));
  if (withProgress > 0) {
    text += " " + tr("Your Learn and Drill progress on %n of the cases goes "
                     "too.",
                     nullptr, withProgress);
  }
  text += "\n\n" + tr("Edit > Undo brings it all back.");
  QString title = groups.empty() ? tr("Delete cases") : tr("Delete groups");
  if (QMessageBox::question(this, title, text) != QMessageBox::Yes) {
    return;
  }
  auto pending = editor_.begin();
  for (auto &g : s->groups) {
    std::erase_if(g.cases, [&ids](const AlgCase &c) {
      return std::find(ids.begin(), ids.end(), c.id) != ids.end();
    });
  }
  std::vector<std::size_t> sorted = groups;
  std::sort(sorted.rbegin(), sorted.rend());
  for (std::size_t gi : sorted) {
    s->groups.erase(s->groups.begin() + static_cast<std::ptrdiff_t>(gi));
  }
  if (std::find(ids.begin(), ids.end(), editor_.caseId) != ids.end()) {
    editor_.caseId.clear();
  }
  editor_.commit(title, *pending, std::nullopt, ids);
}

void LibraryPage::moveSelected(int direction) {
  if (algList_->hasFocus() && algList_->currentItem()) {
    moveAlg(algList_->indexOfTopLevelItem(algList_->currentItem()), direction);
    return;
  }
  AlgSet *s = set();
  if (!s) {
    return;
  }
  std::vector<std::string> cases = selectedCases();
  std::vector<std::size_t> groups = selectedGroups();
  auto pending = editor_.begin();
  if (!cases.empty()) {
    if (shiftCases(*s, cases, direction) &&
        editor_.commit(tr("Move cases"), *pending)) {
      selectItems(cases, {});
    }
    return;
  }
  if (groups.size() != 1) {
    return;
  }
  std::size_t from = groups.front();
  auto to = static_cast<std::size_t>(static_cast<long>(from) + direction);
  if (moveGroup(*s, from, to) && editor_.commit(tr("Move group"), *pending)) {
    selectItems({}, {to});
  }
}

void LibraryPage::moveCasesTo(std::size_t group) {
  AlgSet *s = set();
  std::vector<std::string> cases = selectedCases();
  if (!s || cases.empty()) {
    return;
  }
  auto pending = editor_.begin();
  if (moveCases(*s, cases, group) &&
      editor_.commit(tr("Move cases"), *pending)) {
    selectItems(cases, {});
  }
}

void LibraryPage::dropOnTree(QTreeWidgetItem *target, int position) {
  AlgSet *s = set();
  if (!s || s->groups.empty()) {
    return;
  }
  // The tree is rebuilt by the move, so read the target first.
  std::optional<std::size_t> targetGroup;
  std::string targetCase;
  bool expanded = false;
  if (target) {
    QTreeWidgetItem *groupItem = target->parent() ? target->parent() : target;
    targetGroup =
        static_cast<std::size_t>(groupItem->data(0, kGroupRole).toInt());
    targetCase = ss(target->data(0, kCaseRole).toString());
    expanded = groupItem->isExpanded();
  }
  auto where = static_cast<CaseTreeWidget::Drop>(position);
  std::vector<std::string> cases = selectedCases();
  std::vector<std::size_t> groups = selectedGroups();
  auto pending = editor_.begin();
  if (!cases.empty()) {
    std::size_t group = s->groups.size() - 1;
    std::string before;
    if (targetGroup && *targetGroup < s->groups.size()) {
      group = *targetGroup;
      const auto &list = s->groups[group].cases;
      if (!targetCase.empty()) {
        // Above a case: before it; on or below it: after it.
        auto it = std::find_if(list.begin(), list.end(), [&](const AlgCase &c) {
          return c.id == targetCase;
        });
        if (where != CaseTreeWidget::DropAbove && it != list.end()) {
          ++it;
        }
        before = it == list.end() ? std::string() : it->id;
      } else if (where == CaseTreeWidget::DropAbove && group > 0) {
        --group; // between groups: the end of the one above
      } else if (where == CaseTreeWidget::DropAbove ||
                 (where == CaseTreeWidget::DropBelow && expanded)) {
        before = list.empty() ? std::string() : list.front().id;
      }
    }
    if (moveCases(*s, cases, group, before) &&
        editor_.commit(tr("Move cases"), *pending)) {
      selectItems(cases, {});
    }
    return;
  }
  if (groups.size() != 1) {
    return;
  }
  std::size_t from = groups.front();
  std::size_t to = s->groups.size() - 1;
  if (targetGroup) {
    to = *targetGroup;
    if (where == CaseTreeWidget::DropAbove && targetCase.empty()) {
      to = to > from ? to - 1 : to;
    } else {
      to = to < from ? to + 1 : to;
    }
  }
  to = std::min(to, s->groups.size() - 1);
  if (moveGroup(*s, from, to) && editor_.commit(tr("Move group"), *pending)) {
    selectItems({}, {to});
  }
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
  std::string alg = cleanAlgText(ss(text));
  ParseResult parsed = parseAlg(alg);
  if (!parsed || parsed.alg.empty()) {
    QMessageBox::warning(
        this, tr("New case"),
        tr("Can't read that alg: %1")
            .arg(parsed.error ? qs(parsed.error->message) : tr("empty")));
    return;
  }
  if (auto ref = matchCase(*s, parsed.alg)) {
    AlgCase &existing = s->groups[ref->group].cases[ref->index];
    if (std::find(existing.algs.begin(), existing.algs.end(), alg) !=
        existing.algs.end()) {
      QMessageBox::information(
          this, tr("New case"),
          tr("The case \"%1\" already has that alg.").arg(qs(existing.name)));
      return;
    }
    auto answer =
        QMessageBox::question(this, tr("New case"),
                              tr("That alg solves the existing case \"%1\". "
                                 "Add it to that case instead?")
                                  .arg(qs(existing.name)));
    if (answer == QMessageBox::Yes) {
      auto pending = editor_.begin();
      existing.algs.push_back(alg);
      editor_.caseId = existing.id;
      editor_.commit(tr("Add alg"), *pending,
                     std::vector<std::string>{existing.id});
      buildCaseTree();
    }
    return;
  }
  QString name = QInputDialog::getText(
      this, tr("New case"), tr("Case name:"), QLineEdit::Normal,
      tr("Case %1").arg(s->caseCount() + 1), &ok);
  if (!ok) {
    return;
  }
  auto pending = editor_.begin();
  if (s->groups.empty()) {
    s->groups.push_back({"Cases", std::nullopt, {}});
  }
  // Add to the selected group, or the last one.
  std::size_t gi = std::min(currentGroup().value_or(s->groups.size() - 1),
                            s->groups.size() - 1);
  AlgCase c;
  c.name = ss(name.trimmed().isEmpty() ? tr("Case %1").arg(s->caseCount() + 1)
                                       : name.trimmed());
  c.id = s->uniqueCaseId(c.name);
  c.state = stateSolvedBy(parsed.alg);
  c.algs.push_back(alg);
  editor_.caseId = c.id;
  std::string id = c.id;
  bool solved = stepSolved(c.state, s->specFor(s->groups[gi]).free);
  s->groups[gi].cases.push_back(std::move(c));
  if (editor_.commit(tr("New case"), *pending, std::nullopt, {id}) && solved) {
    emit message(tr("Note: with this set's \"left unsolved\" setting, that "
                    "case already counts as solved."));
  }
}

void LibraryPage::pasteAlgs() {
  AlgSet *s = set();
  if (!s) {
    return;
  }
  std::optional<std::size_t> group = currentGroup();
  PasteDialog dialog(*s, group ? static_cast<int>(*group) : -1,
                     pasteLeftOver_[s->id], this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  PastePlan plan = dialog.plan();
  pasteLeftOver_[s->id] = dialog.leftOver();
  std::vector<std::string> created, changed;
  for (const PasteResult &r : plan.results) {
    if (r.outcome == PasteOutcome::NewCase) {
      created.push_back(r.caseId);
    } else if (r.outcome == PasteOutcome::Added) {
      changed.push_back(r.caseId);
    }
  }
  auto pending = editor_.begin();
  *s = std::move(plan.set);
  if (editor_.caseId.empty() && !created.empty()) {
    editor_.caseId = created.front();
  }
  if (!editor_.commit(tr("Paste algs"), *pending,
                      created.empty() ? std::optional(changed) : std::nullopt,
                      created)) {
    return;
  }
  const ImportReport &r = plan.report;
  QString text = tr("Added %1").arg(counted(r.added, tr("alg"), tr("algs")));
  if (r.created) {
    text += tr(", made %1")
                .arg(counted(r.created, tr("new case"), tr("new cases")));
  }
  int left = static_cast<int>(r.unmatched.size() + r.ambiguous.size() +
                              r.invalid.size());
  if (left) {
    text +=
        tr("; %n not added are kept for the next Paste algs", nullptr, left);
  }
  emit message(text + ".");
}

void LibraryPage::addAlg() {
  AlgCase *c = currentCase();
  std::string text = cleanAlgText(ss(newAlgEdit_->text()));
  if (!c || text.empty()) {
    return;
  }
  if (std::find(c->algs.begin(), c->algs.end(), text) != c->algs.end()) {
    emit message(tr("%1 already has \"%2\"").arg(qs(c->name), qs(text)));
    return;
  }
  // Failing algs are kept, with a warning: the check shows what's wrong.
  auto pending = editor_.begin();
  c->algs.push_back(text);
  std::size_t index = c->algs.size() - 1;
  newAlgEdit_->clear();
  if (editor_.commit(tr("Add alg"), *pending,
                     std::vector<std::string>{editor_.caseId})) {
    warnIfFailing(index);
  }
}

void LibraryPage::removeAlg(int row) {
  AlgCase *c = currentCase();
  if (!c || row < 0 || row >= static_cast<int>(c->algs.size())) {
    return;
  }
  auto pending = editor_.begin();
  std::string alg = c->algs[static_cast<std::size_t>(row)];
  c->algs.erase(c->algs.begin() + row);
  if (editor_.commit(tr("Remove alg"), *pending,
                     std::vector<std::string>{editor_.caseId})) {
    emit message(tr("Removed \"%1\"; %2 brings it back")
                     .arg(qs(alg), undoAction_->shortcut().toString(
                                       QKeySequence::NativeText)));
  }
}

void LibraryPage::moveAlg(int row, int direction) {
  AlgCase *c = currentCase();
  int to = row + direction;
  if (!c || row < 0 || to < 0 || row >= static_cast<int>(c->algs.size()) ||
      to >= static_cast<int>(c->algs.size())) {
    return;
  }
  auto pending = editor_.begin();
  std::swap(c->algs[static_cast<std::size_t>(row)],
            c->algs[static_cast<std::size_t>(to)]);
  if (editor_.commit(tr("Reorder algs"), *pending,
                     std::vector<std::string>{editor_.caseId})) {
    algList_->setCurrentItem(algList_->topLevelItem(to));
  }
}

void LibraryPage::makeMain(int row) {
  AlgCase *c = currentCase();
  if (!c || row <= 0 || row >= static_cast<int>(c->algs.size())) {
    return;
  }
  const CaseAnalysis *a = editor_.analysisOf(c->id);
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
  auto pending = editor_.begin();
  std::string alg = c->algs[static_cast<std::size_t>(row)];
  c->algs.erase(c->algs.begin() + row);
  c->algs.insert(c->algs.begin(), alg);
  if (editor_.commit(tr("Make main"), *pending,
                     std::vector<std::string>{editor_.caseId})) {
    algList_->setCurrentItem(algList_->topLevelItem(0));
  }
}

} // namespace vcube
