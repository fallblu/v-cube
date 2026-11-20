#include "app/context_practice.hpp"

#include "app/ui_text.hpp"

#include "io/algset_json.hpp"
#include "train/move_count.hpp"
#include "train/solve_review.hpp"

#include <QStringList>

namespace vcube {

namespace {

constexpr char kTrContext[] = "vcube::LearnPage";
constexpr auto tr = trIn<kTrContext>;

} // namespace

QString contextStatusText(const ContextPractice &practice, bool done) {
  if (done) {
    return {};
  }
  int open = practice.openPairs();
  return (open == 1 ? tr("In context: solve the open pair.")
                    : tr("In context: solve the %1 open pairs in any order.")
                          .arg(open)) +
         " " + tr("Not graded.");
}

QString contextResultHtml(const ContextPractice &practice, const AlgSet &set,
                          bool done, const QString &muted) {
  auto grey = [&](const QString &t) {
    return "<span style='color:" + muted + "'>" + t + "</span>";
  };
  QString html;
  if (done) {
    html += practice.gaveUp()
                ? "<p>" + tr("Gave up.") + "</p>"
                : "<p><b>" +
                      tr("Solved in %1.")
                          .arg(movesText(htmCount(practice.moves()))) +
                      "</b></p>";
    F2LBreakdown b =
        f2lBreakdown(practice.start().toFacelets(), practice.moves(), &set);
    for (const PairSegment &seg : b.segments) {
      QStringList slotNames;
      for (Slot slot : seg.solved) {
        slotNames << qs(slotName(slot));
      }
      QString line =
          tr("%1: you %2").arg(slotNames.join(" + "), movesText(seg.htm));
      for (const PairOption &o : seg.options) {
        if (seg.solved.size() == 1 && o.slot == seg.solved.front() &&
            !o.caseId.empty()) {
          line += " · " + tr("%1: %2 %3")
                              .arg(qs(o.caseName).toHtmlEscaped(),
                                   movesText(o.moves),
                                   grey(qs(o.alg).toHtmlEscaped()));
        }
      }
      html += line + "<br>";
    }
  }
  PairPlan plan = cheapestPairOrder(practice.start(), set);
  if (!plan.steps.empty()) {
    html +=
        "<p>" + tr("The set's cheapest order, %1:").arg(movesText(plan.moves));
    for (const PairOption &o : plan.steps) {
      html += "<br>" +
              tr("%1 (%2): %3")
                  .arg(qs(slotName(o.slot)), qs(o.caseName).toHtmlEscaped(),
                       qs(o.alg).toHtmlEscaped()) +
              " " + grey(movesText(o.moves));
    }
    if (!plan.complete) {
      html += "<br>" + grey(tr("Some pairs have no case in the set."));
    }
    html += "</p>";
  }
  return html;
}

} // namespace vcube
