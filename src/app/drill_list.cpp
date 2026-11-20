// DrillPage's case list: its columns and statuses, the cases a drill
// picks from (scope, pool and order) and the right-click menu.
#include "app/drill_page.hpp"
#include "app/ui_text.hpp"

#include "app/drill_history_dialog.hpp"
#include "app/ui_state.hpp"

#include <QComboBox>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QToolButton>
#include <QTreeWidget>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <unordered_set>

namespace vcube {

QStringList DrillPage::columns() const {
  return {tr("Avg"), tr("Best"), tr("Fail"), tr("Weak")};
}

std::vector<CasePage::CaseStatusFilter> DrillPage::statusFilters() {
  return {
      {tr("Learned"),
       [this](const std::string &id) {
         return eligible(id) && statusOf(cardOf(id)) == CaseStatus::Learned;
       }},
      {tr("Never drilled"),
       [this](const std::string &id) {
         const CaseProgress *cp = progressOf(id);
         return eligible(id) && (!cp || cp->drillCount(attemptMode()) == 0);
       }},
      {tr("Slower than the target"),
       [this](const std::string &id) {
         const CaseProgress *cp = progressOf(id);
         std::optional<double> mean =
             cp ? cp->recentMean(attemptMode()) : std::nullopt;
         const AlgSet *s = set();
         return mean && s && s->targetSeconds > 0 &&
                *mean > s->targetSeconds * 1000.0;
       }},
      {tr("Slow to recognise"),
       [this](const std::string &id) {
         const CaseProgress *cp = progressOf(id);
         std::optional<double> mean = cp ? cp->recognitionMean() : std::nullopt;
         return mean && *mean > settings_.slowRecognitionMs;
       }},
  };
}

void DrillPage::fillRow(QTreeWidgetItem *item, const std::string &caseId) {
  const AttemptMode mode = attemptMode();
  const CaseProgress *cp = progressOf(caseId);
  const AlgSet *s = set();
  double target = s ? s->targetSeconds * 1000.0 : 0.0;
  std::optional<double> mean = cp ? cp->recentMean(mode) : std::nullopt;
  std::optional<double> best;
  if (auto b = cp ? cp->best(mode) : std::nullopt) {
    best = static_cast<double>(*b);
  }
  std::size_t count = cp ? cp->drillCount(mode) : 0;
  std::optional<double> fail;
  if (count) {
    fail = cp->failRate(mode);
  }
  std::optional<double> weak = drillWeakness(cp, mode, target);
  item->setText(1, secondsText(mean));
  item->setText(2, secondsText(best));
  item->setText(3, fail ? QString("%1%").arg(std::lround(*fail * 100))
                        : QString("-"));
  item->setText(4, weak ? QString::number(*weak, 'f', 1) : QString("-"));
  item->setData(1, kSortRole, mean ? QVariant(*mean) : QVariant());
  item->setData(2, kSortRole, best ? QVariant(*best) : QVariant());
  item->setData(3, kSortRole, fail ? QVariant(*fail) : QVariant());
  item->setData(4, kSortRole, weak ? QVariant(*weak) : QVariant());
  if (mean && target > 0) {
    item->setForeground(1, *mean <= target ? kPassColour : kWarnColour);
    if (*mean > target) {
      item->setText(1, kOverMark + " " + secondsText(mean));
    }
  } else {
    item->setForeground(1, palette().color(QPalette::Text));
  }
  QString tip =
      (onVirtualCube()
           ? tr("Mean of the last 5 good drill attempts on the virtual cube; "
                "%1 virtual drill attempts kept")
           : tr("Mean of the last 5 good drill attempts on a physical cube; "
                "%1 physical drill attempts kept"))
          .arg(count);
  for (int c = 1; c <= 3; ++c) {
    item->setToolTip(c, tip);
  }
  item->setToolTip(
      4, tr("How weak the case is, as Weighted order weighs it: (the median "
            "of its last 5 good times over the target)², held between 0.25 "
            "and 9, times 1 + 3 × its fail rate. 1 is a case at the target "
            "that never fails; higher comes up more. Click the header to "
            "sort by it."));
}

void DrillPage::fillGroupRow(QTreeWidgetItem *item,
                             const std::vector<std::string> &caseIds) {
  // The group's cases taken together: the mean of their means, fail rates
  // and weaknesses, and the best of their bests, over the drilled ones.
  const AttemptMode mode = attemptMode();
  const AlgSet *s = set();
  double target = s ? s->targetSeconds * 1000.0 : 0.0;
  double meanSum = 0, failSum = 0, weakSum = 0;
  int means = 0, drilled = 0;
  std::optional<double> best;
  for (const auto &id : caseIds) {
    const CaseProgress *cp = progressOf(id);
    if (!cp || cp->drillCount(mode) == 0) {
      continue;
    }
    ++drilled;
    failSum += cp->failRate(mode);
    weakSum += drillWeakness(cp, mode, target).value_or(0.0);
    if (auto m = cp->recentMean(mode)) {
      meanSum += *m;
      ++means;
    }
    if (auto b = cp->best(mode); b && (!best || *b < *best)) {
      best = static_cast<double>(*b);
    }
  }
  std::optional<double> mean, fail, weak;
  if (means) {
    mean = meanSum / means;
  }
  if (drilled) {
    fail = failSum / drilled;
    weak = weakSum / drilled;
  }
  item->setText(1, secondsText(mean));
  item->setText(2, secondsText(best));
  item->setText(3, fail ? QString("%1%").arg(std::lround(*fail * 100))
                        : QString("-"));
  item->setText(4, weak ? QString::number(*weak, 'f', 1) : QString("-"));
  item->setData(1, kSortRole, mean ? QVariant(*mean) : QVariant());
  item->setData(2, kSortRole, best ? QVariant(*best) : QVariant());
  item->setData(3, kSortRole, fail ? QVariant(*fail) : QVariant());
  item->setData(4, kSortRole, weak ? QVariant(*weak) : QVariant());
  QColor muted = palette().color(QPalette::PlaceholderText);
  const QString tip = tr("The group's %1 drilled cases of %2 taken together: "
                         "the mean of their means, fail rates and "
                         "weaknesses, and the best of their bests")
                          .arg(drilled)
                          .arg(caseIds.size());
  for (int c = 1; c <= 4; ++c) {
    item->setForeground(c, muted);
    item->setToolTip(c, tip);
  }
}

void DrillPage::updateSummary() {
  AlgSet *s = set();
  if (!s) {
    summary_->setText(tr("No alg sets. Create one in Library mode."));
    return;
  }
  // The picker says what is drilled: "Ticked cases · All (12)".
  const auto drilled = pool().size();
  casesButton_->setText(
      tr("%1 · %2 (%3)")
          .arg(scopeBox_->currentText(), poolBox_->currentText())
          .arg(drilled));
  QString text = tr("%1 of %2 ticked, %3 to drill. Target time %4 s.")
                     .arg(tickedCount())
                     .arg(s->caseCount())
                     .arg(drilled)
                     .arg(s->targetSeconds, 0, 'f', 2);
  if (picker_.mode() == PickMode::Recap && picker_.recapLeft() > 0) {
    text += " " + tr("Recap: %1 still to come.").arg(picker_.recapLeft());
  }
  if (int excluded = excludedCount()) {
    text += " " + skippedText(excluded);
  }
  summary_->setText(text);
}

DrillPage::Pool DrillPage::poolChoice() const {
  return currentEnum(poolBox_, Pool::Learned);
}

bool DrillPage::drillable(const std::string &caseId) const {
  const AlgSet *s = set();
  return s && s->findCase(caseId) && s->groupOf(caseId) && eligible(caseId);
}

std::vector<std::string> DrillPage::scopeCases() const {
  QString scope = scopeBox_->currentData().toString();
  std::vector<std::string> ids;
  if (scope == kScopeShown) {
    ids = shownEligible();
  } else if (scope.startsWith(kScopeGroup)) {
    std::string group = ss(scope.mid(kScopeGroup.size()));
    ids = groupEligible(group);
  } else {
    ids = tickedEligible();
  }
  // The rows' cases are eligible already; a list not yet rebuilt may still
  // show cases the set no longer has. Looked up by id, not one findCase()
  // per case, which would go through the whole set each time.
  const AlgSet *s = set();
  if (!s) {
    return {};
  }
  std::unordered_set<std::string_view> inSet;
  for (const auto &g : s->groups) {
    for (const auto &c : g.cases) {
      inSet.insert(c.id);
    }
  }
  std::erase_if(ids,
                [&](const std::string &id) { return !inSet.contains(id); });
  return ids;
}

std::vector<std::string> DrillPage::pool() const {
  Pool choice = poolChoice();
  std::vector<std::string> scope = scopeCases();
  if (choice == Pool::All) {
    return scope;
  }
  if (choice == Pool::Weakest) {
    const AlgSet *s = set();
    std::vector<std::string> weak = weakestCases(
        scope, progress(), attemptMode(), s ? s->targetSeconds * 1000.0 : 0.0,
        static_cast<std::size_t>(settings_.drill.weakestCount));
    // In list order, as the others are.
    std::vector<std::string> out;
    for (const auto &id : scope) {
      if (std::find(weak.begin(), weak.end(), id) != weak.end()) {
        out.push_back(id);
      }
    }
    return out;
  }
  std::vector<std::string> out;
  for (const auto &id : scope) {
    const CaseProgress *card = cardOf(id);
    CaseStatus st = statusOf(card);
    bool queued = card && isQueued(card->card);
    if ((choice == Pool::Learned && st == CaseStatus::Learned) ||
        (choice == Pool::Learning && st == CaseStatus::Learning && !queued) ||
        (choice == Pool::Queue && queued)) {
      out.push_back(id);
    }
  }
  return out;
}

void DrillPage::fillScopes() {
  QSignalBlocker block(scopeBox_);
  scopeBox_->clear();
  scopeBox_->addItem(tr("Ticked cases"), kScopeTicked);
  scopeBox_->addItem(tr("Cases shown in the list"), kScopeShown);
  if (const AlgSet *s = set()) {
    if (s->groups.size() > 1) {
      scopeBox_->insertSeparator(scopeBox_->count());
      for (const auto &g : s->groups) {
        scopeBox_->addItem(tr("Group: %1").arg(qs(g.name)),
                           kScopeGroup + qs(g.name));
      }
    }
  }
  int i = scopeBox_->findData(saved::drillScope(setId_).get());
  scopeBox_->setCurrentIndex(std::max(0, i));
}

void DrillPage::loadChoices() {
  selectEnum(cubeBox_, cubeKindFromId(ss(saved::drillCube(setId_).get()))
                           .value_or(CubeKind::Virtual));
  picker_.setAttemptMode(attemptMode());
  showCubeKind();
  fillScopes();
  selectEnum(
      poolBox_,
      poolFromId(saved::drillPool(setId_).get()).value_or(Pool::Learned));
  PickMode order = pickModeFromId(ss(saved::drillOrder(setId_).get()))
                       .value_or(PickMode::Random);
  selectEnum(orderBox_, order);
  picker_.setMode(order);
  picker_.restartRecap();
}

QString DrillPage::startCaseLabel(const std::string &) const {
  return tr("Drill this case now");
}

void DrillPage::extendCaseMenu(QMenu &menu,
                               const std::vector<std::string> &caseIds) {
  const bool virt = onVirtualCube();
  if (caseIds.size() == 1) {
    std::string id = caseIds.front();
    menu.addAction(tr("Drill history…"), this, [this, id] {
      showDrillHistory(this, caseName(id), progressOf(id), attemptMode(),
                       settings_.decimals);
    });
  }
  QAction *clear =
      menu.addAction(virt ? tr("Clear virtual drill times…")
                          : tr("Clear physical drill "
                               "times…"),
                     this, [this, caseIds] { clearTimes(caseIds); });
  clear->setEnabled(!isBusy());
}

void DrillPage::clearTimes(const std::vector<std::string> &caseIds) {
  if (isBusy()) {
    return;
  }
  QString what = caseIds.size() == 1 ? caseName(caseIds.front())
                                     : tr("%1 cases").arg(caseIds.size());
  auto answer = QMessageBox::question(
      this, tr("Clear drill times"),
      (onVirtualCube()
           ? tr("Forget every virtual-cube drill time of %1, best and daily "
                "history included? Learn progress stays.")
           : tr("Forget every physical-cube drill time of %1, best and daily "
                "history included? Learn progress stays."))
          .arg(what));
  if (answer != QMessageBox::Yes) {
    return;
  }
  for (const auto &id : caseIds) {
    auto &p = progress();
    if (auto it = p.find(id); it != p.end()) {
      it->second.clearDrill(attemptMode());
    }
    picker_.requeue(id, 0);
    if (deleted_ && deleted_->caseId == id) {
      deleted_.reset();
    }
    if (current_ && current_->caseId == id && flow_.recorded()) {
      flow_.forgotten(); // nothing left to toggle or delete
      showResult();
      showStatus();
    }
  }
  saveProgress();
  updateAllRows();
}

QString DrillPage::caseName(const std::string &caseId) const {
  const AlgSet *s = set();
  const AlgCase *c = s ? s->findCase(caseId) : nullptr;
  return c ? qs(c->name) : qs(caseId);
}

} // namespace vcube
