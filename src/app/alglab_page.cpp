#include "app/alglab_page.hpp"

#include "app/alglab_dialogs.hpp"
#include "app/app_settings.hpp"
#include "app/grip_view.hpp"
#include "app/lab_test_panel.hpp"
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

enum Column {
  ColAlg,
  ColHtm,
  ColStm,
  ColQtm,
  ColEtm,
  ColScore,
  ColSource,
  ColCount,
  ColMean,
  ColMedian,
  ColBest,
  ColAo5,
  ColAo12,
  ColSd,
  ColDnf,
  ColumnCount
};

QString sourceText(const CandidateRow &r) {
  if (r.inSet) {
    return QObject::tr("set");
  }
  switch (r.source) {
  case CandidateSource::Generated:
    return QObject::tr("generated");
  case CandidateSource::Pasted:
    return QObject::tr("pasted");
  case CandidateSource::Typed:
    return QObject::tr("typed");
  case CandidateSource::Set:
    return QObject::tr("was in set");
  }
  return {};
}

QString key(const char *name) { return QStringLiteral("alglab/") + name; }

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

struct AlgLabPage::Job {
  std::atomic<bool> stop{false};
  int cases = 0;
  int found = 0;
  bool dirty = false; // candidates added since the last save
};

AlgLabPage::AlgLabPage(AppSettings &settings, AlgLibrary &library,
                       AlgLabStore &store, QWidget *parent)
    : ModePage(parent), settings_(settings), library_(library), store_(store) {
  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  auto *split = new QSplitter(Qt::Horizontal, this);
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
  QSettings q;
  lengthMode_->setCurrentIndex(q.value(key("lengthMode"), 0).toInt());
  extra_->setValue(q.value(key("extra"), 2).toInt());
  maxLength_->setValue(q.value(key("maxLength"), 16).toInt());
  maxResults_->setValue(q.value(key("maxResults"), 200).toInt());
  statsCube_->setCurrentIndex(q.value(key("statsCube"), 0).toInt());
  statsWindow_->setCurrentIndex(q.value(key("statsWindow"), 0).toInt());
  noRotations_->setChecked(q.value(key("noRotations"), false).toBool());
  maxHtm_->setValue(q.value(key("maxHtm"), 0).toInt());
  if (auto s = MoveSet::parse(
          q.value(key("moveSet"), "U, R, F").toString().toStdString())) {
    genSet_ = *s;
  }
  QString filter = q.value(key("filter")).toString();
  if (auto s = MoveSet::parse(filter.toStdString())) {
    filterSet_ = *s;
    filterOn_ = true;
  }
  tabs_->setCurrentIndex(q.value(key("tab"), 0).toInt());

  fillProfiles();
  fillMoveSetBoxes();
  fillSources();

  connect(sourceBox_, &QComboBox::currentIndexChanged, this, [this] {
    if (!loading_) {
      QSettings().setValue(key("source"), sourceBox_->currentData());
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
      QSettings kept;
      kept.setValue(key("statsCube"), statsCube_->currentIndex());
      kept.setValue(key("statsWindow"), statsWindow_->currentIndex());
      fillTable();
    });
  }
  connect(profileBox_, &QComboBox::currentIndexChanged, this, [this] {
    if (!loading_) {
      QSettings().setValue(kLabProfileSetting, profileBox_->currentText());
      scoreCache_.clear();
      fillTable();
    }
  });
  connect(filterBox_, &QComboBox::currentIndexChanged, this,
          &AlgLabPage::filterChosen);
  connect(genBox_, &QComboBox::currentIndexChanged, this,
          &AlgLabPage::genChosen);
  connect(noRotations_, &QCheckBox::toggled, this, [this](bool on) {
    QSettings().setValue(key("noRotations"), on);
    fillTable();
  });
  connect(maxHtm_, &QSpinBox::valueChanged, this, [this](int v) {
    QSettings().setValue(key("maxHtm"), v);
    fillTable();
  });
  auto keepGen = [this] {
    QSettings kept;
    kept.setValue(key("lengthMode"), lengthMode_->currentIndex());
    kept.setValue(key("extra"), extra_->value());
    kept.setValue(key("maxLength"), maxLength_->value());
    kept.setValue(key("maxResults"), maxResults_->value());
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
    QSettings().setValue(key("tab"), i);
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
  return false;
}

void AlgLabPage::applySettings() {
  test_->applySettings();
  showCase(case_ ? std::optional(case_->key) : std::nullopt);
}

bool AlgLabPage::isBusy() const { return test_->isBusy(); }

void AlgLabPage::activated() {
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
                        : QSettings().value(key("source")).toString();
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
      case_ ? qs(case_->key.text()) : QSettings().value(key("case")).toString();
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
    QSettings().setValue(key("case"), k);
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
  QSettings().setValue(key("case"), qs(LabCaseKey{"", c.id}.text()));
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
          tr("Delete \"%1\" with its candidates and %n time(s)?", nullptr,
             times)
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
                     : QSettings().value(kLabProfileSetting).toString();
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
  QSettings().setValue(kLabProfileSetting, qs(edited.name));
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
  QSettings().setValue(key("filter"),
                       filterOn_ ? qs(filterSet_.toString()) : QString());
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
  QSettings().setValue(key("moveSet"), qs(genSet_.toString()));
}

void AlgLabPage::fillTable() {
  QString selected;
  if (auto *it = table_->currentItem()) {
    selected = table_->item(it->row(), ColAlg)->data(Qt::UserRole).toString();
  }
  // The guidance follows the selection once the table is filled again.
  QSignalBlocker blockSelection(table_);
  struct UpdateAfter {
    AlgLabPage *page;
    ~UpdateAfter() { page->updateGrips(); }
  } updateAfter{this};
  table_->setSortingEnabled(false);
  table_->setRowCount(0);
  if (!case_) {
    return;
  }
  rows_ = candidateRows(*case_, store_);
  const ErgoProfile &prof = profile();
  CubeKind cube =
      statsCube_->currentIndex() == 1 ? CubeKind::Virtual : CubeKind::Physical;
  LabWindow window;
  if (statsWindow_->currentIndex() == 1) {
    window.kind = LabWindow::Kind::Session;
    window.session = store_.current().id;
  } else if (statsWindow_->currentIndex() == 2) {
    window.kind = LabWindow::Kind::Recent;
    window.recent = 50;
  }
  WarmupOf warm = [this](std::string_view id) { return store_.warmupOf(id); };
  const int trim = store_.current().trimPercent;
  const int d = settings_.decimals;
  int hidden = 0;
  for (const CandidateRow &r : rows_) {
    Alg alg = parseAlgOrEmpty(r.alg);
    MoveCounts mc = moveCounts(alg);
    bool rotations = std::any_of(alg.begin(), alg.end(),
                                 [](Move m) { return isRotation(m.type); });
    if ((filterOn_ && !filterSet_.contains(alg)) ||
        (noRotations_->isChecked() && rotations) ||
        (maxHtm_->value() > 0 && mc.htm > maxHtm_->value())) {
      ++hidden;
      continue;
    }
    int row = table_->rowCount();
    table_->insertRow(row);
    auto *algItem = new QTableWidgetItem(r.shown);
    algItem->setFlags(algItem->flags() & ~Qt::ItemIsEditable);
    algItem->setData(Qt::UserRole, qs(r.alg));
    if (!r.fit) {
      algItem->setForeground(palette().color(QPalette::PlaceholderText));
    }
    if (r.inSet && !case_->setAlgs.empty() &&
        labAlgKey(case_->setAlgs.front()) == r.alg) {
      QFont f = algItem->font();
      f.setBold(true);
      algItem->setFont(f);
    }
    table_->setItem(row, ColAlg, algItem);
    auto num = [&](int col, const QString &text, std::optional<double> k) {
      table_->setItem(row, col, new SortItem(text, k));
    };
    num(ColHtm, QString::number(mc.htm), mc.htm);
    num(ColStm, QString::number(mc.stm), mc.stm);
    num(ColQtm, QString::number(mc.qtm), mc.qtm);
    num(ColEtm, QString::number(mc.etm), mc.etm);
    auto cached = scoreCache_.find(r.alg);
    if (cached == scoreCache_.end()) {
      cached = scoreCache_.emplace(r.alg, ergoTotal(alg, prof)).first;
    }
    double score = cached->second;
    num(ColScore, QString::number(score, 'f', 1), score);
    auto *src = new QTableWidgetItem(sourceText(r));
    src->setFlags(src->flags() & ~Qt::ItemIsEditable);
    table_->setItem(row, ColSource, src);
    LabStats st = labStats(store_.attempts(), case_->key, r.alg, cube, window,
                           trim, warm);
    auto ms = [](std::optional<std::int64_t> v) -> std::optional<double> {
      if (!v) {
        return std::nullopt;
      }
      return static_cast<double>(*v);
    };
    num(ColCount, QString::number(st.used), st.used);
    num(ColMean, labTime(st.mean, d), ms(st.mean));
    num(ColMedian, labTime(st.median, d), ms(st.median));
    num(ColBest, labTime(st.best, d), ms(st.best));
    num(ColAo5, labTime(st.ao5, d), ms(st.ao5));
    num(ColAo12, labTime(st.ao12, d), ms(st.ao12));
    num(ColSd, st.sd ? QString::number(*st.sd / 1000.0, 'f', d) : "–", st.sd);
    num(ColDnf,
        st.used + st.dnfs ? QString::number(st.dnfRate() * 100.0, 'f', 0) : "–",
        st.used + st.dnfs ? std::optional(st.dnfRate()) : std::nullopt);
    if (qs(r.alg) == selected) {
      table_->selectRow(row);
    }
  }
  table_->setSortingEnabled(true);
  for (int c = ColHtm; c < ColumnCount; ++c) {
    table_->resizeColumnToContents(c);
  }
  if (hidden > 0 && !job_) {
    genStatus_->setText(
        tr("%n alg(s) hidden by the filters.", nullptr, hidden));
  }
}

void AlgLabPage::updateGrips() {
  const ErgoProfile *g = labGuideProfile(store_, profileBox_->currentText());
  QTableWidgetItem *it = table_->currentItem();
  if (!it || !table_->item(it->row(), ColAlg) || !it->isSelected()) {
    grips_->clear(case_ ? tr("Select an alg to see how to turn it.")
                        : QString());
    return;
  }
  QString alg = table_->item(it->row(), ColAlg)->data(Qt::UserRole).toString();
  grips_->setAlg(alg, g ? &*g->grips : nullptr,
                 tr("No grip profile: make one with Profile… > New grip "
                    "profile, or restore the built-in profiles."));
}

std::vector<std::string> AlgLabPage::selectedAlgs() const {
  std::vector<std::string> out;
  for (const auto &index : table_->selectionModel()->selectedRows(ColAlg)) {
    out.push_back(index.data(Qt::UserRole).toString().toStdString());
  }
  return out;
}

void AlgLabPage::addTyped() {
  if (!case_) {
    return;
  }
  std::string text = cleanAlgText(addEdit_->text().toStdString());
  if (text.empty()) {
    return;
  }
  auto parsed = parseAlg(text);
  if (!parsed) {
    emit message(tr("Can't read that alg at character %1.")
                     .arg(parsed.error->position + 1));
    return;
  }
  std::string k = labAlgKey(text);
  if (!fitAlg(case_->state, parsed.alg, case_->free, case_->aufBefore,
              case_->aufAfter)) {
    AlgCheck check = checkAlg(case_->state, k, case_->free, case_->aufBefore,
                              case_->aufAfter);
    emit message(tr("That alg doesn't solve the case: it %1. Added anyway, "
                    "greyed out.")
                     .arg(qs(describeCheck(check))));
  }
  store_.addCandidate(case_->key, {k, CandidateSource::Typed, unixNow()});
  saveStore();
  addEdit_->clear();
  fillTable();
}

void AlgLabPage::pasteCandidates() {
  if (!case_) {
    return;
  }
  bool ok = false;
  QString text = QInputDialog::getMultiLineText(
      this, tr("Paste algs"),
      tr("One alg per line (\"name: alg / alg\" lines and // comments are "
         "fine). Algs that don't solve the case are left out."),
      QApplication::clipboard()->text(), &ok);
  if (!ok) {
    return;
  }
  int added = 0, failed = 0, have = 0;
  for (const PasteItem &p : readPaste(text.toStdString())) {
    auto parsed = parseAlg(p.alg);
    if (!parsed || parsed.alg.empty() ||
        !fitAlg(case_->state, parsed.alg, case_->free, case_->aufBefore,
                case_->aufAfter)) {
      ++failed;
      continue;
    }
    if (store_.addCandidate(case_->key, {labAlgKey(p.alg),
                                         CandidateSource::Pasted, unixNow()})) {
      ++added;
    } else {
      ++have;
    }
  }
  saveStore();
  fillTable();
  emit message(tr("Added %1 alg(s); %2 didn't solve the case, %3 were there "
                  "already.")
                   .arg(added)
                   .arg(failed)
                   .arg(have));
}

void AlgLabPage::addToSet(bool makeMain) {
  if (!case_) {
    return;
  }
  if (case_->key.isLab()) {
    emit message(tr("A lab case isn't in a set."));
    return;
  }
  auto algs = selectedAlgs();
  if (algs.empty() || (makeMain && algs.size() != 1)) {
    emit message(makeMain ? tr("Select one alg to make it the main alg.")
                          : tr("Select the algs to add to the set."));
    return;
  }
  AlgSet *set = library_.find(case_->key.setId);
  AlgCase *c = set ? set->findCase(case_->key.caseId) : nullptr;
  if (!c) {
    return;
  }
  std::vector<std::string> newAlgs = c->algs;
  int added = 0;
  for (const std::string &a : algs) {
    auto it =
        std::find_if(newAlgs.begin(), newAlgs.end(),
                     [&](const std::string &x) { return labAlgKey(x) == a; });
    if (makeMain) {
      std::string text = it != newAlgs.end() ? *it : a;
      if (it != newAlgs.end()) {
        newAlgs.erase(it);
      }
      newAlgs.insert(newAlgs.begin(), text);
      ++added;
    } else if (it == newAlgs.end()) {
      newAlgs.push_back(a);
      ++added;
    }
  }
  if (added == 0 || newAlgs == c->algs) {
    emit message(tr("The set has those algs already."));
    return;
  }
  QString what = makeMain ? tr("Make main alg") : tr("Add algs");
  bool saved = false;
  if (algEditor_) {
    saved = algEditor_(qs(set->id), qs(c->id), newAlgs, what);
  } else {
    std::vector<std::string> old = c->algs;
    c->algs = newAlgs;
    saved = library_.save(*set);
    if (!saved) {
      c->algs = old;
    }
  }
  if (!saved) {
    emit message(tr("Could not save the set."));
    return;
  }
  emit message(
      makeMain ? tr("Made it the main alg of %1.").arg(case_->name)
               : tr("Added %n alg(s) to %1.", nullptr, added).arg(case_->name));
  if (!algEditor_) {
    emit setsChanged(qs(set->id));
  }
  showCase(case_->key);
}

void AlgLabPage::addToSession() {
  if (!case_) {
    return;
  }
  std::vector<LabEntry> entries;
  for (const std::string &a : selectedAlgs()) {
    auto parsed = parseAlg(a);
    if (parsed && fitAlg(case_->state, parsed.alg, case_->free,
                         case_->aufBefore, case_->aufAfter)) {
      entries.push_back({case_->key, a});
    }
  }
  if (entries.empty()) {
    emit message(tr("Select algs that solve the case to add them."));
    return;
  }
  int added = test_->addEntries(entries);
  emit message(tr("Added %1 alg(s) to the session %2 (Test tab).")
                   .arg(added)
                   .arg(test_->sessionName()));
}

void AlgLabPage::removeCandidates() {
  if (!case_) {
    return;
  }
  int removed = 0, inSet = 0;
  for (const std::string &a : selectedAlgs()) {
    if (store_.removeCandidate(case_->key, a)) {
      ++removed;
    } else {
      ++inSet;
    }
  }
  if (removed) {
    saveStore();
    fillTable();
  }
  if (inSet) {
    emit message(tr("Algs in the set are removed in Library."));
  }
}

void AlgLabPage::clearTimes() {
  auto algs = selectedAlgs();
  if (!case_ || algs.empty()) {
    return;
  }
  if (QMessageBox::question(
          this, tr("Clear times"),
          tr("Forget every time of the %n selected alg(s), on both cubes and "
             "in every session?",
             nullptr, static_cast<int>(algs.size()))) != QMessageBox::Yes) {
    return;
  }
  for (const std::string &a : algs) {
    store_.clearAttempts(case_->key, a);
  }
  saveStore();
  fillTable();
  test_->refresh();
}

void AlgLabPage::tableMenu(const QPoint &at) {
  if (!table_->itemAt(at)) {
    return;
  }
  QMenu menu(this);
  menu.addAction(tr("Add to session"), this, &AlgLabPage::addToSession);
  if (case_ && !case_->key.isLab()) {
    menu.addAction(tr("Add to set"), this, [this] { addToSet(false); });
    menu.addAction(tr("Make main alg"), this, [this] { addToSet(true); });
  }
  menu.addAction(tr("Copy"), this, [this] {
    QStringList lines;
    for (const auto &a : selectedAlgs()) {
      lines << qs(a);
    }
    QApplication::clipboard()->setText(lines.join("\n"));
  });
  menu.addSeparator();
  menu.addAction(tr("Remove"), this, &AlgLabPage::removeCandidates);
  menu.addAction(tr("Clear times…"), this, &AlgLabPage::clearTimes);
  menu.exec(table_->viewport()->mapToGlobal(at));
}

// --- generator --------------------------------------------------------------

SearchOptions AlgLabPage::searchOptions() const {
  SearchOptions o;
  o.moves = genSet_;
  o.maxLength = maxLength_->value();
  o.extra = lengthMode_->currentIndex() == 0 ? extra_->value() : -1;
  o.maxResults = maxResults_->value();
  return o;
}

void AlgLabPage::updateGenControls() {
  bool running = job_ != nullptr;
  runButton_->setEnabled(!running && case_.has_value());
  batchButton_->setEnabled(!running && case_ && !case_->key.isLab());
  stopButton_->setEnabled(running);
  extra_->setEnabled(lengthMode_->currentIndex() == 0);
  genBox_->setEnabled(!running);
}

void AlgLabPage::runSearch(std::vector<ResolvedCase> cases) {
  if (job_ || cases.empty()) {
    return;
  }
  auto job = std::make_shared<Job>();
  job->cases = static_cast<int>(cases.size());
  job_ = job;
  updateGenControls();
  SearchOptions options = searchOptions();
  QString moves = qs(options.moves.display());
  genStatus_->setText(tr("Starting…"));
  jobFuture_ = QtConcurrent::run([this, job, cases, options, moves] {
    int done = 0;
    int unsolvable = 0;
    QString last;
    for (const ResolvedCase &c : cases) {
      if (job->stop) {
        break;
      }
      ++done;
      SearchGoal goal{c.state, c.free, c.aufBefore, c.aufAfter};
      QString prefix =
          cases.size() > 1
              ? tr("%1 of %2, %3: ").arg(done).arg(cases.size()).arg(c.name)
              : QString();
      SearchCallbacks cb;
      LabCaseKey k = c.key;
      cb.result = [this, job, k](const SearchResult &r) {
        std::string text = r.text;
        QMetaObject::invokeMethod(
            this,
            [this, job, k, text] {
              if (store_.addCandidate(
                      k, {text, CandidateSource::Generated, unixNow()})) {
                ++job->found;
                job->dirty = true;
                if (case_ && case_->key == k && !refreshTimer_.isActive()) {
                  refreshTimer_.start();
                }
              }
            },
            Qt::QueuedConnection);
      };
      cb.progress = [this, prefix](const std::string &m) {
        QString text = prefix + qs(m);
        QMetaObject::invokeMethod(
            this, [this, text] { genStatus_->setText(text); },
            Qt::QueuedConnection);
      };
      SearchOutcome out = searchAlgs(goal, options, job->stop, cb);
      if (out.end == SearchEnd::Unsolvable) {
        ++unsolvable;
        last = tr("%1 can't solve %2.").arg(moves, c.name);
      } else if (out.found == 0) {
        last = tr("No alg of %1 up to %2 moves.").arg(moves).arg(out.deepest);
      } else {
        last =
            tr("%n alg(s), shortest %1 moves, every alg up to %2 moves "
               "found%3.",
               nullptr, out.found)
                .arg(out.shortest)
                .arg(out.deepest)
                .arg(out.end == SearchEnd::Capped ? tr(" (stopped at the cap)")
                     : out.end == SearchEnd::Stopped ? tr(" (stopped)")
                                                     : QString());
      }
    }
    QString summary =
        cases.size() > 1
            ? tr("Searched %1 of %2 cases with %3.%4")
                  .arg(done)
                  .arg(cases.size())
                  .arg(moves)
                  .arg(unsolvable ? tr(" %n can't be solved with it.", nullptr,
                                       unsolvable)
                                  : QString())
            : last;
    QMetaObject::invokeMethod(
        this,
        [this, summary] {
          genStatus_->setText(summary);
          searchFinished();
        },
        Qt::QueuedConnection);
  });
}

void AlgLabPage::runBatch(bool wholeSet) {
  if (!case_ || case_->key.isLab()) {
    return;
  }
  const AlgSet *set = library_.find(case_->key.setId);
  const CaseGroup *group = set ? set->groupOf(case_->key.caseId) : nullptr;
  if (!group) {
    return;
  }
  std::vector<ResolvedCase> cases;
  for (const CaseGroup &g : set->groups) {
    if (!wholeSet && &g != group) {
      continue;
    }
    for (const AlgCase &c : g.cases) {
      if (auto r = resolveLabCase(library_, store_, {set->id, c.id})) {
        cases.push_back(*r);
      }
    }
  }
  if (QMessageBox::question(
          this, tr("Find algs"),
          tr("Search %n case(s) with %1, keeping at most %2 algs each? It "
             "runs in the background; Stop ends it.",
             nullptr, static_cast<int>(cases.size()))
              .arg(qs(genSet_.display()))
              .arg(maxResults_->value())) != QMessageBox::Yes) {
    return;
  }
  runSearch(std::move(cases));
}

void AlgLabPage::stopSearch() {
  if (job_) {
    job_->stop = true;
    genStatus_->setText(tr("Stopping…"));
  }
}

void AlgLabPage::searchFinished() {
  jobFuture_.waitForFinished();
  bool dirty = job_ && job_->dirty;
  int found = job_ ? job_->found : 0;
  job_.reset();
  if (dirty) {
    saveStore();
  }
  updateGenControls();
  fillTable();
  if (found > 0) {
    emit message(tr("The generator added %n alg(s).", nullptr, found));
  }
}

} // namespace vcube
