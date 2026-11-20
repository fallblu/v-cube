#include "app/timer_page.hpp"

#include "app/review_dialog.hpp"
#include "app/ui_text.hpp"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QMenu>
#include <QMessageBox>
#include <QtConcurrent/QtConcurrentRun>

namespace vcube {

namespace {

// The table's columns. Those after ao12 are for virtual sessions.
enum Column : int {
  kNumber,
  kTime,
  kAo5,
  kAo12,
  kMoves,
  kTps,
  kCross,
  kF2l,
  kLl,
  kColumns
};

SolveSplits computeSplits(const Solve &s, bool simplified) {
  ParseResult scramble = parseAlg(s.scramble);
  ParseResult moves = parseAlg(s.moves);
  if (!scramble || !moves) {
    return {};
  }
  FaceletCube start;
  start.apply(scramble.alg);
  return solveSplits(start, moves.alg, s.moveTimes, s.timeMs, simplified);
}

} // namespace

void TimerPage::refreshSessionMenu() {
  std::vector<SessionPanel::MenuSession> list;
  for (const Session &s : store_.sessions()) {
    list.push_back({qs(s.name), s.solves.size()});
  }
  panel_->fillSessionMenu(list, store_.currentIndex());
  QMenu *menu = panel_->menu();
  menu->addSeparator();
  menu->addAction(tr("Import from cstimer…"), this, &TimerPage::importCstimer);
  menu->addAction(tr("Export to cstimer…"), this, &TimerPage::exportCstimer);
  // No session changes in the middle of a solve.
  panel_->setMenuEnabled(!isBusy());
}

void TimerPage::selectSession(std::size_t index) {
  if (isBusy()) {
    return;
  }
  bool changed = index != store_.currentIndex();
  store_.setCurrentIndex(index);
  if (changed) {
    deleted_.forget(); // undo is for the session it was deleted from
    justSolved_ = false;
    solveRows_.clear();
  }
  bool wasFinished = flow_.finished();
  flow_.nextScramble();
  panel_->setSessionName(qs(session().name));
  configureFlow(); // the flow is for the session's cube
  showCubeKind();
  typeBox_->setCurrentType(qs(session().scrambleType));
  if (wasFinished) {
    feed_.advance(); // the scramble shown was solved (as a DNF) already
  } else {
    showScramble(); // onto the session's cube
  }
  showLastResult();
  hintLabel_->clear();
  refreshAttempts();
  save();
}

std::string TimerPage::splitKey(const Solve &s) {
  return s.scramble + '\x1f' + s.moves + '\x1f' + std::to_string(s.timeMs) +
         '\x1f' + std::to_string(s.moveTimes.size());
}

bool TimerPage::SolveRow::isOf(const Solve &s) const {
  return moves == s.moves && timeMs == s.timeMs &&
         moveTimes == s.moveTimes.size() && scramble == s.scramble;
}

const SolveSplits *TimerPage::splitsOf(const Solve &s) const {
  auto it = splits_.find(splitKey(s));
  return it == splits_.end() ? nullptr : &it->second;
}

void TimerPage::requestSplits() {
  if (splitsWatcher_.isRunning()) {
    return; // asked again when it finishes
  }
  std::vector<Solve> missing;
  const auto &solves = session().solves;
  for (std::size_t i = 0; i < solves.size(); ++i) {
    const Solve &s = solves[i];
    if (s.moves.empty()) {
      continue;
    }
    // A row that has its splits says so without a key being built.
    bool known =
        i < solveRows_.size() && solveRows_[i].splits && solveRows_[i].isOf(s);
    if (!known && !splits_.contains(splitKey(s))) {
      missing.push_back(s);
    }
  }
  if (missing.empty()) {
    return;
  }
  bool simplified = settings_.simplifiedCounts;
  splitsWatcher_.setFuture(QtConcurrent::run([missing, simplified] {
    SplitResults out;
    for (const Solve &s : missing) {
      out.emplace_back(splitKey(s), computeSplits(s, simplified));
    }
    return out;
  }));
}

void TimerPage::refreshAttempts() {
  const auto &solves = session().solves;
  int d = settings_.decimals;
  std::int64_t unit = statUnit(d);
  auto ao5 = rollingAverages(solves, 5, unit);
  auto ao12 = rollingAverages(solves, 12, unit);
  bool virt = onVirtualCube();
  bool missing = false;
  std::vector<StatValue> crossSeries(solves.size()), f2lSeries(solves.size()),
      llSeries(solves.size());
  std::vector<AttemptRow> rows;
  rows.reserve(solves.size());
  solveRows_.resize(solves.size());
  for (std::size_t i = solves.size(); i-- > 0;) {
    const Solve &s = solves[i];
    AttemptRow row;
    row.index = static_cast<int>(i);
    row.cells.resize(kColumns);
    auto set = [&](int col, const QString &text) {
      AttemptCell &cell = row.cells[static_cast<std::size_t>(col)];
      cell.text = text;
      cell.alignRight = col > 0;
    };
    // A solve with a comment is marked.
    set(kNumber, s.comment.empty() ? QString::number(i + 1)
                                   : QString("%1 *").arg(i + 1));
    set(kTime, qs(formatSolve(s, d)));
    set(kAo5, timeText(ao5[i], d));
    set(kAo12, timeText(ao12[i], d));
    QString moves, rate, cross, f2l, ll;
    if (virt && !s.moves.empty()) {
      SolveRow &kept = solveRows_[i];
      if (!kept.isOf(s)) {
        kept = {
            s.scramble,
            s.moves,
            s.timeMs,
            s.moveTimes.size(),
            solutionHtm(parseAlgOrEmpty(s.moves), settings_.simplifiedCounts),
            nullptr};
      }
      if (!kept.splits) {
        kept.splits = splitsOf(s);
      }
      const int htm = kept.htm;
      moves = QString::number(htm);
      if (s.penalty != Penalty::Dnf && s.timeMs > 0) {
        rate = QString::number(tps(htm, s.timeMs), 'f', 2);
      }
      if (const SolveSplits *sp = kept.splits) {
        auto text = [&](const std::optional<std::int64_t> &ms) {
          return ms ? timeText(*ms, d) : QString();
        };
        cross = text(sp->crossMs);
        f2l = text(sp->f2lMs);
        ll = text(sp->llMs);
        if (sp->crossMs) {
          crossSeries[i] = *sp->crossMs;
        }
        if (sp->f2lMs) {
          f2lSeries[i] = *sp->f2lMs;
        }
        if (sp->llMs) {
          llSeries[i] = *sp->llMs;
        }
      } else {
        missing = true;
        cross = "…";
      }
    }
    set(kMoves, moves);
    set(kTps, rate);
    set(kCross, cross);
    set(kF2l, f2l);
    set(kLl, ll);
    rows.push_back(std::move(row));
  }
  panel_->setRows(std::move(rows));
  if (missing) {
    requestSplits();
  }
  showStats(
      solves, unit, tr("time"),
      [d](const std::string &, StatValue v) { return timeText(v, d); },
      solves.size());
  if (unit != kWcaUnit) { // the chart's averages are in WCA units
    ao5 = rollingAverages(solves, 5);
    ao12 = rollingAverages(solves, 12);
  }
  chart_->setSolves(solves, std::move(ao5), std::move(ao12));
  if (virt) {
    chart_->setSeries({{tr("cross"), Role::Series3, crossSeries},
                       {tr("F2L"), Role::Series1, f2lSeries},
                       {tr("LL"), Role::Series2, llSeries}});
  } else {
    chart_->setSeries({});
  }
  updateControls();
}

void TimerPage::showStat(const QString &link) {
  const auto &solves = session().solves;
  int d = settings_.decimals;
  StatLinkView view;
  view.unit = statUnit(d);
  view.format = [d](StatValue v) { return timeText(v, d); };
  view.result = [&solves, d](std::size_t i) {
    return qs(formatSolve(solves[i], d));
  };
  view.number = [](std::size_t i) { return i + 1; };
  view.details = [this](std::size_t i) { showDetails(static_cast<int>(i)); };
  openStatLink(this, link, solves, view);
}

void TimerPage::setLastPenalty(Penalty p) {
  if (session().solves.empty() || isBusy()) {
    return;
  }
  Solve &s = session().solves.back();
  s.penalty = s.penalty == p ? Penalty::None : p;
  display_->showTime(s.timeMs, s.penalty);
  saveAndRefresh();
}

void TimerPage::deleteAttempt(int index) {
  auto &solves = session().solves;
  if (isBusy() || index < 0 ||
      !deleted_.erase(solves, static_cast<std::size_t>(index))) {
    return;
  }
  justSolved_ = false;
  saveAndRefresh();
  showLastResult();
  emit message(tr("Deleted solve #%1 (Ctrl+Z puts it back)").arg(index + 1));
}

void TimerPage::undoDelete() {
  if (!deleted_.has() || isBusy()) {
    return;
  }
  std::optional<std::size_t> at = deleted_.restore(session().solves);
  justSolved_ = false;
  emit message(tr("Put back solve #%1").arg(*at + 1));
  saveAndRefresh();
  showLastResult();
}

void TimerPage::commentSolve(int index) {
  auto &solves = session().solves;
  if (isBusy() || index < 0 || index >= static_cast<int>(solves.size())) {
    return;
  }
  Solve &s = solves[static_cast<std::size_t>(index)];
  if (std::optional<QString> text = askComment(
          this, tr("Comment on solve #%1:").arg(index + 1), qs(s.comment))) {
    s.comment = ss(*text);
    saveAndRefresh();
  }
}

void TimerPage::review(int index) {
  const auto &solves = session().solves;
  if (isBusy() || index < 0 || index >= static_cast<int>(solves.size())) {
    return;
  }
  const Solve &s = solves[static_cast<std::size_t>(index)];
  if (s.moves.empty()) {
    return;
  }
  ReviewedSolve solve;
  solve.title = tr("Solve #%1").arg(index + 1);
  solve.scramble = parseAlgOrEmpty(s.scramble);
  solve.solution = parseAlgOrEmpty(s.moves);
  solve.dnf = s.penalty == Penalty::Dnf;
  solve.times = s.moveTimes;
  solve.totalMs = s.timeMs;
  ReviewDialog dialog(solve, settings_, &library_, this);
  dialog.exec();
}

void TimerPage::showDetails(int index) {
  const auto &solves = session().solves;
  if (index < 0 || index >= static_cast<int>(solves.size())) {
    return;
  }
  const Solve &s = solves[static_cast<std::size_t>(index)];
  int d = settings_.decimals;
  QList<DetailRow> rows;
  rows.append({tr("Result"), qs(formatSolve(s, d))});
  rows.append({tr("Date"), dateTimeText(s.timestamp)});
  rows.append({tr("Scramble"), qs(s.scramble), true});
  if (!s.moves.empty()) {
    rows.append({tr("Solution"), qs(s.moves), true});
    int htm = solutionHtm(parseAlgOrEmpty(s.moves), settings_.simplifiedCounts);
    rows.append({tr("Moves"), QString::number(htm)});
    if (s.timeMs > 0) {
      rows.append({tr("TPS"), QString::number(tps(htm, s.timeMs), 'f', 2)});
    }
    SolveSplits splits = computeSplits(s, settings_.simplifiedCounts);
    QStringList phases;
    for (const SolvePhase &p : splits.phases) {
      QString line = p.htm == 1 ? tr("%1: 1 move").arg(qs(p.name))
                                : tr("%1: %2 moves").arg(qs(p.name)).arg(p.htm);
      if (p.ms > 0) {
        line += tr(" in %1").arg(timeText(p.ms, d));
      }
      phases << line;
    }
    if (!phases.isEmpty()) {
      rows.append({tr("Splits"), phases.join("\n")});
    }
  }
  rows.append({tr("Comment"), qs(s.comment)});
  showSolveDetails(
      this, tr("Solve #%1").arg(index + 1), rows,
      s.moves.empty() ? std::function<void()>{}
                      : [this, index] { review(index); });
}

void TimerPage::showAttemptMenu(int index, const QPoint &globalPos) {
  const auto &solves = session().solves;
  if (isBusy() || index < 0 || index >= static_cast<int>(solves.size())) {
    return; // nothing changes in the middle of a solve
  }
  const Solve &s = solves[static_cast<std::size_t>(index)];
  ActionMenu menu(this);
  auto penalty = [this, index](Penalty p) {
    return [this, index, p] {
      auto &list = session().solves;
      if (index >= static_cast<int>(list.size())) {
        return;
      }
      list[static_cast<std::size_t>(index)].penalty = p;
      if (index + 1 == static_cast<int>(list.size())) {
        showLastResult();
      }
      saveAndRefresh();
    };
  };
  for (auto [text, p] : {std::pair{tr("No penalty"), Penalty::None},
                         std::pair{QString("+2"), Penalty::Plus2},
                         std::pair{QString("DNF"), Penalty::Dnf}}) {
    QAction *a = menu.add(text, penalty(p));
    a->setCheckable(true);
    a->setChecked(s.penalty == p);
  }
  menu.addSeparator();
  if (!s.moves.empty()) {
    menu.add(tr("Review…"), [this, index] { review(index); });
  }
  AttemptMenuActions common;
  common.details = [this, index] { showDetails(index); };
  common.comment = [this, index] { commentSolve(index); };
  common.scramble = qs(s.scramble);
  common.solution = qs(s.moves);
  common.remove = [this, index] { deleteAttempt(index); };
  addAttemptActions(menu, common);
  menu.run(globalPos, [this] { return !isBusy(); });
}

void TimerPage::addSession() {
  std::optional<QString> name =
      askSessionName(this, tr("New session"), tr("Name:"),
                     tr("Session %1").arg(store_.sessions().size() + 1));
  if (!name) {
    return;
  }
  CubeKind cube = session().cube;
  Session &s = store_.addSession(ss(*name));
  s.scrambleType = ss(typeBox_->currentType());
  s.cube = cube;
  selectSession(store_.currentIndex());
}

void TimerPage::renameSession() {
  if (!renameSessionAsked(this, session())) {
    return;
  }
  panel_->setSessionName(qs(session().name));
  save();
}

void TimerPage::deleteSession() {
  if (!deleteSessionAsked(this, store_, session().solves.size(),
                          tr("solves"))) {
    return;
  }
  deleted_.forget();
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
    QMessageBox::warning(
        this, tr("Import"),
        tr("Couldn't open %1: %2")
            .arg(QDir::toNativeSeparators(path), f.errorString()));
    return;
  }
  QString error;
  auto imported = sessionsFromCstimerFile(f.readAll(), &error);
  if (!imported) {
    QMessageBox::warning(this, tr("Import"),
                         tr("Couldn't import %1: %2").arg(path, error));
    return;
  }
  std::size_t solves = 0;
  std::size_t count = imported->sessions.size();
  for (auto &s : imported->sessions) {
    solves += s.solves.size();
    s.name += " (cstimer)";
    store_.sessions().push_back(std::move(s));
  }
  QStringList notes;
  if (imported->emptySkipped > 0) {
    notes << plural(imported->emptySkipped, tr("%n empty session left out"),
                    tr("%n empty sessions left out"));
  }
  if (!imported->others.empty()) {
    QStringList names;
    for (const auto &n : imported->others) {
      names << qs(n);
    }
    notes
        << tr("sessions for other puzzles left out: %1").arg(names.join(", "));
  }
  if (count > 0) {
    selectSession(store_.sessions().size() - 1);
  }
  QString text =
      tr("Imported %1 sessions with %2 solves").arg(count).arg(solves);
  if (!notes.isEmpty()) {
    text += " (" + notes.join("; ") + ")";
  }
  emit message(text);
  if (count == 0) {
    QMessageBox::information(this, tr("Import"), text);
  }
}

void TimerPage::exportCstimer() {
  QString path = QFileDialog::getSaveFileName(
      this, tr("Export for cstimer"), "v-cube-sessions.txt",
      tr("cstimer import file (*.txt)"));
  if (path.isEmpty()) {
    return;
  }
  QString error;
  if (!writeFileAtomically(path, sessionsToCstimer(store_.sessions()),
                           &error)) {
    QMessageBox::warning(this, tr("Export"), error);
    return;
  }
  QMessageBox::information(
      this, tr("Export"),
      tr("Exported %1 to %2.\n\nIn cstimer, import it with Data "
         "Import/Export > \"Import session(s) from other timers\", which adds "
         "the sessions to yours. (\"Import from file\" would replace all your "
         "cstimer data with them.)")
          .arg(counted(static_cast<int>(store_.sessions().size()),
                       tr("session"), tr("sessions")))
          .arg(path));
}

void TimerPage::save() {
  saveInBackground(store_, this, [this](const QString &error) {
    emit message(tr("Could not save sessions: %1").arg(error));
  });
}

} // namespace vcube
