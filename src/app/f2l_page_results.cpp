#include "app/f2l_page.hpp"

#include "app/cube_painter.hpp"
#include "app/ui_state.hpp"
#include "app/ui_text.hpp"
#include "io/session_store.hpp"
#include "train/f2l_stats.hpp"

#include <QFutureWatcher>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace vcube {

namespace {

QString slotText(Slot s) { return qs(slotName(s)); }

QString movesText(int n) {
  return n == 1 ? QObject::tr("1 move") : QObject::tr("%1 moves").arg(n);
}

QString costText(double cost) { return QString::number(cost, 'f', 1); }

QString secondsText(std::int64_t ms, int decimals) {
  return QString::number(static_cast<double>(ms) / 1000.0, 'f', decimals);
}

QString ordinal(int n) {
  switch (n) {
  case 1:
    return QObject::tr("1st");
  case 2:
    return QObject::tr("2nd");
  case 3:
    return QObject::tr("3rd");
  default:
    return QObject::tr("%1th").arg(n);
  }
}

const PairRank *rankOf(const std::vector<PairRank> &ranks, Slot slot) {
  auto it = std::find_if(ranks.begin(), ranks.end(), [slot](const PairRank &r) {
    return r.pair.slot == slot;
  });
  return it == ranks.end() ? nullptr : &*it;
}

int placeOf(const std::vector<PairRank> &ranks, Slot slot) {
  for (std::size_t i = 0; i < ranks.size(); ++i) {
    if (ranks[i].pair.slot == slot) {
      return static_cast<int>(i) + 1;
    }
  }
  return 0;
}

// The corner a case is best seen from: that of a back slot it uses.
DiagramView viewOf(PairCase c) {
  for (std::optional<Slot> s : {c.cornerSlot(), c.edgeSlot()}) {
    if (s == Slot::BL) {
      return DiagramView::Iso3DBackLeft;
    }
    if (s == Slot::BR) {
      return DiagramView::Iso3DBackRight;
    }
  }
  for (std::optional<Slot> s : {c.cornerSlot(), c.edgeSlot()}) {
    if (s == Slot::FL) {
      return DiagramView::Iso3DFrontLeft;
    }
  }
  return DiagramView::Iso3D;
}

const StatusColour kGood{Status::Good};
const StatusColour kWeak{Status::Warning};

} // namespace

// --- analysis ----------------------------------------------------------------

AnalysisOptions F2LPage::analysisOptions() const {
  AnalysisOptions o;
  o.config = config_;
  o.profile =
      lab_.profile(saved::lab("scoreProfile").get().toString().toStdString());
  o.reference = reference_;
  return o;
}

double F2LPage::costOf(const Alg &moves) const {
  return solutionCost(moves, analysisOptions());
}

void F2LPage::requestAnalysis() {
  if (stop_) {
    stop_->store(true);
  }
  stop_ = std::make_shared<std::atomic<bool>>(false);
  round_.analysed = false;
  const int id = round_.id;
  const Drill drill = drill_;
  const CubieCube start = round_.start;
  const std::optional<Slot> target = round_.target;
  std::vector<CubieCube> befores;
  for (const F2LSegment &s : round_.segments) {
    befores.push_back(s.before);
  }
  auto *watcher = new QFutureWatcher<Analysis>(this);
  connect(watcher, &QFutureWatcher<Analysis>::finished, this, [this, watcher] {
    analysisArrived(watcher->result());
    watcher->deleteLater();
  });
  watcher->setFuture(
      QtConcurrent::run([id, drill, start, target, befores,
                         options = analysisOptions(), stop = stop_] {
        Analysis a;
        a.round = id;
        switch (drill) {
        case Drill::Pair:
        case Drill::Explore:
          a.pair = analysePair(start, *target, options, stop.get());
          break;
        case Drill::Choice:
          a.ranks = rankPairs(start, options, stop.get());
          break;
        case Drill::Full:
          for (const CubieCube &before : befores) {
            a.segments.push_back(rankPairs(before, options, stop.get()));
          }
          break;
        }
        return a;
      }));
}

void F2LPage::analysisArrived(const Analysis &a) {
  if (a.round != round_.id) {
    return; // for a round that is over
  }
  round_.analysis = a;
  round_.analysed = true;
  if (phase_ == Phase::Done) {
    record();
  }
  showKeys();
  showResult();
}

std::vector<RankedSolution> F2LPage::shownSolutions() const {
  if (!round_.analysed) {
    return {};
  }
  if (round_.analysis.pair) {
    return round_.analysis.pair->solutions;
  }
  if (drill_ == Drill::Choice && !round_.analysis.ranks.empty()) {
    const PairRank *r =
        round_.picked ? rankOf(round_.analysis.ranks, *round_.picked) : nullptr;
    return (r ? r : &round_.analysis.ranks.front())->pair.solutions;
  }
  return {};
}

// --- recording ---------------------------------------------------------------

void F2LPage::record() {
  if (round_.recorded || round_.retry || !round_.analysed) {
    return;
  }
  round_.recorded = true;
  const std::int64_t now = unixNow();
  auto pairAttempt = [&](const PairAnalysis &pair, const Alg &moves,
                         std::int64_t ms, F2LDrill drill) {
    if (!pair.best()) {
      return;
    }
    PairAttempt a;
    a.timestamp = now;
    a.ms = ms;
    a.moves = htmCount(simplify(moves));
    a.bestMoves = pair.best()->htm;
    a.excess = std::max(0.0, costOf(moves) - pair.best()->cost);
    a.drill = drill;
    store_.stats.add(caseKey(pair.pcase), a);
  };
  switch (drill_) {
  case Drill::Pair: {
    const PairAnalysis &pair = *round_.analysis.pair;
    if (!pair.best()) {
      return;
    }
    PairAttempt a;
    a.timestamp = now;
    a.ms = round_.ms;
    a.moves = htmCount(simplify(round_.moves));
    a.bestMoves = pair.best()->htm;
    a.gaveUp = round_.gaveUp;
    a.hint = round_.hint;
    a.excess = round_.gaveUp
                   ? 0.0
                   : std::max(0.0, costOf(round_.moves) - pair.best()->cost);
    store_.stats.add(caseKey(pair.pcase), a);
    break;
  }
  case Drill::Choice: {
    const std::vector<PairRank> &ranks = round_.analysis.ranks;
    const PairRank *picked =
        round_.picked ? rankOf(ranks, *round_.picked) : nullptr;
    if (round_.gaveUp || !picked) {
      return;
    }
    if (!round_.hint) {
      ChoiceAttempt c;
      c.timestamp = now;
      c.rank = placeOf(ranks, *round_.picked);
      c.of = static_cast<int>(ranks.size());
      c.regret = std::max(0.0, picked->total - ranks.front().total);
      store_.stats.add(c);
    }
    pairAttempt(picked->pair, round_.moves, round_.ms, F2LDrill::Choice);
    break;
  }
  case Drill::Full: {
    if (round_.gaveUp) {
      return;
    }
    FullAttempt f;
    f.timestamp = now;
    f.ms = round_.ms;
    f.moves = htmCount(simplify(round_.moves));
    f.pairs = static_cast<int>(round_.segments.size());
    for (std::size_t i = 0;
         i < round_.segments.size() && i < round_.analysis.segments.size();
         ++i) {
      const F2LSegment &seg = round_.segments[i];
      if (seg.solved.size() != 1) {
        continue;
      }
      const PairRank *r = rankOf(round_.analysis.segments[i], seg.solved[0]);
      if (!r || !r->pair.best()) {
        continue;
      }
      Alg moves(round_.moves.begin() + static_cast<std::ptrdiff_t>(seg.begin),
                round_.moves.begin() + static_cast<std::ptrdiff_t>(seg.end));
      std::int64_t from = seg.begin ? round_.moveMs[seg.begin - 1] : 0;
      pairAttempt(r->pair, moves, round_.moveMs[seg.end - 1] - from,
                  F2LDrill::Full);
      f.excess += std::max(0.0, costOf(moves) - r->pair.best()->cost);
    }
    store_.stats.add(f);
    break;
  }
  case Drill::Explore:
    return;
  }
  save();
  refreshTree();
}

void F2LPage::save() {
  saveInBackground(store_, this, [this](const QString &error) {
    emit message(tr("Could not save the F2L statistics: %1").arg(error));
  });
}

// --- results -----------------------------------------------------------------

QString F2LPage::resultHtml() const { return result_->text(); }

void F2LPage::showResult() {
  const QString working = "<p>" + tr("Working out the solutions…") + "</p>";
  QString html;
  if (phase_ == Phase::Explore) {
    html = round_.analysed ? exploreHtml() : working;
  } else if (phase_ == Phase::Done) {
    if (!round_.analysed) {
      html = working;
    } else if (drill_ == Drill::Pair) {
      html = pairResultHtml();
    } else if (drill_ == Drill::Choice) {
      html = choiceResultHtml();
    } else {
      html = fullResultHtml();
    }
  }
  result_->setText(html);
}

QString F2LPage::solutionsHtml(const PairAnalysis &pair,
                               std::size_t first) const {
  const QString muted = palette().color(QPalette::PlaceholderText).name();
  QString html = "<table align='center' cellspacing='0' cellpadding='3'>";
  for (std::size_t i = 0; i < pair.solutions.size(); ++i) {
    const RankedSolution &s = pair.solutions[i];
    QString alg = QString("<a href='sol:%1'>%2</a>")
                      .arg(first + i)
                      .arg(qs(s.text).toHtmlEscaped());
    if (first + i == shown_) {
      alg = "<b>" + alg + "</b>";
    }
    QString note;
    if (s.fromSet()) {
      note = tr("your set: %1").arg(qs(s.setCase).toHtmlEscaped());
    }
    html += QString("<tr><td align='right' style='color:%1'>%2</td><td>%3</td>"
                    "<td align='right'>%4</td><td align='right'>%5</td>"
                    "<td style='color:%1'>%6</td></tr>")
                .arg(muted)
                .arg(first + i + 1)
                .arg(alg, movesText(s.htm), costText(s.cost), note);
  }
  html += "</table>";
  if (pair.solutions.empty()) {
    html = "<p>" + tr("No solution was found with these moves.") + "</p>";
  }
  return html;
}

QString F2LPage::pairResultHtml() const {
  const PairAnalysis &pair = *round_.analysis.pair;
  const QString muted = palette().color(QPalette::PlaceholderText).name();
  QString html;
  if (round_.gaveUp) {
    html += "<p><b>" + tr("Gave up.") + "</b></p>";
  } else {
    Alg moves = simplify(round_.moves);
    double cost = costOf(round_.moves);
    QString verdict;
    if (pair.best()) {
      double over = cost - pair.best()->cost;
      verdict = over < 0.05
                    ? QString("<span style='color:%1'>%2</span>")
                          .arg(kGood.name(), tr("as good as the best"))
                    : QString("<span style='color:%1'>%2</span>")
                          .arg(over >= 2.0 ? kFailColour.name() : kWeak.name(),
                               tr("+%1 over the best").arg(costText(over)));
    }
    html += "<p><b>" + tr("You: %1").arg(qs(toString(moves)).toHtmlEscaped()) +
            "</b><br>" +
            tr("%1, cost %2").arg(movesText(htmCount(moves)), costText(cost)) +
            " · " + verdict +
            (round_.hint ? " · " + tr("with a hint") : QString()) + "</p>";
  }
  if (round_.retry) {
    html += QString("<p style='color:%1'>%2</p>")
                .arg(muted, tr("A retry: not recorded."));
  }
  html += "<p>" +
          tr("The best solutions from this state (cost by your Alg "
             "Lab score profile):") +
          "</p>" + solutionsHtml(pair, 0);
  html += QString("<p style='color:%1'>%2</p>")
              .arg(muted, qs(caseName(pair.pcase)).toHtmlEscaped());
  return html;
}

QString F2LPage::choiceResultHtml() const {
  const std::vector<PairRank> &ranks = round_.analysis.ranks;
  const QString muted = palette().color(QPalette::PlaceholderText).name();
  if (ranks.empty()) {
    return "<p>" + tr("No solution was found with these moves.") + "</p>";
  }
  const PairRank *picked =
      round_.picked ? rankOf(ranks, *round_.picked) : nullptr;
  QString html;
  if (!picked) {
    html += "<p><b>" + tr("Gave up.") + "</b></p>";
  } else {
    int place = placeOf(ranks, *round_.picked);
    double regret = picked->total - ranks.front().total;
    QString verdict =
        place == 1 || regret < 0.05
            ? QString("<span style='color:%1'>%2</span>")
                  .arg(kGood.name(), tr("%1: the best pair to start with")
                                         .arg(slotText(*round_.picked)))
            : QString("<span style='color:%1'>%2</span>")
                  .arg(regret >= 2.0 ? kFailColour.name() : kWeak.name(),
                       tr("%1: the %2 best of %3 pairs, +%4 over starting "
                          "with %5")
                           .arg(slotText(*round_.picked), ordinal(place))
                           .arg(ranks.size())
                           .arg(costText(regret),
                                slotText(ranks.front().pair.slot)));
    Alg moves = simplify(round_.moves);
    double cost = costOf(round_.moves);
    double over = cost - picked->pair.best()->cost;
    html += "<p><b>" + verdict + "</b><br>" +
            tr("You: %1 (%2, cost %3; %4)")
                .arg(qs(toString(moves)).toHtmlEscaped(),
                     movesText(htmCount(moves)), costText(cost),
                     over < 0.05 ? tr("as good as the best")
                                 : tr("+%1 over the best for this pair")
                                       .arg(costText(over))) +
            (round_.hint ? " · " + tr("with a hint") : QString()) + "</p>";
  }
  if (round_.retry) {
    html += QString("<p style='color:%1'>%2</p>")
                .arg(muted, tr("A retry: not recorded."));
  }
  html += "<table align='center' cellspacing='0' cellpadding='3'><tr "
          "style='color:" +
          muted + "'><th></th><th align='left'>" + tr("Pair") +
          "</th><th align='left'>" + tr("Best solution") + "</th><th>" +
          tr("Pair") + "</th><th>" + tr("Rest") + "</th><th>" + tr("Hidden") +
          "</th><th>" + tr("Total") + "</th><th align='left'>" + tr("Order") +
          "</th></tr>";
  for (std::size_t i = 0; i < ranks.size(); ++i) {
    const PairRank &r = ranks[i];
    QStringList order;
    for (Slot s : r.order) {
      order << slotText(s);
    }
    QString name = slotText(r.pair.slot);
    if (round_.picked == r.pair.slot) {
      name = "<b>" + name + "</b> " + tr("(yours)");
    }
    html += QString("<tr><td align='right' style='color:%1'>%2</td><td>%3</td>"
                    "<td>%4</td><td align='right'>%5</td>"
                    "<td align='right'>%6</td><td align='right'>%7</td>"
                    "<td align='right'><b>%8</b></td><td>%9</td></tr>")
                .arg(muted)
                .arg(i + 1)
                .arg(name, qs(r.pair.best()->text).toHtmlEscaped(),
                     costText(r.pair.best()->cost), costText(r.rest),
                     r.hidden > 0 ? "+" + costText(r.hidden) : kNoValue,
                     costText(r.total), order.join(" → "));
  }
  html += "</table>";
  html +=
      QString("<p style='color:%1'>%2</p>")
          .arg(muted, tr("Total = the pair's best solution + the cheapest way "
                         "through the pairs after it + a penalty for pieces "
                         "the flat view hides at the start."));
  const PairRank *shown = picked ? picked : &ranks.front();
  html += "<p>" +
          tr("Solutions of the %1 pair:").arg(slotText(shown->pair.slot)) +
          "</p>" + solutionsHtml(shown->pair, 0);
  return html;
}

QString F2LPage::fullResultHtml() const {
  const QString muted = palette().color(QPalette::PlaceholderText).name();
  QString html;
  Alg all = simplify(round_.moves);
  if (round_.gaveUp) {
    html += "<p><b>" + tr("Gave up.") + "</b></p>";
  } else {
    html += "<p><b>" +
            tr("F2L done in %1, cost %2")
                .arg(movesText(htmCount(all)), costText(costOf(round_.moves))) +
            "</b></p>";
  }
  if (round_.retry) {
    html += QString("<p style='color:%1'>%2</p>")
                .arg(muted, tr("A retry: not recorded."));
  }
  if (round_.segments.empty()) {
    return html;
  }
  html +=
      "<table align='center' cellspacing='0' cellpadding='3'><tr "
      "style='color:" +
      muted + "'><th align='left'>" + tr("Pair") + "</th><th align='left'>" +
      tr("You") + "</th><th></th><th align='left'>" + tr("Best for that pair") +
      "</th><th></th><th align='left'>" + tr("Best pair then") + "</th></tr>";
  for (std::size_t i = 0; i < round_.segments.size(); ++i) {
    const F2LSegment &seg = round_.segments[i];
    QStringList names;
    for (Slot s : seg.solved) {
      names << slotText(s);
    }
    Alg moves(round_.moves.begin() + static_cast<std::ptrdiff_t>(seg.begin),
              round_.moves.begin() + static_cast<std::ptrdiff_t>(seg.end));
    Alg simple = simplify(moves);
    double cost = costOf(moves);
    QString best, over, pick;
    if (i < round_.analysis.segments.size() &&
        !round_.analysis.segments[i].empty()) {
      const std::vector<PairRank> &ranks = round_.analysis.segments[i];
      const PairRank *r =
          seg.solved.size() == 1 ? rankOf(ranks, seg.solved[0]) : nullptr;
      if (r && r->pair.best()) {
        best = QString("%1 <span style='color:%2'>(%3, %4)</span>")
                   .arg(qs(r->pair.best()->text).toHtmlEscaped(), muted,
                        movesText(r->pair.best()->htm),
                        costText(r->pair.best()->cost));
        double d = cost - r->pair.best()->cost;
        over =
            d < 0.05
                ? QString("<span style='color:%1'>✓</span>").arg(kGood.name())
                : QString("<span style='color:%1'>+%2</span>")
                      .arg(d >= 2.0 ? kFailColour.name() : kWeak.name(),
                           costText(d));
        double regret = r->total - ranks.front().total;
        pick =
            regret < 0.05
                ? QString("<span style='color:%1'>✓</span>").arg(kGood.name())
                : QString("%1 <span style='color:%2'>(%3)</span>")
                      .arg(slotText(ranks.front().pair.slot),
                           regret >= 2.0 ? kFailColour.name() : kWeak.name(),
                           tr("−%1").arg(costText(regret)));
      }
    }
    html +=
        QString("<tr><td><b>%1</b></td><td>%2 <span style='color:%3'>(%4, "
                "%5)</span></td><td>%6</td><td>%7</td><td></td>"
                "<td>%8</td></tr>")
            .arg(names.join(" + "), qs(toString(simple)).toHtmlEscaped(), muted,
                 movesText(htmCount(simple)), costText(cost), over, best, pick);
  }
  html += "</table>";
  if (!round_.analysis.segments.empty() &&
      !round_.analysis.segments.front().empty()) {
    const PairRank &plan = round_.analysis.segments.front().front();
    QStringList order;
    for (Slot s : plan.order) {
      order << slotText(s);
    }
    html += "<p>" +
            tr("The cheapest way found from the start: %1 (%2, cost %3)")
                .arg(order.join(" → "), movesText(plan.moves),
                     costText(plan.total - plan.hidden)) +
            "</p>";
  }
  html += QString("<p style='color:%1'>%2</p>")
              .arg(muted, tr("\"Best pair then\" is the pair that starts the "
                             "cheapest way through the pairs left, with what "
                             "starting with yours cost over it."));
  return html;
}

QString F2LPage::exploreHtml() const {
  const PairAnalysis &pair = *round_.analysis.pair;
  const QString muted = palette().color(QPalette::PlaceholderText).name();
  QString html = "<p>" +
                 tr("The best solutions from this state (cost by "
                    "your Alg Lab score profile):") +
                 "</p>" + solutionsHtml(pair, 0);
  auto it = store_.stats.cases.find(caseKey(pair.pcase));
  if (it != store_.stats.cases.end()) {
    CaseSummary s = summarizeCase(it->second, meanPairMs(store_.stats));
    html += QString("<p style='color:%1'>%2</p>")
                .arg(muted,
                     tr("%1 tries; lately +%2 over the best, %3 s")
                         .arg(s.attempts)
                         .arg(costText(s.excess),
                              secondsText(static_cast<std::int64_t>(s.meanMs),
                                          settings_.decimals)));
  }
  return html;
}

// --- the case list -----------------------------------------------------------

void F2LPage::fillTree() {
  QSignalBlocker block(tree_);
  tree_->clear();
  CubeLook look = cubeView_->look();
  look.map = kIdentityColourMap;
  const int size = settings_.thumbnailSize;
  tree_->setIconSize({size, size});
  std::array<QTreeWidgetItem *, kPairGroups> groups{};
  for (PairCase c : pairCases(config_.eo)) {
    auto g = static_cast<std::size_t>(groupOf(c));
    if (!groups[g]) {
      groups[g] = new QTreeWidgetItem(tree_);
      groups[g]->setText(0, qs(groupName(groupOf(c))));
      groups[g]->setFirstColumnSpanned(true);
      groups[g]->setFlags(Qt::ItemIsEnabled);
      QFont f = groups[g]->font(0);
      f.setBold(true);
      groups[g]->setFont(0, f);
    }
    auto *item = new QTreeWidgetItem(groups[g]);
    item->setData(0, Qt::UserRole, qs(caseKey(c)));
    item->setText(0, qs(caseName(c)));
    item->setIcon(0,
                  cube_painter::cachedDiagram(caseDiagram(c), viewOf(c), size,
                                              look, devicePixelRatioF()));
    for (int col = 1; col < 4; ++col) {
      item->setTextAlignment(col, Qt::AlignRight | Qt::AlignVCenter);
    }
  }
  tree_->expandAll();
  refreshTree();
}

void F2LPage::refreshTree() {
  const double overall = meanPairMs(store_.stats);
  const QBrush normal = palette().brush(QPalette::Text);
  for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
    QTreeWidgetItem *group = tree_->topLevelItem(g);
    for (int i = 0; i < group->childCount(); ++i) {
      QTreeWidgetItem *item = group->child(i);
      auto it = store_.stats.cases.find(
          item->data(0, Qt::UserRole).toString().toStdString());
      if (it == store_.stats.cases.end()) {
        for (int col = 1; col < 4; ++col) {
          item->setText(col, col == 1 ? QString() : kNoValue);
        }
        item->setForeground(0, normal);
        continue;
      }
      CaseSummary s = summarizeCase(it->second, overall);
      item->setText(1, QString::number(s.attempts));
      item->setText(2, "+" + costText(s.excess));
      item->setText(3, s.meanMs > 0
                           ? secondsText(static_cast<std::int64_t>(s.meanMs),
                                         settings_.decimals)
                           : kNoValue);
      item->setForeground(0, s.weak ? QBrush(kWeak) : normal);
      item->setForeground(2, s.weak ? QBrush(kWeak) : normal);
    }
  }
  showSummary();
}

void F2LPage::showSummary() {
  const F2LStats &stats = store_.stats;
  int tried = 0, weak = 0;
  const double overall = meanPairMs(stats);
  for (PairCase c : pairCases(config_.eo)) {
    auto it = stats.cases.find(caseKey(c));
    if (it != stats.cases.end()) {
      ++tried;
      weak += summarizeCase(it->second, overall).weak ? 1 : 0;
    }
  }
  QStringList lines;
  lines << tr("<b>%1</b> of %2 cases tried, <b>%3</b> weak")
               .arg(tried)
               .arg(pairCases(config_.eo).size())
               .arg(weak);
  if (!stats.choices.empty()) {
    std::size_t from =
        stats.choices.size() > 50 ? stats.choices.size() - 50 : 0;
    int best = 0;
    double regret = 0.0;
    for (std::size_t i = from; i < stats.choices.size(); ++i) {
      best += stats.choices[i].rank == 1 ? 1 : 0;
      regret += stats.choices[i].regret;
    }
    auto n = static_cast<int>(stats.choices.size() - from);
    lines << tr("Pair choice, last %1: <b>%2%</b> the best pair, +%3 on "
                "average")
                 .arg(n)
                 .arg(best * 100 / n)
                 .arg(costText(regret / n));
  }
  if (!stats.fulls.empty()) {
    std::size_t from = stats.fulls.size() > 12 ? stats.fulls.size() - 12 : 0;
    double moves = 0.0, ms = 0.0;
    for (std::size_t i = from; i < stats.fulls.size(); ++i) {
      moves += stats.fulls[i].moves;
      ms += static_cast<double>(stats.fulls[i].ms);
    }
    auto n = static_cast<double>(stats.fulls.size() - from);
    lines << tr("Full F2L, last %1: <b>%2</b> moves, %3 s on average")
                 .arg(static_cast<int>(n))
                 .arg(QString::number(moves / n, 'f', 1),
                      secondsText(static_cast<std::int64_t>(ms / n),
                                  settings_.decimals));
  }
  summary_->setText(lines.join("<br>"));
}

void F2LPage::selectInTree(PairCase c) {
  const QString key = qs(caseKey(c));
  QSignalBlocker block(tree_);
  for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
    QTreeWidgetItem *group = tree_->topLevelItem(g);
    for (int i = 0; i < group->childCount(); ++i) {
      if (group->child(i)->data(0, Qt::UserRole).toString() == key) {
        tree_->setCurrentItem(group->child(i));
        tree_->scrollToItem(group->child(i));
        return;
      }
    }
  }
}

void F2LPage::treeSelected() {
  QTreeWidgetItem *item = tree_->currentItem();
  if (!item || drill_ != Drill::Explore) {
    return;
  }
  if (auto c =
          caseFromKey(item->data(0, Qt::UserRole).toString().toStdString())) {
    explored_ = *c;
    if (active_) {
      newRound();
    }
  }
}

void F2LPage::clearStats() {
  if (QMessageBox::question(
          this, "v-cube",
          tr("Clear every F2L statistic: the cases' tries, the pair choices "
             "and the Full F2L solves?")) != QMessageBox::Yes) {
    return;
  }
  store_.stats = {};
  save();
  refreshTree();
}

} // namespace vcube
