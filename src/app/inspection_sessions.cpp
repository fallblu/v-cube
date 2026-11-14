#include "app/inspection_page.hpp"
#include "app/inspection_private.hpp"
#include "app/page_parts.hpp"

#include "app/flow_layout.hpp"
#include "app/session_panel.hpp"
#include "app/ui_state.hpp"

#include "io/session_store.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <string_view>

namespace vcube {

namespace {

QString movesText(StatValue v, int decimals = 2) {
  return QString::fromStdString(formatMoves(v, decimals));
}

} // namespace

void InspectionPage::refreshSessionMenu() {
  std::vector<SessionPanel::MenuSession> list;
  for (const InspectionSession &s : store_.sessions()) {
    list.push_back({qs(s.name), s.attempts.size()});
  }
  panel_->fillSessionMenu(list, store_.currentIndex());
}

void InspectionPage::selectSession(std::size_t index) {
  if (isBusy()) {
    return;
  }
  store_.setCurrentIndex(index);
  deleted_.reset(); // undo is for the session it was deleted from
  panel_->setSessionName(qs(session().name));
  showDrill();
  flow_.next();
  shownMoves_.reset();
  scramble_.reset();
  cubeView_->clearMarks();
  showPhase();
  showScramble();
  refreshAttempts();
  backfillOptima();
  save();
}

void InspectionPage::refreshAttempts() {
  const auto &attempts = session().attempts;
  bool plan = planning();
  std::vector<AttemptRow> rows;
  rows.reserve(attempts.size());
  for (std::size_t i = attempts.size(); i-- > 0;) {
    const InspectionAttempt &a = attempts[i];
    AttemptRow row;
    row.index = static_cast<int>(i);
    row.cells.resize(4);
    auto set = [&](int col, const QString &text, const QColor &colour = {}) {
      AttemptCell &cell = row.cells[static_cast<std::size_t>(col)];
      cell.text = text;
      cell.colour = colour;
      cell.alignRight = col > 0;
    };
    set(0, QString::number(i + 1));
    QStringList tip;
    if (!a.comment.empty()) {
      tip << qs(a.comment);
    }
    tip << tr("Scramble: %1").arg(qs(a.scramble));
    if (plan) {
      set(1,
          a.dnf ? QString("DNF(%1)").arg(a.moves) : QString::number(a.moves));
      set(2, a.optimal ? QString::number(*a.optimal) : QString("…"),
          a.optimal && !a.dnf && a.moves <= *a.optimal ? kGood : QColor());
      set(3, secondsText(a.ms));
      if (!a.solution.empty()) {
        tip << tr("Solution: %1").arg(qs(a.solution));
      }
      if (a.solvedIn) {
        tip << tr("Done held %1 top, %2 front")
                   .arg(colourName(a.solvedIn->top).toLower(),
                        colourName(a.solvedIn->front).toLower());
      }
      if (a.pairSolved) {
        tip << (*a.pairSolved
                    ? tr("Pair solved in %1 more moves").arg(a.pairMoves)
                    : tr("Pair not solved"));
      }
    } else {
      for (int axis = 0; axis < 2; ++axis) {
        auto k = static_cast<std::size_t>(axis);
        if (a.correct[k] < 0) {
          set(1 + axis, "-");
        } else if (a.answer[k] == a.correct[k]) {
          set(1 + axis, QString::number(a.answer[k]), kGood);
        } else {
          set(1 + axis, tr("%1 (%2)").arg(a.answer[k]).arg(a.correct[k]), kBad);
        }
      }
      set(3, QString::fromStdString(formatTime(a.ms)));
    }
    // The tooltip is on the number and the first value only.
    row.cells[0].tip = tip.join("\n");
    row.cells[1].tip = tip.join("\n");
    rows.push_back(std::move(row));
  }
  panel_->setRows(std::move(rows));
  if (plan) {
    showPlanStats();
  } else {
    showEdgesStats();
  }
  showChart();
  updateControls();
}

void InspectionPage::showPlanStats() {
  const auto &attempts = session().attempts;
  std::vector<Solve> solves = planAsSolves(attempts);
  static constexpr std::string_view kStats[] = {"single", "mo3", "ao5", "ao12"};
  SessionStats stats = sessionStats(solves, kStats);
  const SessionSummary &sum = stats.summary;
  // A single is a whole number of moves; the means and averages have two
  // decimals.
  QString html = statsTableHtml(
      stats,
      [](const std::string &id) {
        return id == "single" ? tr("single") : qs(id);
      },
      [](const std::string &id, StatValue v) {
        return movesText(v, id == "single" ? 0 : 2);
      });
  PlanSummary all = summarizePlan(attempts);
  html += tr("<p>Attempts: %1%2 &nbsp; Mean: %3</p>")
              .arg(attempts.size())
              .arg(sum.dnfs ? QString(" (%1 DNF)").arg(sum.dnfs) : QString())
              .arg(movesText(sum.mean));
  auto overText = [](const PlanSummary &p) {
    return p.meanOver() ? QString("+%1").arg(*p.meanOver(), 0, 'f', 2)
                        : QString("-");
  };
  auto rateText = [](const PlanSummary &p) {
    return p.optimalRate()
               ? QString("%1%").arg(std::lround(*p.optimalRate() * 100))
               : QString("-");
  };
  if (all.compared > 0) {
    html += tr("<p>Over optimal: %1 &nbsp; At optimal: %2</p>")
                .arg(overText(all), rateText(all));
  }
  if (!attempts.empty()) {
    html += tr("<p>Mean inspection: %1</p>")
                .arg(secondsText(all.inspectionMs /
                                 static_cast<std::int64_t>(attempts.size())));
  }
  if (trackedPair()) {
    int tracked = 0, solved = 0, pairMoves = 0;
    for (const auto &a : attempts) {
      if (a.pairSolved) {
        ++tracked;
        if (*a.pairSolved) {
          ++solved;
          pairMoves += a.pairMoves;
        }
      }
    }
    if (tracked > 0) {
      html +=
          tr("<p>Pair solved: %1 of %2%3</p>")
              .arg(solved)
              .arg(tracked)
              .arg(solved ? tr(", in %1 moves on average")
                                .arg(static_cast<double>(pairMoves) / solved, 0,
                                     'f', 1)
                          : QString());
    }
  }
  // How the attempts went by how hard the scramble was and by how long
  // inspection took.
  auto breakdown =
      [&](const QString &title,
          const std::vector<std::pair<QString, PlanSummary>> &rows) {
        if (rows.size() < 2) {
          return; // nothing to compare
        }
        html += "<p><b>" + title +
                "</b></p><table cellspacing='3' width='100%'><tr><th></th>"
                "<th align='right'>" +
                tr("tries") + "</th><th align='right'>" + tr("at opt.") +
                "</th><th align='right'>" + tr("over") +
                "</th><th align='right'>" + "DNF</th></tr>";
        for (const auto &[name, p] : rows) {
          html += QString("<tr><td>%1</td><td align='right'>%2</td><td "
                          "align='right'>%3</td><td align='right'>%4</td><td "
                          "align='right'>%5</td></tr>")
                      .arg(name)
                      .arg(p.attempts)
                      .arg(rateText(p), overText(p))
                      .arg(p.dnfs);
        }
        html += "</table>";
      };
  std::vector<std::pair<QString, PlanSummary>> byOptimal;
  for (const auto &[length, p] : planByOptimal(attempts)) {
    byOptimal.emplace_back(
        length == 1 ? tr("1 move") : tr("%1 moves").arg(length), p);
  }
  breakdown(tr("By optimal length"), byOptimal);
  std::vector<std::pair<QString, PlanSummary>> byTime;
  for (const InspectionRange &r : planByInspection(attempts)) {
    byTime.emplace_back(r.to < 0 ? tr("%1 s and more").arg(r.from)
                                 : tr("%1–%2 s").arg(r.from).arg(r.to),
                        r.summary);
  }
  breakdown(tr("By inspection time used"), byTime);
  panel_->setStatsHtml(html);
}

void InspectionPage::showChart() {
  const auto &attempts = session().attempts;
  if (!planning()) {
    chart_->setEmptyText(tr("Times will be charted here"));
    chart_->setSolves(edgesAsSolves(attempts));
    return;
  }
  switch (chartBox_->currentIndex()) {
  case 0:
    chart_->setEmptyText(tr("Moves over optimal will be charted here"));
    chart_->setSolves(overOptimalAsSolves(attempts));
    break;
  case 1:
    chart_->setEmptyText(tr("The share of optimal attempts (in %) will be "
                            "charted here"));
    chart_->setSolves(optimalRateAsSolves(attempts, 12));
    break;
  default:
    chart_->setEmptyText(tr("Move counts will be charted here"));
    chart_->setSolves(planAsSolves(attempts));
    break;
  }
}

void InspectionPage::showEdgesStats() {
  const auto &attempts = session().attempts;
  std::vector<Solve> solves = edgesAsSolves(attempts);
  int right = 0;
  std::int64_t rightMs = 0;
  for (const auto &a : attempts) {
    if (a.right()) {
      ++right;
      rightMs += a.ms;
    }
  }
  static constexpr std::string_view kStats[] = {"ao5", "ao12"};
  QString html = statsTableHtml(
      sessionStats(solves, kStats),
      [](const std::string &id) { return qs(id); },
      [](const std::string &, StatValue v) {
        return QString::fromStdString(formatTime(v));
      });
  if (attempts.empty()) {
    html += tr("<p>No attempts yet.</p>");
  } else {
    html += tr("<p>Attempts: %1 &nbsp; Right: %2%</p>")
                .arg(attempts.size())
                .arg(100 * right / static_cast<int>(attempts.size()));
    if (right > 0) {
      html += tr("<p>Mean time when right: %1</p>")
                  .arg(QString::fromStdString(formatTime(rightMs / right)));
    }
  }
  html +=
      tr("<p style='color:gray'>A wrong count is a DNF in the averages.</p>");
  panel_->setStatsHtml(html);
}

void InspectionPage::deleteAttempt(int index) {
  auto &attempts = session().attempts;
  if (index < 0 || index >= static_cast<int>(attempts.size())) {
    return;
  }
  // Kept for Ctrl+Z, until the session changes.
  deleted_ = {index, attempts[static_cast<std::size_t>(index)]};
  attempts.erase(attempts.begin() + index);
  save();
  refreshAttempts();
  updateControls();
  emit message(tr("Attempt deleted (Ctrl+Z puts it back)"));
}

void InspectionPage::undoDelete() {
  if (!deleted_) {
    return;
  }
  auto &attempts = session().attempts;
  int at = std::clamp(deleted_->first, 0, static_cast<int>(attempts.size()));
  attempts.insert(attempts.begin() + at, deleted_->second);
  deleted_.reset();
  save();
  refreshAttempts();
  updateControls();
  emit message(tr("Attempt put back"));
}

void InspectionPage::showAttemptMenu(int index, const QPoint &globalPos) {
  const auto &attempts = session().attempts;
  if (index < 0 || index >= static_cast<int>(attempts.size())) {
    return;
  }
  const InspectionAttempt &a = attempts[static_cast<std::size_t>(index)];
  ActionMenu menu(this);
  if (planning()) {
    menu.add(tr("Review…"), [this, index] { requestReview(index); });
    menu.addSeparator();
  }
  AttemptMenuActions actions;
  actions.comment = [this, index] {
    auto &list = session().attempts;
    if (index >= static_cast<int>(list.size())) {
      return;
    }
    InspectionAttempt &attempt = list[static_cast<std::size_t>(index)];
    if (std::optional<QString> text =
            askComment(this, tr("Comment:"), qs(attempt.comment))) {
      attempt.comment = ss(*text);
      save();
      refreshAttempts();
    }
  };
  actions.scramble = qs(a.scramble);
  if (planning()) {
    actions.solution = qs(a.solution); // none: no Copy solution
  }
  actions.remove = [this, index] { deleteAttempt(index); };
  addAttemptActions(menu, actions);
  menu.run(globalPos);
}

void InspectionPage::addSession() {
  // What the session practises is chosen with it, in one form: it is
  // fixed once the session has attempts.
  const InspectionSession from = session();
  QDialog dialog(this);
  dialog.setWindowTitle(tr("New session"));
  auto *form = new QFormLayout(&dialog);
  auto *drill = new QComboBox(&dialog);
  drill->addItem(tr("Plan a step"));
  drill->addItem(tr("Count bad edges"));
  drill->setCurrentIndex(from.drill == InspectionDrill::Plan ? 0 : 1);
  auto *step = new QComboBox(&dialog);
  for (SolveStep st : planSteps()) {
    step->addItem(qs(std::string(stepName(st))));
  }
  step->setCurrentIndex(stepBox_->currentIndex());
  auto *pair = new QComboBox(&dialog);
  pair->addItem(tr("None"));
  for (Slot slot : kSlots4) {
    pair->addItem(tr("%1 pair (%2)")
                      .arg(qs(std::string(slotName(slot))), slotWords(slot)));
  }
  pair->setCurrentIndex(pairBox_->currentIndex());
  auto *axes = new QComboBox(&dialog);
  axes->addItem(tr("F/B"));
  axes->addItem(tr("R/L"));
  axes->addItem(tr("F/B, then R/L"));
  axes->setCurrentIndex(static_cast<int>(from.axes));
  auto *name = new QLineEdit(&dialog);
  form->addRow(tr("Drill:"), drill);
  form->addRow(tr("Step:"), step);
  form->addRow(tr("Then:"), pair);
  form->addRow(tr("Axes:"), axes);
  form->addRow(tr("Name:"), name);
  form->addRow(
      helpLabel(tr("The drill and what it is for can't be changed once the "
                   "session has attempts. The inspection time limit can."),
                &dialog));
  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  // The name follows the choices until it is typed over.
  auto suggested = [=] {
    if (drill->currentIndex() == 1) {
      return tr("Bad edges");
    }
    QString text = step->currentText();
    if (planSteps()[static_cast<std::size_t>(step->currentIndex())] ==
            SolveStep::EOCross &&
        pair->currentIndex() > 0) {
      text +=
          " + " +
          qs(std::string(slotName(
              kSlots4[static_cast<std::size_t>(pair->currentIndex() - 1)])));
    }
    return text;
  };
  auto refresh = [=] {
    bool plan = drill->currentIndex() == 0;
    bool cross =
        plan && planSteps()[static_cast<std::size_t>(step->currentIndex())] ==
                    SolveStep::EOCross;
    form->setRowVisible(step, plan);
    form->setRowVisible(pair, cross);
    form->setRowVisible(axes, !plan);
    if (!name->isModified()) {
      name->setText(suggested());
    }
  };
  for (QComboBox *box : {drill, step, pair, axes}) {
    connect(box, &QComboBox::currentIndexChanged, &dialog, refresh);
  }
  refresh();
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  QString chosen = name->text().trimmed();
  if (chosen.isEmpty()) {
    chosen = suggested();
  }
  InspectionSession &s = store_.addSession(ss(chosen));
  s.drill = drill->currentIndex() == 0 ? InspectionDrill::Plan
                                       : InspectionDrill::Edges;
  s.step = planSteps()[static_cast<std::size_t>(step->currentIndex())];
  s.axes = static_cast<EdgeAxes>(axes->currentIndex());
  if (s.drill == InspectionDrill::Plan && s.step == SolveStep::EOCross &&
      pair->currentIndex() > 0) {
    s.pair = kSlots4[static_cast<std::size_t>(pair->currentIndex() - 1)];
  }
  s.limitSeconds = from.limitSeconds;
  s.wca = from.wca;
  if (s.drill == InspectionDrill::Plan && s.limitSeconds == 0 &&
      from.drill != InspectionDrill::Plan) {
    // Edges sessions have no limit: a Plan session starts with WCA
    // inspection.
    s.limitSeconds = 15;
    s.wca = true;
  }
  selectSession(store_.currentIndex());
}

void InspectionPage::renameSession() {
  if (renameSessionAsked(this, session())) {
    selectSession(store_.currentIndex());
  }
}

void InspectionPage::deleteSession() {
  if (deleteSessionAsked(this, store_, session().attempts.size(),
                         tr("attempts"))) {
    selectSession(store_.currentIndex());
  }
}

void InspectionPage::save() {
  saveInBackground(store_, this, [this](const QString &error) {
    emit message(tr("Could not save inspection sessions: %1").arg(error));
  });
}

} // namespace vcube
