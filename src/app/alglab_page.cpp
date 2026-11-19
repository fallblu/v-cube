#include "app/alglab_page.hpp"
#include "app/alglab_page_private.hpp"
#include "app/ui_state.hpp"

#include "app/alglab_dialogs.hpp"
#include "app/app_settings.hpp"
#include "app/grip_view.hpp"
#include "app/lab_test_panel.hpp"
#include "app/ui_text.hpp"
#include "app/widgets.hpp"
#include "io/algset_json.hpp"
#include "train/ergonomics.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace vcube {

namespace {

constexpr int kDiagramSize = 110;

// The move-set drop-downs: each item holds its move set's text, "custom"
// opens the dialog.
void addMoveSets(QComboBox *box, const AlgLabStore &store, bool any) {
  box->clear();
  if (any) {
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
  auto *split = new QSplitter(Qt::Horizontal, this);
  split->setObjectName("main");
  layout->addWidget(split);

  // Cases.
  left_ = new QWidget(split);
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

  tabs_ = new QTabWidget(split);
  left_->setMinimumWidth(170);
  split->setStretchFactor(0, 0);
  split->setStretchFactor(1, 1);
  split->setSizes({230, 1000});

  // --- Search tab ---
  auto *search = new QWidget(tabs_);
  auto *sl = new QVBoxLayout(search);
  auto *head = new QHBoxLayout;
  diagram_ = new QLabel(search);
  diagram_->setFixedSize(kDiagramSize, kDiagramSize);
  diagram_->setAlignment(Qt::AlignCenter);
  caseTitle_ = new QLabel(search);
  caseTitle_->setWordWrap(true);
  caseTitle_->setTextFormat(Qt::RichText);
  caseTitle_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  head->addWidget(diagram_);
  head->addWidget(caseTitle_, 1);
  sl->addLayout(head);

  auto *filters = new QHBoxLayout;
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
  pm->addAction(tr("Edit this profile…"), this, [this] { editProfile(false); });
  pm->addAction(tr("New profile from this one…"), this,
                [this] { editProfile(true); });
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
      saved::lab("scoreProfile").set(profileBox_->currentText());
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
  filters->addWidget(statsCube_);
  filters->addWidget(statsWindow_);
  filters->addWidget(new QLabel(tr("Score:"), search));
  filters->addWidget(profileBox_);
  filters->addWidget(profileMenu);
  filters->addSpacing(12);
  filters->addWidget(new QLabel(tr("Show:"), search));
  filters->addWidget(filterBox_);
  filters->addWidget(noRotations_);
  filters->addWidget(maxHtm_);
  filters->addStretch();
  sl->addLayout(filters);

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
  grips_ = new GripView(gripBox);
  gripLayout->addWidget(grips_);
  gripLayout->addStretch();
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

  auto *actions = new QHBoxLayout;
  auto *toSet = new QPushButton(tr("Add to set"), search);
  auto *makeMain = new QPushButton(tr("Make main alg"), search);
  auto *toSession = new QPushButton(tr("Add to session"), search);
  auto *remove = new QPushButton(tr("Remove"), search);
  auto *clear = new QPushButton(tr("Clear times…"), search);
  for (QPushButton *b : {toSession, toSet, makeMain, remove, clear}) {
    actions->addWidget(b);
  }
  actions->addStretch();
  sl->addLayout(actions);

  auto *gen = new QGroupBox(tr("Generator"), search);
  auto *genLayout = new QVBoxLayout(gen);
  auto *gl = new QHBoxLayout;
  genLayout->addLayout(gl);
  genBox_ = new QComboBox(gen);
  auto *genMenu = new QToolButton(gen);
  genMenu->setText(tr("…"));
  genMenu->setPopupMode(QToolButton::InstantPopup);
  auto *gm = new QMenu(genMenu);
  gm->addAction(tr("Edit the move set…"), this, [this] {
    MoveSetDialog d(genSet_, this);
    if (d.exec() != QDialog::Accepted) {
      return;
    }
    genSet_ = d.moveSet();
    if (!d.presetName().isEmpty()) {
      store_.moveSets().push_back({d.presetName().toStdString(), genSet_});
      saveStore();
      fillMoveSetBoxes();
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
                [this] { runBatch(false); });
  bm->addAction(tr("Every case in this set"), this, [this] { runBatch(true); });
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
    fillTable();
    if (job_ && job_->dirty) {
      job_->dirty = false;
      saveStore();
    }
  });

  // Choices kept from last time.
  lengthMode_->setCurrentIndex(saved::lab("lengthMode", 0).get().toInt());
  extra_->setValue(saved::lab("extra", 2).get().toInt());
  maxLength_->setValue(saved::lab("maxLength", 16).get().toInt());
  maxResults_->setValue(saved::lab("maxResults", 200).get().toInt());
  statsCube_->setCurrentIndex(saved::lab("statsCube", 0).get().toInt());
  statsWindow_->setCurrentIndex(saved::lab("statsWindow", 0).get().toInt());
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
  connect(caseFilter_, &QLineEdit::textChanged, this, &AlgLabPage::fillCases);
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
      saved::lab("scoreProfile").set(profileBox_->currentText());
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
  connect(maxHtm_, &QSpinBox::valueChanged, this, [this](int v) {
    saved::lab("maxHtm").set(v);
    fillTable();
  });
  auto keepGen = [this] {
    saved::lab("lengthMode").set(lengthMode_->currentIndex());
    saved::lab("extra").set(extra_->value());
    saved::lab("maxLength").set(maxLength_->value());
    saved::lab("maxResults").set(maxResults_->value());
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
  connect(toSet, &QPushButton::clicked, this, [this] { addToSet(false); });
  connect(makeMain, &QPushButton::clicked, this, [this] { addToSet(true); });
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
      refreshTimer_.start();
    }
  });
  updateGenControls();
}

AlgLabPage::~AlgLabPage() {
  if (job_) {
    job_->stop = true;
  }
  jobFuture_.waitForFinished();
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
    if (setEdits_ > 0 && setUndo_ && setUndo_()) {
      --setEdits_;
      showCase(case_ ? std::optional(case_->key) : std::nullopt);
      emit message(tr("The set's algs are as they were before"));
    } else {
      emit message(tr("Nothing changed in a set here to undo"));
    }
    return true;
  }
  return false;
}

void AlgLabPage::applySettings() {
  test_->applySettings();
  showCase(case_ ? std::optional(case_->key) : std::nullopt);
}

bool AlgLabPage::isBusy() const { return test_->isBusy(); }

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

void AlgLabPage::focusModeChanged() { left_->setVisible(!focusMode()); }

void AlgLabPage::reloadSets() {
  fillSources();
  test_->refresh();
}

void AlgLabPage::selectCase(const QString &setId, const QString &caseId) {
  int i = sourceBox_->findData(setId);
  if (i < 0) {
    return;
  }
  caseFilter_->clear();
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

void AlgLabPage::fillCases() {
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
      labDiagram(*case_, settings_, kDiagramSize, devicePixelRatioF()));
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
  QString want = profileBox_->count() > 0
                     ? profileBox_->currentText()
                     : saved::lab("scoreProfile").get().toString();
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
  return store_.profile(profileBox_->currentText().toStdString());
}

void AlgLabPage::editProfile(bool copy) {
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
    ps.push_back(edited);
  } else {
    ps[static_cast<std::size_t>(i)] = edited;
  }
  saveStore();
  loading_ = true;
  profileBox_->clear();
  for (const auto &x : ps) {
    profileBox_->addItem(qs(x.name));
  }
  profileBox_->setCurrentText(qs(edited.name));
  loading_ = false;
  saved::lab("scoreProfile").set(qs(edited.name));
  scoreCache_.clear();
  fillTable();
}

void AlgLabPage::fillMoveSetBoxes() {
  loading_ = true;
  addMoveSets(genBox_, store_, false);
  addMoveSets(filterBox_, store_, true);
  selectMoveSet(genBox_, genSet_);
  if (filterOn_) {
    selectMoveSet(filterBox_, filterSet_);
  } else {
    filterBox_->setCurrentIndex(0);
  }
  loading_ = false;
}

void AlgLabPage::filterChosen() {
  if (loading_) {
    return;
  }
  QString d = filterBox_->currentData().toString();
  if (d == "custom") {
    MoveSetDialog dialog(filterOn_ ? filterSet_ : MoveSet::allFaces(), this);
    if (dialog.exec() == QDialog::Accepted) {
      filterSet_ = dialog.moveSet();
      filterOn_ = true;
      if (!dialog.presetName().isEmpty()) {
        store_.moveSets().push_back(
            {dialog.presetName().toStdString(), filterSet_});
        saveStore();
      }
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
    MoveSetDialog dialog(genSet_, this);
    if (dialog.exec() == QDialog::Accepted) {
      genSet_ = dialog.moveSet();
      if (!dialog.presetName().isEmpty()) {
        store_.moveSets().push_back(
            {dialog.presetName().toStdString(), genSet_});
        saveStore();
      }
    }
    fillMoveSetBoxes();
  } else if (auto s = MoveSet::parse(d.mid(2).toStdString())) {
    genSet_ = *s;
  }
  saved::lab("moveSet").set(qs(genSet_.toString()));
}

} // namespace vcube
