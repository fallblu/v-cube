#include "app/trainer_page.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>

namespace vcube {

namespace {

constexpr int kCaseIdRole = Qt::UserRole;
constexpr int kThumb = 38;

const std::array<const char *, 6> kColourNames = {"White",  "Red",    "Green",
                                                  "Yellow", "Orange", "Blue"};

QPushButton *button(const QString &text, QWidget *parent) {
  auto *b = new QPushButton(text, parent);
  b->setFocusPolicy(Qt::NoFocus);
  return b;
}

QComboBox *combo(QWidget *parent) {
  auto *c = new QComboBox(parent);
  c->setFocusPolicy(Qt::NoFocus);
  return c;
}

QString dueText(const CaseProgress *p, std::int64_t now) {
  if (!p || p->card.state == CardState::New) {
    return "-";
  }
  std::int64_t d = p->card.due - now;
  if (d <= 0) {
    return QObject::tr("now");
  }
  if (d < 3600) {
    return QObject::tr("%1 min").arg((d + 59) / 60);
  }
  if (d < 86400) {
    return QObject::tr("%1 h").arg((d + 1799) / 3600);
  }
  return QObject::tr("%1 d").arg((d + 43199) / 86400);
}

QString seconds(std::optional<double> ms) {
  return ms ? QString::number(*ms / 1000.0, 'f', 2) : QString("-");
}

const char *gradeName(Grade g) {
  switch (g) {
  case Grade::Again:
    return "Again";
  case Grade::Hard:
    return "Hard";
  case Grade::Good:
    return "Good";
  case Grade::Easy:
    return "Easy";
  }
  return "";
}

} // namespace

TrainerPage::TrainerPage(AppSettings &settings, AlgLibrary &library,
                         QWidget *parent)
    : QWidget(parent), settings_(settings), library_(library), rng_(makeRng()) {
  // --- left: set, picking, cases ---
  auto *left = new QWidget(this);
  auto *leftLayout = new QVBoxLayout(left);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  auto *form = new QFormLayout;
  setBox_ = combo(left);
  modeBox_ = combo(left);
  modeBox_->addItem(tr("Random"), static_cast<int>(PickMode::Random));
  modeBox_->addItem(tr("Weighted (slow and failed cases more often)"),
                    static_cast<int>(PickMode::Weighted));
  modeBox_->addItem(tr("Recap (each case once)"),
                    static_cast<int>(PickMode::Recap));
  modeBox_->addItem(tr("In order"), static_cast<int>(PickMode::InOrder));
  modeBox_->addItem(tr("Spaced repetition"), static_cast<int>(PickMode::Srs));
  focusBox_ = combo(left);
  focusBox_->addItem(tr("All ticked cases"));
  focusBox_->addItem(tr("Ticked cases I'm learning"));
  focusBox_->addItem(tr("Ticked cases not learned yet"));
  focusBox_->addItem(tr("Ticked cases already learned"));
  form->addRow(tr("Alg set:"), setBox_);
  form->addRow(tr("Order:"), modeBox_);
  form->addRow(tr("Drill:"), focusBox_);
  leftLayout->addLayout(form);

  tree_ = new QTreeWidget(left);
  tree_->setColumnCount(4);
  tree_->setHeaderLabels({tr("Case"), tr("Status"), tr("Avg"), tr("Due")});
  tree_->setIconSize(QSize(kThumb, kThumb));
  tree_->setIndentation(14);
  tree_->setFocusPolicy(Qt::NoFocus);
  tree_->setContextMenuPolicy(Qt::CustomContextMenu);
  tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  for (int c = 1; c < 4; ++c) {
    tree_->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
  }
  leftLayout->addWidget(tree_, 1);

  auto *selectRow = new QHBoxLayout;
  auto *allButton = button(tr("Tick all"), left);
  auto *noneButton = button(tr("Tick none"), left);
  learnCount_ = new QSpinBox(left);
  learnCount_->setRange(1, 50);
  learnCount_->setValue(3);
  learnCount_->setFocusPolicy(Qt::ClickFocus);
  auto *learnButton = button(tr("Start learning"), left);
  learnButton->setToolTip(tr(
      "Put the next N ticked cases you haven't started into your learn queue"));
  selectRow->addWidget(allButton);
  selectRow->addWidget(noneButton);
  selectRow->addStretch();
  selectRow->addWidget(learnCount_);
  selectRow->addWidget(learnButton);
  leftLayout->addLayout(selectRow);
  summary_ = new QLabel(left);
  summary_->setWordWrap(true);
  leftLayout->addWidget(summary_);

  // --- right: the drill ---
  auto *right = new QWidget(this);
  auto *rightLayout = new QVBoxLayout(right);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  auto *options = new QHBoxLayout;
  virtualBox_ = new QCheckBox(tr("Virtual cube"), right);
  virtualBox_->setFocusPolicy(Qt::NoFocus);
  showScrambleBox_ = new QCheckBox(tr("Show scramble"), right);
  showScrambleBox_->setFocusPolicy(Qt::NoFocus);
  topBox_ = combo(right);
  frontBox_ = combo(right);
  for (int i = 0; i < 6; ++i) {
    topBox_->addItem(tr(kColourNames[static_cast<std::size_t>(i)]), i);
    frontBox_->addItem(tr(kColourNames[static_cast<std::size_t>(i)]), i);
  }
  neutralBox_ = combo(right);
  neutralBox_->addItem(tr("Fixed"));
  neutralBox_->addItem(tr("Random front"));
  neutralBox_->addItem(tr("Random top and front"));
  options->addWidget(virtualBox_);
  options->addWidget(showScrambleBox_);
  options->addStretch();
  options->addWidget(new QLabel(tr("Top:"), right));
  options->addWidget(topBox_);
  options->addWidget(new QLabel(tr("Front:"), right));
  options->addWidget(frontBox_);
  options->addWidget(neutralBox_);
  rightLayout->addLayout(options);

  hintLabel_ = new QLabel(right);
  hintLabel_->setTextFormat(Qt::RichText);
  hintLabel_->setAlignment(Qt::AlignCenter);
  rightLayout->addWidget(hintLabel_);
  scrambleLabel_ = new QLabel(right);
  scrambleLabel_->setWordWrap(true);
  scrambleLabel_->setAlignment(Qt::AlignCenter);
  scrambleLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  QFont sf = scrambleLabel_->font();
  sf.setPointSize(17);
  scrambleLabel_->setFont(sf);
  scrambleLabel_->setMinimumHeight(56);
  rightLayout->addWidget(scrambleLabel_);

  auto *middle = new QHBoxLayout;
  auto *timeColumn = new QVBoxLayout;
  display_ = new TimeDisplay(right);
  statusLabel_ = new QLabel(right);
  statusLabel_->setAlignment(Qt::AlignCenter);
  statusLabel_->setWordWrap(true);
  timeColumn->addStretch();
  timeColumn->addWidget(display_);
  timeColumn->addWidget(statusLabel_);
  resultRow_ = new QWidget(right);
  auto *resultLayout = new QHBoxLayout(resultRow_);
  resultLayout->setContentsMargins(0, 0, 0, 0);
  resultLabel_ = new QLabel(resultRow_);
  wrongButton_ = button(tr("Wrong alg (Backspace)"), resultRow_);
  wrongButton_->setCheckable(true);
  resultLayout->addStretch();
  resultLayout->addWidget(resultLabel_);
  resultLayout->addWidget(wrongButton_);
  for (int g = 0; g < 4; ++g) {
    auto grade = static_cast<Grade>(g + 1);
    auto *b = button(QString("%1 %2").arg(g + 1).arg(tr(gradeName(grade))),
                     resultRow_);
    b->setCheckable(true);
    b->setToolTip(tr("Override the grade (key %1)").arg(g + 1));
    gradeButtons_[static_cast<std::size_t>(g)] = b;
    resultLayout->addWidget(b);
    connect(b, &QPushButton::clicked, this,
            [this, grade] { regrade(grade, std::nullopt); });
  }
  resultLayout->addStretch();
  timeColumn->addWidget(resultRow_);
  timeColumn->addStretch();
  middle->addLayout(timeColumn, 3);

  auto *cubeColumn = new QVBoxLayout;
  cubeView_ = new CubeView(right);
  cubeView_->setMinimumWidth(260);
  diagram_ = new QLabel(right);
  diagram_->setAlignment(Qt::AlignCenter);
  diagram_->setMinimumSize(200, 200);
  cubeColumn->addWidget(cubeView_);
  cubeColumn->addWidget(diagram_);
  middle->addLayout(cubeColumn, 2);
  rightLayout->addLayout(middle, 3);

  reveal_ = new QLabel(right);
  reveal_->setTextFormat(Qt::RichText);
  reveal_->setWordWrap(true);
  reveal_->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
  reveal_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  QFont rf = reveal_->font();
  rf.setPointSize(rf.pointSize() + 3);
  reveal_->setFont(rf);
  reveal_->setMinimumHeight(110);
  rightLayout->addWidget(reveal_, 1);

  auto *split = new QSplitter(this);
  split->addWidget(left);
  split->addWidget(right);
  split->setStretchFactor(1, 1);
  left->setMinimumWidth(380);
  split->setSizes({470, 730});
  auto *layout = new QHBoxLayout(this);
  layout->addWidget(split);

  // --- wiring ---
  tick_.start(16);
  connect(&tick_, &QTimer::timeout, this, &TrainerPage::onTick);
  connect(&watcher_, &QFutureWatcher<GeneratedScramble>::finished, this,
          &TrainerPage::scrambleReady);
  connect(setBox_, &QComboBox::activated, this,
          [this](int i) { selectSet(ss(setBox_->itemData(i).toString())); });
  connect(modeBox_, &QComboBox::activated, this, [this] {
    picker_.setMode(static_cast<PickMode>(modeBox_->currentData().toInt()));
    QSettings().setValue(setKey("mode"), modeBox_->currentData().toInt());
    updateSummary();
    nextCase();
  });
  connect(focusBox_, &QComboBox::activated, this, [this] {
    QSettings().setValue(setKey("focus"), focusBox_->currentIndex());
    updateSummary();
    nextCase();
  });
  connect(tree_, &QTreeWidget::itemChanged, this,
          [this](QTreeWidgetItem *, int column) {
            if (column == 0) {
              saveSelection();
              updateSummary();
            }
          });
  connect(tree_, &QTreeWidget::customContextMenuRequested, this,
          &TrainerPage::showCaseMenu);
  connect(allButton, &QPushButton::clicked, this, [this] {
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
      tree_->topLevelItem(i)->setCheckState(0, Qt::Checked);
    }
  });
  connect(noneButton, &QPushButton::clicked, this, [this] {
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
      tree_->topLevelItem(i)->setCheckState(0, Qt::Unchecked);
    }
  });
  connect(learnButton, &QPushButton::clicked, this, &TrainerPage::learnNext);
  connect(wrongButton_, &QPushButton::clicked, this,
          [this](bool on) { regrade(std::nullopt, on); });
  connect(virtualBox_, &QCheckBox::toggled, this, [this](bool on) {
    QSettings().setValue("trainer/virtual", on);
    if (drill_) {
      retryCase();
    }
    updateStatus();
  });
  connect(showScrambleBox_, &QCheckBox::toggled, this, [this](bool on) {
    QSettings().setValue("trainer/showScramble", on);
    updateStatus();
  });
  for (QComboBox *c : {topBox_, frontBox_, neutralBox_}) {
    connect(c, &QComboBox::activated, this, [this] {
      saveOrientation();
      buildTree();
      retryCase();
    });
  }

  QSettings q;
  virtualBox_->setChecked(q.value("trainer/virtual", false).toBool());
  showScrambleBox_->setChecked(q.value("trainer/showScramble", true).toBool());
  applySettings();
  populateSets();
  QString lastSet = q.value("trainer/set").toString();
  selectSet(lastSet.isEmpty() && !library_.sets().empty()
                ? library_.sets().front().id
                : ss(lastSet));
}

// --- set and case list ---------------------------------------------------

AlgSet *TrainerPage::set() { return library_.find(setId_); }

SetProgress &TrainerPage::progress() { return library_.progress(setId_); }

QString TrainerPage::setKey(const QString &name) const {
  return QString("trainer/sets/%1/%2").arg(qs(setId_), name);
}

bool TrainerPage::isVirtual() const { return virtualBox_->isChecked(); }

void TrainerPage::applySettings() {
  timer_.setConfig({settings_.holdMs, false, 15000});
  display_->setDecimals(settings_.decimals);
  CubeLook look = cubeView_->look();
  look.scheme = settings_.scheme;
  cubeView_->setLook(look);
  cubeView_->setAnimationMs(settings_.animationMs);
  for (int i = 0; i < 6; ++i) {
    QPixmap swatch(14, 14);
    swatch.fill(settings_.scheme.colourOfSlot(i));
    topBox_->setItemIcon(i, swatch);
    frontBox_->setItemIcon(i, swatch);
  }
  if (set()) {
    buildTree();
    if (drill_) {
      showReveal();
    }
  }
}

void TrainerPage::populateSets() {
  QSignalBlocker block(setBox_);
  setBox_->clear();
  for (const auto &s : library_.sets()) {
    setBox_->addItem(QString("%1 (%2)").arg(qs(s.name)).arg(s.caseCount()),
                     qs(s.id));
  }
}

void TrainerPage::reloadSets() {
  populateSets();
  std::string id = setId_;
  if (!library_.find(id)) {
    id = library_.sets().empty() ? std::string() : library_.sets().front().id;
  }
  bool same = id == setId_;
  if (same && set()) {
    int i = setBox_->findData(qs(id));
    setBox_->setCurrentIndex(i);
    buildTree();
    if (drill_ && !set()->findCase(drill_->caseId)) {
      drill_.reset();
      nextCase();
    } else if (drill_ && phase_ == Phase::Revealed) {
      showReveal();
    }
    updateSummary();
  } else {
    selectSet(id);
  }
}

void TrainerPage::selectSet(const std::string &id) {
  setId_ = id;
  if (!library_.find(id) && !library_.sets().empty()) {
    setId_ = library_.sets().front().id;
  }
  QSettings q;
  q.setValue("trainer/set", qs(setId_));
  setBox_->setCurrentIndex(setBox_->findData(qs(setId_)));
  int mode =
      q.value(setKey("mode"), static_cast<int>(PickMode::Random)).toInt();
  modeBox_->setCurrentIndex(std::max(0, modeBox_->findData(mode)));
  picker_.setMode(static_cast<PickMode>(modeBox_->currentData().toInt()));
  focusBox_->setCurrentIndex(q.value(setKey("focus"), 0).toInt());
  loadOrientation();
  drill_.reset();
  buildTree();
  nextCase();
}

void TrainerPage::buildTree() {
  QSignalBlocker block(tree_);
  tree_->clear();
  AlgSet *s = set();
  if (!s) {
    updateSummary();
    return;
  }
  QSettings q;
  bool hasSelection = q.contains(setKey("selection"));
  QStringList selected = q.value(setKey("selection")).toStringList();
  CubeLook look;
  look.scheme = settings_.scheme;
  look.map = baseColours();
  qreal dpr = devicePixelRatioF();
  for (const auto &g : s->groups) {
    auto *groupItem = new QTreeWidgetItem(tree_, {qs(g.name)});
    groupItem->setFlags(groupItem->flags() | Qt::ItemIsUserCheckable |
                        Qt::ItemIsAutoTristate);
    const StepSpec &spec = s->specFor(g);
    for (const auto &c : g.cases) {
      auto *item = new QTreeWidgetItem(groupItem, {qs(c.name)});
      item->setData(0, kCaseIdRole, qs(c.id));
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      bool on = !hasSelection || selected.contains(qs(c.id));
      item->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
      FaceletCube masked = maskedFacelets(c.state, spec.mask);
      item->setIcon(0, QIcon(cube_painter::diagram(masked, spec.view, kThumb,
                                                   look, dpr)));
      QString tip = c.algs.empty() ? tr("No algs yet") : qs(c.algs.front());
      item->setToolTip(0, tip);
      updateCaseRow(item);
    }
    groupItem->setExpanded(true);
  }
  updateSummary();
}

void TrainerPage::updateCaseRow(QTreeWidgetItem *item) {
  std::string id = ss(item->data(0, kCaseIdRole).toString());
  const SetProgress &p = progress();
  auto it = p.find(id);
  const CaseProgress *cp = it == p.end() ? nullptr : &it->second;
  CaseStatus status = statusOf(cp);
  static const QColor learning(0xff, 0xb0, 0x30), learned(0x3d, 0xd6, 0x6b);
  switch (status) {
  case CaseStatus::Unstarted:
    item->setText(1, tr("new"));
    item->setForeground(1, palette().color(QPalette::PlaceholderText));
    break;
  case CaseStatus::Learning:
    item->setText(1, tr("learning"));
    item->setForeground(1, learning);
    break;
  case CaseStatus::Learned:
    item->setText(1, tr("learned"));
    item->setForeground(1, learned);
    break;
  }
  item->setText(2, cp ? seconds(cp->recentMean()) : "-");
  std::optional<double> best;
  if (cp && cp->best()) {
    best = static_cast<double>(*cp->best());
  }
  item->setToolTip(
      2, tr("Mean of the last 5 good attempts; best %1 s; %2 attempts")
             .arg(seconds(best))
             .arg(cp ? cp->attempts.size() : 0));
  item->setText(3, dueText(cp, unixNow()));
}

void TrainerPage::updateAllRows() {
  QSignalBlocker block(tree_);
  for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
    auto *group = tree_->topLevelItem(g);
    for (int i = 0; i < group->childCount(); ++i) {
      updateCaseRow(group->child(i));
    }
  }
  updateSummary();
}

QTreeWidgetItem *TrainerPage::itemFor(const std::string &caseId) const {
  for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
    auto *group = tree_->topLevelItem(g);
    for (int i = 0; i < group->childCount(); ++i) {
      if (ss(group->child(i)->data(0, kCaseIdRole).toString()) == caseId) {
        return group->child(i);
      }
    }
  }
  return nullptr;
}

std::vector<std::string> TrainerPage::selectedCases() const {
  std::vector<std::string> out;
  for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
    auto *group = tree_->topLevelItem(g);
    for (int i = 0; i < group->childCount(); ++i) {
      if (group->child(i)->checkState(0) == Qt::Checked) {
        out.push_back(ss(group->child(i)->data(0, kCaseIdRole).toString()));
      }
    }
  }
  return out;
}

std::vector<std::string> TrainerPage::pool() const {
  auto &p = const_cast<TrainerPage *>(this)->progress();
  int focus = focusBox_->currentIndex();
  std::vector<std::string> out;
  for (const auto &id : selectedCases()) {
    auto it = p.find(id);
    CaseStatus st = statusOf(it == p.end() ? nullptr : &it->second);
    bool keep = focus == 0 || (focus == 1 && st == CaseStatus::Learning) ||
                (focus == 2 && st != CaseStatus::Learned) ||
                (focus == 3 && st == CaseStatus::Learned);
    if (keep) {
      out.push_back(id);
    }
  }
  return out;
}

void TrainerPage::saveSelection() {
  QStringList ids;
  for (const auto &id : selectedCases()) {
    ids << qs(id);
  }
  QSettings().setValue(setKey("selection"), ids);
}

void TrainerPage::updateSummary() {
  AlgSet *s = set();
  if (!s) {
    summary_->setText(tr("No alg sets. Create one in the Library tab."));
    return;
  }
  auto selected = selectedCases();
  auto drillPool = pool();
  int learning = 0, learned = 0, due = 0, withAlgs = 0;
  std::int64_t now = unixNow();
  const SetProgress &p = progress();
  for (const auto &g : s->groups) {
    for (const auto &c : g.cases) {
      withAlgs += c.algs.empty() ? 0 : 1;
      auto it = p.find(c.id);
      const CaseProgress *cp = it == p.end() ? nullptr : &it->second;
      CaseStatus st = statusOf(cp);
      learning += st == CaseStatus::Learning;
      learned += st == CaseStatus::Learned;
      due += cp && isDue(cp->card, now, settings_.srs);
    }
  }
  summary_->setText(
      tr("%1 of %2 ticked, %3 to drill. Learning %4, learned %5, due %6. "
         "%7 cases have algs.")
          .arg(selected.size())
          .arg(s->caseCount())
          .arg(drillPool.size())
          .arg(learning)
          .arg(learned)
          .arg(due)
          .arg(withAlgs));
}

void TrainerPage::showCaseMenu(const QPoint &pos) {
  QTreeWidgetItem *item = tree_->itemAt(pos);
  if (!item || item->data(0, kCaseIdRole).isNull()) {
    return;
  }
  std::string id = ss(item->data(0, kCaseIdRole).toString());
  QMenu menu(this);
  auto *drill = menu.addAction(tr("Drill this case now"));
  menu.addSeparator();
  auto *learn = menu.addAction(tr("Start learning"));
  auto *known = menu.addAction(tr("Mark as learned"));
  auto *reset = menu.addAction(tr("Reset progress"));
  menu.addSeparator();
  auto *edit = menu.addAction(tr("Edit algs in Library…"));
  auto *copy = menu.addAction(tr("Copy main alg"));
  const AlgCase *c = set() ? set()->findCase(id) : nullptr;
  copy->setEnabled(c && !c->algs.empty());
  QAction *chosen = menu.exec(tree_->viewport()->mapToGlobal(pos));
  if (!chosen) {
    return;
  }
  std::int64_t now = unixNow();
  if (chosen == drill) {
    if (AlgSet *s = set(); s && s->findCase(id)) {
      drill_ = Drill{id, {}, drillColours(), {}, false};
      retryCase();
    }
    return;
  }
  if (chosen == edit) {
    emit editCaseRequested(qs(setId_), qs(id));
    return;
  }
  if (chosen == copy) {
    QApplication::clipboard()->setText(qs(c->algs.front()));
    return;
  }
  SrsCard &card = progress()[id].card;
  if (chosen == learn) {
    startLearning(card, now, settings_.srs);
  } else if (chosen == known) {
    markLearned(card, now, settings_.srs);
  } else if (chosen == reset) {
    progress()[id] = CaseProgress{};
  }
  saveProgress();
  updateCaseRow(item);
  updateSummary();
}

void TrainerPage::learnNext() {
  int n = learnCount_->value();
  std::int64_t now = unixNow();
  int started = 0;
  for (const auto &id : selectedCases()) {
    if (started >= n) {
      break;
    }
    auto &p = progress();
    auto it = p.find(id);
    if (statusOf(it == p.end() ? nullptr : &it->second) ==
        CaseStatus::Unstarted) {
      startLearning(p[id].card, now, settings_.srs);
      ++started;
    }
  }
  saveProgress();
  updateAllRows();
  emit message(started ? tr("Added %1 cases to your learn queue").arg(started)
                       : tr("No unstarted ticked cases left"));
}

// --- orientation ---------------------------------------------------------

void TrainerPage::loadOrientation() {
  QSettings q;
  topBox_->setCurrentIndex(q.value(setKey("top"), 0).toInt());
  frontBox_->setCurrentIndex(q.value(setKey("front"), 2).toInt());
  neutralBox_->setCurrentIndex(q.value(setKey("neutrality"), 0).toInt());
}

void TrainerPage::saveOrientation() {
  if (!colourMapFor(topBox_->currentIndex(), frontBox_->currentIndex())) {
    // Keep the pair valid: pick the first front that fits the top.
    for (int f = 0; f < 6; ++f) {
      if (colourMapFor(topBox_->currentIndex(), f)) {
        frontBox_->setCurrentIndex(f);
        break;
      }
    }
  }
  QSettings q;
  q.setValue(setKey("top"), topBox_->currentIndex());
  q.setValue(setKey("front"), frontBox_->currentIndex());
  q.setValue(setKey("neutrality"), neutralBox_->currentIndex());
}

ColourMap TrainerPage::baseColours() const {
  return colourMapFor(topBox_->currentIndex(), frontBox_->currentIndex())
      .value_or(kIdentityColourMap);
}

ColourMap TrainerPage::drillColours() {
  ColourMap base = baseColours();
  int mode = neutralBox_->currentIndex();
  if (mode == 0) {
    return base;
  }
  std::vector<ColourMap> options;
  for (int top = 0; top < 6; ++top) {
    for (int front = 0; front < 6; ++front) {
      auto m = colourMapFor(top, front);
      if (m && (mode == 2 || top == base[0])) {
        options.push_back(*m);
      }
    }
  }
  std::uniform_int_distribution<std::size_t> pick(0, options.size() - 1);
  return options[pick(rng_)];
}

// --- drilling --------------------------------------------------------------

void TrainerPage::setPhase(Phase p) {
  phase_ = p;
  updateStatus();
}

void TrainerPage::nextCase() {
  timer_.cancel();
  statusLabel_->clear();
  AlgSet *s = set();
  hintLabel_->clear();
  reveal_->clear();
  diagram_->clear();
  resultRow_->setVisible(false);
  if (!s) {
    drill_.reset();
    setPhase(Phase::NoCase);
    return;
  }
  picker_.setPool(pool());
  PickResult r =
      picker_.next(progress(), unixNow(), localDayStart(), settings_.srs, rng_);
  if (!r.caseId) {
    drill_.reset();
    setPhase(Phase::NoCase);
    if (picker_.mode() == PickMode::Srs && !picker_.pool().empty()) {
      QString next =
          r.nextDue
              ? QDateTime::fromSecsSinceEpoch(*r.nextDue).toString("ddd hh:mm")
              : tr("when you start learning more cases");
      statusLabel_->setText(
          tr("All caught up! Next review: %1. Raise the daily new-case "
             "limit in Settings, or switch the order to keep practising.")
              .arg(next));
    }
    return;
  }
  if (r.roundComplete) {
    emit message(tr("Recap round complete: every case drilled once. Starting "
                    "another round."));
  }
  drill_ = Drill{*r.caseId, {}, drillColours(), {}, r.isNew};
  retryCase();
}

void TrainerPage::retryCase() {
  timer_.cancel();
  AlgSet *s = set();
  if (!drill_ || !s) {
    nextCase();
    return;
  }
  const AlgCase *c = s->findCase(drill_->caseId);
  const CaseGroup *g = s->groupOf(drill_->caseId);
  if (!c || !g) {
    drill_.reset();
    setPhase(Phase::NoCase);
    return;
  }
  drill_->state = drillState(*s, *g, *c, rng_);
  drill_->scramble.clear();
  hinted_ = false;
  hintLabel_->setText(orientationHint(drill_->colours, settings_.scheme));
  cubeView_->setColourMap(drill_->colours);
  reveal_->clear();
  diagram_->clear();
  resultRow_->setVisible(false);
  display_->showTime(0);
  if (drill_->isNew) {
    // A case being introduced: show its algs before the first try.
    showReveal();
    reveal_->setText(tr("<p><b>New case</b>: learn it, then solve it.</p>") +
                     reveal_->text());
  }
  if (auto *item = itemFor(drill_->caseId)) {
    tree_->setCurrentItem(item);
    tree_->scrollToItem(item);
  }
  startVirtualCase();
  requestScramble();
  setPhase(Phase::Waiting);
}

void TrainerPage::startVirtualCase() {
  if (!isVirtual() || !drill_) {
    return;
  }
  cube_ = drill_->state.toFacelets();
  cubeView_->setState(cube_);
  timer_.arm(monotonicMs());
}

void TrainerPage::requestScramble() {
  if (!drill_) {
    return;
  }
  int request = ++scrambleRequest_;
  CubieCube state = drill_->state;
  // Results of superseded requests are dropped in scrambleReady().
  watcher_.setProperty("request", request);
  watcher_.setFuture(generateScramble([state](Rng &) { return state; }));
}

void TrainerPage::scrambleReady() {
  if (watcher_.property("request").toInt() != scrambleRequest_ || !drill_) {
    if (drill_ && drill_->scramble.isEmpty()) {
      requestScramble();
    }
    return;
  }
  drill_->scramble = watcher_.result().text;
  updateStatus();
}

void TrainerPage::updateStatus() {
  bool virt = isVirtual();
  cubeView_->setVisible(virt);
  diagram_->setVisible(!virt && phase_ == Phase::Revealed);
  if (!drill_) {
    scrambleLabel_->clear();
    if (phase_ == Phase::NoCase && statusLabel_->text().isEmpty()) {
      statusLabel_->setText(
          set() ? tr("Tick some cases to drill (and check the Drill filter).")
                : tr("No alg set selected."));
    }
    return;
  }
  bool showScramble = !virt || showScrambleBox_->isChecked();
  if (!showScramble) {
    scrambleLabel_->setText(" ");
  } else if (drill_->scramble.isEmpty()) {
    scrambleLabel_->setText(tr("Generating scramble…"));
  } else {
    scrambleLabel_->setText(drill_->scramble);
  }
  QString status;
  switch (phase_) {
  case Phase::NoCase:
    break;
  case Phase::Waiting:
    status = virt ? tr("Solve the case. The first turn starts the timer. "
                       "Space gives up, Tab shows the algs, Esc resets.")
                  : tr("Scramble, then hold Space and release to start. Tab "
                       "shows the algs.");
    break;
  case Phase::Running:
    status = virt ? tr("Space gives up") : tr("Any key stops the timer");
    break;
  case Phase::Revealed:
    status =
        tr("Space/Enter: next case. Backspace: wrong alg. 1-4: change grade. "
           "Esc: try the case again.");
    break;
  }
  statusLabel_->setText(status);
}

void TrainerPage::onTick() {
  std::int64_t now = monotonicMs();
  timer_.tick(now);
  auto st = timer_.state();
  if (st == SolveTimer::State::Holding || st == SolveTimer::State::Running ||
      (st == SolveTimer::State::Ready && !isVirtual())) {
    display_->showTimer(timer_, now);
  }
}

bool TrainerPage::handleKey(QKeyEvent *e, bool press) {
  if (e->isAutoRepeat()) {
    return e->key() == Qt::Key_Space;
  }
  std::int64_t now = monotonicMs();
  int key = e->key();
  using S = SolveTimer::State;

  if (phase_ == Phase::NoCase) {
    if (press && (key == Qt::Key_Space || key == Qt::Key_Return ||
                  key == Qt::Key_Enter)) {
      nextCase();
      return true;
    }
    return false;
  }

  if (phase_ == Phase::Revealed) {
    if (!press) {
      return key == Qt::Key_Space;
    }
    if (key == Qt::Key_Space || key == Qt::Key_Return || key == Qt::Key_Enter) {
      nextCase();
      return true;
    }
    if (key == Qt::Key_Backspace) {
      regrade(std::nullopt, !failed_);
      return true;
    }
    if (key >= Qt::Key_1 && key <= Qt::Key_4) {
      regrade(static_cast<Grade>(key - Qt::Key_1 + 1), std::nullopt);
      return true;
    }
    if (key == Qt::Key_Escape) {
      if (isVirtual()) {
        cube_ = drill_->state.toFacelets();
        cubeView_->setState(cube_);
      }
      return true;
    }
    if (isVirtual()) {
      if (auto m = settings_.keymap.lookup(e)) {
        cube_.apply(*m); // untimed practice
        cubeView_->animateMove(*m);
        return true;
      }
    }
    return false;
  }

  // Waiting or Running.
  if (press && key == Qt::Key_Tab && phase_ == Phase::Waiting) {
    hinted_ = true;
    showReveal();
    return true;
  }
  if (isVirtual()) {
    if (!press) {
      return false;
    }
    if (key == Qt::Key_Space) {
      std::int64_t t = phase_ == Phase::Running ? timer_.elapsed(now) : 0;
      timer_.cancel();
      finish(t, true);
      cube_ = drill_->state.toFacelets(); // reset so the alg can be tried
      cubeView_->setState(cube_);
      return true;
    }
    if (key == Qt::Key_Escape) {
      cube_ = drill_->state.toFacelets();
      cubeView_->setState(cube_);
      timer_.arm(now);
      display_->showTime(0);
      setPhase(Phase::Waiting);
      return true;
    }
    if (auto m = settings_.keymap.lookup(e)) {
      virtualMove(*m);
      return true;
    }
    return false;
  }

  // Physical cube.
  if (phase_ == Phase::Running) {
    if (press) {
      timer_.pressStop(now);
      if (auto r = timer_.takeResult()) {
        finish(r->timeMs, hinted_);
      }
      return true;
    }
    return false;
  }
  if (key == Qt::Key_Escape && press) {
    timer_.cancel();
    display_->showTime(0);
    return true;
  }
  if (key != Qt::Key_Space) {
    return false;
  }
  if (press) {
    if (drill_->scramble.isEmpty()) {
      return true; // wait for the scramble
    }
    timer_.pressStart(now);
  } else {
    timer_.releaseStart(now);
    if (timer_.state() == S::Running) {
      setPhase(Phase::Running);
    }
  }
  display_->showTimer(timer_, now);
  return true;
}

void TrainerPage::virtualMove(Move m) {
  std::int64_t now = monotonicMs();
  cube_.apply(m);
  cubeView_->animateMove(m);
  if (phase_ == Phase::Waiting && !isRotation(m.type)) {
    timer_.startNow(now);
    setPhase(Phase::Running);
  }
  if (phase_ != Phase::Running) {
    return;
  }
  FaceletCube f = cube_;
  f.normalizeOrientation();
  auto c = CubieCube::fromFacelets(f);
  const AlgSet *s = set();
  const CaseGroup *g = s ? s->groupOf(drill_->caseId) : nullptr;
  if (c && g && stepSolved(*c, s->specFor(*g).free)) {
    timer_.stopNow(now);
    auto r = timer_.takeResult();
    finish(r ? r->timeMs : timer_.elapsed(now), hinted_);
  }
}

void TrainerPage::finish(std::int64_t timeMs, bool failed) {
  AlgSet *s = set();
  if (!drill_ || !s) {
    return;
  }
  attemptMs_ = timeMs;
  failed_ = failed;
  CaseProgress &cp = progress()[drill_->caseId];
  cardBefore_ = cp.card;
  cp.record({unixNow(), timeMs, failed});
  grade_ = gradeFromTime(static_cast<double>(timeMs) / 1000.0, s->targetSeconds,
                         failed);
  review(cp.card, grade_, unixNow(), settings_.srs);
  saveProgress();
  setPhase(Phase::Revealed);
  showReveal();
  showResult();
  if (auto *item = itemFor(drill_->caseId)) {
    QSignalBlocker block(tree_);
    updateCaseRow(item);
  }
  updateSummary();
}

void TrainerPage::regrade(std::optional<Grade> grade,
                          std::optional<bool> failed) {
  AlgSet *s = set();
  if (phase_ != Phase::Revealed || !drill_ || !s) {
    showResult();
    return;
  }
  CaseProgress &cp = progress()[drill_->caseId];
  if (failed) {
    failed_ = *failed;
    if (!cp.attempts.empty()) {
      cp.attempts.back().failed = failed_;
    }
    grade_ = gradeFromTime(static_cast<double>(attemptMs_) / 1000.0,
                           s->targetSeconds, failed_);
  }
  if (grade) {
    grade_ = *grade;
  }
  cp.card = cardBefore_;
  review(cp.card, grade_, unixNow(), settings_.srs);
  saveProgress();
  showResult();
  if (auto *item = itemFor(drill_->caseId)) {
    QSignalBlocker block(tree_);
    updateCaseRow(item);
  }
  updateSummary();
}

void TrainerPage::showResult() {
  if (phase_ != Phase::Revealed || !drill_) {
    resultRow_->setVisible(false);
    return;
  }
  resultRow_->setVisible(true);
  const auto &cp = progress()[drill_->caseId];
  QString next = dueText(&cp, unixNow());
  resultLabel_->setText(tr("%1 → next %2").arg(tr(gradeName(grade_)), next));
  wrongButton_->setChecked(failed_);
  for (int g = 0; g < 4; ++g) {
    gradeButtons_[static_cast<std::size_t>(g)]->setChecked(
        static_cast<int>(grade_) == g + 1);
  }
  if (failed_ && attemptMs_ == 0) {
    display_->showText("DNF");
  } else {
    display_->showTime(attemptMs_, failed_ ? Penalty::Dnf : Penalty::None);
  }
}

void TrainerPage::showReveal() {
  AlgSet *s = set();
  if (!drill_ || !s) {
    return;
  }
  const AlgCase *c = s->findCase(drill_->caseId);
  const CaseGroup *g = s->groupOf(drill_->caseId);
  if (!c || !g) {
    return;
  }
  const StepSpec &spec = s->specFor(*g);
  QString html =
      QString("<p style='font-size:large'><b>%1</b> <span style='color:gray'>· "
              "%2</span></p>")
          .arg(qs(c->name).toHtmlEscaped(), qs(g->name).toHtmlEscaped());
  if (c->algs.empty()) {
    html += tr(
        "<p>No algs for this case yet. Add some in the Library tab (or paste a "
        "list there and they are matched to their cases automatically).</p>");
  }
  for (std::size_t i = 0; i < c->algs.size(); ++i) {
    const std::string &text = c->algs[i];
    ParseResult parsed = parseAlg(text);
    std::optional<AufFit> fit;
    if (parsed) {
      fit = fitAlg(drill_->state, parsed.alg, spec.free);
    }
    QString line = fit ? qs(withAufs(text, *fit)).toHtmlEscaped()
                       : qs(text).toHtmlEscaped();
    if (!fit) {
      line = QString("<span style='color:#e66'>%1 &nbsp;⚠ %2</span>")
                 .arg(line, tr("does not solve this case"));
    } else if (i == 0) {
      line = "<b>" + line + "</b>";
    } else {
      line = "<span style='color:gray'>" + line + "</span>";
    }
    html += "<p>" + line + "</p>";
  }
  reveal_->setText(html);
  // The case diagram (in the drill's colours).
  CubeLook look;
  look.scheme = settings_.scheme;
  look.map = drill_->colours;
  qreal dpr = devicePixelRatioF();
  // Show the case as presented, with the step's mask, after undoing the
  // drill's random free pieces: the canonical case turned by the AUF.
  FaceletCube masked = maskedFacelets(drill_->state, spec.mask);
  diagram_->setPixmap(cube_painter::diagram(masked, spec.view, 200, look, dpr));
  diagram_->setVisible(!isVirtual() && phase_ == Phase::Revealed);
}

void TrainerPage::saveProgress() {
  if (!library_.saveProgress(setId_)) {
    emit message(tr("Could not save progress for %1").arg(qs(setId_)));
  }
}

} // namespace vcube
