#include "app/eo_cross_page.hpp"

#include "app/ui_text.hpp"
#include "io/session_store.hpp"

#include <QFutureWatcher>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTreeWidget>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cstdlib>

namespace vcube {

namespace {

// Optimal solutions worked out per hold, to list a few that differ.
constexpr int kSolutionsPerHold = 12;

const StatusColour kGood{Status::Good};
const StatusColour kWeak{Status::Warning};

QString movesText(int n) {
  return n == 1 ? QObject::tr("1 move") : QObject::tr("%1 moves").arg(n);
}

QString algText(const Alg &alg) { return qs(toString(alg)).toHtmlEscaped(); }

QString coloured(const QColor &colour, const QString &text) {
  return QString("<span style='color:%1'>%2</span>").arg(colour.name(), text);
}

bool hasStep(EoDrill d) {
  return d == EoDrill::Axis || d == EoDrill::Plan || d == EoDrill::Solve;
}

// How far a cube that didn't reach the step got: "EO not done (2 bad edges
// left); 3 of 4 cross edges solved".
QString progressText(const FaceletCube &cube, SolveStep step) {
  StepProgress p = stepProgress(cube);
  if (step == SolveStep::EO) {
    return p.badEdges == 1 ? QObject::tr("1 bad edge left")
                           : QObject::tr("%1 bad edges left").arg(p.badEdges);
  }
  QString eo =
      p.badEdges == 0 ? QObject::tr("EO done")
      : p.badEdges == 1
          ? QObject::tr("EO not done (1 bad edge left)")
          : QObject::tr("EO not done (%1 bad edges left)").arg(p.badEdges);
  QString cross = QObject::tr("%1 of 4 cross edges solved").arg(p.crossEdges);
  if (step == SolveStep::EOLine) {
    cross += p.line ? QObject::tr(", the line among them")
                    : QObject::tr(", not the line (DF and DB)");
  }
  return eo + "; " + cross;
}

} // namespace

// --- analysis ----------------------------------------------------------------

void EoCrossPage::requestAnalysis() {
  auto *watcher = new QFutureWatcher<Analysis>(this);
  connect(watcher, &QFutureWatcher<Analysis>::finished, this, [this, watcher] {
    analysisArrived(watcher->result());
    watcher->deleteLater();
  });
  watcher->setFuture(
      QtConcurrent::run([id = round_.id, scrambled = round_.scrambled,
                         step = roundStep(), holds = allowedHolds()] {
        return Analysis{
            id, solveStepHeld(scrambled, step, holds, kSolutionsPerHold)};
      }));
}

void EoCrossPage::analysisArrived(const Analysis &a) {
  if (a.round != round_.id || round_.analysed) {
    return;
  }
  round_.ranked = a.ranked;
  round_.analysed = true;
}

void EoCrossPage::ensureAnalysed() {
  if (round_.analysed) {
    return;
  }
  round_.ranked = solveStepHeld(round_.scrambled, roundStep(), allowedHolds(),
                                kSolutionsPerHold);
  round_.analysed = true;
}

// --- recording ---------------------------------------------------------------

void EoCrossPage::record() {
  if (round_.recorded || round_.retry) {
    return;
  }
  round_.recorded = true;
  PairAttempt a;
  a.timestamp = unixNow();
  a.ms = round_.ms;
  a.hint = round_.hint;
  const int best =
      round_.ranked.empty() ? 0 : round_.ranked.front().optimum.length;
  switch (drill_) {
  case Drill::Edges:
    a.moves = round_.answers[0];
    a.bestMoves = round_.correct[0];
    for (std::size_t axis = 0; axis < 2; ++axis) {
      if (round_.correct[axis] >= 0) {
        a.excess += std::abs(round_.answers[axis] - round_.correct[axis]);
      }
    }
    break;
  case Drill::Axis:
    a.bestMoves = best;
    a.gaveUp = !round_.picked;
    if (round_.picked) {
      a.moves = round_.ranked[*round_.picked].optimum.length;
      a.excess = a.moves - best;
    }
    break;
  case Drill::Plan:
  case Drill::Solve:
  case Drill::Full:
    a.bestMoves = best;
    a.gaveUp = round_.gaveUp;
    if (!a.gaveUp) {
      a.moves = solutionHtm(round_.moves);
      a.excess = std::max(0, a.moves - best);
    }
    break;
  }
  store_.stats.add(round_.kind, a);
  save();
  refreshTree();
}

void EoCrossPage::save() {
  saveInBackground(store_, this, [this](const QString &error) {
    emit message(tr("Could not save the EO Cross statistics: %1").arg(error));
  });
}

// --- results -----------------------------------------------------------------

QString EoCrossPage::resultHtml() const { return result_->text(); }

void EoCrossPage::showResult() {
  QString html;
  if (phase_ == Phase::Done) {
    switch (drill_) {
    case Drill::Edges:
      html = edgesResultHtml();
      break;
    case Drill::Axis:
      html = axisResultHtml();
      break;
    case Drill::Plan:
    case Drill::Solve:
    case Drill::Full:
      html = solveResultHtml();
      break;
    }
    if (round_.retry) {
      html += QString("<p style='color:%1'>%2</p>")
                  .arg(palette().color(QPalette::PlaceholderText).name(),
                       tr("A retry: not recorded."));
    }
  }
  result_->setText(html);
}

QString EoCrossPage::verdictHtml() const {
  const QString step = stepText(roundStep());
  const int best =
      round_.ranked.empty() ? 0 : round_.ranked.front().optimum.length;
  switch (drill_) {
  case Drill::Edges: {
    bool right = true;
    for (std::size_t axis = 0; axis < 2; ++axis) {
      right = right && (round_.correct[axis] < 0 ||
                        round_.answers[axis] == round_.correct[axis]);
    }
    QString counts = round_.both ? tr("%1 bad edges for F/B, %2 for R/L")
                                       .arg(round_.correct[0])
                                       .arg(round_.correct[1])
                                 : tr("%1 bad edges").arg(round_.correct[0]);
    return right ? coloured(kGood, tr("Right: %1").arg(counts))
                 : coloured(kFailColour, tr("Wrong: %1").arg(counts));
  }
  case Drill::Axis: {
    if (!round_.picked) {
      return coloured(kFailColour, tr("No hold chosen"));
    }
    int over = round_.ranked[*round_.picked].optimum.length - best;
    return over == 0
               ? coloured(
                     kGood,
                     tr("The best hold: %1 in %2").arg(step, movesText(best)))
               : coloured(over >= 2 ? QColor(kFailColour) : QColor(kWeak),
                          tr("Not the best hold: +%1").arg(movesText(over)));
  }
  case Drill::Plan:
  case Drill::Solve:
  case Drill::Full: {
    if (!round_.solvedIn) {
      return coloured(kFailColour, round_.moves.empty()
                                       ? tr("Gave up")
                                       : tr("%1 wasn't done").arg(step));
    }
    int moves = solutionHtm(round_.moves);
    QString done = tr("%1 done in %2").arg(step, movesText(moves));
    int over = std::max(0, moves - best);
    QString against =
        over == 0 ? coloured(kGood, tr("optimal"))
                  : coloured(over >= 3 ? QColor(kFailColour) : QColor(kWeak),
                             tr("+%1 over the optimum").arg(over));
    return done + " · " + against;
  }
  }
  return {};
}

QString EoCrossPage::edgesResultHtml() const {
  QStringList lines;
  for (int axis = 0; axis < 2; ++axis) {
    auto i = static_cast<std::size_t>(axis);
    if (round_.correct[i] < 0) {
      continue;
    }
    bool right = round_.answers[i] == round_.correct[i];
    QString what = axisText(axis, true);
    lines << (right ? "<b>" +
                          coloured(kGood,
                                   tr("right: %1").arg(round_.answers[i])) +
                          "</b> · " + what
                    : "<b>" +
                          coloured(kFailColour, tr("you said %1, it's %2")
                                                    .arg(round_.answers[i])
                                                    .arg(round_.correct[i])) +
                          "</b> · " + what);
  }
  return "<p>" + lines.join("<br>") + "</p><p>" +
         tr("An edge is bad for an axis when it can't be solved without a "
            "quarter turn of one of that axis's two faces.") +
         "</p>";
}

QString EoCrossPage::holdsHtml() const {
  if (round_.ranked.empty()) {
    return {};
  }
  const QString muted = palette().color(QPalette::PlaceholderText).name();
  const SolveStep step = roundStep();
  const bool eoOnly = step == SolveStep::EO;
  std::optional<std::size_t> mine =
      drill_ == Drill::Axis ? round_.picked
      : round_.solvedIn     ? rankOfHold(round_.ranked, *round_.solvedIn, step)
                            : std::nullopt;
  const int best = round_.ranked.front().optimum.length;
  std::vector<Solution> solutions = shownSolutions();
  QString html = "<table align='center' cellspacing='0' cellpadding='3'><tr "
                 "style='color:" +
                 muted + "'><th align='left'>" + tr("Hold") + "</th><th>" +
                 tr("Optimum") + "</th><th align='left'>" +
                 tr("Optimal solutions") + "</th><th></th></tr>";
  std::size_t at = 0;
  for (std::size_t i = 0; i < round_.ranked.size(); ++i) {
    const HeldOptimum &r = round_.ranked[i];
    QStringList algs;
    for (; at < solutions.size() && solutions[at].hold == r.hold; ++at) {
      algs << QString("<span style='color:%1'>%2</span>&nbsp;"
                      "<a href='sol:%3'>%4</a>")
                  .arg(muted)
                  .arg(at + 1)
                  .arg(at)
                  .arg(solutions[at].alg.empty() ? tr("(done)")
                                                 : algText(solutions[at].alg));
    }
    if (r.optimum.more) {
      algs << QString("<span style='color:%1'>…</span>").arg(muted);
    }
    QString hold = stepHoldHtml(r.hold, eoOnly, settings_.scheme);
    QString mark = mine == i ? tr("yours") : QString();
    QString length = r.optimum.length == best
                         ? coloured(kGood, movesText(r.optimum.length))
                         : movesText(r.optimum.length);
    html += QString("<tr><td>%1</td><td align='right'>%2</td><td>%3</td>"
                    "<td style='color:%4'>%5</td></tr>")
                .arg(mine == i ? "<b>" + hold + "</b>" : hold, length,
                     algs.join(QStringLiteral(" &nbsp; ")), muted, mark);
  }
  return html + "</table>";
}

QString EoCrossPage::axisResultHtml() const {
  QString html;
  if (round_.picked) {
    const HeldOptimum &mine = round_.ranked[*round_.picked];
    html += "<p>" +
            tr("You picked %1: %2 in %3 at best.")
                .arg(stepHoldHtml(mine.hold, roundStep() == SolveStep::EO,
                                  settings_.scheme),
                     stepText(roundStep()), movesText(mine.optimum.length)) +
            "</p>";
  }
  return html + holdsHtml();
}

QString EoCrossPage::breakdownHtml() const {
  if (!round_.breakdown || round_.breakdown->parts.empty()) {
    return {};
  }
  const EoBreakdown &b = *round_.breakdown;
  const QString muted = palette().color(QPalette::PlaceholderText).name();
  QString html =
      "<p>" +
      tr("Where the moves went, in the hold you solved in (EO Cross was %1 "
         "away at the start):")
          .arg(movesText(b.optimal)) +
      "</p><table align='center' cellspacing='0' cellpadding='3'><tr "
      "style='color:" +
      muted + "'><th align='left'>" + tr("Up to") + "</th><th align='left'>" +
      tr("You") + "</th><th>" + tr("Moves") + "</th><th>" +
      tr("EO Cross then") + "</th><th>" + tr("Lost") + "</th></tr>";
  for (const EoPart &p : b.parts) {
    Alg moves(round_.moves.begin() + static_cast<std::ptrdiff_t>(p.begin),
              round_.moves.begin() + static_cast<std::ptrdiff_t>(p.end));
    QString lost = p.lost <= 0 ? coloured(kGood, tr("nothing"))
                               : coloured(p.lost >= 3 ? QColor(kFailColour)
                                                      : QColor(kWeak),
                                          "+" + QString::number(p.lost));
    html += QString("<tr><td>%1</td><td><b>%2</b></td><td align='right'>%3"
                    "</td><td align='right'>%4</td><td align='right'>%5</td>"
                    "</tr>")
                .arg(stepText(p.step), algText(simplify(moves)))
                .arg(p.moves)
                .arg(p.toGo == 0 ? tr("done") : movesText(p.toGo), lost);
  }
  return html + "</table><p style='color:" + muted + "'>" +
         tr("A part loses what its moves are over how much nearer EO Cross "
            "they brought the cube.") +
         "</p>";
}

QString EoCrossPage::solveResultHtml() const {
  const SolveStep step = roundStep();
  QString html;
  if (!round_.moves.empty()) {
    QString line = tr("You: %1 (%2)")
                       .arg("<b>" + algText(simplify(round_.moves)) + "</b>",
                            movesText(solutionHtm(round_.moves)));
    if (round_.solvedIn) {
      line += ", " + stepHoldHtml(*round_.solvedIn, step == SolveStep::EO,
                                  settings_.scheme);
    }
    if (round_.hint) {
      line += " · " + tr("with a hint");
    }
    html += "<p>" + line + "</p>";
  }
  if (!round_.solvedIn) {
    html += "<p>" + coloured(kFailColour, progressText(cube_, step)) + "</p>";
  } else if (auto mine = rankOfHold(round_.ranked, *round_.solvedIn, step);
             mine && !round_.ranked.empty()) {
    int inHold = round_.ranked[*mine].optimum.length;
    int best = round_.ranked.front().optimum.length;
    html +=
        "<p>" +
        (inHold == best
             ? tr("The optimum in that hold is %1, the best of your "
                  "holds.")
                   .arg(movesText(inHold))
             : tr("The optimum in that hold is %1; the best of your "
                  "holds, %2, has %3.")
                   .arg(movesText(inHold),
                        stepHoldHtml(round_.ranked.front().hold,
                                     step == SolveStep::EO, settings_.scheme),
                        movesText(best))) +
        "</p>";
  }
  return html + breakdownHtml() + holdsHtml();
}

// --- the list of kinds -------------------------------------------------------

void EoCrossPage::fillTree() {
  QSignalBlocker block(tree_);
  tree_->clear();
  auto group = [this](const QString &title) {
    auto *g = new QTreeWidgetItem(tree_);
    g->setText(0, title);
    g->setFirstColumnSpanned(true);
    g->setFlags(Qt::ItemIsEnabled);
    QFont f = g->font(0);
    f.setBold(true);
    g->setFont(0, f);
    return g;
  };
  auto row = [](QTreeWidgetItem *g, const EoKind &kind, const QString &text) {
    auto *item = new QTreeWidgetItem(g);
    item->setData(0, Qt::UserRole, qs(eoKindKey(kind)));
    item->setText(0, text);
    for (int col = 1; col < 4; ++col) {
      item->setTextAlignment(col, Qt::AlignRight | Qt::AlignVCenter);
    }
  };
  for (int d = 0; d < kEoDrills; ++d) {
    auto drill = static_cast<Drill>(d);
    if (drill == Drill::Axis) {
      QTreeWidgetItem *g = group(drillName(drill));
      for (SolveStep s : eoCrossSteps()) {
        row(g, {drill, s, -1}, stepText(s));
      }
      continue;
    }
    EoKind kind = normalized({drill, step_, -1});
    QTreeWidgetItem *g =
        group(hasStep(drill) ? QString("%1 · %2").arg(drillName(drill),
                                                      stepText(kind.step))
                             : drillName(drill));
    for (int bad : kBadEdgeKinds) {
      if (bad == 0 && kind.step == SolveStep::EO && drill != Drill::Edges) {
        continue; // nothing to solve
      }
      kind.bad = bad;
      row(g, kind, tr("%1 bad edges").arg(bad));
    }
  }
  tree_->expandAll();
  refreshTree();
}

void EoCrossPage::refreshTree() {
  const QBrush normal = palette().brush(QPalette::Text);
  const QBrush muted = palette().brush(QPalette::PlaceholderText);
  for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
    QTreeWidgetItem *group = tree_->topLevelItem(g);
    for (int i = 0; i < group->childCount(); ++i) {
      QTreeWidgetItem *item = group->child(i);
      std::optional<EoKind> kind =
          eoKindFromKey(item->data(0, Qt::UserRole).toString().toStdString());
      if (!kind || store_.stats.tries(*kind).empty()) {
        item->setText(1, QString());
        item->setText(2, kNoValue);
        item->setText(3, kNoValue);
        item->setForeground(0, muted);
        item->setForeground(2, normal);
        continue;
      }
      CaseSummary s = eoSummary(store_.stats, *kind);
      item->setText(1, QString::number(s.attempts));
      item->setText(2, "+" + QString::number(s.excess, 'f', 1));
      item->setText(3, s.meanMs > 0
                           ? clockText(static_cast<std::int64_t>(s.meanMs))
                           : kNoValue);
      item->setForeground(0, s.weak ? QBrush(kWeak) : normal);
      item->setForeground(2, s.weak ? QBrush(kWeak) : normal);
    }
  }
  showSummary();
}

void EoCrossPage::showSummary() {
  int tries = 0, weak = 0;
  for (const auto &[key, attempts] : store_.stats.kinds) {
    tries += static_cast<int>(attempts.size());
    std::optional<EoKind> kind = eoKindFromKey(key);
    if (kind && eoSummary(store_.stats, *kind).weak) {
      ++weak;
    }
  }
  QStringList lines;
  lines
      << tr("<b>%1</b> tries kept, <b>%2</b> kinds weak").arg(tries).arg(weak);
  // The drill in hand, over all its kinds: the newest tries.
  EoKind kind = normalized({drill_, step_, -1});
  std::vector<PairAttempt> all;
  if (drill_ == Drill::Axis) {
    all = store_.stats.tries(kind);
  } else {
    for (int bad : kBadEdgeKinds) {
      kind.bad = bad;
      const std::vector<PairAttempt> &t = store_.stats.tries(kind);
      all.insert(all.end(), t.begin(), t.end());
    }
    std::stable_sort(all.begin(), all.end(),
                     [](const PairAttempt &a, const PairAttempt &b) {
                       return a.timestamp < b.timestamp;
                     });
  }
  if (!all.empty()) {
    std::size_t from = all.size() > 12 ? all.size() - 12 : 0;
    double extra = 0.0, ms = 0.0;
    int clean = 0, timed = 0;
    for (std::size_t i = from; i < all.size(); ++i) {
      const PairAttempt &a = all[i];
      if (!a.gaveUp) {
        extra += a.excess;
        ms += static_cast<double>(a.ms);
        ++timed;
        clean += a.excess == 0.0 && !a.hint ? 1 : 0;
      }
    }
    int n = static_cast<int>(all.size() - from);
    QString name = hasStep(drill_) ? QString("%1 · %2").arg(drillName(drill_),
                                                            stepText(kind.step))
                                   : drillName(drill_);
    lines << tr("%1, last %2: <b>%3</b> spot on, +%4 extra and %5 s on "
                "average")
                 .arg(name)
                 .arg(n)
                 .arg(clean)
                 .arg(QString::number(timed ? extra / timed : 0.0, 'f', 1),
                      clockText(
                          static_cast<std::int64_t>(timed ? ms / timed : 0.0)));
  }
  summary_->setText(lines.join("<br>"));
}

void EoCrossPage::treeActivated(QTreeWidgetItem *item) {
  std::optional<EoKind> kind =
      item ? eoKindFromKey(item->data(0, Qt::UserRole).toString().toStdString())
           : std::nullopt;
  if (!kind) {
    return;
  }
  drill_ = kind->drill;
  {
    QSignalBlocker block(tabs_);
    tabs_->setCurrentIndex(static_cast<int>(drill_));
  }
  if (kind->bad >= 0) {
    pinned_ = kind->bad;
  }
  if (hasStep(drill_) && kind->step != step_) {
    step_ = kind->step;
    // Not while the list is handling the double-click on its own item.
    QMetaObject::invokeMethod(
        this, [this] { fillTree(); }, Qt::QueuedConnection);
  }
  optionChanged();
}

void EoCrossPage::clearStats() {
  if (QMessageBox::question(
          this, "v-cube",
          tr("Clear all EO Cross statistics: every kind's tries in every "
             "drill?")) != QMessageBox::Yes) {
    return;
  }
  store_.stats = {};
  save();
  refreshTree();
  ready_ = false;
  if (active_) {
    newRound();
  }
}

} // namespace vcube
