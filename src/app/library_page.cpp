#include "app/library_page.hpp"
#include "app/library_private.hpp"

namespace vcube {

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
  int position = dropIndicatorPosition();
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
    return s ? s->findCase(caseIdOf(item)) : nullptr;
  };
  auto analysis = [this](const QTreeWidgetItem *item) {
    return editor_.analysisOf(caseIdOf(item));
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
  // --- the case ---
  caseBox_ = new QGroupBox(tr("Case"), this);
  // Until a case is selected the box says only that; the editor is shown
  // with a case.
  auto *caseOuter = new QVBoxLayout(caseBox_);
  caseEmpty_ =
      new QLabel(tr("Select a case to see and edit its algs"), caseBox_);
  caseEmpty_->setAlignment(Qt::AlignCenter);
  caseEmpty_->setWordWrap(true);
  caseEmpty_->setForegroundRole(QPalette::PlaceholderText);
  caseContent_ = new QWidget(caseBox_);
  caseOuter->addWidget(caseEmpty_, 1);
  caseOuter->addWidget(caseContent_, 1);
  auto *caseLayout = new QVBoxLayout(caseContent_);
  caseLayout->setContentsMargins(0, 0, 0, 0);
  auto *caseTop = new QHBoxLayout;
  caseDiagram_ = new QLabel(caseBox_);
  caseDiagram_->setFixedSize(diagramSize(), diagramSize());
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
  // Where the case goes from here: the modes that practise it, Alg Lab
  // and its history.
  auto *goRow = new QHBoxLayout;
  auto goButton = [this, goRow](const QString &text, auto run) {
    auto *b = new QPushButton(text, caseBox_);
    connect(b, &QPushButton::clicked, this, run);
    goRow->addWidget(b);
    return b;
  };
  goButton(tr("Learn"), [this] {
    if (currentCase()) {
      emit practiceRequested(AppMode::Learn, qs(editor_.setId),
                             qs(currentCase()->id));
    }
  });
  goButton(tr("Drill"), [this] {
    if (currentCase()) {
      emit practiceRequested(AppMode::Drill, qs(editor_.setId),
                             qs(currentCase()->id));
    }
  });
  goButton(tr("Alg Lab"), [this] {
    if (currentCase()) {
      emit algLabRequested(qs(editor_.setId), qs(currentCase()->id));
    }
  });
  goRow->addStretch();
  caseLayout->addLayout(goRow);
  // The case's algs (with the fingertricks of the one selected) and its
  // history, on tabs.
  auto *caseTabs = new QTabWidget(caseBox_);
  auto *algsTab = new QWidget(caseTabs);
  auto *algsLayout = new QVBoxLayout(algsTab);
  auto *algHelp = new QLabel(
      tr("The first passing alg is the main alg (F1 lists the keys)."),
      caseBox_);
  algHelp->setWordWrap(true);
  algHelp->setForegroundRole(QPalette::PlaceholderText);
  algsLayout->addWidget(algHelp);
  algList_ = new AlgListWidget(caseBox_);
  algsLayout->addWidget(algList_, 1);
  auto *addRow = new QHBoxLayout;
  newAlgEdit_ = new QLineEdit(caseBox_);
  newAlgEdit_->setPlaceholderText(tr("New alg, e.g. R U R' U R U2 R'"));
  auto *addButton = new QPushButton(tr("Add"), caseBox_);
  addRow->addWidget(newAlgEdit_, 1);
  addRow->addWidget(addButton);
  algsLayout->addLayout(addRow);
  gripTitle_ = new QLabel(tr("<b>Fingertricks</b>"), caseBox_);
  algsLayout->addWidget(gripTitle_);
  grips_ = new GripView(caseBox_);
  algsLayout->addWidget(grips_);

  caseTabs->addTab(algsTab, tr("Algs"));
  auto *historyTab = new QWidget(caseTabs);
  auto *historyLayout = new QVBoxLayout(historyTab);
  caseHistory_ = new QLabel(historyTab);
  caseHistory_->setTextFormat(Qt::RichText);
  caseHistory_->setWordWrap(true);
  caseHistory_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  caseHistory_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  historyLayout->addWidget(caseHistory_);
  auto *learnHistory = new QPushButton(tr("Learn history…"), caseBox_);
  connect(learnHistory, &QPushButton::clicked, this, [this] {
    const AlgCase *c = currentCase();
    if (!c) {
      return;
    }
    const CaseProgress *own = progressOf(c->id);
    showCaseHistory(this, qs(c->name), own, caseAttempts(own, nullptr), false,
                    settings_.srs);
  });
  auto *drillHistory = new QToolButton(caseBox_);
  drillHistory->setText(tr("Drill history…"));
  drillHistory->setPopupMode(QToolButton::InstantPopup);
  auto *drillMenu = new QMenu(drillHistory);
  const std::pair<AttemptMode, QString> drillModes[] = {
      {AttemptMode::VirtualDrill, tr("Virtual cube")},
      {AttemptMode::PhysicalDrill, tr("Physical cube")}};
  for (const auto &[mode, name] : drillModes) {
    drillMenu->addAction(name, this, [this, mode] {
      const AlgCase *c = currentCase();
      if (!c) {
        return;
      }
      showDrillHistory(this, qs(c->name), progressOf(c->id), mode,
                       settings_.decimals);
    });
  }
  drillHistory->setMenu(drillMenu);
  auto *historyButtons = new QHBoxLayout;
  historyButtons->addWidget(learnHistory);
  historyButtons->addWidget(drillHistory);
  historyButtons->addStretch();
  historyLayout->addLayout(historyButtons);
  historyLayout->addStretch(1);
  caseTabs->addTab(historyTab, tr("History"));
  caseLayout->addWidget(caseTabs, 1);

  QSplitter *split =
      mainSplitter(this, {left, middle, caseBox_}, {220, 520, 460});
  // The commands used most, which were only in the menus.
  auto *toolbar = new QHBoxLayout;
  for (QAction *a : {newSetAction_, propertiesAction_, newCaseAction_,
                     pasteAction_, newGroupAction_, checkAction_}) {
    auto *b = new QToolButton(this);
    b->setDefaultAction(a);
    b->setToolButtonStyle(Qt::ToolButtonTextOnly);
    b->setFocusPolicy(Qt::NoFocus);
    toolbar->addWidget(b);
    if (a == propertiesAction_) {
      toolbar->addSpacing(12);
    }
  }
  toolbar->addStretch();
  auto *layout = new QVBoxLayout(this);
  layout->addLayout(toolbar);
  layout->addWidget(split, 1);

  // --- wiring ---
  connect(setList_, &QListWidget::currentRowChanged, this, [this](int row) {
    if (row >= 0 && row < static_cast<int>(library_.sets().size())) {
      editor_.setId = library_.sets()[static_cast<std::size_t>(row)].id;
      editor_.caseId.clear();
      newAlgEdit_->clear();
      refreshSetList();
      buildCaseTree();
      loadCase();
    }
  });
  connect(setList_, &QListWidget::itemDoubleClicked, this,
          &LibraryPage::editProperties);
  connect(setList_, &QListWidget::customContextMenuRequested, this,
          [this](const QPoint &pos) {
            if (QListWidgetItem *item = setList_->itemAt(pos)) {
              setList_->setCurrentItem(item);
            }
            execContextMenu(*setMenu_, setList_->viewport()->mapToGlobal(pos));
          });
  connect(caseTree_, &QTreeWidget::customContextMenuRequested, this,
          [this](const QPoint &pos) {
            QTreeWidgetItem *item = caseTree_->itemAt(pos);
            if (item && !item->isSelected()) {
              caseTree_->setCurrentItem(item);
            }
            execContextMenu(*caseMenu_,
                            caseTree_->viewport()->mapToGlobal(pos));
          });
  connect(caseTree_, &QTreeWidget::currentItemChanged, this,
          [this](QTreeWidgetItem *item) {
            std::string id = item ? caseIdOf(item) : std::string();
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
  connect(
      algList_, &QTreeWidget::itemChanged, this,
      [this](QTreeWidgetItem *item, int column) {
        if (!loading_ && column == 0) {
          int row = algList_->indexOfTopLevelItem(item);
          // After the item's signal: storing rebuilds the list. The
          // edit is taken as it stands now, for this case: the click
          // that ended it may show another case or set before the
          // call arrives.
          QMetaObject::invokeMethod(
              this,
              [this, row, setId = editor_.setId, caseId = editor_.caseId,
               texts = algTexts()] { storeAlgEdit(setId, caseId, texts, row); },
              Qt::QueuedConnection);
        }
      });
  connect(algList_, &QTreeWidget::itemDoubleClicked, this,
          [this](QTreeWidgetItem *item) { algList_->editItem(item, 0); });
  connect(algList_, &AlgListWidget::reordered, this, [this] {
    storeAlgTexts(tr("Reorder algs"), editor_.caseId, algTexts(), {});
  });
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
  deleteAction_ = action(tr("&Delete"), &LibraryPage::deleteSelected,
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

QList<QMenu *> LibraryPage::pageMenus() const { return {setMenu_, caseMenu_}; }

QList<QAction *> LibraryPage::editActions() const {
  return {undoAction_, redoAction_, editSeparator_};
}

void LibraryPage::execContextMenu(const QMenu &from, const QPoint &at) {
  updateActions();
  QMenu menu(this);
  menu.setToolTipsVisible(true);
  menu.addActions(from.actions());
  menu.exec(at);
}

void LibraryPage::updateActions() {
  if (!caseTree_ || !algList_) {
    return; // still being built
  }
  const AlgSet *s = set();
  bool alg = algList_->hasFocus() && algList_->currentItem();
  bool cases = !selectedCases().empty();
  bool groups = !selectedGroups().empty();
  for (QAction *a :
       {duplicateSetAction_, deleteSetAction_, propertiesAction_, checkAction_,
        newGroupAction_, newCaseAction_, pasteAction_}) {
    a->setEnabled(s);
  }
  restoreCasesAction_->setEnabled(s &&
                                  library_.builtinIds().contains(qs(s->id)));
  renameAction_->setEnabled(s && (alg || caseTree_->currentItem()));
  for (QAction *a : {deleteAction_, moveUpAction_, moveDownAction_}) {
    a->setEnabled(s && (alg || cases || groups));
  }
  moveToMenu_->setEnabled(s && cases && s->groups.size() > 1);
  deleteAction_->setText(alg      ? tr("&Remove alg")
                         : groups ? tr("&Delete group")
                                  : tr("&Delete"));
  renameAction_->setText(alg ? tr("&Edit alg") : tr("&Rename…"));
}

void LibraryPage::activated() {
  active_ = true;
  refreshAlgList(); // Alg Lab's times may have changed
  updateActions();
}

void LibraryPage::deactivated() {
  active_ = false;
  caseFilter_->release();
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
        std::size_t g = groupIndexOf(groupItem);
        if (g >= s->groups.size()) {
          continue;
        }
        bool touched = false;
        for (int ci = 0; ci < groupItem->childCount(); ++ci) {
          QTreeWidgetItem *item = groupItem->child(ci);
          std::string id = caseIdOf(item);
          if (std::find(cases.begin(), cases.end(), id) == cases.end()) {
            continue;
          }
          if (const AlgCase *c = s->findCase(id)) {
            fillCaseItem(item, *s, s->groups[g], *c);
            touched = true;
          }
        }
        if (touched) {
          fillGroupItem(groupItem, s->groups[g]);
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
      item->setForeground(kWarnColour);
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

void LibraryPage::fillCaseItem(QTreeWidgetItem *item, const AlgSet &s,
                               const CaseGroup &g, const AlgCase &c) {
  const StepSpec &spec = s.specFor(g);
  const CaseAnalysis *a = editor_.analysisOf(c.id);
  QColor muted = palette().color(QPalette::PlaceholderText);
  QColor normal = palette().color(QPalette::Text);
  item->setText(0, qs(c.name));
  item->setData(0, kCaseRole, qs(c.id));
  item->setIcon(
      0, cube_painter::caseIcon(cube_painter::cachedDiagram(
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
    item->setForeground(2, kFailColour);
    item->setText(3, tr("(no alg solves this case)"));
    item->setForeground(3, kFailColour);
  } else {
    if (failing > 0) {
      item->setText(2, tr("⚠ %1 failing").arg(failing));
      item->setForeground(2, kWarnColour);
    } else {
      item->setText(2, "✓");
      item->setForeground(2, kPassColour);
    }
    item->setText(3, qs(c.algs[*a->main]));
    item->setForeground(3, normal);
  }
  // Cases can be dragged, but nothing is dropped onto them.
  item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled |
                 Qt::ItemIsDragEnabled);
}

void LibraryPage::fillGroupItem(QTreeWidgetItem *item, const CaseGroup &g) {
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
  item->setForeground(2, kWarnColour);
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
      fillGroupItem(groupItem, g);
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

void LibraryPage::pasteAlgsInto(const QString &setId) {
  selectCase(setId, {});
  if (editor_.setId == ss(setId)) {
    pasteAlgs();
  }
}

bool LibraryPage::undoLast() {
  if (!undo_->canUndo()) {
    return false;
  }
  undo_->undo();
  return true;
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

} // namespace vcube
