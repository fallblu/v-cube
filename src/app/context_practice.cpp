#include "app/context_practice.hpp"

#include "io/algset_json.hpp"
#include "train/move_count.hpp"
#include "train/solve_review.hpp"

#include <QCoreApplication>
#include <QStringList>

#include <algorithm>
#include <iterator>

namespace vcube {

namespace {

QString tr(const char *text) {
  return QCoreApplication::translate("vcube::LearnPage", text);
}

constexpr Slot kAllSlots[] = {Slot::FR, Slot::FL, Slot::BL, Slot::BR};

// Tries for an in-context state whose open pairs are all cases already
// started in Learn, before taking any.
constexpr int kContextTries = 40;

QString movesText(int n) {
  return n == 1 ? tr("1 move") : tr("%1 moves").arg(n);
}

} // namespace

ContextPractice::ContextPractice(
    AlgSet &set, int open, Rng &rng,
    const std::function<bool(const std::string &)> &started) {
  CubieCube state = randomPairState(open, rng);
  for (int i = 0; i < kContextTries; ++i) {
    CubieCube candidate = i == 0 ? state : randomPairState(open, rng);
    std::vector<PairOption> options = pairOptions(candidate, &set);
    bool allStarted = std::all_of(
        options.begin(), options.end(), [&started](const PairOption &o) {
          return !o.caseId.empty() && started(o.caseId);
        });
    if (allStarted) {
      state = candidate;
      break;
    }
  }
  start_ = state;
}

void ContextPractice::reset() {
  moves_.clear();
  gaveUp_ = false;
}

bool ContextPractice::turned(Move m, const FaceletCube &cube) {
  moves_.push_back(m);
  FaceletCube f = cube;
  f.normalizeOrientation();
  auto c = CubieCube::fromFacelets(f);
  return c && eoCrossSolved(*c) &&
         std::all_of(std::begin(kAllSlots), std::end(kAllSlots),
                     [&](Slot slot) { return slotSolved(*c, slot); });
}

QString ContextPractice::statusText(bool done) const {
  if (done) {
    return {};
  }
  int open = 0;
  for (Slot slot : kAllSlots) {
    open += slotSolved(start_, slot) ? 0 : 1;
  }
  return (open == 1 ? tr("In context: solve the open pair.")
                    : tr("In context: solve the %1 open pairs in any order.")
                          .arg(open)) +
         " " + tr("Not graded.");
}

QString ContextPractice::resultHtml(const AlgSet &set, bool done,
                                    const QString &muted) const {
  auto grey = [&](const QString &t) {
    return "<span style='color:" + muted + "'>" + t + "</span>";
  };
  QString html;
  if (done) {
    html += gaveUp_ ? "<p>" + tr("Gave up.") + "</p>"
                    : "<p><b>" +
                          tr("Solved in %1.").arg(movesText(htmCount(moves_))) +
                          "</b></p>";
    F2LBreakdown b = f2lBreakdown(start_.toFacelets(), moves_, &set);
    for (const PairSegment &seg : b.segments) {
      QStringList slotNames;
      for (Slot slot : seg.solved) {
        slotNames << qs(std::string(slotName(slot)));
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
  PairPlan plan = cheapestPairOrder(start_, set);
  if (!plan.steps.empty()) {
    html +=
        "<p>" + tr("The set's cheapest order, %1:").arg(movesText(plan.moves));
    for (const PairOption &o : plan.steps) {
      html +=
          "<br>" +
          tr("%1 (%2): %3")
              .arg(qs(std::string(slotName(o.slot))),
                   qs(o.caseName).toHtmlEscaped(), qs(o.alg).toHtmlEscaped()) +
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
