#include "app/timer_page.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace vcube {

namespace {

QPushButton *flatButton(const QString &text, QWidget *parent) {
  auto *b = new QPushButton(text, parent);
  b->setFocusPolicy(Qt::NoFocus);
  return b;
}

QString statText(StatValue v, int decimals) {
  return QString::fromStdString(formatTime(v, decimals));
}

} // namespace

TimerPage::TimerPage(AppSettings &settings, SessionStore &store,
                     AlgLibrary &library, QWidget *parent)
    : ModePage(parent), settings_(settings), store_(store), library_(library),
      feed_(library) {
  // --- left column: sessions, stats, times ---
  auto *left = leftPanel_ = new QWidget(this);
  auto *leftLayout = new QVBoxLayout(left);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  sessionButton_ = new QToolButton(left);
  sessionButton_->setFocusPolicy(Qt::NoFocus);
  sessionButton_->setPopupMode(QToolButton::InstantPopup);
  sessionButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  sessionButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  sessionButton_->setToolTip(
      tr("Switch, add, rename or delete sessions, or import and export "
         "cstimer sessions"));
  sessionMenu_ = new QMenu(sessionButton_);
  sessionButton_->setMenu(sessionMenu_);
  leftLayout->addWidget(sessionButton_);

  stats_ = new QLabel(left);
  stats_->setTextFormat(Qt::RichText);
  stats_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  leftLayout->addWidget(stats_);

  table_ = new QTableWidget(0, 4, left);
  table_->setHorizontalHeaderLabels({"#", tr("Time"), "ao5", "ao12"});
  table_->verticalHeader()->setVisible(false);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setFocusPolicy(Qt::NoFocus);
  table_->setContextMenuPolicy(Qt::CustomContextMenu);
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::ResizeToContents);
  leftLayout->addWidget(table_, 1);

  // --- right column: scramble, timer, chart ---
  auto *right = new QWidget(this);
  auto *rightLayout = new QVBoxLayout(right);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  typeRow_ = new QWidget(right);
  auto *typeRow = new QHBoxLayout(typeRow_);
  typeRow->setContentsMargins(0, 0, 0, 0);
  typeBox_ = new ScrambleTypeBox(library_, typeRow_);
  auto *nextButton = flatButton(tr("Next scramble"), typeRow_);
  nextButton_ = nextButton;
  cubeBox_ = new QComboBox(typeRow_);
  cubeBox_->setFocusPolicy(Qt::NoFocus);
  cubeBox_->addItem(tr("Virtual cube"));
  cubeBox_->addItem(tr("Physical cube"));
  cubeBox_->setToolTip(tr("The cube this session's solves are done on"));
  cubeView_ = new CubeView(right);
  cubeView_->setToolTip(
      tr("Turn the cube as a whole to look at it, hold Space to start the "
         "timer, solve, and press Space to stop"));
  viewPicker_ = new QWidget(typeRow_);
  auto *viewLayout = new QHBoxLayout(viewPicker_);
  viewLayout->setContentsMargins(0, 0, 0, 0);
  viewLayout->addWidget(new QLabel(tr("View:"), viewPicker_));
  viewLayout->addWidget(createViewSelector(cubeView_, "timed", viewPicker_));
  typeRow->addWidget(new QLabel(tr("Scramble:"), typeRow_));
  algs_ = new ScrambleAlgs(library_, "timer/showAlgs", right);
  typeRow->addWidget(typeBox_, 1);
  typeRow->addWidget(algs_->button());
  typeRow->addSpacing(12);
  typeRow->addWidget(cubeBox_);
  typeRow->addWidget(viewPicker_);
  typeRow->addWidget(nextButton);
  rightLayout->addWidget(typeRow_);

  auto *scrambleRow = new QHBoxLayout;
  scrambleLabel_ = new QLabel(right);
  scrambleLabel_->setWordWrap(true);
  scrambleLabel_->setAlignment(Qt::AlignCenter);
  scrambleLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  QFont sf = scrambleLabel_->font();
  sf.setPointSize(17);
  scrambleLabel_->setFont(sf);
  scrambleLabel_->setMinimumHeight(60);
  net_ = new NetPreview(right);
  scrambleRow->addWidget(scrambleLabel_, 1);
  scrambleRow->addWidget(net_);
  rightLayout->addLayout(scrambleRow);

  auto *timeColumn = new QVBoxLayout;
  display_ = new TimeDisplay(right);
  statusLabel_ = new QLabel(right);
  statusLabel_->setAlignment(Qt::AlignCenter);
  auto *penaltyRow = new QHBoxLayout;
  plus2Button_ = flatButton("+2", right);
  dnfButton_ = flatButton("DNF", right);
  deleteButton_ = flatButton(tr("Delete"), right);
  plus2Button_->setToolTip(tr("Toggle +2 on the last solve"));
  dnfButton_->setToolTip(tr("Toggle DNF on the last solve"));
  deleteButton_->setToolTip(tr("Delete the last solve"));
  penaltyRow->addStretch();
  penaltyRow->addWidget(plus2Button_);
  penaltyRow->addWidget(dnfButton_);
  penaltyRow->addWidget(deleteButton_);
  penaltyRow->addStretch();
  timeColumn->addStretch(1);
  timeColumn->addWidget(cubeView_, 8);
  timeColumn->addWidget(display_);
  timeColumn->addWidget(statusLabel_);
  timeColumn->addLayout(penaltyRow);
  timeColumn->addWidget(algs_->label());
  timeColumn->addStretch(1);
  rightLayout->addLayout(timeColumn, 3);

  chart_ = new TrendChart(right);
  rightLayout->addWidget(chart_, 1);

  auto *split = new QSplitter(this);
  split->addWidget(left);
  split->addWidget(right);
  split->setStretchFactor(0, 0);
  split->setStretchFactor(1, 1);
  split->setSizes({300, 800});
  auto *layout = new QHBoxLayout(this);
  layout->addWidget(split);

  // --- wiring ---
  connect(&tick_, &QTimer::timeout, this, &TimerPage::onTick);
  connect(&feed_, &ScrambleFeed::currentChanged, this,
          &TimerPage::showScramble);
  connect(nextButton, &QPushButton::clicked, this, [this] {
    if (!isBusy()) {
      feed_.advance();
    }
  });
  connect(typeBox_, &ScrambleTypeBox::typeChanged, this,
          [this](const QString &type) {
            if (session().scrambleType != ss(type)) {
              session().scrambleType = ss(type);
              save();
            }
            algs_->setType(type);
            feed_.setType(type);
          });
  connect(cubeBox_, &QComboBox::activated, this, [this](int index) {
    CubeKind kind = index == 0 ? CubeKind::Virtual : CubeKind::Physical;
    if (isBusy() || session().cube == kind) {
      return;
    }
    session().cube = kind;
    save();
    showCubeKind();
    showScramble();
  });
  connect(sessionMenu_, &QMenu::aboutToShow, this,
          &TimerPage::refreshSessionMenu);
  connect(plus2Button_, &QPushButton::clicked, this,
          [this] { setLastPenalty(Penalty::Plus2); });
  connect(dnfButton_, &QPushButton::clicked, this,
          [this] { setLastPenalty(Penalty::Dnf); });
  connect(deleteButton_, &QPushButton::clicked, this, [this] {
    if (!session().solves.empty()) {
      deleteSolve(static_cast<int>(session().solves.size()) - 1);
    }
  });
  connect(table_, &QTableWidget::customContextMenuRequested, this,
          &TimerPage::showSolveMenu);

  applySettings();
  selectSession(store_.currentIndex());
  algs_->setType(typeBox_->currentType());
  feed_.setType(typeBox_->currentType()); // if the type didn't change
}

bool TimerPage::isBusy() const {
  auto s = timer_.state();
  return s == SolveTimer::State::Running ||
         s == SolveTimer::State::Inspecting ||
         s == SolveTimer::State::Holding || s == SolveTimer::State::Ready;
}

void TimerPage::activated() { updateControls(); }

bool TimerPage::onVirtualCube() const {
  return store_.sessions()[store_.currentIndex()].cube == CubeKind::Virtual;
}

void TimerPage::showCubeKind() {
  bool virt = onVirtualCube();
  cubeBox_->setCurrentIndex(virt ? 0 : 1);
  cubeView_->setVisible(virt);
  viewPicker_->setVisible(virt);
  net_->setVisible(!virt);
}

void TimerPage::deactivated() {
  cubeView_->finishAnimations();
  tick_.stop();
  timer_.cancel();
  updateControls();
}

void TimerPage::windowDeactivated() {
  auto s = timer_.state();
  if (s == SolveTimer::State::Holding || s == SolveTimer::State::Ready) {
    timer_.cancel(); // Space's release would never arrive
    display_->showTimer(timer_, monotonicMs()); // what was shown before
    updateControls();
  }
}

void TimerPage::updateControls() {
  bool busy = isBusy();
  // The time only changes during a hold, inspection or solve, so the
  // display is only redrawn then.
  if (busy != tick_.isActive()) {
    if (busy) {
      tick_.start(16);
    } else {
      tick_.stop();
    }
  }
  if (busy == controlsLocked_) {
    return;
  }
  controlsLocked_ = busy;
  for (QWidget *w : std::initializer_list<QWidget *>{
           typeBox_, nextButton_, sessionButton_, cubeBox_, plus2Button_,
           dnfButton_, deleteButton_}) {
    w->setEnabled(!busy);
  }
}

void TimerPage::setFocusMode(bool on) {
  leftPanel_->setVisible(!on);
  typeRow_->setVisible(!on);
  chart_->setVisible(!on);
}

void TimerPage::applySettings() {
  timer_.setConfig({settings_.holdMs, settings_.inspection, 15000});
  display_->setDecimals(settings_.decimals);
  net_->setScheme(settings_.scheme);
  CubeLook look = cubeView_->look();
  look.scheme = settings_.scheme;
  cubeView_->setLook(look);
  cubeView_->setAnimationMs(settings_.animationMs);
  feed_.setNeutrality(settings_.neutrality);
  algs_->refresh();
  refreshSolves();
}

void TimerPage::refreshScrambleTypes(const QString &changedSet) {
  QString before = typeBox_->currentType();
  typeBox_->refresh(); // emits typeChanged if the type went away
  if (typeBox_->currentType() == before && !changedSet.isEmpty() &&
      before == "set:" + changedSet) {
    feed_.regenerate(); // the set's cases or ticks may have changed
  }
  algs_->refresh();
}

void TimerPage::showScramble() {
  const auto &current = feed_.current();
  algs_->setScramble(current ? &*current : nullptr);
  if (!current) {
    scrambleLabel_->setText(tr("Generating scramble…"));
    net_->setCube(std::nullopt);
    attempt_.load(Alg{});
    cubeView_->setState(attempt_.cube());
    return;
  }
  scrambleLabel_->setText(current->text);
  net_->setScramble(current->text);
  // Shown as scrambled holding white on top and green in front.
  attempt_.load(parseAlgOrEmpty(ss(current->text)));
  cubeView_->setState(attempt_.cube());
}

void TimerPage::onTick() {
  std::int64_t now = monotonicMs();
  timer_.tick(now);
  if (isBusy()) {
    display_->showTimer(timer_, now);
  }
  updateControls();
}

bool TimerPage::handleKey(QKeyEvent *e, bool press) {
  struct Update {
    TimerPage *page;
    ~Update() { page->updateControls(); }
  } update{this};
  bool virt = onVirtualCube();
  std::optional<Move> move =
      virt ? settings_.keymap.lookup(e) : std::optional<Move>();
  if (e->isAutoRepeat()) {
    return e->key() == Qt::Key_Space || move.has_value();
  }
  std::int64_t now = monotonicMs();
  using S = SolveTimer::State;
  // Back to the scramble as it was dealt, e.g. after a cancelled solve.
  auto restartCube = [this] {
    attempt_.restart();
    cubeView_->setState(attempt_.cube());
  };

  if (timer_.state() == S::Running) {
    if (press && e->key() == Qt::Key_Escape) {
      timer_.cancel();
      display_->showTime(0);
      statusLabel_->setText(tr("Solve cancelled; nothing was recorded."));
      if (virt) {
        restartCube();
      }
      return true;
    }
    if (virt) {
      // Only Space stops the timer; the turning keys turn the cube.
      if (press && e->key() == Qt::Key_Space) {
        stopVirtual(now);
      } else if (press && move && attempt_.turn(*move)) {
        cubeView_->animateMove(*move);
      }
      return e->key() == Qt::Key_Space || move.has_value();
    }
    if (press) {
      timer_.pressStop(now);
      if (auto r = timer_.takeResult()) {
        recordSolve(r->timeMs, r->penalty);
      }
      return true;
    }
    return false;
  }
  if (e->key() == Qt::Key_Escape && press) {
    timer_.cancel();
    display_->showTime(0);
    statusLabel_->clear();
    if (virt) {
      restartCube();
    }
    return true;
  }
  if (e->key() == Qt::Key_Tab && press && !move && algs_->toggle()) {
    return true;
  }
  if (move) {
    // Before the solve the cube can only be turned as a whole.
    if (press && timer_.state() != S::Holding && timer_.state() != S::Ready) {
      if (attempt_.turn(*move)) {
        cubeView_->animateMove(*move);
      } else {
        statusLabel_->setText(
            tr("Only rotations until the timer starts (hold Space)"));
      }
    }
    return true;
  }
  if (e->key() != Qt::Key_Space) {
    return false;
  }
  if (press) {
    if (!feed_.current() && timer_.state() != S::Inspecting) {
      return true; // wait for a scramble
    }
    timer_.pressStart(now);
  } else {
    timer_.releaseStart(now);
    if (timer_.state() == S::Running) {
      statusLabel_->clear();
      if (virt) {
        attempt_.start();
      }
    }
  }
  display_->showTimer(timer_, now);
  return true;
}

void TimerPage::stopVirtual(std::int64_t now) {
  timer_.pressStop(now);
  auto r = timer_.takeResult();
  if (!r) {
    return;
  }
  bool solved = attempt_.cube().isSolved();
  int moves = attempt_.count();
  recordSolve(r->timeMs, solved ? r->penalty : Penalty::Dnf,
              toString(attempt_.solution()));
  statusLabel_->setText(
      solved ? (moves == 1 ? tr("Solved in 1 move (HTM).")
                           : tr("Solved in %1 moves (HTM).").arg(moves))
             : tr("DNF: the cube wasn't solved when you stopped the timer."));
}

void TimerPage::recordSolve(std::int64_t timeMs, Penalty penalty,
                            const std::string &solution) {
  Solve s;
  s.timeMs = timeMs;
  s.penalty = penalty;
  s.moves = solution;
  s.scramble = feed_.current() ? ss(feed_.current()->text) : std::string();
  s.timestamp = unixNow();
  session().solves.push_back(s);
  display_->showTime(timeMs, penalty);
  statusLabel_->clear();
  save();
  refreshSolves();
  feed_.advance(); // on to the next scramble
}

void TimerPage::refreshSessionMenu() {
  sessionMenu_->clear();
  auto *group = new QActionGroup(sessionMenu_);
  for (std::size_t i = 0; i < store_.sessions().size(); ++i) {
    const Session &s = store_.sessions()[i];
    QAction *a = sessionMenu_->addAction(
        tr("%1 (%2)").arg(qs(s.name)).arg(s.solves.size()), this,
        [this, i] { selectSession(i); });
    a->setCheckable(true);
    a->setChecked(i == store_.currentIndex());
    group->addAction(a);
  }
  sessionMenu_->addSeparator();
  sessionMenu_->addAction(tr("New session…"), this, &TimerPage::addSession);
  sessionMenu_->addAction(tr("Rename…"), this, &TimerPage::renameSession);
  sessionMenu_->addAction(tr("Delete…"), this, &TimerPage::deleteSession);
  sessionMenu_->addSeparator();
  sessionMenu_->addAction(tr("Import from cstimer…"), this,
                          &TimerPage::importCstimer);
  sessionMenu_->addAction(tr("Export to cstimer…"), this,
                          &TimerPage::exportCstimer);
  // No session changes in the middle of a solve.
  for (QAction *a : sessionMenu_->actions()) {
    a->setEnabled(!isBusy());
  }
}

void TimerPage::selectSession(std::size_t index) {
  if (isBusy()) {
    return;
  }
  store_.setCurrentIndex(index);
  sessionButton_->setText(tr("Session: %1").arg(qs(session().name)));
  showCubeKind();
  typeBox_->setCurrentType(qs(session().scrambleType));
  showScramble(); // onto the session's cube
  refreshSolves();
  save();
}

void TimerPage::refreshSolves() {
  const auto &solves = session().solves;
  int d = settings_.decimals;
  auto ao5 = rollingAverages(solves, 5);
  auto ao12 = rollingAverages(solves, 12);
  table_->setRowCount(static_cast<int>(solves.size()));
  for (std::size_t i = 0; i < solves.size(); ++i) {
    int row = static_cast<int>(solves.size() - 1 - i);
    auto set = [&](int col, const QString &text) {
      auto *item = new QTableWidgetItem(text);
      item->setData(Qt::UserRole, static_cast<int>(i));
      if (col > 0) {
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      }
      table_->setItem(row, col, item);
    };
    set(0, QString::number(i + 1));
    set(1, qs(formatSolve(solves[i], d)));
    set(2, statText(ao5[i], d));
    set(3, statText(ao12[i], d));
    QStringList tip;
    if (!solves[i].comment.empty()) {
      tip << qs(solves[i].comment);
    }
    if (!solves[i].moves.empty()) {
      tip << tr("Solution: %1").arg(qs(solves[i].moves));
    }
    table_->item(row, 1)->setToolTip(tip.join("\n"));
  }
  SessionSummary sum = summarize(solves);
  QString html =
      "<table cellspacing='4' width='100%'><tr><th></th><th align='right'>" +
      tr("current") + "</th><th align='right'>" + tr("best") + "</th></tr>";
  auto row = [&](const QString &name, StatValue cur, StatValue best) {
    html += QString("<tr><td>%1</td><td align='right'>%2</td><td "
                    "align='right'><b>%3</b></td></tr>")
                .arg(name, statText(cur, d), statText(best, d));
  };
  row(tr("time"),
      solves.empty() ? StatValue{} : StatValue{effectiveTime(solves.back())},
      sum.best);
  row("mo3", currentMean(solves, 3), bestMean(solves, 3));
  row("ao5", currentAverage(solves, 5), bestAverage(solves, 5));
  row("ao12", currentAverage(solves, 12), bestAverage(solves, 12));
  row("ao100", currentAverage(solves, 100), bestAverage(solves, 100));
  html += "</table>";
  html += tr("<p>Solves: %1%2 &nbsp; Mean: %3</p>")
              .arg(sum.count)
              .arg(sum.dnfs ? QString(" (%1 DNF)").arg(sum.dnfs) : QString())
              .arg(statText(sum.mean, d));
  stats_->setText(html);
  chart_->setSolves(solves);
  bool any = !solves.empty();
  plus2Button_->setEnabled(any);
  dnfButton_->setEnabled(any);
  deleteButton_->setEnabled(any);
}

void TimerPage::setLastPenalty(Penalty p) {
  if (session().solves.empty()) {
    return;
  }
  Solve &s = session().solves.back();
  s.penalty = s.penalty == p ? Penalty::None : p;
  display_->showTime(s.timeMs, s.penalty);
  save();
  refreshSolves();
}

void TimerPage::deleteSolve(int index) {
  auto &solves = session().solves;
  if (index < 0 || index >= static_cast<int>(solves.size())) {
    return;
  }
  solves.erase(solves.begin() + index);
  save();
  refreshSolves();
}

void TimerPage::showSolveMenu(const QPoint &pos) {
  auto *item = table_->itemAt(pos);
  if (!item) {
    return;
  }
  int index = item->data(Qt::UserRole).toInt();
  auto &solves = session().solves;
  if (index < 0 || index >= static_cast<int>(solves.size())) {
    return;
  }
  Solve &s = solves[static_cast<std::size_t>(index)];
  QMenu menu(this);
  auto *ok = menu.addAction(tr("No penalty"));
  auto *plus2 = menu.addAction("+2");
  auto *dnf = menu.addAction("DNF");
  ok->setCheckable(true);
  plus2->setCheckable(true);
  dnf->setCheckable(true);
  ok->setChecked(s.penalty == Penalty::None);
  plus2->setChecked(s.penalty == Penalty::Plus2);
  dnf->setChecked(s.penalty == Penalty::Dnf);
  menu.addSeparator();
  auto *comment = menu.addAction(tr("Comment…"));
  auto *copy = menu.addAction(tr("Copy scramble"));
  QAction *copyMoves = nullptr;
  if (!s.moves.empty()) {
    copyMoves = menu.addAction(tr("Copy solution"));
  }
  menu.addSeparator();
  auto *del = menu.addAction(tr("Delete"));
  QAction *chosen = menu.exec(table_->viewport()->mapToGlobal(pos));
  if (!chosen) {
    return;
  }
  if (chosen == ok) {
    s.penalty = Penalty::None;
  } else if (chosen == plus2) {
    s.penalty = Penalty::Plus2;
  } else if (chosen == dnf) {
    s.penalty = Penalty::Dnf;
  } else if (chosen == comment) {
    bool accepted = false;
    QString text =
        QInputDialog::getText(this, tr("Comment"), tr("Comment:"),
                              QLineEdit::Normal, qs(s.comment), &accepted);
    if (accepted) {
      s.comment = ss(text);
    }
  } else if (chosen == copy) {
    QApplication::clipboard()->setText(qs(s.scramble));
    return;
  } else if (copyMoves && chosen == copyMoves) {
    QApplication::clipboard()->setText(qs(s.moves));
    return;
  } else if (chosen == del) {
    deleteSolve(index);
    return;
  }
  save();
  refreshSolves();
}

void TimerPage::addSession() {
  bool ok = false;
  QString name = QInputDialog::getText(
      this, tr("New session"), tr("Name:"), QLineEdit::Normal,
      tr("Session %1").arg(store_.sessions().size() + 1), &ok);
  if (!ok || name.trimmed().isEmpty()) {
    return;
  }
  store_.addSession(ss(name.trimmed())).scrambleType =
      ss(typeBox_->currentType());
  selectSession(store_.currentIndex());
}

void TimerPage::renameSession() {
  bool ok = false;
  QString name =
      QInputDialog::getText(this, tr("Rename session"), tr("Name:"),
                            QLineEdit::Normal, qs(session().name), &ok);
  if (!ok || name.trimmed().isEmpty()) {
    return;
  }
  session().name = ss(name.trimmed());
  selectSession(store_.currentIndex());
}

void TimerPage::deleteSession() {
  auto answer =
      QMessageBox::question(this, tr("Delete session"),
                            tr("Delete session \"%1\" and its %2 solves?")
                                .arg(qs(session().name))
                                .arg(session().solves.size()));
  if (answer != QMessageBox::Yes) {
    return;
  }
  store_.removeSession(store_.currentIndex());
  selectSession(store_.currentIndex());
}

void TimerPage::importCstimer() {
  QString path = QFileDialog::getOpenFileName(
      this, tr("Import cstimer export"), QString(),
      tr("cstimer export (*.txt *.json);;All files (*)"));
  if (path.isEmpty()) {
    return;
  }
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    QMessageBox::warning(this, tr("Import"), f.errorString());
    return;
  }
  QString error;
  auto imported = sessionsFromCstimer(
      QJsonDocument::fromJson(f.readAll()).object(), &error);
  if (!imported) {
    QMessageBox::warning(this, tr("Import"),
                         tr("Not a cstimer export: %1").arg(error));
    return;
  }
  std::size_t solves = 0;
  for (auto &s : *imported) {
    solves += s.solves.size();
    s.name += " (cstimer)";
    store_.sessions().push_back(std::move(s));
  }
  selectSession(store_.sessions().size() - 1);
  emit message(tr("Imported %1 sessions with %2 solves")
                   .arg(imported->size())
                   .arg(solves));
}

void TimerPage::exportCstimer() {
  QString path = QFileDialog::getSaveFileName(
      this, tr("Export for cstimer"), "v-cube-sessions.txt",
      tr("cstimer import file (*.txt)"));
  if (path.isEmpty()) {
    return;
  }
  QString error;
  QByteArray bytes = QJsonDocument(sessionsToCstimer(store_.sessions()))
                         .toJson(QJsonDocument::Compact);
  if (!writeFileAtomically(path, bytes, &error)) {
    QMessageBox::warning(this, tr("Export"), error);
    return;
  }
  emit message(
      tr("Exported %1 sessions to %2").arg(store_.sessions().size()).arg(path));
}

void TimerPage::save() {
  QString error;
  if (!store_.save(&error)) {
    emit message(tr("Could not save sessions: %1").arg(error));
  }
}

} // namespace vcube
