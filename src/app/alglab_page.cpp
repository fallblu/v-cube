#include "app/alglab_page.hpp"
#include "app/alglab_private.hpp"
#include "app/page_parts.hpp"
#include "app/text_style.hpp"
#include "app/ui_text.hpp"

#include <QScrollArea>

namespace vcube {

namespace {

int diagramSize() { return scaledToText(kSmallDiagram); }

// The move-set drop-downs: each item holds its move set's text, "custom"
// opens the dialog.
enum class AnyMoves { Left, Offered }; // the filter's "Any moves" item
void addMoveSets(QComboBox *box, const AlgLabStore &store, AnyMoves any) {
  box->clear();
  if (any == AnyMoves::Offered) {
    box->addItem(QObject::tr("Any moves"), QString());
  }
  for (const auto &p : builtinMoveSets()) {
    box->addItem(qs(p.name), "t:" + qs(p.set.toString()));
  }
  for (const auto &p : store.moveSets()) {
    box->addItem(qs(p.name) + "  " + qs(p.set.display()),
                 "u:" + qs(p.set.toString()));
  }
  box->addItem(QObject::tr("Custom…"), "custom");
}

// Selects the item for `set`, adding a "Custom" item for it if no preset
// has it.
void selectMoveSet(QComboBox *box, const MoveSet &set) {
  for (int i = 0; i < box->count(); ++i) {
    QString d = box->itemData(i).toString();
    if (d.size() > 2 && d[1] == ':' &&
        MoveSet::parse(d.mid(2).toStdString()) == set) {
      box->setCurrentIndex(i);
      return;
    }
  }
  int at = box->count() - 1;
  box->insertItem(at, QObject::tr("Custom %1").arg(qs(set.display())),
                  "c:" + qs(set.toString()));
  box->setCurrentIndex(at);
}

} // namespace

AlgLabPage::AlgLabPage(AppSettings &settings, AlgLibrary &library,
                       AlgLabStore &store, QWidget *parent)
    : ModePage(parent), settings_(settings), library_(library), store_(store) {
  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  // Cases.
  left_ = new QWidget(this);
  auto *leftLayout = new QVBoxLayout(left_);
  leftLayout->setContentsMargins(6, 6, 0, 6);
  sourceBox_ = new QComboBox(left_);
  caseFilter_ = new QLineEdit(left_);
  caseFilter_->setPlaceholderText(tr("Search cases"));
  caseFilter_->setClearButtonEnabled(true);
  caseTree_ = new QTreeWidget(left_);
  caseTree_->setHeaderHidden(true);
  caseTree_->setFocusPolicy(Qt::ClickFocus);
  labButtons_ = new QWidget(left_);
  auto *labLayout = new QHBoxLayout(labButtons_);
  labLayout->setContentsMargins(0, 0, 0, 0);
  auto *newCase = new QPushButton(tr("New…"), labButtons_);
  auto *editCase = new QPushButton(tr("Edit…"), labButtons_);
  auto *deleteCase = new QPushButton(tr("Delete…"), labButtons_);
  labLayout->addWidget(newCase);
  labLayout->addWidget(editCase);
  labLayout->addWidget(deleteCase);
  leftLayout->addWidget(sourceBox_);
  leftLayout->addWidget(caseFilter_);
  leftLayout->addWidget(caseTree_, 1);
  leftLayout->addWidget(labButtons_);

  tabs_ = new QTabWidget(this);
  left_->setMinimumWidth(170);
  QSplitter *split = mainSplitter(this, {left_, tabs_}, {230, 1000});
  split->setStretchFactor(0, 0);
  layout->addWidget(split);

  // --- Search tab ---
  auto *search = new QWidget(tabs_);
  auto *sl = new QVBoxLayout(search);
  auto *head = new QHBoxLayout;
  diagram_ = new QLabel(search);
  diagram_->setFixedSize(diagramSize(), diagramSize());
  diagram_->setAlignment(Qt::AlignCenter);
  caseTitle_ = new QLabel(search);
  caseTitle_->setWordWrap(true);
  caseTitle_->setTextFormat(Qt::RichText);
  caseTitle_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  head->addWidget(diagram_);
  head->addWidget(caseTitle_, 1);
  sl->addLayout(head);

  // The rows of options wrap in a narrow window.
  auto *filters = new OptionBar(search);
  statsCube_ = new QComboBox(search);
  statsCube_->addItem(tr("Physical times"));
  statsCube_->addItem(tr("Virtual times"));
  statsWindow_ = new QComboBox(search);
  statsWindow_->addItem(tr("All time"));
  statsWindow_->addItem(tr("Current session"));
  statsWindow_->addItem(tr("Last 50"));
  profileBox_ = new QComboBox(search);
  auto *profileMenu = new QToolButton(search);
  profileMenu->setText(tr("Profile…"));
  profileMenu->setPopupMode(QToolButton::InstantPopup);
  auto *pm = new QMenu(profileMenu);
  pm->addAction(tr("Edit this profile…"), this,
                [this] { editProfile(ProfileEdit::InPlace); });
  pm->addAction(tr("New profile from this one…"), this,
                [this] { editProfile(ProfileEdit::Copy); });
  pm->addAction(tr("New grip profile…"), this, [this] {
    ErgoProfile p = builtinErgoProfiles().front();
    p.name = tr("My grips").toStdString();
    GripProfileDialog d(p, this);
    if (d.exec() != QDialog::Accepted) {
      return;
    }
    ErgoProfile made = d.profile();
    if (made.name.empty()) {
      made.name = p.name;
    }
    made.name = store_.uniqueProfileName(made.name);
    store_.profiles().push_back(made);
    saveStore();
    fillProfiles();
    profileBox_->setCurrentText(qs(made.name));
  });
  pm->addAction(tr("Delete this profile"), this, [this] {
    auto &ps = store_.profiles();
    if (ps.size() <= 1) {
      emit message(tr("The last profile can't be deleted."));
      return;
    }
    int i = profileBox_->currentIndex();
    if (i >= 0 && i < static_cast<int>(ps.size())) {
      ps.erase(ps.begin() + i);
      saveStore();
      fillProfiles();
      // The box fell back to another profile: that is the one chosen now.
      setScoreProfileName(profileBox_->currentText());
      scoreCache_.clear();
      fillTable();
    }
  });
  pm->addAction(tr("Restore the built-in profiles"), this, [this] {
    auto &ps = store_.profiles();
    for (const auto &b : builtinErgoProfiles()) {
      auto it = std::find_if(ps.begin(), ps.end(), [&](const ErgoProfile &p) {
        return p.name == b.name;
      });
      if (it != ps.end()) {
        *it = b;
      } else {
        ps.push_back(b);
      }
    }
    saveStore();
    fillProfiles();
    scoreCache_.clear();
    fillTable();
  });
  profileMenu->setMenu(pm);
  filterBox_ = new QComboBox(search);
  noRotations_ = new QCheckBox(tr("No rotations"), search);
  maxHtm_ = new QSpinBox(search);
  maxHtm_->setRange(0, 60);
  maxHtm_->setSpecialValueText(tr("any length"));
  maxHtm_->setPrefix(tr("≤ "));
  maxHtm_->setSuffix(tr(" HTM"));
  // Two groups, each named: what the time and score columns show, and
  // which algs are listed.
  auto groupLabel = [search](const QString &text) {
    auto *label = new QLabel(text, search);
    QFont f = label->font();
    f.setBold(true);
    label->setFont(f);
    return label;
  };
  auto *timesLabel = groupLabel(tr("Times shown:"));
  timesLabel->setBuddy(statsCube_);
  filters->addWidget(timesLabel);
  filters->addWidget(statsCube_);
  filters->addWidget(statsWindow_);
  filters->addSpacing(12);
  filters->addWidget(new QLabel(tr("Score:"), search));
  filters->addWidget(profileBox_);
  filters->addWidget(profileMenu);
  filters->addSpacing(12);
  // One bar, so that the page still fits half a 1080p screen; the second
  // group is one item, which wraps onto its own row when narrow.
  auto *shown = new QWidget(search);
  auto *shownLayout = new QHBoxLayout(shown);
  shownLayout->setContentsMargins(0, 0, 0, 0);
  auto *algsLabel = groupLabel(tr("Algs shown:"));
  algsLabel->setBuddy(filterBox_);
  shownLayout->addWidget(algsLabel);
  shownLayout->addWidget(filterBox_);
  shownLayout->addWidget(noRotations_);
  shownLayout->addWidget(maxHtm_);
  filters->addWidget(shown);
  filters->addStretch();
  sl->addWidget(filters);
  statsCube_->setAccessibleName(tr("Times of the physical or virtual cube"));
  statsWindow_->setAccessibleName(tr("Times from"));
  profileBox_->setAccessibleName(tr("Score profile"));
  profileMenu->setAccessibleName(tr("Score profile menu"));
  filterBox_->setAccessibleName(tr("Moves the algs may use"));
  maxHtm_->setAccessibleName(tr("Longest alg shown"));

  table_ = new QTableWidget(0, ColumnCount, search);
  table_->setHorizontalHeaderLabels(
      {tr("Alg"), tr("HTM"), tr("STM"), tr("QTM"), tr("ETM"), tr("Score"),
       tr("From"), tr("n"), tr("Mean"), tr("Median"), tr("Best"), tr("ao5"),
       tr("ao12"), tr("SD"), tr("DNF %")});
  table_->verticalHeader()->hide();
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  // Unsorted (the set's algs first) until a column is clicked.
  table_->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
  table_->setSortingEnabled(true);
  table_->setContextMenuPolicy(Qt::CustomContextMenu);
  table_->horizontalHeader()->setSectionResizeMode(ColAlg,
                                                   QHeaderView::Stretch);
  table_->setFocusPolicy(Qt::ClickFocus);
  auto *tableSplit = new QSplitter(Qt::Vertical, search);
  tableSplit->setObjectName("candidates");
  tableSplit->setChildrenCollapsible(false);
  tableSplit->addWidget(table_);
  auto *gripBox = new QGroupBox(tr("Fingertricks"), tableSplit);
  auto *gripLayout = new QVBoxLayout(gripBox);
  // The fingertricks scroll in a low window rather than holding it high.
  grips_ = new GripView(gripBox);
  gripLayout->addWidget(scrollArea(grips_, gripBox));
  tableSplit->addWidget(gripBox);
  tableSplit->setStretchFactor(0, 1);
  tableSplit->setStretchFactor(1, 0);
  sl->addWidget(tableSplit, 1);

  auto *addRow = new QHBoxLayout;
  addEdit_ = new QLineEdit(search);
  addEdit_->setPlaceholderText(tr("Type an alg and press Enter to add it"));
  auto *addButton = new QPushButton(tr("Add"), search);
  auto *pasteButton = new QPushButton(tr("Paste algs…"), search);
  addRow->addWidget(addEdit_, 1);
  addRow->addWidget(addButton);
  addRow->addWidget(pasteButton);
  sl->addLayout(addRow);

  auto *actions = new OptionBar(search);
  auto *toSet = new QPushButton(tr("Add to set"), search);
  auto *makeMain = new QPushButton(tr("Make main alg"), search);
  auto *toSession = new QPushButton(tr("Add to session"), search);
  // The one prominent button: what the tab is mostly for.
  QFont tf = toSession->font();
  tf.setBold(true);
  toSession->setFont(tf);
  toSession->setDefault(true);
  toSession->setAutoDefault(false);
  auto *remove = new QPushButton(tr("Remove"), search);
  auto *clear = new QPushButton(tr("Clear times…"), search);
  for (QPushButton *b : {toSession, toSet, makeMain, remove, clear}) {
    actions->addWidget(b);
  }
  actions->addStretch();
  sl->addWidget(actions);

  auto *gen = new QGroupBox(tr("Generator"), search);
  auto *genLayout = new QVBoxLayout(gen);
  auto *gl = new OptionBar(gen);
  genLayout->addWidget(gl);
  genBox_ = new QComboBox(gen);
  auto *genMenu = new QToolButton(gen);
  genMenu->setText(tr("…"));
  genMenu->setAccessibleName(tr("Move set menu"));
  genMenu->setPopupMode(QToolButton::InstantPopup);
  auto *gm = new QMenu(genMenu);
  gm->addAction(tr("Edit the move set…"), this, [this] {
    if (!editMoveSet(genSet_)) {
      return;
    }
    loading_ = true;
    selectMoveSet(genBox_, genSet_);
    loading_ = false;
    genChosen();
  });
  gm->addAction(tr("Delete this preset"), this, [this] {
    QString d = genBox_->currentData().toString();
    if (!d.startsWith("u:")) {
      emit message(tr("Only your own presets can be deleted."));
      return;
    }
    auto &ms = store_.moveSets();
    auto set = MoveSet::parse(d.mid(2).toStdString());
    std::erase_if(ms, [&](const MoveSetPreset &p) { return p.set == set; });
    saveStore();
    fillMoveSetBoxes();
  });
  genMenu->setMenu(gm);
  lengthMode_ = new QComboBox(gen);
  lengthMode_->addItem(tr("Shortest +"));
  lengthMode_->addItem(tr("Every length"));
  extra_ = new QSpinBox(gen);
  extra_->setRange(0, 8);
  extra_->setSuffix(tr(" moves"));
  maxLength_ = new QSpinBox(gen);
  maxLength_->setRange(1, 30);
  maxLength_->setPrefix(tr("up to "));
  maxLength_->setSuffix(tr(" moves"));
  maxResults_ = new QSpinBox(gen);
  maxResults_->setRange(1, 5000);
  maxResults_->setPrefix(tr("at most "));
  maxResults_->setSuffix(tr(" algs"));
  runButton_ = new QPushButton(tr("Find algs"), gen);
  batchButton_ = new QPushButton(tr("Whole group…"), gen);
  auto *bm = new QMenu(batchButton_);
  bm->addAction(tr("Every case in this case's group"), this,
                [this] { runBatch(Batch::Group); });
  bm->addAction(tr("Every case in this set"), this,
                [this] { runBatch(Batch::WholeSet); });
  batchButton_->setMenu(bm);
  stopButton_ = new QPushButton(tr("Stop"), gen);
  genStatus_ = new QLabel(gen);
  genStatus_->setWordWrap(true);
  for (QWidget *w : std::initializer_list<QWidget *>{
           genBox_, genMenu, lengthMode_, extra_, maxLength_, maxResults_,
           runButton_, batchButton_, stopButton_}) {
    gl->addWidget(w);
  }
  gl->addStretch();
  genLayout->addWidget(genStatus_);
  // The generator folds away: a button with an arrow shows or hides it.
  auto *genToggle = new QToolButton(search);
  genToggle->setText(tr("Generator"));
  genToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  genToggle->setAutoRaise(true);
  genToggle->setCheckable(true);
  genToggle->setFocusPolicy(Qt::NoFocus);
  gen->setTitle(QString());
  auto showGen = [gen, genToggle](bool on) {
    gen->setVisible(on);
    genToggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
  };
  bool genShown = saved::lab("generatorShown", true).get().toBool();
  genToggle->setChecked(genShown);
  showGen(genShown);
  connect(genToggle, &QToolButton::toggled, this, [showGen](bool on) {
    saved::lab("generatorShown").set(on);
    showGen(on);
  });
  sl->addWidget(genToggle);
  sl->addWidget(gen);
  tabs_->addTab(search, tr("Search"));

  // --- Test tab ---
  test_ = new LabTestPanel(settings_, library_, store_, tabs_);
  tabs_->addTab(test_, tr("Test"));

  refreshTimer_.setSingleShot(true);
  refreshTimer_.setInterval(250);
  connect(&refreshTimer_, &QTimer::timeout, this, [this] {
    // While a search runs, only what it found is added; the fill at its
    // end fits the columns.
    if (job_ && !tableStale_) {
      appendToTable();
    } else {
      fillTable(!job_);
    }
    tableStale_ = false;
    if (job_ && job_->dirty) {
      job_->dirty = false;
      saveStore();
    }
  });

  // A search of many cases only shows one of them, so what it finds is
  // also saved every few seconds.
  saveTimer_.setSingleShot(true);
  saveTimer_.setInterval(5000);
  connect(&saveTimer_, &QTimer::timeout, this, [this] {
    if (job_ && job_->dirty) {
      job_->dirty = false;
      saveStore();
    }
  });

  // Choices kept from last time. A position the box doesn't have would
  // leave it blank.
  auto restoreIndex = [](QComboBox *box, const char *name) {
    int i = saved::lab(name, 0).get().toInt();
    box->setCurrentIndex(i >= 0 && i < box->count() ? i : 0);
  };
  restoreIndex(lengthMode_, "lengthMode");
  extra_->setValue(saved::lab("extra", 2).get().toInt());
  maxLength_->setValue(saved::lab("maxLength", 16).get().toInt());
  maxResults_->setValue(saved::lab("maxResults", 200).get().toInt());
  restoreIndex(statsCube_, "statsCube");
  restoreIndex(statsWindow_, "statsWindow");
  noRotations_->setChecked(saved::lab("noRotations", false).get().toBool());
  maxHtm_->setValue(saved::lab("maxHtm", 0).get().toInt());
  if (auto s = MoveSet::parse(
          saved::lab("moveSet", "U, R, F").get().toString().toStdString())) {
    genSet_ = *s;
  }
  QString filter = saved::lab("filter").get().toString();
  if (auto s = MoveSet::parse(filter.toStdString())) {
    filterSet_ = *s;
    filterOn_ = true;
  }
  tabs_->setCurrentIndex(saved::lab("tab", 0).get().toInt());

  fillProfiles();
  fillMoveSetBoxes();
  fillSources();

  connect(sourceBox_, &QComboBox::currentIndexChanged, this, [this] {
    if (!loading_) {
      saved::lab("source").set(sourceBox_->currentData());
      fillCases();
    }
  });
  // The case list is filled again once the typing pauses, and only shows
  // the case again when another one is left selected.
  filterTimer_.setSingleShot(true);
  filterTimer_.setInterval(150);
  connect(&filterTimer_, &QTimer::timeout, this,
          [this] { fillCases(Reshow::IfChanged); });
  connect(caseFilter_, &QLineEdit::textChanged, &filterTimer_,
          qOverload<>(&QTimer::start));
  connect(caseTree_, &QTreeWidget::currentItemChanged, this,
          &AlgLabPage::caseChosen);
  connect(newCase, &QPushButton::clicked, this, &AlgLabPage::newLabCase);
  connect(editCase, &QPushButton::clicked, this, &AlgLabPage::editLabCase);
  connect(deleteCase, &QPushButton::clicked, this, &AlgLabPage::deleteLabCase);
  connect(caseTree_, &QTreeWidget::itemDoubleClicked, this, [this] {
    if (sourceBox_->currentData().toString().isEmpty()) {
      editLabCase();
    }
  });
  for (QComboBox *b : {statsCube_, statsWindow_}) {
    connect(b, &QComboBox::currentIndexChanged, this, [this] {
      saved::lab("statsCube").set(statsCube_->currentIndex());
      saved::lab("statsWindow").set(statsWindow_->currentIndex());
      fillTable();
    });
  }
  connect(profileBox_, &QComboBox::currentIndexChanged, this, [this] {
    if (!loading_) {
      setScoreProfileName(profileBox_->currentText());
      scoreCache_.clear();
      fillTable();
    }
  });
  connect(filterBox_, &QComboBox::currentIndexChanged, this,
          &AlgLabPage::filterChosen);
  connect(genBox_, &QComboBox::currentIndexChanged, this,
          &AlgLabPage::genChosen);
  connect(noRotations_, &QCheckBox::toggled, this, [this](bool on) {
    saved::lab("noRotations").set(on);
    fillTable();
  });
  // Holding a spin box's arrow steps it many times a second, and each save
  // rewrites the settings file: the values are saved once it has paused,
  // and the table is filled again then too.
  spinSaveTimer_.setSingleShot(true);
  spinSaveTimer_.setInterval(400);
  connect(&spinSaveTimer_, &QTimer::timeout, this, &AlgLabPage::saveSpinBoxes);
  connect(maxHtm_, &QSpinBox::valueChanged, this, [this] {
    spinSaveTimer_.start();
    tableStale_ = true;
    refreshTimer_.start();
  });
  auto keepGen = [this] {
    spinSaveTimer_.start();
    updateGenControls();
  };
  connect(lengthMode_, &QComboBox::currentIndexChanged, this, keepGen);
  for (QSpinBox *b : {extra_, maxLength_, maxResults_}) {
    connect(b, &QSpinBox::valueChanged, this, keepGen);
  }
  connect(addEdit_, &QLineEdit::returnPressed, this, &AlgLabPage::addTyped);
  connect(addButton, &QPushButton::clicked, this, &AlgLabPage::addTyped);
  connect(pasteButton, &QPushButton::clicked, this,
          &AlgLabPage::pasteCandidates);
  connect(toSet, &QPushButton::clicked, this,
          [this] { addToSet(SetAlg::Add); });
  connect(makeMain, &QPushButton::clicked, this,
          [this] { addToSet(SetAlg::MakeMain); });
  connect(toSession, &QPushButton::clicked, this, &AlgLabPage::addToSession);
  connect(remove, &QPushButton::clicked, this, &AlgLabPage::removeCandidates);
  connect(clear, &QPushButton::clicked, this, &AlgLabPage::clearTimes);
  connect(table_, &QWidget::customContextMenuRequested, this,
          &AlgLabPage::tableMenu);
  connect(table_, &QTableWidget::itemSelectionChanged, this,
          &AlgLabPage::updateGrips);
  connect(runButton_, &QPushButton::clicked, this, [this] {
    if (case_) {
      runSearch({*case_});
    }
  });
  connect(stopButton_, &QPushButton::clicked, this, &AlgLabPage::stopSearch);
  connect(tabs_, &QTabWidget::currentChanged, this, [this](int i) {
    saved::lab("tab").set(i);
    test_->cancelAttempt();
    fillTable();
  });
  connect(test_, &LabTestPanel::message, this, &AlgLabPage::message);
  connect(test_, &LabTestPanel::attemptsChanged, this, [this] {
    if (tabs_->currentWidget() != test_) {
      fillTable();
    } else {
      tableStale_ = true;
      refreshTimer_.start();
    }
  });
  updateGenControls();
}

AlgLabPage::~AlgLabPage() {
  if (spinSaveTimer_.isActive()) {
    saveSpinBoxes();
  }
  if (job_) {
    job_->stop = true;
  }
  jobFuture_.waitForFinished();
}

void AlgLabPage::saveSpinBoxes() {
  spinSaveTimer_.stop();
  saved::lab("maxHtm").set(maxHtm_->value());
  saved::lab("lengthMode").set(lengthMode_->currentIndex());
  saved::lab("extra").set(extra_->value());
  saved::lab("maxLength").set(maxLength_->value());
  saved::lab("maxResults").set(maxResults_->value());
}

void AlgLabPage::saveStore() {
  saveInBackground(store_, this, [this](const QString &error) {
    emit message(tr("Could not save Alg Lab: %1").arg(error));
  });
}

bool AlgLabPage::handleKey(QKeyEvent *e, bool press) {
  if (tabs_->currentWidget() == test_) {
    return test_->handleKey(e, press);
  }
  // Ctrl+Z takes back an alg added to the set or made its main alg here.
  if (press && e->key() == Qt::Key_Z &&
      (e->modifiers() & ~Qt::KeypadModifier) == Qt::ControlModifier) {
    if (setEdits_ > 0 && setUndo_()) {
      --setEdits_;
      showCase(case_ ? std::optional(case_->key) : std::nullopt);
      emit message(tr("The set's algs are as they were before"));
    } else {
      emit message(tr("Nothing changed in a set here to undo"));
    }
    return true;
  }
  // The candidate table takes the keyboard as Library's lists do: the
  // arrows pick algs (with Shift, several), Enter adds them to the
  // session and Delete removes them. The case list keeps its own arrows
  // while it has the focus.
  if (e->modifiers() & ~(Qt::KeypadModifier | Qt::ShiftModifier)) {
    return false;
  }
  if (caseTree_->hasFocus()) {
    return false;
  }
  switch (e->key()) {
  case Qt::Key_Up:
  case Qt::Key_Down:
  case Qt::Key_PageUp:
  case Qt::Key_PageDown:
  case Qt::Key_Home:
  case Qt::Key_End:
    if (!press || table_->rowCount() == 0) {
      return false;
    }
    if (!table_->hasFocus()) {
      table_->setFocus(Qt::ShortcutFocusReason);
      if (!table_->currentIndex().isValid() ||
          table_->selectedItems().isEmpty()) {
        table_->selectRow(e->key() == Qt::Key_Up || e->key() == Qt::Key_End
                              ? table_->rowCount() - 1
                              : 0);
        return true;
      }
      // The table moves its selection as it would with the focus.
      QApplication::sendEvent(table_, e);
      return true;
    }
    return false;
  case Qt::Key_Return:
  case Qt::Key_Enter:
    if (press && !selectedAlgs().empty()) {
      addToSession();
      return true;
    }
    return false;
  case Qt::Key_Delete:
    if (press && !selectedAlgs().empty()) {
      removeCandidates();
      return true;
    }
    return false;
  default:
    return false;
  }
}

void AlgLabPage::applySettings() {
  test_->applySettings();
  showCase(case_ ? std::optional(case_->key) : std::nullopt);
}

bool AlgLabPage::isBusy() const { return test_->isBusy(); }

void AlgLabPage::aboutToClose() {
  if (spinSaveTimer_.isActive()) {
    saveSpinBoxes();
  }
  if (!job_) {
    return;
  }
  // Stop the search and take in what it has found: its results and its
  // last call (searchFinished, which saves) are waiting in the queue.
  job_->stop = true;
  jobFuture_.waitForFinished();
  QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
}

QString AlgLabPage::workStoppedOnClose() const {
  return job_ ? tr("Alg Lab's search") : QString();
}

void AlgLabPage::activated() {
  setEdits_ = 0; // Library may have changed the sets since

  // Sets and their algs may have changed in the other modes.
  reloadSets();
}

void AlgLabPage::deactivated() { test_->cancelAttempt(); }

void AlgLabPage::windowDeactivated() {
  if (test_->isBusy()) {
    test_->cancelAttempt();
  }
}

void AlgLabPage::reloadSets() {
  fillSources();
  test_->refresh();
}

void AlgLabPage::selectCase(const QString &setId, const QString &caseId) {
  int i = sourceBox_->findData(setId);
  if (i < 0) {
    return;
  }
  if (!caseFilter_->text().isEmpty()) {
    caseFilter_->clear();
    filterTimer_.stop();
    fillCases();
  }
  sourceBox_->setCurrentIndex(i);
  QString k = qs(LabCaseKey{setId.toStdString(), caseId.toStdString()}.text());
  for (QTreeWidgetItemIterator it(caseTree_); *it; ++it) {
    if ((*it)->data(0, Qt::UserRole).toString() == k) {
      caseTree_->setCurrentItem(*it);
      return;
    }
  }
}

// --- cases ------------------------------------------------------------------

void AlgLabPage::fillSources() {
  QString current = sourceBox_->count() > 0
                        ? sourceBox_->currentData().toString()
                        : saved::lab("source").get().toString();
  loading_ = true;
  sourceBox_->clear();
  sourceBox_->addItem(tr("Lab cases"), QString());
  for (const AlgSet &s : library_.sets()) {
    sourceBox_->addItem(qs(s.name), qs(s.id));
  }
  int i = sourceBox_->findData(current);
  sourceBox_->setCurrentIndex(i >= 0 ? i : 0);
  loading_ = false;
  fillCases();
}

void AlgLabPage::fillCases(Reshow reshow) {
  QString source = sourceBox_->currentData().toString();
  QString want =
      case_ ? qs(case_->key.text()) : saved::lab("case").get().toString();
  QString filter = caseFilter_->text().trimmed();
  labButtons_->setVisible(source.isEmpty());
  QSignalBlocker block(caseTree_);
  caseTree_->clear();
  QTreeWidgetItem *select = nullptr;
  auto matches = [&](const QString &a, const QString &b) {
    return filter.isEmpty() || a.contains(filter, Qt::CaseInsensitive) ||
           b.contains(filter, Qt::CaseInsensitive);
  };
  if (source.isEmpty()) {
    for (const LabCase &c : store_.cases()) {
      if (!matches(qs(c.name), {})) {
        continue;
      }
      auto *it = new QTreeWidgetItem(caseTree_, {qs(c.name)});
      QString k = qs(LabCaseKey{"", c.id}.text());
      it->setData(0, Qt::UserRole, k);
      if (k == want) {
        select = it;
      }
    }
  } else if (const AlgSet *s = library_.find(source.toStdString())) {
    for (const CaseGroup &g : s->groups) {
      QTreeWidgetItem *group = nullptr;
      for (const AlgCase &c : g.cases) {
        if (!matches(qs(c.name), qs(g.name))) {
          continue;
        }
        if (!group) {
          group = new QTreeWidgetItem(caseTree_, {qs(g.name)});
          group->setFlags(group->flags() & ~Qt::ItemIsSelectable);
          group->setExpanded(true);
        }
        auto *it = new QTreeWidgetItem(group, {qs(c.name)});
        QString k = qs(LabCaseKey{s->id, c.id}.text());
        it->setData(0, Qt::UserRole, k);
        if (k == want) {
          select = it;
        }
      }
    }
  }
  if (!select) {
    // The first case shown, rather than none.
    for (QTreeWidgetItemIterator it(caseTree_); *it; ++it) {
      if (!(*it)->data(0, Qt::UserRole).toString().isEmpty()) {
        select = *it;
        break;
      }
    }
  }
  if (select) {
    caseTree_->setCurrentItem(select);
  }
  block.unblock();
  if (reshow == Reshow::IfChanged && case_ && select &&
      select->data(0, Qt::UserRole).toString() == qs(case_->key.text())) {
    return; // the case shown stays
  }
  caseChosen();
}

void AlgLabPage::caseChosen() {
  QTreeWidgetItem *it = caseTree_->currentItem();
  QString k = it ? it->data(0, Qt::UserRole).toString() : QString();
  auto parsed = LabCaseKey::parse(k.toStdString());
  if (parsed) {
    saved::lab("case").set(k);
  }
  showCase(parsed);
}

void AlgLabPage::showCase(const std::optional<LabCaseKey> &k) {
  case_.reset();
  if (k) {
    case_ = resolveLabCase(library_, store_, *k);
  }
  if (!case_) {
    diagram_->clear();
    caseTitle_->setText(
        sourceBox_->currentData().toString().isEmpty() && store_.cases().empty()
            ? tr("<b>No lab cases yet.</b><br>New… makes one from a setup "
                 "alg, or pick an alg set above to work on its cases.")
            : tr("Pick a case on the left."));
    fillTable();
    updateGenControls();
    return;
  }
  diagram_->setPixmap(
      labDiagram(*case_, settings_, diagramSize(), devicePixelRatioF()));
  QString free = qs(freeToString(case_->free));
  QStringList aufs;
  if (case_->aufBefore) {
    aufs << tr("before");
  }
  if (case_->aufAfter) {
    aufs << tr("after");
  }
  caseTitle_->setText(
      tr("<b>%1</b><br>%2<br>Algs must solve the cube%3.<br>U turns allowed: "
         "%4.")
          .arg(case_->name.toHtmlEscaped(), case_->where.toHtmlEscaped(),
               free.isEmpty() ? QString()
                              : tr(" apart from %1").arg(free.toHtmlEscaped()),
               aufs.isEmpty() ? tr("none") : aufs.join(tr(" and "))));
  fillTable();
  updateGenControls();
}

void AlgLabPage::newLabCase() {
  LabCase c;
  c.id = store_.newCaseId();
  c.free = "LL:p";
  LabCaseDialog d(c, library_, settings_, this);
  if (d.exec() != QDialog::Accepted) {
    return;
  }
  store_.cases().push_back(d.labCase());
  saveStore();
  saved::lab("case").set(qs(LabCaseKey{"", c.id}.text()));
  case_.reset();
  sourceBox_->setCurrentIndex(0);
  fillCases();
}

void AlgLabPage::editLabCase() {
  if (!case_ || !case_->key.isLab()) {
    return;
  }
  for (LabCase &c : store_.cases()) {
    if (c.id == case_->key.caseId) {
      LabCaseDialog d(c, library_, settings_, this);
      if (d.exec() == QDialog::Accepted) {
        c = d.labCase();
        if (job_) {
          job_->dropped.push_back(case_->key);
        }
        saveStore();
        fillCases();
        test_->refresh();
      }
      return;
    }
  }
}

void AlgLabPage::deleteLabCase() {
  if (!case_ || !case_->key.isLab()) {
    return;
  }
  LabCaseKey k = case_->key;
  int times = 0;
  for (const auto &a : store_.attempts()) {
    times += a.key == k ? 1 : 0;
  }
  if (QMessageBox::question(
          this, tr("Delete lab case"),
          plural(times, tr("Delete \"%1\" with its candidates and %n time?"),
                 tr("Delete \"%1\" with its candidates and %n times?"))
              .arg(case_->name)) != QMessageBox::Yes) {
    return;
  }
  std::erase_if(store_.cases(),
                [&](const LabCase &c) { return c.id == k.caseId; });
  if (job_) {
    job_->dropped.push_back(k);
  }
  store_.removeCandidates(k);
  for (std::size_t i = store_.attempts().size(); i-- > 0;) {
    if (store_.attempts()[i].key == k) {
      store_.removeAttempt(i);
    }
  }
  for (auto &s : store_.sessions()) {
    std::erase_if(s.entries, [&](const LabEntry &e) { return e.key == k; });
  }
  saveStore();
  case_.reset();
  fillCases();
  test_->refresh();
}

// --- candidates -------------------------------------------------------------

void AlgLabPage::fillProfiles() {
  QString want = profileBox_->count() > 0 ? profileBox_->currentText()
                                          : scoreProfileName();
  loading_ = true;
  profileBox_->clear();
  for (const auto &p : store_.profiles()) {
    profileBox_->addItem(qs(p.name));
  }
  int i = profileBox_->findText(want);
  profileBox_->setCurrentIndex(i >= 0 ? i : 0);
  loading_ = false;
}

const ErgoProfile &AlgLabPage::profile() const {
  // By position, as editing and deleting go; the box lists them in order.
  const auto &ps = store_.profiles();
  int i = profileBox_->currentIndex();
  if (i >= 0 && i < static_cast<int>(ps.size()) &&
      profileBox_->count() == static_cast<int>(ps.size())) {
    return ps[static_cast<std::size_t>(i)];
  }
  return store_.profile(profileBox_->currentText().toStdString());
}

void AlgLabPage::editProfile(ProfileEdit edit) {
  const bool copy = edit == ProfileEdit::Copy;
  ErgoProfile p = profile();
  if (copy) {
    p.name += " (copy)";
  }
  ErgoProfile edited;
  if (p.grips) {
    GripProfileDialog d(p, this);
    if (d.exec() != QDialog::Accepted) {
      return;
    }
    edited = d.profile();
  } else {
    ProfileDialog d(p, this);
    if (d.exec() != QDialog::Accepted) {
      return;
    }
    edited = d.profile();
  }
  if (edited.name.empty()) {
    edited.name = p.name;
  }
  auto &ps = store_.profiles();
  int i = profileBox_->currentIndex();
  if (copy || i < 0 || i >= static_cast<int>(ps.size())) {
    edited.name = store_.uniqueProfileName(edited.name);
    ps.push_back(edited);
  } else {
    edited.name =
        store_.uniqueProfileName(edited.name, static_cast<std::size_t>(i));
    ps[static_cast<std::size_t>(i)] = edited;
  }
  saveStore();
  fillProfiles();
  loading_ = true;
  profileBox_->setCurrentText(qs(edited.name));
  loading_ = false;
  setScoreProfileName(qs(edited.name));
  scoreCache_.clear();
  fillTable();
}

void AlgLabPage::fillMoveSetBoxes() {
  loading_ = true;
  addMoveSets(genBox_, store_, AnyMoves::Left);
  addMoveSets(filterBox_, store_, AnyMoves::Offered);
  selectMoveSet(genBox_, genSet_);
  if (filterOn_) {
    selectMoveSet(filterBox_, filterSet_);
  } else {
    filterBox_->setCurrentIndex(0);
  }
  loading_ = false;
}

bool AlgLabPage::editMoveSet(MoveSet &set) {
  MoveSetDialog d(set, this);
  if (d.exec() != QDialog::Accepted) {
    return false;
  }
  set = d.moveSet();
  if (!d.presetName().isEmpty()) {
    store_.moveSets().push_back({d.presetName().toStdString(), set});
    saveStore();
    fillMoveSetBoxes();
  }
  return true;
}

void AlgLabPage::filterChosen() {
  if (loading_) {
    return;
  }
  QString d = filterBox_->currentData().toString();
  if (d == "custom") {
    MoveSet set = filterOn_ ? filterSet_ : MoveSet::allFaces();
    if (editMoveSet(set)) {
      filterSet_ = set;
      filterOn_ = true;
    }
    fillMoveSetBoxes();
  } else if (d.isEmpty()) {
    filterOn_ = false;
  } else if (auto s = MoveSet::parse(d.mid(2).toStdString())) {
    filterSet_ = *s;
    filterOn_ = true;
  }
  saved::lab("filter").set(filterOn_ ? qs(filterSet_.toString()) : QString());
  fillTable();
}

void AlgLabPage::genChosen() {
  if (loading_) {
    return;
  }
  QString d = genBox_->currentData().toString();
  if (d == "custom") {
    editMoveSet(genSet_);
    fillMoveSetBoxes();
  } else if (auto s = MoveSet::parse(d.mid(2).toStdString())) {
    genSet_ = *s;
  }
  saved::lab("moveSet").set(qs(genSet_.toString()));
}

} // namespace vcube
