#include "app/lab_test_panel.hpp"
#include "app/key_router.hpp"
#include "app/page_parts.hpp"

#include "app/app_settings.hpp"
#include "app/colours.hpp"
#include "app/cube_painter.hpp"
#include "app/lab_chart.hpp"
#include "io/algset_json.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <random>

namespace vcube {

namespace {

constexpr int kDiagramSize = 120;

QTableWidgetItem *item(const QString &text, bool number = false) {
  auto *it = new QTableWidgetItem(text);
  it->setFlags(it->flags() & ~Qt::ItemIsEditable);
  if (number) {
    it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  }
  return it;
}

QTableWidget *table(const QStringList &headers, QWidget *parent) {
  auto *t = new QTableWidget(0, static_cast<int>(headers.size()), parent);
  t->setHorizontalHeaderLabels(headers);
  t->verticalHeader()->hide();
  t->setSelectionBehavior(QAbstractItemView::SelectRows);
  t->setEditTriggers(QAbstractItemView::NoEditTriggers);
  t->horizontalHeader()->setStretchLastSection(false);
  t->setFocusPolicy(Qt::ClickFocus);
  return t;
}

} // namespace

LabTestPanel::LabTestPanel(AppSettings &settings, AlgLibrary &library,
                           AlgLabStore &store, QWidget *parent)
    : QWidget(parent), settings_(settings), library_(library), store_(store),
      rng_(std::random_device{}()) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);

  // Sessions and their options.
  // The rows of options wrap in a narrow window.
  auto *sessionRow = new OptionBar(this);
  sessionBox_ = new QComboBox(this);
  sessionBox_->setMinimumContentsLength(14);
  auto *newButton = new QPushButton(tr("New…"), this);
  auto *renameButton = new QPushButton(tr("Rename…"), this);
  auto *deleteButton = new QPushButton(tr("Delete…"), this);
  sessionRow->addWidget(new QLabel(tr("Session:"), this));
  sessionRow->addWidget(sessionBox_, 1);
  sessionRow->addWidget(newButton);
  sessionRow->addWidget(renameButton);
  sessionRow->addWidget(deleteButton);
  layout->addWidget(sessionRow);

  auto *options = new OptionBar(this);
  cubeBox_ = new QComboBox(this);
  cubeBox_->addItem(tr("Physical cube"));
  cubeBox_->addItem(tr("Virtual cube"));
  orderBox_ = new QComboBox(this);
  orderBox_->addItem(tr("Interleaved"));
  orderBox_->addItem(tr("Blocked"));
  repsBox_ = new QSpinBox(this);
  repsBox_->setRange(1, 1000);
  repsBox_->setSuffix(tr(" reps"));
  warmupBox_ = new QSpinBox(this);
  warmupBox_->setRange(0, 100);
  warmupBox_->setPrefix(tr("warm-up "));
  trimBox_ = new QSpinBox(this);
  trimBox_->setRange(0, 40);
  trimBox_->setPrefix(tr("trim "));
  trimBox_->setSuffix("%");
  randomAufBox_ = new QCheckBox(tr("Random AUF"), this);
  for (QWidget *w :
       std::initializer_list<QWidget *>{cubeBox_, orderBox_, repsBox_,
                                        warmupBox_, trimBox_, randomAufBox_}) {
    options->addWidget(w);
  }
  options->addStretch();
  layout->addWidget(options);

  auto *split = new QSplitter(Qt::Vertical, this);
  split->setObjectName("test");
  auto *top = new QWidget(split);
  auto *topLayout = new QHBoxLayout(top);
  topLayout->setContentsMargins(0, 0, 0, 0);

  // The session's algs.
  auto *entryBox = new QVBoxLayout;
  entries_ = table({tr("Case"), tr("Alg"), tr("Done")}, top);
  entries_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  entries_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  entryBox->addWidget(entries_, 1);
  auto *entryButtons = new QHBoxLayout;
  auto *removeButton = new QPushButton(tr("Remove"), top);
  auto *upButton = new QPushButton(tr("Up"), top);
  auto *downButton = new QPushButton(tr("Down"), top);
  entryButtons->addWidget(removeButton);
  entryButtons->addWidget(upButton);
  entryButtons->addWidget(downButton);
  entryButtons->addStretch();
  entryBox->addLayout(entryButtons);
  topLayout->addLayout(entryBox, 2);

  // The attempt.
  auto *run = new QVBoxLayout;
  caseLabel_ = new QLabel(top);
  caseLabel_->setAlignment(Qt::AlignCenter);
  algLabel_ = new QLabel(top);
  algLabel_->setAlignment(Qt::AlignCenter);
  algLabel_->setWordWrap(true);
  QFont big = algLabel_->font();
  big.setPointSizeF(big.pointSizeF() * 1.5);
  big.setBold(true);
  algLabel_->setFont(big);
  algLabel_->setTextInteractionFlags(Qt::NoTextInteraction);
  diagram_ = new QLabel(top);
  diagram_->setAlignment(Qt::AlignCenter);
  diagram_->setFixedHeight(kDiagramSize);
  scrambleLabel_ = new QLabel(top);
  scrambleLabel_->setAlignment(Qt::AlignCenter);
  scrambleLabel_->setWordWrap(true);
  markScrambleLabel(scrambleLabel_);
  cubeView_ = new CubeView(top);
  // As in the other stages: the cube takes the room the pane has, with
  // the time and the status right under it.
  cubeView_->setFillsArea(true);
  // The view is chosen at the end of the options' row, as in the other
  // stages.
  viewRow_ = options->addViewPicker(cubeView_, "alglab");
  display_ = new TimeDisplay(top);
  display_->setAlignment(Qt::AlignCenter);
  status_ = new HintBar(top);
  // The message in the text colour, as the other status lines are.
  status_->setForegroundRole(QPalette::WindowText);
  run->addWidget(caseLabel_);
  run->addWidget(algLabel_);
  run->addWidget(diagram_);
  run->addWidget(scrambleLabel_);
  run->addWidget(cubeView_, 1);
  run->addWidget(display_);
  run->addWidget(status_);
  topLayout->addLayout(run, 3);
  // Shown for an attempt (see showCurrent()): the cube on the virtual
  // cube, else the case's picture and the scramble.
  for (QWidget *w : std::initializer_list<QWidget *>{
           cubeView_, viewRow_, diagram_, scrambleLabel_}) {
    w->hide();
  }

  // Comparison and times.
  bottom_ = new QTabWidget(split);
  auto *comparePage = new QWidget(bottom_);
  auto *compareLayout = new QVBoxLayout(comparePage);
  auto *compareRow = new QHBoxLayout;
  windowBox_ = new QComboBox(comparePage);
  windowBox_->addItem(tr("This session"));
  windowBox_->addItem(tr("All time"));
  chartBox_ = new QComboBox(comparePage);
  chartBox_->addItem(tr("Spread of times"));
  chartBox_->addItem(tr("Times over attempts"));
  compareRow->addWidget(windowBox_);
  compareRow->addWidget(chartBox_);
  compareRow->addStretch();
  compareLayout->addLayout(compareRow);
  auto *compareSplit = new QSplitter(Qt::Horizontal, comparePage);
  compareSplit->setObjectName("compare");
  compare_ = table({tr("Alg"), tr("Case"), tr("n"), tr("Mean"), tr("Median"),
                    tr("Best"), tr("Worst"), tr("ao5"), tr("ao12"), tr("SD"),
                    tr("DNF %")},
                   compareSplit);
  compare_->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
  compare_->setSortingEnabled(true);
  chart_ = new LabChart(compareSplit);
  chart_->setEmptyText(tr("Time the session's algs to compare them here."));
  compareSplit->setStretchFactor(0, 3);
  compareSplit->setStretchFactor(1, 2);
  compareLayout->addWidget(compareSplit, 1);
  bottom_->addTab(comparePage, tr("Compare"));
  times_ = table(
      {tr("#"), tr("Case"), tr("Alg"), tr("Time"), tr("Notes"), tr("When")},
      bottom_);
  times_->setContextMenuPolicy(Qt::CustomContextMenu);
  times_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  bottom_->addTab(times_, tr("Times"));
  split->setStretchFactor(0, 3);
  split->setStretchFactor(1, 2);
  // The comparison may be squeezed in a low window (its table scrolls),
  // so that the window can be as low as the other modes'.
  bottom_->setMinimumHeight(100);
  layout->addWidget(split, 1);

  tick_.setInterval(16);
  connect(&tick_, &QTimer::timeout, this, &LabTestPanel::onTick);
  connect(&watcher_, &QFutureWatcher<GeneratedScramble>::finished, this,
          &LabTestPanel::scrambleReady);
  connect(sessionBox_, &QComboBox::currentIndexChanged, this, [this](int i) {
    if (loading_ || i < 0) {
      return;
    }
    cancelAttempt();
    store_.setCurrentIndex(static_cast<std::size_t>(i));
    saveStore();
    loadSessionOptions();
    refresh();
  });
  for (QComboBox *b : {cubeBox_, orderBox_}) {
    connect(b, &QComboBox::currentIndexChanged, this,
            &LabTestPanel::optionsChanged);
  }
  for (QSpinBox *b : {repsBox_, warmupBox_, trimBox_}) {
    connect(b, &QSpinBox::valueChanged, this, &LabTestPanel::optionsChanged);
  }
  connect(randomAufBox_, &QCheckBox::toggled, this,
          &LabTestPanel::optionsChanged);
  connect(windowBox_, &QComboBox::currentIndexChanged, this,
          &LabTestPanel::fillCompare);
  connect(chartBox_, &QComboBox::currentIndexChanged, this, [this](int i) {
    chart_->setMode(i == 1 ? LabChart::Mode::Trend : LabChart::Mode::Spread);
  });
  connect(newButton, &QPushButton::clicked, this, &LabTestPanel::newSession);
  connect(renameButton, &QPushButton::clicked, this,
          &LabTestPanel::renameSession);
  connect(deleteButton, &QPushButton::clicked, this,
          &LabTestPanel::deleteSession);
  connect(removeButton, &QPushButton::clicked, this,
          &LabTestPanel::removeEntries);
  connect(upButton, &QPushButton::clicked, this, [this] { moveEntry(-1); });
  connect(downButton, &QPushButton::clicked, this, [this] { moveEntry(1); });
  connect(times_, &QWidget::customContextMenuRequested, this,
          &LabTestPanel::timesMenu);
  connect(entries_, &QTableWidget::cellDoubleClicked, this,
          [this](int row, int) {
            if (flow_.phase() != Phase::Running && row >= 0) {
              cancelAttempt();
              setUp(static_cast<std::size_t>(row));
            }
          });

  applySettings();
  loadSessions();
  refresh();
}

LabSession &LabTestPanel::session() { return store_.current(); }
const LabSession &LabTestPanel::session() const { return store_.current(); }

QString LabTestPanel::sessionName() const { return qs(session().name); }

bool LabTestPanel::onVirtual() const {
  return session().cube == CubeKind::Virtual;
}

void LabTestPanel::saveStore() {
  saveInBackground(store_, this, [this](const QString &error) {
    emit message(tr("Could not save Alg Lab: %1").arg(error));
  });
}

void LabTestPanel::applySettings() {
  display_->setDecimals(settings_.decimals);
  chart_->setDecimals(settings_.decimals);
  CubeLook look = cubeView_->look();
  look.scheme = settings_.scheme;
  cubeView_->setLook(look);
  cubeView_->setAnimationMs(settings_.animationMs);
  timer_.setConfig({settings_.holdMs, false, 15000});
  if (current_) {
    showCurrent();
  }
  fillCompare();
}

void LabTestPanel::loadSessions() {
  loading_ = true;
  sessionBox_->clear();
  for (const auto &s : store_.sessions()) {
    sessionBox_->addItem(qs(s.name));
  }
  sessionBox_->setCurrentIndex(static_cast<int>(store_.currentIndex()));
  loading_ = false;
  loadSessionOptions();
}

void LabTestPanel::loadSessionOptions() {
  loading_ = true;
  const LabSession &s = session();
  cubeBox_->setCurrentIndex(s.cube == CubeKind::Virtual ? 1 : 0);
  orderBox_->setCurrentIndex(s.order == LabOrder::Blocked ? 1 : 0);
  repsBox_->setValue(s.reps);
  warmupBox_->setValue(s.warmup);
  trimBox_->setValue(s.trimPercent);
  randomAufBox_->setChecked(s.randomAuf);
  loading_ = false;
}

void LabTestPanel::optionsChanged() {
  if (loading_) {
    return;
  }
  LabSession &s = session();
  CubeKind cube =
      cubeBox_->currentIndex() == 1 ? CubeKind::Virtual : CubeKind::Physical;
  bool setupChanged =
      cube != s.cube || randomAufBox_->isChecked() != s.randomAuf;
  s.cube = cube;
  s.order = orderBox_->currentIndex() == 1 ? LabOrder::Blocked
                                           : LabOrder::Interleaved;
  s.reps = repsBox_->value();
  s.warmup = warmupBox_->value();
  s.trimPercent = trimBox_->value();
  s.randomAuf = randomAufBox_->isChecked();
  saveStore();
  if (setupChanged && current_ && flow_.phase() == Phase::Ready && !isBusy()) {
    setUp(current_->entry); // on the other cube, or with(out) the AUF
  }
  refresh();
  emit attemptsChanged();
}

LabWindow LabTestPanel::window() const {
  LabWindow w;
  if (windowBox_->currentIndex() == 0) {
    w.kind = LabWindow::Kind::Session;
    w.session = session().id;
  }
  return w;
}

WarmupOf LabTestPanel::warmup() const {
  return [this](std::string_view id) { return store_.warmupOf(id); };
}

void LabTestPanel::refresh() {
  // Entries whose case is gone stay listed, marked, until removed.
  fillEntries();
  fillCompare();
  fillTimes();
  if (current_) {
    if (auto c = resolveLabCase(library_, store_, current_->c.key)) {
      current_->c = *c;
    }
  }
  if (flow_.phase() == Phase::Idle || flow_.phase() == Phase::Finished) {
    showStatus();
  }
}

void LabTestPanel::fillEntries() {
  const LabSession &s = session();
  std::vector<int> done = repsDone(s, store_.attempts());
  entries_->setRowCount(static_cast<int>(s.entries.size()));
  for (std::size_t i = 0; i < s.entries.size(); ++i) {
    const LabEntry &e = s.entries[i];
    auto c = resolveLabCase(library_, store_, e.key);
    int row = static_cast<int>(i);
    entries_->setItem(row, 0, item(c ? c->name : tr("(case gone)")));
    entries_->setItem(row, 1, item(qs(e.alg)));
    entries_->setItem(row, 2,
                      item(QString("%1/%2").arg(done[i]).arg(s.reps), true));
  }
  entries_->resizeColumnToContents(0);
  entries_->resizeColumnToContents(2);
}

void LabTestPanel::fillCompare() {
  const LabSession &s = session();
  LabWindow w = window();
  compare_->setSortingEnabled(false);
  compare_->setRowCount(static_cast<int>(s.entries.size()));
  std::vector<LabChart::Series> series;
  const int d = settings_.decimals;
  for (std::size_t i = 0; i < s.entries.size(); ++i) {
    const LabEntry &e = s.entries[i];
    LabStats st = labStats(store_.attempts(), e.key, e.alg, s.cube, w,
                           s.trimPercent, warmup());
    auto c = resolveLabCase(library_, store_, e.key);
    int row = static_cast<int>(i);
    QColor colour = seriesColour(i, settings_.darkTheme);
    auto *algItem = item(qs(e.alg));
    if (i < 8) {
      QPixmap swatch(10, 10);
      swatch.fill(colour);
      algItem->setIcon(QIcon(swatch));
    }
    compare_->setItem(row, 0, algItem);
    compare_->setItem(row, 1, item(c ? c->name : QString()));
    auto num = [&](int col, const QString &text, std::optional<double> key) {
      compare_->setItem(row, col, new SortItem(text, key));
    };
    auto ms = [](std::optional<std::int64_t> v) -> std::optional<double> {
      if (!v) {
        return std::nullopt;
      }
      return static_cast<double>(*v);
    };
    num(2, QString::number(st.used), st.used);
    num(3, labTime(st.mean, d), ms(st.mean));
    num(4, labTime(st.median, d), ms(st.median));
    num(5, labTime(st.best, d), ms(st.best));
    num(6, labTime(st.worst, d), ms(st.worst));
    num(7, labTime(st.ao5, d), ms(st.ao5));
    num(8, labTime(st.ao12, d), ms(st.ao12));
    num(9, st.sd ? QString::number(*st.sd / 1000.0, 'f', d) : "–", st.sd);
    num(10,
        st.used + st.dnfs ? QString::number(st.dnfRate() * 100.0, 'f', 0) : "–",
        st.dnfRate());
    if (i < 8) {
      LabChart::Series ser;
      ser.name = qs(e.alg);
      ser.colour = colour;
      for (const auto &a :
           labTimed(store_.attempts(), e.key, e.alg, s.cube, w, warmup())) {
        if (!a.dnf) {
          ser.times.push_back(a.timeMs);
        }
      }
      series.push_back(std::move(ser));
    }
  }
  compare_->setSortingEnabled(true);
  compare_->resizeColumnsToContents();
  chart_->setSeries(std::move(series));
}

void LabTestPanel::fillTimes() {
  const LabSession &s = session();
  std::vector<std::size_t> rows;
  for (std::size_t i = 0; i < store_.attempts().size(); ++i) {
    if (store_.attempts()[i].session == s.id) {
      rows.push_back(i);
    }
  }
  times_->setRowCount(static_cast<int>(rows.size()));
  const int d = settings_.decimals;
  // Newest first.
  for (std::size_t r = 0; r < rows.size(); ++r) {
    std::size_t i = rows[rows.size() - 1 - r];
    const LabAttempt &a = store_.attempts()[i];
    auto c = resolveLabCase(library_, store_, a.key);
    int row = static_cast<int>(r);
    auto *num = item(QString::number(rows.size() - r), true);
    num->setData(Qt::UserRole, static_cast<qulonglong>(i));
    times_->setItem(row, 0, num);
    times_->setItem(row, 1, item(c ? c->name : QString()));
    times_->setItem(row, 2, item(qs(a.alg)));
    times_->setItem(row, 3,
                    item(a.dnf ? "DNF(" + qs(formatTime(a.timeMs, d)) + ")"
                               : qs(formatTime(a.timeMs, d)),
                         true));
    QStringList notes;
    if (a.deviated) {
      notes << (a.include ? tr("left the alg (counted)")
                          : tr("left the alg (not counted)"));
    }
    if (a.cube == CubeKind::Virtual) {
      notes << tr("virtual");
    }
    times_->setItem(row, 4, item(notes.join(", ")));
    times_->setItem(row, 5,
                    item(QDateTime::fromSecsSinceEpoch(a.timestamp)
                             .toString("yyyy-MM-dd hh:mm")));
  }
  times_->resizeColumnsToContents();
}

int LabTestPanel::addEntries(const std::vector<LabEntry> &entries) {
  LabSession &s = session();
  int added = 0;
  for (const auto &e : entries) {
    if (std::find(s.entries.begin(), s.entries.end(), e) == s.entries.end()) {
      s.entries.push_back(e);
      ++added;
    }
  }
  if (added) {
    saveStore();
    if (flow_.phase() == Phase::Finished) {
      flow_.setPhase(Phase::Idle);
    }
    refresh();
  }
  return added;
}

void LabTestPanel::newSession() {
  cancelAttempt();
  bool ok = false;
  QString name = QInputDialog::getText(
      this, tr("New test session"), tr("Name:"), QLineEdit::Normal,
      tr("Session %1").arg(store_.sessions().size() + 1), &ok);
  if (!ok || name.trimmed().isEmpty()) {
    return;
  }
  LabSession old = session();
  LabSession &s = store_.addLabSession(name.trimmed().toStdString());
  // A new session keeps the last one's options.
  s.cube = old.cube;
  s.order = old.order;
  s.reps = old.reps;
  s.warmup = old.warmup;
  s.trimPercent = old.trimPercent;
  s.randomAuf = old.randomAuf;
  saveStore();
  loadSessions();
  refresh();
}

void LabTestPanel::renameSession() {
  bool ok = false;
  QString name =
      QInputDialog::getText(this, tr("Rename session"), tr("Name:"),
                            QLineEdit::Normal, qs(session().name), &ok);
  if (!ok || name.trimmed().isEmpty()) {
    return;
  }
  session().name = name.trimmed().toStdString();
  saveStore();
  loadSessions();
}

void LabTestPanel::deleteSession() {
  int times = 0;
  for (const auto &a : store_.attempts()) {
    times += a.session == session().id ? 1 : 0;
  }
  if (QMessageBox::question(
          this, tr("Delete session"),
          tr("Delete the session \"%1\"? Its %n time(s) stay with their algs "
             "(in the all-time statistics).",
             nullptr, times)
              .arg(qs(session().name))) != QMessageBox::Yes) {
    return;
  }
  cancelAttempt();
  store_.removeSession(store_.currentIndex());
  if (store_.current().id.empty()) {
    store_.current().id = newLabSessionId();
  }
  saveStore();
  loadSessions();
  refresh();
  emit attemptsChanged();
}

void LabTestPanel::removeEntries() {
  std::vector<int> rows;
  for (const auto &r : entries_->selectionModel()->selectedRows()) {
    rows.push_back(r.row());
  }
  if (rows.empty()) {
    return;
  }
  cancelAttempt();
  std::sort(rows.rbegin(), rows.rend());
  for (int r : rows) {
    auto &e = session().entries;
    if (r < static_cast<int>(e.size())) {
      e.erase(e.begin() + r);
    }
  }
  saveStore();
  refresh();
}

void LabTestPanel::moveEntry(int direction) {
  int row = entries_->currentRow();
  auto &e = session().entries;
  int to = row + direction;
  if (row < 0 || to < 0 || to >= static_cast<int>(e.size())) {
    return;
  }
  std::swap(e[static_cast<std::size_t>(row)], e[static_cast<std::size_t>(to)]);
  if (current_ && current_->entry == static_cast<std::size_t>(row)) {
    current_->entry = static_cast<std::size_t>(to);
  } else if (current_ && current_->entry == static_cast<std::size_t>(to)) {
    current_->entry = static_cast<std::size_t>(row);
  }
  saveStore();
  refresh();
  entries_->selectRow(to);
}

// --- attempts ---------------------------------------------------------------

bool LabTestPanel::isBusy() const {
  return flow_.phase() == Phase::Running ||
         (flow_.phase() == Phase::Ready &&
          (timer_.state() == SolveTimer::State::Holding ||
           timer_.state() == SolveTimer::State::Ready));
}

void LabTestPanel::cancelAttempt() {
  timer_.cancel();
  tick_.stop();
  if (flow_.phase() == Phase::Running || flow_.phase() == Phase::Ready) {
    flow_.setPhase(current_ ? Phase::Ready : Phase::Idle);
    if (current_) {
      resetCube();
      display_->showTime(0);
    }
  }
  showStatus();
}

void LabTestPanel::next() {
  auto n = nextEntry(session(), store_.attempts(), lastEntry_, rng_);
  if (!n) {
    current_.reset();
    flow_.setPhase(session().entries.empty() ? Phase::Idle : Phase::Finished);
    showCurrent();
    showStatus();
    return;
  }
  setUp(*n);
}

void LabTestPanel::setUp(std::size_t entry) {
  const LabSession &s = session();
  if (entry >= s.entries.size()) {
    return;
  }
  const LabEntry &e = s.entries[entry];
  auto c = resolveLabCase(library_, store_, e.key);
  if (!c) {
    emit message(tr("That alg's case is gone; remove it from the session."));
    return;
  }
  Current cur;
  cur.entry = entry;
  cur.c = *c;
  cur.alg = e.alg;
  cur.setup = labSetup(c->state, e.alg, c->free, c->aufBefore, c->aufAfter,
                       s.randomAuf, rng_);
  auto d =
      displayOrientation(c->state, e.alg, c->free, c->aufBefore, c->aufAfter);
  cur.shownAlg = qs(d ? d->alg : e.alg);
  current_ = std::move(cur);
  flow_.setPhase(Phase::Ready);
  lastAttempt_.reset();
  timer_.cancel();
  display_->showTime(0);
  resetCube();
  showCurrent();
  if (!onVirtual()) {
    current_->scramble.clear();
    requestScramble();
  }
  showStatus();
  entries_->selectRow(static_cast<int>(entry));
}

void LabTestPanel::showCurrent() {
  bool virt = onVirtual();
  cubeView_->setVisible(virt && current_);
  viewRow_->setVisible(virt && current_);
  scrambleLabel_->setVisible(!virt && current_);
  // On the virtual cube the cube itself shows the case.
  diagram_->setVisible(!virt && current_);
  if (!current_) {
    caseLabel_->clear();
    algLabel_->clear();
    diagram_->clear();
    scrambleLabel_->clear();
    return;
  }
  caseLabel_->setText(current_->c.name + "  ·  " + current_->c.where);
  QString alg = current_->shownAlg;
  if (session().randomAuf && current_->c.aufBefore) {
    alg = tr("(AUF) %1").arg(alg);
  }
  algLabel_->setText(alg);
  ResolvedCase shown = current_->c;
  shown.state = current_->setup;
  diagram_->setPixmap(
      labDiagram(shown, settings_, kDiagramSize, devicePixelRatioF()));
  scrambleLabel_->setText(current_->scramble.isEmpty() ? tr("Scrambling…")
                                                       : current_->scramble);
}

void LabTestPanel::requestScramble() {
  if (!current_) {
    return;
  }
  int request = ++scrambleRequest_;
  watcher_.setProperty("request", request);
  CubieCube state = current_->setup;
  ColourNeutrality hold;
  watcher_.setFuture(generateScramble(
      [state](Rng &) { return ScrambleSetup{state, {}, {}}; }, hold));
}

void LabTestPanel::scrambleReady() {
  if (watcher_.property("request").toInt() != scrambleRequest_ || !current_ ||
      onVirtual()) {
    return;
  }
  current_->scramble = watcher_.result().text;
  scrambleLabel_->setText(current_->scramble);
}

void LabTestPanel::resetCube() {
  if (!current_) {
    return;
  }
  CubeLook look = cubeView_->look();
  look.yaw = cube_painter::yawFor(current_->c.spec.view, CubeLook{}.yaw);
  cubeView_->setLook(look);
  FaceletCube f = current_->setup.toFacelets();
  attempt_.load(f);
  follower_ =
      AlgFollower(f, {parseAlgOrEmpty(current_->shownAlg.toStdString())});
  cubeView_->setState(attempt_.cube());
}

void LabTestPanel::showStatus() {
  QString text;
  QList<KeyHint> keys;
  switch (flow_.phase()) {
  case Phase::Idle:
    if (session().entries.empty()) {
      text = tr("Add algs to the session from the Search tab (Add to "
                "session).");
    } else {
      keys = {{tr("Space"), tr("start timing the session's algs")}};
    }
    break;
  case Phase::Ready:
    if (onVirtual()) {
      text = tr("Do the alg: the timer starts with your first turn and "
                "stops when the case is solved.");
      keys = {{tr("Esc"), tr("set it up again")}};
    } else {
      text = tr("Scramble, then start the timer.");
      keys = {{tr("hold Space"), tr("start on letting go")},
              {tr("Esc"), tr("cancel")}};
    }
    break;
  case Phase::Running:
    keys = {onVirtual() ? KeyHint{tr("Space"), tr("stop (a DNF unless solved)")}
                        : KeyHint{tr("any key"), tr("stop")}};
    break;
  case Phase::Done: {
    keys = {{tr("Space"), tr("next alg")},
            {tr("Backspace"), tr("DNF")},
            {tr("Delete"), tr("delete the time")},
            {tr("Esc"), tr("same alg again")}};
    if (lastAttempt_) {
      if (const LabAttempt *a = store_.attempt(*lastAttempt_);
          a && a->deviated) {
        text = tr("Your moves left the alg: the time is kept but not "
                  "counted (the Times tab can count it).");
      }
    }
    break;
  }
  case Phase::Finished:
    text = tr("Every alg has its %n rep(s). Raise the reps or add algs to "
              "carry on.",
              nullptr, session().reps);
    break;
  }
  status_->setHint(text, keys);
}

void LabTestPanel::onTick() {
  std::int64_t now = monotonicMs();
  timer_.tick(now);
  display_->showTimer(timer_, now);
}

bool LabTestPanel::stepDone() const {
  if (!current_) {
    return false;
  }
  FaceletCube f = attempt_.cube();
  f.normalizeOrientation();
  auto c = CubieCube::fromFacelets(f);
  if (!c) {
    return false;
  }
  for (int v = 0; v < 4; ++v) {
    if (v != 0 && !current_->c.aufAfter) {
      continue;
    }
    // A U turn left before the alg (with a random AUF) is the solver's to
    // do, so only the AUF after it is slack.
    if (withinMask(*c * uTurn(v), current_->c.free)) {
      return true;
    }
  }
  return false;
}

void LabTestPanel::virtualTurn(Move m, std::int64_t now) {
  if (flow_.phase() == Phase::Ready && !isRotation(m.type)) {
    attempt_.setClock(now);
    attempt_.start();
    timer_.startNow(now);
    flow_.setPhase(Phase::Running);
    tick_.start();
    showStatus();
  }
  if (!attempt_.turn(m, now)) {
    return;
  }
  follower_.move(m);
  cubeView_->animateMove(m);
  if (flow_.phase() == Phase::Running && stepDone()) {
    timer_.stopNow(now);
    finish(timer_.elapsed(now), false);
  }
}

void LabTestPanel::finish(std::int64_t ms, bool dnf) {
  tick_.stop();
  flow_.stopped(monotonicMs());
  if (!current_) {
    return;
  }
  LabAttempt a;
  a.key = current_->c.key;
  a.alg = current_->alg;
  a.cube = session().cube;
  a.timeMs = ms;
  a.dnf = dnf;
  a.session = session().id;
  a.timestamp = unixNow();
  if (onVirtual()) {
    a.deviated = follower_.deviated();
    a.moves = toString(attempt_.solution());
  } else {
    a.scramble = current_->scramble.toStdString();
  }
  store_.addAttempt(a);
  lastAttempt_ = store_.attempts().size() - 1;
  lastEntry_ = current_->entry;
  flow_.setPhase(Phase::Done);
  display_->showTime(ms, dnf ? Penalty::Dnf : Penalty::None);
  saveStore();
  refresh();
  showStatus();
  emit attemptsChanged();
}

void LabTestPanel::toggleDnf() {
  if (!lastAttempt_) {
    return;
  }
  if (LabAttempt *a = store_.attempt(*lastAttempt_)) {
    a->dnf = !a->dnf;
    display_->showTime(a->timeMs, a->dnf ? Penalty::Dnf : Penalty::None);
    saveStore();
    refresh();
    emit attemptsChanged();
  }
}

void LabTestPanel::deleteLast() {
  if (!lastAttempt_) {
    return;
  }
  store_.removeAttempt(*lastAttempt_);
  lastAttempt_.reset();
  saveStore();
  refresh();
  emit attemptsChanged();
  emit message(tr("Time deleted."));
  if (current_) {
    setUp(current_->entry);
  }
}

bool LabTestPanel::handleKey(QKeyEvent *e, bool press) {
  const std::int64_t now = monotonicMs();
  const int key = e->key();
  std::optional<Move> move =
      onVirtual() ? settings_.keymap.lookup(e) : std::optional<Move>();
  using K = LabFlow::Key;
  LabFlow::Input in;
  in.key = key == Qt::Key_Space       ? K::Space
           : key == Qt::Key_Escape    ? K::Escape
           : key == Qt::Key_Backspace ? K::Backspace
           : key == Qt::Key_Delete    ? K::Delete
           : isModifierKey(key)       ? K::Modifier
           : move                     ? K::Move
                                      : K::Other;
  in.press = press;
  in.plain = !(e->modifiers() &
               (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
  in.now = now;
  flow_.setVirtualCube(onVirtual());
  const LabFlow::Step step = flow_.key(in);
  using A = LabFlow::Action;
  switch (step.action) {
  case A::None:
    break;
  case A::Next:
    next();
    break;
  case A::ToggleDnf:
    toggleDnf();
    break;
  case A::DeleteLast:
    deleteLast();
    break;
  case A::SameAgain:
    if (current_) {
      setUp(current_->entry);
    }
    break;
  case A::ResetReady:
    timer_.cancel();
    tick_.stop();
    resetCube();
    display_->showTime(0);
    showStatus();
    break;
  case A::Turn:
    virtualTurn(*move, now);
    break;
  case A::HoldPress:
    timer_.pressStart(now);
    tick_.start();
    display_->showTimer(timer_, now);
    break;
  case A::HoldRelease:
    timer_.releaseStart(now);
    if (timer_.state() == SolveTimer::State::Running) {
      flow_.setPhase(Phase::Running);
      showStatus();
    } else {
      tick_.stop();
    }
    display_->showTimer(timer_, now);
    break;
  case A::StopVirtual:
    timer_.stopNow(now);
    finish(timer_.elapsed(now), !stepDone());
    break;
  case A::StopPhysical:
    timer_.pressStop(now);
    if (auto r = timer_.takeResult()) {
      finish(r->timeMs, false);
    } else {
      finish(timer_.elapsed(now), false);
    }
    break;
  case A::Cancel:
    cancelAttempt();
    emit message(tr("Attempt cancelled; nothing was recorded."));
    break;
  }
  return step.used;
}

void LabTestPanel::timesMenu(const QPoint &at) {
  auto *it = times_->itemAt(at);
  if (!it) {
    return;
  }
  auto index = static_cast<std::size_t>(
      times_->item(it->row(), 0)->data(Qt::UserRole).toULongLong());
  LabAttempt *a = store_.attempt(index);
  if (!a) {
    return;
  }
  QMenu menu(this);
  QAction *dnf = menu.addAction(a->dnf ? tr("Not a DNF") : tr("DNF"));
  QAction *include = nullptr;
  if (a->deviated) {
    include = menu.addAction(a->include ? tr("Don't count it (left the alg)")
                                        : tr("Count it although it left the "
                                             "alg"));
  }
  QAction *remove = menu.addAction(tr("Delete"));
  QAction *chosen = menu.exec(times_->viewport()->mapToGlobal(at));
  if (!chosen) {
    return;
  }
  if (chosen == dnf) {
    a->dnf = !a->dnf;
  } else if (chosen == include) {
    a->include = !a->include;
  } else if (chosen == remove) {
    store_.removeAttempt(index);
    if (lastAttempt_ == index) {
      lastAttempt_.reset();
    } else if (lastAttempt_ && *lastAttempt_ > index) {
      --*lastAttempt_;
    }
  }
  saveStore();
  refresh();
  emit attemptsChanged();
}

} // namespace vcube
