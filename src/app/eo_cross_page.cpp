#include "app/eo_cross_page.hpp"

#include "app/ui_state.hpp"
#include "app/ui_text.hpp"

#include <QComboBox>
#include <QFutureWatcher>
#include <QSignalBlocker>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>

namespace vcube {

namespace {

// Optimal solutions worked out per hold, to list a few that differ.
constexpr int kSolutionsPerHold = 12;

// The inspection limits offered, in seconds (0: none).
constexpr std::array<int, 6> kLimits = {0, 8, 10, 15, 20, 30};

// The rotations that turn a cube held as `from` to `to`.
Alg rotationBetween(Hold from, Hold to) {
  Alg r = invert(holdRotation(from));
  Alg on = holdRotation(to);
  r.insert(r.end(), on.begin(), on.end());
  return simplify(r);
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

EoCrossPage::EoCrossPage(AppSettings &settings, PracticeStore &store,
                         QWidget *parent)
    : PracticePage(settings, store, parent) {
  step_ = stepFromId(saved::practice("eocross", "step", "eocross")
                         .get()
                         .toString()
                         .toStdString())
              .value_or(SolveStep::EOCross);
  const std::vector<SolveStep> &steps = eoCrossSteps();
  if (std::find(steps.begin(), steps.end(), step_) == steps.end()) {
    step_ = SolveStep::EOCross;
  }
  limitSeconds_ =
      saved::practice("eocross", "inspectionLimit", 15).get().toInt();
  if (std::find(kLimits.begin(), kLimits.end(), limitSeconds_) ==
      kLimits.end()) {
    limitSeconds_ = 15;
  }
  build();
}

EoCrossPage::~EoCrossPage() = default;

// --- options -----------------------------------------------------------------

void EoCrossPage::addOptions(OptionBar *bar) {
  stepBox_ = new QComboBox(bar);
  stepBox_->setFocusPolicy(Qt::NoFocus);
  for (SolveStep s : eoCrossSteps()) {
    stepBox_->addItem(qs(vcube::stepName(s)), qs(stepId(s)));
  }
  bar->addPicker(tr("Step:"), stepBox_);
  limitBox_ = new QComboBox(bar);
  limitBox_->setFocusPolicy(Qt::NoFocus);
  for (int seconds : kLimits) {
    limitBox_->addItem(seconds == 0    ? tr("None")
                       : seconds == 15 ? tr("15 s (WCA)")
                                       : tr("%1 s").arg(seconds),
                       seconds);
  }
  limitItem_ = bar->addPicker(tr("Inspection:"), limitBox_);
  connect(stepBox_, &QComboBox::activated, this, [this] {
    if (auto s = stepFromId(stepBox_->currentData().toString().toStdString())) {
      setStep(*s);
    }
  });
  connect(limitBox_, &QComboBox::activated, this,
          [this] { setInspectionLimit(limitBox_->currentData().toInt()); });
}

void EoCrossPage::showOptions() {
  QSignalBlocker a(stepBox_), b(limitBox_);
  stepBox_->setCurrentIndex(stepBox_->findData(qs(stepId(step_))));
  limitBox_->setCurrentIndex(limitBox_->findData(limitSeconds_));
  limitItem_->setVisible(tab() == Tab::Inspection);
}

void EoCrossPage::setStep(SolveStep step) {
  step_ = step;
  saved::practice("eocross", "step").set(qs(stepId(step_)));
  optionChanged();
}

void EoCrossPage::setInspectionLimit(int seconds) {
  limitSeconds_ = std::max(0, seconds);
  saved::practice("eocross", "inspectionLimit").set(limitSeconds_);
  optionChanged();
}

std::vector<Hold> EoCrossPage::allowedHolds() const {
  return eoHolds(settings().neutrality);
}

// --- states ------------------------------------------------------------------

std::optional<PracticePage::Setup> EoCrossPage::setUp(Extent,
                                                      const std::string &) {
  const std::vector<Hold> allowed = allowedHolds();
  solving_ = step_;
  shown_ = settings().neutrality.main;
  // A random-state scramble; not one with the step already done.
  for (int tries = 0; tries < 60; ++tries) {
    scrambled_ = randomStateOf(scrambleTypes().front(), rng());
    if (!doneHold(scrambled_.toFacelets(), solving_, allowed)) {
      break;
    }
  }
  restarted();
  Setup s;
  s.cube = scrambled_.toFacelets();
  s.cube.apply(holdRotation(shown_));
  s.state = scrambled_;
  s.group = std::string(stepId(solving_));
  requestAnalysis();
  return s;
}

void EoCrossPage::restarted() { solvedIn_.reset(); }

bool EoCrossPage::turned() {
  solvedIn_ = doneHold(cube(), solving_, allowedHolds());
  return solvedIn_.has_value();
}

void EoCrossPage::requestAnalysis() {
  analysed_ = false;
  ranked_.clear();
  auto *watcher = new QFutureWatcher<Analysis>(this);
  connect(watcher, &QFutureWatcher<Analysis>::finished, this, [this, watcher] {
    Analysis a = watcher->result();
    watcher->deleteLater();
    if (a.request == requests_ && !analysed_) {
      ranked_ = std::move(a.ranked);
      analysed_ = true;
      // A verdict waiting for it is given now; one already up is given
      // again.
      analysisArrived(item().id);
    }
  });
  watcher->setFuture(QtConcurrent::run([request = ++requests_,
                                        scrambled = scrambled_, step = solving_,
                                        holds = allowedHolds()] {
    return Analysis{request,
                    solveStepHeld(scrambled, step, holds, kSolutionsPerHold)};
  }));
}

std::vector<EoCrossPage::Solution> EoCrossPage::shownSolutions() const {
  std::vector<Solution> out;
  // A few that differ per hold, fewer when there are many holds.
  const std::size_t perHold = ranked_.size() <= 2   ? 4
                              : ranked_.size() <= 4 ? 3
                                                    : 2;
  for (const HeldOptimum &r : ranked_) {
    for (std::size_t i : distinctSolutions(r.optimum.solutions, perHold)) {
      out.push_back({r.hold, r.optimum.solutions[i]});
    }
  }
  return out;
}

Alg EoCrossPage::fromShown(const Solution &s) const {
  Alg moves = rotationBetween(shown_, s.hold);
  moves.insert(moves.end(), s.alg.begin(), s.alg.end());
  return moves;
}

// --- what is said ------------------------------------------------------------

QString EoCrossPage::whereText(SolveStep step) const {
  const std::vector<Hold> holds = allowedHolds();
  if (step == SolveStep::EO) {
    // EO alone has only an axis.
    std::vector<int> axes;
    for (Hold h : holds) {
      if (std::find(axes.begin(), axes.end(), h.front % 3) == axes.end()) {
        axes.push_back(h.front % 3);
      }
    }
    return axes.size() == 2 ? tr("on either of your EO axes")
                            : tr("on any of your %1 EO axes").arg(axes.size());
  }
  QStringList crosses;
  for (Hold h : holds) {
    QString name = colourName((h.top + 3) % 6).toLower();
    if (!crosses.contains(name)) {
      crosses << name;
    }
  }
  return crosses.size() > 3
             ? tr("with any of your %1 cross colours, on either EO axis")
                   .arg(crosses.size())
             : tr("with a %1 cross, on either EO axis")
                   .arg(crosses.join(tr(" or ")));
}

QString EoCrossPage::task() const {
  return tr("Solve %1").arg(qs(vcube::stepName(solving_)));
}

QString EoCrossPage::taskNote() const { return whereText(solving_); }

PracticePage::Outcome EoCrossPage::judge() {
  Outcome o;
  if (!analysed_) {
    // The search is still running in the background (it is never run
    // here, where it would hold up the window): asked again when it
    // arrives.
    o.moves = solutionHtm(item().moves);
    o.verdict = tr("Working out the optimum…");
    return o;
  }
  if (ranked_.empty()) {
    return o;
  }
  const HeldOptimum &top = ranked_.front();
  o.bestMoves = top.optimum.length;
  if (!top.optimum.solutions.empty()) {
    o.best = fromShown({top.hold, top.optimum.solutions.front()});
  }
  if (item().moves.empty() && !item().gaveUp) {
    // A physical cube: only the optimum is known.
    o.verdict = tr("The optimum is %1").arg(movesText(o.bestMoves));
    return o;
  }
  o.moves = solutionHtm(item().moves);
  if (item().gaveUp || !solvedIn_) {
    FaceletCube ended = startCube();
    ended.apply(item().moves);
    o.verdict = coloured(kFailColour, item().moves.empty() ? tr("Gave up")
                                                           : tr("Not done")) +
                (item().moves.empty() ? QString()
                                      : " · " + progressText(ended, solving_)) +
                " · " + tr("the optimum is %1").arg(movesText(o.bestMoves));
    return o;
  }
  const int over = std::max(0, o.moves - o.bestMoves);
  o.extra = over;
  o.good = over == 0;
  o.verdict =
      tr("%1 in %2").arg(qs(vcube::stepName(solving_)), movesText(o.moves)) +
      " · " +
      (over == 0
           ? coloured(kPassColour, tr("optimal"))
           : overMarked(
                 tr("+%1 over the optimum (%2)").arg(over).arg(o.bestMoves),
                 over >= 3));
  return o;
}

QString EoCrossPage::breakdownHtml() const {
  if (solving_ != SolveStep::EOCross || !solvedIn_ || item().moves.empty()) {
    return {};
  }
  const EoBreakdown b = eoBreakdown(startCube(), item().moves, *solvedIn_);
  if (b.parts.empty()) {
    return {};
  }
  const QString muted = mutedColour();
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
    Alg moves(item().moves.begin() + static_cast<std::ptrdiff_t>(p.begin),
              item().moves.begin() + static_cast<std::ptrdiff_t>(p.end));
    QString lost = p.lost <= 0
                       ? coloured(kPassColour, tr("nothing"))
                       : overMarked("+" + QString::number(p.lost), p.lost >= 3);
    html += QString("<tr><td>%1</td><td><b>%2</b></td><td align='right'>%3"
                    "</td><td align='right'>%4</td><td align='right'>%5</td>"
                    "</tr>")
                .arg(qs(vcube::stepName(p.step)), algText(simplify(moves)))
                .arg(p.moves)
                .arg(p.toGo == 0 ? tr("done") : movesText(p.toGo), lost);
  }
  return html + "</table>";
}

QString EoCrossPage::holdsHtml() const {
  if (ranked_.empty()) {
    return {};
  }
  const QString muted = mutedColour();
  const bool eoOnly = solving_ == SolveStep::EO;
  std::optional<std::size_t> mine =
      solvedIn_ ? rankOfHold(ranked_, *solvedIn_, solving_) : std::nullopt;
  const int best = ranked_.front().optimum.length;
  std::vector<Solution> solutions = shownSolutions();
  QString html = "<table align='center' cellspacing='0' cellpadding='3'><tr "
                 "style='color:" +
                 muted + "'><th align='left'>" + tr("Hold") + "</th><th>" +
                 tr("Optimum") + "</th><th align='left'>" +
                 tr("Optimal solutions") + "</th><th></th></tr>";
  std::size_t at = 0;
  for (std::size_t i = 0; i < ranked_.size(); ++i) {
    const HeldOptimum &r = ranked_[i];
    QStringList algs;
    for (; at < solutions.size() && solutions[at].hold == r.hold; ++at) {
      algs << playLink(fromShown(solutions[at]),
                       solutions[at].alg.empty() ? tr("(done)")
                                                 : algText(solutions[at].alg));
    }
    if (r.optimum.more) {
      algs << QString("<span style='color:%1'>…</span>").arg(muted);
    }
    QString hold = stepHoldHtml(r.hold, eoOnly, settings().scheme);
    QString length = r.optimum.length == best
                         ? coloured(kPassColour, movesText(r.optimum.length))
                         : movesText(r.optimum.length);
    html += QString("<tr><td>%1</td><td align='right'>%2</td><td>%3</td>"
                    "<td style='color:%4'>%5</td></tr>")
                .arg(mine == i ? "<b>" + hold + "</b>" : hold, length,
                     algs.join(QStringLiteral(" &nbsp; ")), muted,
                     mine == i ? tr("yours") : QString());
  }
  return html + "</table>";
}

QString EoCrossPage::detail() {
  if (!analysed_) {
    return "<p>" + tr("Working out the best solutions…") + "</p>";
  }
  QString html;
  if (!item().moves.empty()) {
    QString line = tr("You: %1 (%2)")
                       .arg("<b>" + algText(simplify(item().moves)) + "</b>",
                            movesText(solutionHtm(item().moves)));
    if (solvedIn_) {
      line += ", " + stepHoldHtml(*solvedIn_, solving_ == SolveStep::EO,
                                  settings().scheme);
    }
    html += "<p>" + line + "</p>";
  }
  if (solvedIn_ && !ranked_.empty()) {
    if (auto mine = rankOfHold(ranked_, *solvedIn_, solving_)) {
      const int inHold = ranked_[*mine].optimum.length;
      const int best = ranked_.front().optimum.length;
      html += "<p>" +
              (inHold == best
                   ? tr("The optimum in that hold is %1, the best of your "
                        "holds.")
                         .arg(movesText(inHold))
                   : tr("The optimum in that hold is %1; the best of your "
                        "holds, %2, has %3.")
                         .arg(movesText(inHold),
                              stepHoldHtml(ranked_.front().hold,
                                           solving_ == SolveStep::EO,
                                           settings().scheme),
                              movesText(best))) +
              "</p>";
    }
  }
  return html + breakdownHtml() + holdsHtml();
}

// --- the lists ---------------------------------------------------------------

std::vector<PracticePage::Split> EoCrossPage::splits() {
  if (solving_ != SolveStep::EOCross || !solvedIn_ || item().moves.empty()) {
    return {};
  }
  std::vector<Split> out;
  for (const EoPart &p :
       eoBreakdown(startCube(), item().moves, *solvedIn_).parts) {
    out.push_back({qs(vcube::stepName(p.step)), p.end});
  }
  return out;
}

std::vector<PracticePage::ListRow> EoCrossPage::untimedGroups() {
  std::vector<ListRow> rows;
  for (SolveStep s : eoCrossSteps()) {
    rows.push_back({std::string(stepId(s)), qs(vcube::stepName(s)), {}, {}});
  }
  return rows;
}

std::vector<PracticePage::ListRow> EoCrossPage::timedGroups() {
  return untimedGroups();
}

void EoCrossPage::groupActivated(const std::string &key) {
  if (auto s = stepFromId(key)) {
    setStep(*s);
  }
}

QString EoCrossPage::listHelp() const {
  switch (tab()) {
  case Tab::Inspection:
    return tr("Your plans by step (F1 explains the columns). Double-click a "
              "step to practise it.");
  case Tab::Untimed:
    return tr("Your untimed solves by step (F1 explains the columns). "
              "Double-click a step to practise it.");
  default:
    return tr("Timed solves by step. Double-click a step to switch to it.");
  }
}

} // namespace vcube
