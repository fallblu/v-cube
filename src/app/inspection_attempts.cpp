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

// Optimal solutions worked out per hold, to show three that differ.
constexpr int kSolutionsPerHold = 12;

// What a cube that didn't reach `step` did get: "EO not done (3 bad edges
// left); 2 of 4 cross edges solved".
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

// --- Plan ------------------------------------------------------------------

void InspectionPage::startInspection() {
  if (flow_.retry()) {
    GeneratedScramble again;
    again.text = qs(retryOf_.scramble);
    scramble_ = again;
  } else {
    scramble_ = *feed_.current(); // the flow waited for it
  }
  // Scrambled holding white on top and green in front, as in Move Count.
  attempt_.load(parseAlgOrEmpty(ss(scramble_->text)));
  cubeView_->clearMarks();
  cubeView_->setState(attempt_.cube());
  scrambleLabel_->setText(scramble_->text.toHtmlEscaped());
  showPhase();
}

void InspectionPage::startExecution() {
  attempt_.start();
  showPhase();
}

void InspectionPage::finishPlan() {
  if (!scramble_) {
    return;
  }
  SolveStep step = session().step;
  const FaceletCube &end = attempt_.cube();
  bool reached = stepReached(end, step);
  if (attempt_.count() == 0 && !reached) {
    // Most likely a Space meant to hide the cube: nothing is recorded.
    emit message(tr("No moves done yet: do your plan, then press Space (Esc "
                    "puts the scramble back)"));
    return;
  }
  InspectionAttempt a;
  a.scramble = ss(scramble_->text);
  a.timestamp = unixNow();
  a.ms = flow_.usedMs();
  a.moves = std::clamp(attempt_.count(), 0, kMaxMoveCount);
  a.dnf = !reached;
  a.solution = toString(attempt_.solution());
  if (reached) {
    a.solvedIn = doneHold(end, step);
  }
  std::optional<Slot> pair = trackedPair();
  if (pair) {
    a.pairSolved = false;
    if (reached && a.solvedIn) {
      // The moves to EO Cross are compared with the optimum; the rest went
      // on the pair.
      FaceletCube start;
      start.apply(parseAlgOrEmpty(a.scramble));
      std::vector<StepMark> marks =
          stepMarks(start, attempt_.solution(), {SolveStep::EOCross});
      if (!marks.empty()) {
        a.pairMoves = std::max(0, a.moves - marks.front().htm);
        a.moves = marks.front().htm;
      }
      std::optional<CubieCube> held = heldCubie(end, *a.solvedIn);
      a.pairSolved = held && slotSolved(*held, *pair);
    }
  }

  QString stepText = qs(std::string(stepName(step)));
  QString moves = a.moves == 1 ? tr("1 move") : tr("%1 moves").arg(a.moves);
  QString head =
      reached ? QString("<b style='color:%1'>%2</b>")
                    .arg(kGood.name(), tr("%1 done in %2").arg(stepText, moves))
              : QString("<b style='color:%1'>DNF</b>: %2")
                    .arg(kBad.name(),
                         tr("%1 wasn't done (%2): %3")
                             .arg(stepText, moves, progressText(end, step))
                             .toHtmlEscaped());
  if (pair && reached) {
    QString pairText = tr("the %1 pair").arg(qs(std::string(slotName(*pair))));
    head +=
        "<br>" +
        (*a.pairSolved
             ? QString("<b style='color:%1'>%2</b>")
                   .arg(kGood.name(),
                        tr("then %1 in %2")
                            .arg(pairText,
                                 a.pairMoves == 1
                                     ? tr("1 move")
                                     : tr("%1 moves").arg(a.pairMoves)))
             : QString("<b style='color:%1'>%2</b>")
                   .arg(kBad.name(), tr("%1 wasn't solved as the cross is held")
                                         .arg(pairText)));
  }
  std::optional<std::size_t> retry = flow_.retry();
  if (retry) {
    const InspectionAttempt &was = retryOf_;
    head += "<br>" + tr("Retry of #%1 (not recorded); it was %2")
                         .arg(*retry + 1)
                         .arg(was.dnf ? QString("DNF(%1)").arg(was.moves)
                                      : QString::number(was.moves));
  }
  resultHead_ = "<p style='font-size:large'>" + head + "</p>";
  QString holdText =
      a.solvedIn
          ? " " + tr("in %1").arg(holdHtml(*a.solvedIn, settings_.scheme))
          : QString();
  resultHead_ +=
      "<p>" +
      tr("Inspection: %1. Your moves%2: %3")
          .arg(secondsText(a.ms), holdText,
               a.solution.empty()
                   ? tr("none")
                   : "<code>" + qs(a.solution).toHtmlEscaped() + "</code>") +
      "</p>";

  std::size_t index = 0;
  if (retry) {
    index = *retry;
  } else {
    session().attempts.push_back(a);
    index = session().attempts.size() - 1;
    save();
    refreshAttempts();
    feed_.advance(); // this scramble has been seen: the next one is new
  }
  shownMoves_ = a.dnf ? std::nullopt : std::optional<int>(a.moves);
  lookTurns_.clear();
  cubeView_->setState(end);
  flow_.planDone();
  showPhase();
  display_->showText(a.dnf ? QString("DNF") : QString::number(a.moves),
                     a.dnf ? kBad : kGood);
  lastAnalysis_.reset();
  analyse(a.scramble, step, index);
  showAnalysis();
}

void InspectionPage::analyse(const std::string &scramble, SolveStep step,
                             std::size_t attempt) {
  std::vector<Hold> holds = withBothEoAxes(settings_.neutrality.holds);
  Analysis job;
  job.session = store_.currentIndex();
  job.attempt = attempt;
  job.scramble = scramble;
  job.step = step;
  analysis_.setFuture(QtConcurrent::run([job, holds]() mutable {
    Alg alg = parseAlgOrEmpty(job.scramble);
    job.neutral = solveStepHeld(alg, job.step, holds, kSolutionsPerHold);
    std::vector<Hold> all(allHolds().begin(), allHolds().end());
    std::vector<HeldOptimum> best = solveStepHeld(alg, job.step, all, 1);
    if (!best.empty()) {
      job.overall = best.front().optimum.length;
    }
    return job;
  }));
}

void InspectionPage::showAnalysis() {
  if (analysis_.isFinished() && analysis_.future().resultCount() > 0) {
    Analysis a = analysis_.result();
    if (!lastAnalysis_ || lastAnalysis_->scramble != a.scramble) {
      lastAnalysis_ = a;
      // Keep the optimum with the attempt, if it is still there.
      if (a.session < store_.sessions().size() && !a.neutral.empty()) {
        auto &attempts = store_.sessions()[a.session].attempts;
        if (a.attempt < attempts.size() &&
            attempts[a.attempt].scramble == a.scramble &&
            !attempts[a.attempt].optimal) {
          attempts[a.attempt].optimal = a.neutral.front().optimum.length;
          save();
          refreshAttempts();
        }
      }
    }
  }
  if (flow_.phase() != Phase::Result || !planning() || !scramble_) {
    return;
  }
  QString html = resultHead_;
  bool ready = lastAnalysis_ && lastAnalysis_->scramble == ss(scramble_->text);
  if (!ready) {
    html += "<p style='color:gray'>" +
            tr("Working out the optimal solutions…") + "</p>";
    resultLabel_->setText(html);
    return;
  }
  const Analysis &a = *lastAnalysis_;
  if (a.neutral.empty()) {
    resultLabel_->setText(html);
    return;
  }
  int best = a.neutral.front().optimum.length;
  QString yours;
  if (shownMoves_) {
    int over = *shownMoves_ - best;
    yours =
        over <= 0 ? tr(" (yours is optimal)") : tr(" (yours: +%1)").arg(over);
  }
  html += "<p>" +
          tr("Optimal in your holds: <b>%1</b>%2 · in any hold: %3")
              .arg(best)
              .arg(yours)
              .arg(a.overall ? QString::number(*a.overall) : QString("-")) +
          "</p>";
  // Up to three, none a trivial variant of another in the same hold: the
  // most different of each optimal hold in turn, then the next ones.
  std::vector<std::pair<const HeldOptimum *, std::vector<std::size_t>>> picks;
  for (const HeldOptimum &h : a.neutral) {
    if (h.optimum.length != best) {
      break;
    }
    picks.emplace_back(&h, distinctSolutions(h.optimum.solutions, 3));
  }
  QStringList lines;
  for (std::size_t rank = 0; rank < 3 && lines.size() < 3; ++rank) {
    for (const auto &[h, chosen] : picks) {
      if (rank >= chosen.size() || lines.size() >= 3) {
        continue;
      }
      const Alg &solution = h->optimum.solutions[chosen[rank]];
      QString rotation = qs(toString(holdRotation(h->hold)));
      QString moves =
          solution.empty() ? tr("(already done)") : qs(toString(solution));
      QString text = rotation.isEmpty() ? moves : rotation + " · " + moves;
      lines << stepHoldHtml(h->hold, session().step == SolveStep::EO,
                            settings_.scheme) +
                   ": <code>" + text.toHtmlEscaped() + "</code>";
    }
  }
  if (!lines.isEmpty()) {
    html += "<p>" + lines.join("<br>") + "</p>";
  }
  resultLabel_->setText(html);
}

void InspectionPage::backfillOptima() {
  if (backfill_.isRunning() || !planning()) {
    return;
  }
  std::vector<Filled> todo;
  const auto &attempts = session().attempts;
  for (std::size_t i = 0; i < attempts.size(); ++i) {
    if (!attempts[i].optimal && !attempts[i].scramble.empty()) {
      todo.push_back({i, attempts[i].scramble, 0});
    }
  }
  // The newest attempt's analysis is on its way already.
  if (todo.empty() || (todo.size() == 1 && analysis_.isRunning() &&
                       todo.front().attempt + 1 == attempts.size())) {
    return;
  }
  backfillSession_ = store_.currentIndex();
  std::vector<Hold> holds = withBothEoAxes(settings_.neutrality.holds);
  SolveStep step = session().step;
  backfill_.setFuture(QtConcurrent::run([todo, holds, step]() mutable {
    std::vector<Filled> out;
    for (Filled &f : todo) {
      if (auto d = heldDistance(parseAlgOrEmpty(f.scramble), step, holds)) {
        f.optimal = *d;
        out.push_back(f);
      }
    }
    return out;
  }));
}

// --- Edges -----------------------------------------------------------------

void InspectionPage::startCounting() {
  scramble_ = *feed_.current(); // the flow waited for it
  hold_ = scramble_->hold;
  // Scrambled holding white top and green front, then turned to the hold.
  FaceletCube f;
  f.apply(parseAlgOrEmpty(ss(scramble_->text)));
  f.apply(holdRotation(hold_));
  attempt_.load(f);
  cubeView_->clearMarks();
  cubeView_->setState(attempt_.cube());
  scrambleLabel_->setText(scramble_->text.toHtmlEscaped());
  showPhase();
}

void InspectionPage::finishEdges() {
  if (!scramble_) {
    return;
  }
  FaceletCube f;
  f.apply(parseAlgOrEmpty(ss(scramble_->text)));
  CubieCube held =
      solverState(CubieCube::fromFacelets(f).value_or(CubieCube{}), hold_);
  InspectionAttempt a;
  a.scramble = ss(scramble_->text);
  a.timestamp = unixNow();
  a.ms = flow_.elapsed(monotonicMs());
  a.hold = hold_;
  for (int axis = 0; axis < 2; ++axis) {
    if (asksAxis(session().axes, axis)) {
      auto i = static_cast<std::size_t>(axis);
      a.answer[i] = flow_.answers()[i];
      a.correct[i] = badEdgeCount(held, axis);
    }
  }
  session().attempts.push_back(a);
  save();
  refreshAttempts();
  feed_.advance(); // this scramble has been seen: the next one is new

  QStringList lines;
  for (int axis = 0; axis < 2; ++axis) {
    auto i = static_cast<std::size_t>(axis);
    if (a.correct[i] < 0) {
      continue;
    }
    bool right = a.answer[i] == a.correct[i];
    QString what =
        QString("%1 (%2)").arg(tr(axisName(axis)), axisColours(a.hold, axis));
    lines << (right ? QString("<b style='color:%1'>%2</b>")
                          .arg(kGood.name(),
                               tr("%1: %2, right").arg(what).arg(a.answer[i]))
                    : QString("<b style='color:%1'>%2</b>")
                          .arg(kBad.name(), tr("%1: you said %2, it's %3")
                                                .arg(what)
                                                .arg(a.answer[i])
                                                .arg(a.correct[i])));
  }
  lines << tr("Time: %1").arg(secondsText(a.ms));
  resultHead_ = "<p style='font-size:large'>" + lines.join("<br>") + "</p>";
  resultLabel_->setText(resultHead_);
  // Back to the hold it was shown in, with the bad edges ringed (those of
  // the first axis asked, or the first one that was wrong).
  attempt_.restart();
  lookTurns_.clear();
  cubeView_->setState(attempt_.cube());
  flow_.edgesDone(a.correct);
  showPhase();
  display_->showText(a.right() ? tr("Right") : tr("Wrong"),
                     a.right() ? kGood : kBad);
  showRings();
}

void InspectionPage::showRings() {
  int ring = flow_.ringAxis();
  if (!scramble_ || ring < 0) {
    cubeView_->clearMarks();
    return;
  }
  FaceletCube f;
  f.apply(parseAlgOrEmpty(ss(scramble_->text)));
  CubieCube held =
      solverState(CubieCube::fromFacelets(f).value_or(CubieCube{}), hold_);
  std::array<bool, kEdges> bad = badEdges(held, ring);
  StickerMarks marks;
  marks.ringColour = kRing[ring];
  for (std::size_t e = 0; e < kEdges; ++e) {
    if (bad[e]) {
      for (std::uint8_t facelet : kEdgeFacelets[e]) {
        marks.rings.push_back(facelet);
      }
    }
  }
  // Moved with the cube if it was turned to look at it.
  for (Move m : lookTurns_) {
    marks = movedMarks(marks, m);
  }
  cubeView_->setMarks(marks);
  hintLabel_->setText(
      tr("Ringed: the bad edges for the %1 axis (%2). %3")
          .arg(tr(axisName(ring)), axisColours(hold_, ring),
               session().axes == EdgeAxes::Both
                   ? tr("Tab: the other axis. Space: next scramble.")
                   : tr("Space: next scramble.")));
}

void InspectionPage::startRetry(int index) {
  const auto &attempts = session().attempts;
  if (isBusy() || !planning() || index < 0 ||
      index >= static_cast<int>(attempts.size())) {
    return;
  }
  auto i = static_cast<std::size_t>(index);
  if (!flow_.startRetry(i)) {
    return;
  }
  retryOf_ = attempts[i];
  scramble_.reset();
  shownMoves_.reset();
  cubeView_->clearMarks();
  showPhase();
  showScramble();
}

void InspectionPage::requestReview(int index) {
  const auto &attempts = session().attempts;
  if (!planning() || index < 0 || index >= static_cast<int>(attempts.size())) {
    return;
  }
  const InspectionAttempt &a = attempts[static_cast<std::size_t>(index)];
  emit reviewRequested(qs(a.scramble), qs(a.solution),
                       static_cast<int>(session().step), index);
}

} // namespace vcube
