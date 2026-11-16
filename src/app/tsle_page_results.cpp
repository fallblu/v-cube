#include "app/tsle_page.hpp"

#include "app/cube_painter.hpp"
#include "app/ui_text.hpp"
#include "io/session_store.hpp"
#include "train/f2l.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace vcube {

namespace {

QString slotText(Slot s) { return qs(slotName(s)); }

QString movesText(int n) {
  return n == 1 ? QObject::tr("1 move") : QObject::tr("%1 moves").arg(n);
}

QString algText(const Alg &alg) { return qs(toString(alg)).toHtmlEscaped(); }

QString coloured(const QColor &colour, const QString &text) {
  return QString("<span style='color:%1'>%2</span>").arg(colour.name(), text);
}

double meanMs(const TsleProgress &p) {
  double sum = 0.0;
  int n = 0;
  for (const auto &[key, attempts] : p.cases) {
    for (const PairAttempt &a : attempts) {
      if (!a.gaveUp) {
        sum += static_cast<double>(a.ms);
        ++n;
      }
    }
  }
  return n ? sum / n : 0.0;
}

// The group a case is listed in: 0 the OCLLs, else its steps left.
int groupOf(TsleState c) { return c.ocll() ? 0 : tsleStepsLeft(c); }

const StatusColour kGood{Status::Good};
const StatusColour kWeak{Status::Warning};

} // namespace

// --- recording ---------------------------------------------------------------

void TslePage::record() {
  if (round_.recorded || round_.retry) {
    return;
  }
  round_.recorded = true;
  const std::int64_t now = unixNow();
  const TsleSolver &s = solver(round_.slot);
  auto bestMoves = [&s](TsleState state) {
    if (state.ocll()) {
      return s.movesLeft(state);
    }
    std::vector<TsleOption> options = s.options(state);
    return options.empty() ? 0 : options.front().moves;
  };
  auto attemptOf = [&](const Step &step, std::int64_t ms, F2LDrill drill) {
    PairAttempt a;
    a.timestamp = now;
    a.ms = ms;
    a.moves = step.moves;
    a.bestMoves = bestMoves(step.before);
    a.excess = step.good ? step.extra : kWrongTrigger;
    a.hint = step.hint;
    a.drill = drill;
    return a;
  };
  if (drill_ == Drill::Next) {
    PairAttempt a;
    if (round_.gaveUp || round_.steps.empty()) {
      a.timestamp = now;
      a.ms = round_.ms;
      a.bestMoves = bestMoves(round_.start);
      a.gaveUp = true;
      a.hint = round_.stepHint;
    } else {
      a = attemptOf(round_.steps.front(), round_.ms, F2LDrill::Pair);
    }
    store_.data.add(tsleKey(tsleCase(round_.start)), a);
  } else if (drill_ == Drill::Full) {
    if (round_.gaveUp) {
      return;
    }
    FullAttempt f;
    f.timestamp = now;
    f.ms = round_.ms;
    f.pairs = static_cast<int>(round_.steps.size());
    for (const Step &step : round_.steps) {
      f.moves += step.moves;
      std::int64_t from = step.begin ? round_.moveMs[step.begin - 1] : 0;
      PairAttempt a =
          attemptOf(step, round_.moveMs[step.end - 1] - from, F2LDrill::Full);
      f.excess += a.excess;
      store_.data.add(tsleKey(tsleCase(step.before)), a);
    }
    store_.data.add(f);
  } else {
    return;
  }
  save();
  refreshTree();
}

void TslePage::save() {
  saveInBackground(store_, this, [this](const QString &error) {
    emit message(tr("Could not save the TSLE progress: %1").arg(error));
  });
}

// --- results -----------------------------------------------------------------

QString TslePage::resultHtml() const { return result_->text(); }

void TslePage::showResult() {
  QString html;
  if (phase_ == Phase::Ready) {
    result_->setText(html);
    return;
  }
  switch (drill_) {
  case Drill::Intro:
    html = introHtml();
    break;
  case Drill::Next:
    html = phase_ == Phase::Done ? nextResultHtml() : QString();
    break;
  case Drill::Full:
    html = phase_ == Phase::Done ? fullResultHtml() : QString();
    break;
  case Drill::Explore:
    html = exploreHtml();
    break;
  }
  result_->setText(html);
}

QString TslePage::optionsHtml(TsleState state, Slot slot) const {
  const TsleSolver &s = solver(slot);
  const QString muted = palette().color(QPalette::PlaceholderText).name();
  if (state.done()) {
    return "<p>" + tr("TSLE is done.") + "</p>";
  }
  if (state.ocll()) {
    auto f = s.finish(state);
    if (!f) {
      return {};
    }
    return "<p>" +
           tr("An OCLL is left, %1: %2 (%3)")
               .arg(qs(f->name).toHtmlEscaped(),
                    "<a href='opt:0'>" + algText(f->alg) + "</a>",
                    movesText(f->moves)) +
           "</p>";
  }
  const int best = s.movesLeft(state);
  std::vector<TsleOption> options = s.options(state);
  QString html = "<table align='center' cellspacing='0' cellpadding='3'><tr "
                 "style='color:" +
                 muted + "'><th></th><th align='left'>" + tr("Trigger") +
                 "</th><th align='left'>" + tr("Leaves") + "</th><th>" +
                 tr("To the end") + "</th><th></th></tr>";
  for (std::size_t i = 0; i < options.size(); ++i) {
    const TsleOption &o = options[i];
    // The good ones, and any with more steps but fewer moves.
    if (!o.good && o.total >= best) {
      continue;
    }
    QString leaves = caseText(o.next);
    if (o.finish) {
      leaves += " · " + algText(o.finish->alg);
    }
    QString mark = o.best ? coloured(kGood, tr("best from this AUF"))
                   : o.good
                       ? coloured(kWeak, tr("good, +%1").arg(o.total - best))
                       : QString("<span style='color:%1'>%2</span>")
                             .arg(muted, tr("a step more, %1 fewer")
                                             .arg(movesText(best - o.total)));
    QString alg = QString("<a href='opt:%1'>%2</a>").arg(i).arg(algText(o.alg));
    html += QString("<tr><td align='right' style='color:%1'>%2</td>"
                    "<td>%3</td><td>%4</td><td align='right'>%5</td>"
                    "<td>%6</td></tr>")
                .arg(muted)
                .arg(i + 1)
                .arg(o.best ? "<b>" + alg + "</b>" : alg, leaves,
                     movesText(o.total), mark);
  }
  return html + "</table>";
}

QString TslePage::pathHtml(TsleState state, Slot slot) const {
  if (state.done()) {
    return {};
  }
  const TsleSolver &s = solver(slot);
  QStringList steps;
  for (const Alg &step : s.bestPath(state)) {
    steps << algText(step);
  }
  return "<p>" +
         tr("The best way to the end, %1: %2")
             .arg(movesText(s.movesLeft(state)),
                  steps.join(QStringLiteral(" &nbsp;·&nbsp; "))) +
         "</p>";
}

QString TslePage::verdictHtml(const Step &step) const {
  if (step.before.ocll()) {
    return step.extra == 0
               ? coloured(kGood, tr("OCLL solved"))
               : coloured(kWeak, tr("OCLL solved, +%1 over your alg")
                                     .arg(movesText(step.extra)));
  }
  if (!step.good) {
    int before = tsleStepsLeft(step.before), after = tsleStepsLeft(step.after);
    return coloured(
        kFailColour,
        tr("Not a good trigger: %1 left after it (%2 before)")
            .arg(after == 1 ? tr("1 step") : tr("%1 steps").arg(after))
            .arg(before));
  }
  return step.extra == 0
             ? coloured(kGood, tr("The best trigger from this AUF"))
             : coloured(kWeak, tr("A good trigger, but +%1 over the best "
                                  "from this AUF")
                                   .arg(movesText(step.extra)));
}

QString TslePage::nextResultHtml() const {
  const QString muted = palette().color(QPalette::PlaceholderText).name();
  QString html;
  if (round_.gaveUp || round_.steps.empty()) {
    html += "<p><b>" + tr("Gave up.") + "</b></p>";
  } else {
    const Step &step = round_.steps.front();
    Alg moves = simplify(round_.moves);
    html +=
        "<p><b>" + verdictHtml(step) + "</b><br>" +
        tr("You: %1 (%2), leaving %3")
            .arg(algText(moves), movesText(step.moves), caseText(step.after)) +
        (step.hint ? " · " + tr("with a hint") : QString()) + "</p>";
  }
  if (round_.retry) {
    html += QString("<p style='color:%1'>%2</p>")
                .arg(muted, tr("A retry: not recorded."));
  }
  html += "<p>" +
          tr("%1, %2 slot. Every good trigger from this AUF:")
              .arg(caseText(round_.start), slotText(round_.slot)) +
          "</p>" + optionsHtml(round_.start, round_.slot) +
          pathHtml(round_.start, round_.slot);
  return html;
}

QString TslePage::fullResultHtml() const {
  const TsleSolver &s = solver(round_.slot);
  const QString muted = palette().color(QPalette::PlaceholderText).name();
  QString html;
  int best = s.movesLeft(round_.start);
  if (round_.gaveUp) {
    html += "<p><b>" + tr("Gave up.") + "</b></p>";
  } else {
    // Step by step, as done: a trigger undone by the next one still
    // counts.
    int moves = 0;
    for (const Step &step : round_.steps) {
      moves += step.moves;
    }
    QString over =
        moves <= best
            ? coloured(kGood, tr("as short as the best way"))
            : coloured(moves - best >= 3 ? QColor(kFailColour) : QColor(kWeak),
                       tr("+%1 over the best way").arg(moves - best));
    html += "<p><b>" + tr("TSLE done in %1").arg(movesText(moves)) + "</b> · " +
            over + "</p>";
  }
  if (round_.retry) {
    html += QString("<p style='color:%1'>%2</p>")
                .arg(muted, tr("A retry: not recorded."));
  }
  if (!round_.steps.empty()) {
    html += "<table align='center' cellspacing='0' cellpadding='3'>"
            "<tr style='color:" +
            muted + "'><th align='left'>" + tr("From") +
            "</th><th align='left'>" + tr("You") + "</th><th align='left'>" +
            tr("Verdict") + "</th><th align='left'>" + tr("Best then") +
            "</th></tr>";
    for (const Step &step : round_.steps) {
      Alg moves(round_.moves.begin() + static_cast<std::ptrdiff_t>(step.begin),
                round_.moves.begin() + static_cast<std::ptrdiff_t>(step.end));
      QString bestThen;
      if (step.before.ocll()) {
        if (auto f = s.finish(step.before)) {
          bestThen = algText(f->alg);
        }
      } else {
        bestThen = algText(s.options(step.before).front().alg);
      }
      html += QString("<tr><td>%1</td><td><b>%2</b></td><td>%3</td>"
                      "<td>%4</td></tr>")
                  .arg(caseText(step.before), algText(simplify(moves)),
                       verdictHtml(step) +
                           (step.hint ? " · " + tr("hint") : QString()),
                       bestThen);
    }
    html += "</table>";
  }
  html += pathHtml(round_.start, round_.slot);
  return html;
}

QString TslePage::exploreHtml() const {
  const QString muted = palette().color(QPalette::PlaceholderText).name();
  QString html;
  if (!anchor_.done() && !anchor_.ocll()) {
    html += "<p>" + tr("Every good trigger from this AUF:") + "</p>";
  }
  html += optionsHtml(anchor_, round_.slot) + pathHtml(anchor_, round_.slot);
  auto it = store_.data.cases.find(tsleKey(tsleCase(anchor_)));
  if (it != store_.data.cases.end()) {
    CaseSummary sum = summarizeCase(it->second, meanMs(store_.data));
    html +=
        QString("<p style='color:%1'>%2</p>")
            .arg(muted,
                 tr("%1 tries; lately +%2 moves, %3 s")
                     .arg(sum.attempts)
                     .arg(QString::number(sum.excess, 'f', 1),
                          clockText(static_cast<std::int64_t>(sum.meanMs))));
  }
  return html;
}

QString TslePage::introHtml() const {
  const QString muted = palette().color(QPalette::PlaceholderText).name();
  if (phase_ == Phase::Idle || !introCase_) {
    return {};
  }
  if (phase_ == Phase::Done) {
    return "<p>" + tr("Its good triggers, with the edge at UF:") + "</p>" +
           optionsHtml(*introCase_, Slot::FR) + pathHtml(*introCase_, Slot::FR);
  }
  if (introStage_ < introTriggers()) {
    // While the triggers are shown one by one, the whole list is in view.
    return "<p>" + tr("Every good trigger from this AUF:") + "</p>" +
           optionsHtml(round_.start, round_.slot) +
           QString("<p style='color:%1'>%2</p>")
               .arg(muted,
                    tr("Each good trigger is shown where it needs no U turn "
                       "first. Afterwards you pick the best one from %1 "
                       "random AUFs.")
                        .arg(kIntroPicks));
  }
  return {};
}

// --- the case list -----------------------------------------------------------

void TslePage::fillTree() {
  QSignalBlocker block(tree_);
  tree_->clear();
  CubeLook look = cubeView_->look();
  look.map = kIdentityColourMap;
  const int size = settings_.thumbnailSize;
  tree_->setIconSize({size, size});
  QTreeWidgetItem *group = nullptr;
  int shown = -1;
  for (TsleState c : tsleCases()) {
    if (groupOf(c) != shown) {
      shown = groupOf(c);
      group = new QTreeWidgetItem(tree_);
      group->setData(0, Qt::UserRole + 1, shown);
      group->setFirstColumnSpanned(true);
      group->setFlags(Qt::ItemIsEnabled);
      QFont f = group->font(0);
      f.setBold(true);
      group->setFont(0, f);
    }
    auto *item = new QTreeWidgetItem(group);
    item->setData(0, Qt::UserRole, qs(tsleKey(c)));
    item->setText(0, qs(tsleName(c)));
    item->setIcon(0, cube_painter::cachedDiagram(tsleDiagram(c),
                                                 DiagramView::Iso3D, size, look,
                                                 devicePixelRatioF()));
    for (int col = 1; col < 4; ++col) {
      item->setTextAlignment(col, Qt::AlignRight | Qt::AlignVCenter);
    }
  }
  tree_->expandAll();
  refreshTree();
}

void TslePage::refreshTree() {
  const TsleProgress &p = store_.data;
  const double overall = meanMs(p);
  const QBrush normal = palette().brush(QPalette::Text);
  const QBrush muted = palette().brush(QPalette::PlaceholderText);
  const int open = tsleOpenLevel(p);
  for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
    QTreeWidgetItem *group = tree_->topLevelItem(g);
    int level = group->data(0, Qt::UserRole + 1).toInt();
    QString title = level == 0   ? tr("OCLL")
                    : level == 1 ? tr("1 step left")
                                 : tr("%1 steps left").arg(level);
    if (level > open) {
      title += " — " + tr("opens when the level before is solid");
    }
    group->setText(0, title);
    for (int i = 0; i < group->childCount(); ++i) {
      QTreeWidgetItem *item = group->child(i);
      std::string key = item->data(0, Qt::UserRole).toString().toStdString();
      bool introduced = p.isIntroduced(*tsleFromKey(key));
      auto it = p.cases.find(key);
      if (it == p.cases.end()) {
        item->setText(1, introduced ? QString() : tr("new"));
        item->setText(2, kNoValue);
        item->setText(3, kNoValue);
        item->setForeground(0, introduced ? normal : muted);
        item->setForeground(1, muted);
        item->setForeground(2, normal);
        continue;
      }
      CaseSummary s = summarizeCase(it->second, overall);
      item->setText(1, QString::number(s.attempts));
      item->setText(2, "+" + QString::number(s.excess, 'f', 1));
      item->setText(3, s.meanMs > 0
                           ? clockText(static_cast<std::int64_t>(s.meanMs))
                           : kNoValue);
      item->setForeground(0, s.weak ? QBrush(kWeak) : normal);
      item->setForeground(1, normal);
      item->setForeground(2, s.weak ? QBrush(kWeak) : normal);
    }
  }
  showSummary();
}

void TslePage::showSummary() {
  const TsleProgress &p = store_.data;
  const double overall = meanMs(p);
  int toIntroduce = 0, introduced = 0, weak = 0;
  for (TsleState c : tsleCases()) {
    if (!c.ocll()) {
      ++toIntroduce;
      introduced += p.isIntroduced(c) ? 1 : 0;
    }
    auto it = p.cases.find(tsleKey(c));
    if (it != p.cases.end() && summarizeCase(it->second, overall).weak) {
      ++weak;
    }
  }
  QStringList lines;
  lines << tr("<b>%1</b> of %2 cases introduced, <b>%3</b> weak")
               .arg(introduced)
               .arg(toIntroduce)
               .arg(weak);
  if (!p.fulls.empty()) {
    std::size_t from = p.fulls.size() > 12 ? p.fulls.size() - 12 : 0;
    double moves = 0.0, ms = 0.0, extra = 0.0;
    for (std::size_t i = from; i < p.fulls.size(); ++i) {
      moves += p.fulls[i].moves;
      ms += static_cast<double>(p.fulls[i].ms);
      extra += p.fulls[i].excess;
    }
    auto n = static_cast<double>(p.fulls.size() - from);
    lines << tr("Full TSLE, last %1: <b>%2</b> moves (+%3), %4 s on average")
                 .arg(static_cast<int>(n))
                 .arg(QString::number(moves / n, 'f', 1),
                      QString::number(extra / n, 'f', 1),
                      clockText(static_cast<std::int64_t>(ms / n)));
  }
  summary_->setText(lines.join("<br>"));
}

void TslePage::selectInTree(TsleState c) {
  const QString key = qs(tsleKey(c));
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

void TslePage::treeSelected() {
  QTreeWidgetItem *item = tree_->currentItem();
  if (!item || drill_ != Drill::Explore) {
    return;
  }
  if (auto c =
          tsleFromKey(item->data(0, Qt::UserRole).toString().toStdString())) {
    explored_ = *c;
    exploreAuf_ = 0;
    if (active_) {
      newRound();
    }
  }
}

void TslePage::clearProgress() {
  if (QMessageBox::question(
          this, "v-cube",
          tr("Clear all TSLE progress: the cases' tries, which cases are "
             "introduced, and the Full TSLE solves? Your OCLL algs are "
             "kept.")) != QMessageBox::Yes) {
    return;
  }
  TsleProgress fresh;
  fresh.ocll = store_.data.ocll;
  store_.data = fresh;
  ready_ = false;
  save();
  refreshTree();
  introCase_.reset();
  if (active_) {
    newRound();
  }
}

void TslePage::editOcllAlgs() {
  QDialog dialog(this);
  dialog.setWindowTitle(tr("OCLL algs"));
  auto *layout = new QVBoxLayout(&dialog);
  layout->addWidget(helpLabel(
      tr("The alg a chain ends with when an OCLL is left. They are shown, "
         "and their lengths counted, as written here for every slot; when "
         "you solve, any alg that works counts."),
      &dialog));
  auto *form = new QFormLayout;
  std::vector<OcllAlg> algs = store_.data.ocllAlgs();
  std::vector<QLineEdit *> edits;
  for (const OcllAlg &a : algs) {
    auto *edit = new QLineEdit(qs(a.alg), &dialog);
    edit->setObjectName(qs(a.id));
    edit->setMinimumWidth(320);
    form->addRow(qs(a.name) + ":", edit);
    edits.push_back(edit);
  }
  layout->addLayout(form);
  auto *problem = new QLabel(&dialog);
  problem->setWordWrap(true);
  layout->addWidget(problem);
  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                               QDialogButtonBox::RestoreDefaults,
                           &dialog);
  layout->addWidget(buttons);
  connect(buttons->button(QDialogButtonBox::RestoreDefaults),
          &QPushButton::clicked, &dialog, [&edits] {
            const std::vector<OcllAlg> &defaults = defaultOcllAlgs();
            for (std::size_t i = 0; i < edits.size(); ++i) {
              edits[i]->setText(qs(defaults[i].alg));
            }
          });
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    for (std::size_t i = 0; i < edits.size(); ++i) {
      std::string text = cleanAlgText(edits[i]->text().toStdString());
      if (!ocllAlgWorks(algs[i].id, parseAlgOrEmpty(text))) {
        problem->setText(coloured(
            kFailColour, tr("The %1 alg doesn't solve that OCLL (from any U "
                            "turn, keeping the F2L).")
                             .arg(qs(algs[i].name))));
        edits[i]->setFocus();
        return;
      }
    }
    dialog.accept();
  });
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  const std::vector<OcllAlg> &defaults = defaultOcllAlgs();
  store_.data.ocll.clear();
  for (std::size_t i = 0; i < edits.size(); ++i) {
    std::string text = cleanAlgText(edits[i]->text().toStdString());
    if (text != defaults[i].alg) {
      store_.data.ocll[defaults[i].id] = text;
    }
  }
  save();
  rebuildSolvers();
  if (active_) {
    showKeys();
    showResult();
  }
}

} // namespace vcube
