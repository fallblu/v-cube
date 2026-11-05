#include "app/move_count_page.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
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

QString movesText(StatValue v, int decimals = 2) {
  return QString::fromStdString(formatMoves(v, decimals));
}

QStringList offeredTypes() {
  QStringList out;
  for (const auto &id : moveCountScrambleTypes()) {
    out << qs(id);
  }
  return out;
}

// The number of digits a move count can have.
constexpr int kMaxDigits = 3;

} // namespace

MoveCountPage::MoveCountPage(AppSettings &settings, MoveCountStore &store,
                             AlgLibrary &library, QWidget *parent)
    : ModePage(parent), settings_(settings), store_(store), feed_(library) {
  // --- left column: sessions, stats, move counts ---
  auto *left = leftPanel_ = new QWidget(this);
  auto *leftLayout = new QVBoxLayout(left);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  sessionButton_ = new QToolButton(left);
  sessionButton_->setFocusPolicy(Qt::NoFocus);
  sessionButton_->setPopupMode(QToolButton::InstantPopup);
  sessionButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  sessionButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  sessionButton_->setToolTip(tr("Switch, add, rename or delete sessions"));
  sessionMenu_ = new QMenu(sessionButton_);
  sessionButton_->setMenu(sessionMenu_);
  leftLayout->addWidget(sessionButton_);

  stats_ = new QLabel(left);
  stats_->setTextFormat(Qt::RichText);
  stats_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  leftLayout->addWidget(stats_);

  table_ = new QTableWidget(0, 4, left);
  table_->setHorizontalHeaderLabels({"#", tr("Moves"), "ao5", "ao12"});
  table_->verticalHeader()->setVisible(false);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setFocusPolicy(Qt::NoFocus);
  table_->setContextMenuPolicy(Qt::CustomContextMenu);
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::ResizeToContents);
  leftLayout->addWidget(table_, 1);

  // --- right column: scramble, move count entry, chart ---
  auto *right = new QWidget(this);
  auto *rightLayout = new QVBoxLayout(right);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  typeRow_ = new QWidget(right);
  auto *typeRow = new QHBoxLayout(typeRow_);
  typeRow->setContentsMargins(0, 0, 0, 0);
  typeBox_ = new ScrambleTypeBox(library, typeRow_, offeredTypes());
  typeBox_->setToolTip(
      tr("What the scramble leaves solved: a full WCA scramble for EO, "
         "EOLine, EO Cross or cross, EO Cross solved for ZZ F2L, or the "
         "cross solved for CFOP F2L"));
  auto *skipButton = flatButton(tr("Skip scramble"), typeRow_);
  skipButton->setToolTip(tr("Go on to the next scramble without recording "
                            "this one"));
  typeRow->addWidget(new QLabel(tr("Scramble:"), typeRow_));
  typeRow->addWidget(typeBox_, 1);
  typeRow->addWidget(skipButton);
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

  auto *entryColumn = new QVBoxLayout;
  stepLabel_ = new QLabel(right);
  stepLabel_->setAlignment(Qt::AlignCenter);
  QFont stepFont = stepLabel_->font();
  stepFont.setPointSizeF(stepFont.pointSizeF() * 1.3);
  stepFont.setBold(true);
  stepLabel_->setFont(stepFont);
  stepLabel_->setToolTip(tr("The session, named after the step it is for"));
  display_ = new TimeDisplay(right);
  display_->setToolTip(tr("The move count you are typing"));
  hintLabel_ = new QLabel(right);
  hintLabel_->setAlignment(Qt::AlignCenter);
  hintLabel_->setWordWrap(true);
  hintLabel_->setForegroundRole(QPalette::PlaceholderText);
  hintLabel_->setText(
      tr("Type the number of moves your solution took and press Enter. "
         "Backspace corrects, Esc clears."));
  auto *buttonRow = new QHBoxLayout;
  recordButton_ = flatButton(tr("Record (Enter)"), right);
  dnfButton_ = flatButton("DNF", right);
  deleteButton_ = flatButton(tr("Delete last"), right);
  recordButton_->setToolTip(tr("Record the move count for this scramble"));
  dnfButton_->setToolTip(
      tr("Record this scramble as DNF, e.g. when the step went wrong"));
  deleteButton_->setToolTip(tr("Delete the last recorded move count"));
  buttonRow->addStretch();
  buttonRow->addWidget(recordButton_);
  buttonRow->addWidget(dnfButton_);
  buttonRow->addWidget(deleteButton_);
  buttonRow->addStretch();
  lastLabel_ = new QLabel(right);
  lastLabel_->setAlignment(Qt::AlignCenter);
  entryColumn->addStretch();
  entryColumn->addWidget(stepLabel_);
  entryColumn->addWidget(display_);
  entryColumn->addWidget(hintLabel_);
  entryColumn->addLayout(buttonRow);
  entryColumn->addWidget(lastLabel_);
  entryColumn->addStretch();
  rightLayout->addLayout(entryColumn, 3);

  chart_ = new TrendChart(right);
  chart_->setEmptyText(tr("Move counts will be charted here"));
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
  connect(&feed_, &ScrambleFeed::currentChanged, this,
          &MoveCountPage::showScramble);
  connect(skipButton, &QPushButton::clicked, this, &MoveCountPage::skip);
  connect(typeBox_, &ScrambleTypeBox::typeChanged, this,
          [this](const QString &type) {
            if (session().scrambleType != ss(type)) {
              session().scrambleType = ss(type);
              save();
            }
            if (feed_.type() != type) {
              setEntry({}); // the count was for the old scramble
            }
            feed_.setType(type);
          });
  connect(sessionMenu_, &QMenu::aboutToShow, this,
          &MoveCountPage::refreshSessionMenu);
  connect(recordButton_, &QPushButton::clicked, this,
          [this] { record(false); });
  connect(dnfButton_, &QPushButton::clicked, this, [this] { record(true); });
  connect(deleteButton_, &QPushButton::clicked, this, [this] {
    if (!session().attempts.empty()) {
      deleteAttempt(static_cast<int>(session().attempts.size()) - 1);
    }
  });
  connect(table_, &QTableWidget::customContextMenuRequested, this,
          &MoveCountPage::showAttemptMenu);

  applySettings();
  selectSession(store_.currentIndex());
  feed_.setType(typeBox_->currentType()); // if the type didn't change
  showEntry();
}

void MoveCountPage::setFocusMode(bool on) {
  leftPanel_->setVisible(!on);
  typeRow_->setVisible(!on);
  hintLabel_->setVisible(!on);
  chart_->setVisible(!on);
}

void MoveCountPage::applySettings() {
  net_->setScheme(settings_.scheme);
  feed_.setNeutrality(settings_.neutrality);
  refreshAttempts();
}

void MoveCountPage::showScramble() {
  const auto &current = feed_.current();
  if (!current) {
    scrambleLabel_->setText(tr("Generating scramble…"));
    net_->setCube(std::nullopt);
  } else {
    scrambleLabel_->setText(current->text);
    net_->setScramble(current->text);
  }
  showEntry();
}

void MoveCountPage::setEntry(const QString &digits) {
  entry_ = digits;
  showEntry();
}

void MoveCountPage::showEntry() {
  if (entry_.isEmpty()) {
    display_->showText("–", palette().color(QPalette::PlaceholderText));
  } else {
    display_->showText(entry_);
  }
  recordButton_->setEnabled(!entry_.isEmpty() && feed_.current());
  dnfButton_->setEnabled(feed_.current().has_value());
}

bool MoveCountPage::handleKey(QKeyEvent *e, bool press) {
  int key = e->key();
  bool digit = key >= Qt::Key_0 && key <= Qt::Key_9;
  bool known = digit || key == Qt::Key_Backspace || key == Qt::Key_Return ||
               key == Qt::Key_Enter || key == Qt::Key_Escape;
  if (!known || (e->modifiers() &
                 (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
    return false;
  }
  if (!press) {
    return true;
  }
  if (digit) {
    if (!e->isAutoRepeat() && entry_.size() < kMaxDigits) {
      // No leading zeros: "0" then "7" is 7.
      QString next = entry_ == "0" ? QString() : entry_;
      setEntry(next + QChar('0' + (key - Qt::Key_0)));
    }
  } else if (key == Qt::Key_Backspace) {
    setEntry(entry_.left(entry_.size() - 1));
  } else if (key == Qt::Key_Escape) {
    setEntry({});
  } else if (!e->isAutoRepeat()) { // Enter
    record(false);
  }
  return true;
}

void MoveCountPage::record(bool dnf) {
  if (!feed_.current()) {
    emit message(tr("Wait for the scramble before recording"));
    return;
  }
  if (!dnf && entry_.isEmpty()) {
    emit message(tr("Type the move count first"));
    return;
  }
  MoveCountAttempt a;
  a.moves = std::clamp(entry_.toInt(), 0, kMaxMoveCount);
  a.dnf = dnf;
  a.scramble = ss(feed_.current()->text);
  a.timestamp = unixNow();
  session().attempts.push_back(a);
  save();
  refreshAttempts();
  entry_.clear();
  feed_.advance(); // on to the next scramble (also shows the empty entry)
  showEntry();
}

void MoveCountPage::skip() {
  setEntry({});
  feed_.advance();
}

void MoveCountPage::refreshSessionMenu() {
  sessionMenu_->clear();
  auto *group = new QActionGroup(sessionMenu_);
  for (std::size_t i = 0; i < store_.sessions().size(); ++i) {
    const MoveCountSession &s = store_.sessions()[i];
    QAction *a = sessionMenu_->addAction(
        tr("%1 (%2)").arg(qs(s.name)).arg(s.attempts.size()), this,
        [this, i] { selectSession(i); });
    a->setCheckable(true);
    a->setChecked(i == store_.currentIndex());
    group->addAction(a);
  }
  sessionMenu_->addSeparator();
  sessionMenu_->addAction(tr("New session…"), this, &MoveCountPage::addSession);
  sessionMenu_->addAction(tr("Rename…"), this, &MoveCountPage::renameSession);
  sessionMenu_->addAction(tr("Delete…"), this, &MoveCountPage::deleteSession);
}

void MoveCountPage::selectSession(std::size_t index) {
  store_.setCurrentIndex(index);
  sessionButton_->setText(tr("Session: %1").arg(qs(session().name)));
  stepLabel_->setText(qs(session().name));
  typeBox_->setCurrentType(qs(session().scrambleType));
  refreshAttempts();
  save();
}

void MoveCountPage::refreshAttempts() {
  const auto &attempts = session().attempts;
  std::vector<Solve> solves = attemptsAsSolves(attempts);
  auto ao5 = rollingAverages(solves, 5);
  auto ao12 = rollingAverages(solves, 12);
  table_->setRowCount(static_cast<int>(attempts.size()));
  for (std::size_t i = 0; i < attempts.size(); ++i) {
    int row = static_cast<int>(attempts.size() - 1 - i);
    auto set = [&](int col, const QString &text) {
      auto *item = new QTableWidgetItem(text);
      item->setData(Qt::UserRole, static_cast<int>(i));
      if (col > 0) {
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      }
      table_->setItem(row, col, item);
    };
    set(0, QString::number(i + 1));
    set(1, qs(formatAttempt(attempts[i])));
    set(2, movesText(ao5[i]));
    set(3, movesText(ao12[i]));
    QString tip = qs(attempts[i].scramble);
    if (!attempts[i].comment.empty()) {
      tip = qs(attempts[i].comment) + "\n" + tip;
    }
    table_->item(row, 1)->setToolTip(tip);
  }
  SessionSummary sum = summarize(solves);
  QString html =
      "<table cellspacing='4' width='100%'><tr><th></th><th align='right'>" +
      tr("current") + "</th><th align='right'>" + tr("best") + "</th></tr>";
  auto row = [&](const QString &name, const QString &cur, const QString &best) {
    html += QString("<tr><td>%1</td><td align='right'>%2</td><td "
                    "align='right'><b>%3</b></td></tr>")
                .arg(name, cur, best);
  };
  row(tr("single"),
      attempts.empty() ? QString("-") : qs(formatAttempt(attempts.back())),
      movesText(sum.best, 0));
  row("mo3", movesText(currentMean(solves, 3)), movesText(bestMean(solves, 3)));
  row("ao5", movesText(currentAverage(solves, 5)),
      movesText(bestAverage(solves, 5)));
  row("ao12", movesText(currentAverage(solves, 12)),
      movesText(bestAverage(solves, 12)));
  row("ao100", movesText(currentAverage(solves, 100)),
      movesText(bestAverage(solves, 100)));
  html += "</table>";
  html += tr("<p>Attempts: %1%2 &nbsp; Mean: %3</p>")
              .arg(sum.count)
              .arg(sum.dnfs ? QString(" (%1 DNF)").arg(sum.dnfs) : QString())
              .arg(movesText(sum.mean));
  stats_->setText(html);
  chart_->setSolves(solves);
  deleteButton_->setEnabled(!attempts.empty());
  if (attempts.empty()) {
    lastLabel_->clear();
  } else {
    const MoveCountAttempt &last = attempts.back();
    lastLabel_->setText(last.dnf ? tr("Last: %1").arg(qs(formatAttempt(last)))
                        : last.moves == 1
                            ? tr("Last: 1 move")
                            : tr("Last: %1 moves").arg(last.moves));
  }
}

void MoveCountPage::deleteAttempt(int index) {
  auto &attempts = session().attempts;
  if (index < 0 || index >= static_cast<int>(attempts.size())) {
    return;
  }
  attempts.erase(attempts.begin() + index);
  save();
  refreshAttempts();
}

void MoveCountPage::showAttemptMenu(const QPoint &pos) {
  auto *item = table_->itemAt(pos);
  if (!item) {
    return;
  }
  int index = item->data(Qt::UserRole).toInt();
  auto &attempts = session().attempts;
  if (index < 0 || index >= static_cast<int>(attempts.size())) {
    return;
  }
  MoveCountAttempt &a = attempts[static_cast<std::size_t>(index)];
  QMenu menu(this);
  auto *edit = menu.addAction(tr("Edit move count…"));
  auto *dnf = menu.addAction("DNF");
  dnf->setCheckable(true);
  dnf->setChecked(a.dnf);
  menu.addSeparator();
  auto *comment = menu.addAction(tr("Comment…"));
  auto *copy = menu.addAction(tr("Copy scramble"));
  menu.addSeparator();
  auto *del = menu.addAction(tr("Delete"));
  QAction *chosen = menu.exec(table_->viewport()->mapToGlobal(pos));
  if (!chosen) {
    return;
  }
  if (chosen == edit) {
    bool ok = false;
    int moves = QInputDialog::getInt(this, tr("Edit move count"), tr("Moves:"),
                                     a.moves, 0, kMaxMoveCount, 1, &ok);
    if (!ok) {
      return;
    }
    a.moves = moves;
  } else if (chosen == dnf) {
    a.dnf = !a.dnf;
  } else if (chosen == comment) {
    bool ok = false;
    QString text = QInputDialog::getText(this, tr("Comment"), tr("Comment:"),
                                         QLineEdit::Normal, qs(a.comment), &ok);
    if (!ok) {
      return;
    }
    a.comment = ss(text);
  } else if (chosen == copy) {
    QApplication::clipboard()->setText(qs(a.scramble));
    return;
  } else if (chosen == del) {
    deleteAttempt(index);
    return;
  }
  save();
  refreshAttempts();
}

void MoveCountPage::addSession() {
  bool ok = false;
  QString name = QInputDialog::getText(
      this, tr("New session"), tr("Name (e.g. the step, such as EO Cross):"),
      QLineEdit::Normal, tr("Session %1").arg(store_.sessions().size() + 1),
      &ok);
  if (!ok || name.trimmed().isEmpty()) {
    return;
  }
  store_.addSession(ss(name.trimmed())).scrambleType =
      ss(typeBox_->currentType());
  selectSession(store_.currentIndex());
}

void MoveCountPage::renameSession() {
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

void MoveCountPage::deleteSession() {
  auto answer =
      QMessageBox::question(this, tr("Delete session"),
                            tr("Delete session \"%1\" and its %2 move counts?")
                                .arg(qs(session().name))
                                .arg(session().attempts.size()));
  if (answer != QMessageBox::Yes) {
    return;
  }
  store_.removeSession(store_.currentIndex());
  selectSession(store_.currentIndex());
}

void MoveCountPage::save() {
  QString error;
  if (!store_.save(&error)) {
    emit message(tr("Could not save move counts: %1").arg(error));
  }
}

} // namespace vcube
