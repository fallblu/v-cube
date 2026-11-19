// LearnPage's case list: its columns and statuses, the header above the
// cube, the right-click menu and the learn queue.
#include "app/learn_page.hpp"
#include "app/ui_text.hpp"

#include "app/case_history_dialog.hpp"
#include "app/widgets.hpp"

#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>

#include <algorithm>
#include <cmath>

namespace vcube {

namespace {

const StatusColour kLearned{Status::Good};
const StatusColour kLeech{Status::Bad};

// Every set's due cards (whatever is ticked), for the set drop-down.
int dueCards(const SetProgress &progress, std::int64_t now, std::int64_t dayEnd,
             const SrsConfig &config) {
  int n = 0;
  for (const auto &[id, p] : progress) {
    n += isDueNow(p.card, now, dayEnd, config) ? 1 : 0;
  }
  return n;
}

} // namespace

QStringList LearnPage::columns() const {
  return {tr("Status"), tr("Due"), tr("Recog"), tr("Lapses"), tr("Ease")};
}

std::vector<CasePage::CaseStatusFilter> LearnPage::statusFilters() {
  // As the Status and Due columns show them.
  auto queued = [this](const std::string &id) {
    const CaseProgress *cp = cardOf(id);
    return cp && isQueued(cp->card);
  };
  auto status = [this, queued](CaseStatus st) {
    return [this, queued, st](const std::string &id) {
      const CaseProgress *cp = cardOf(id);
      return eligible(id) && !queued(id) && statusOf(cp) == st &&
             !(cp && isHeld(cp->card, unixNow()));
    };
  };
  return {
      {tr("New"), status(CaseStatus::Unstarted)},
      {tr("In the learn queue"), queued},
      {tr("Learning"), status(CaseStatus::Learning)},
      {tr("Learned"), status(CaseStatus::Learned)},
      {tr("Due"),
       [this](const std::string &id) {
         const CaseProgress *cp = cardOf(id);
         return cp &&
                isDueNow(cp->card, unixNow(), localDayEnd(), settings_.srs);
       }},
      {tr("Suspended or buried"),
       [this](const std::string &id) {
         const CaseProgress *cp = cardOf(id);
         return cp && isHeld(cp->card, unixNow());
       }},
      {tr("Leeches"),
       [this](const std::string &id) {
         const CaseProgress *cp = cardOf(id);
         return cp && isLeech(cp->card, settings_.srs);
       }},
      {tr("Slow to recognise"),
       [this](const std::string &id) {
         std::optional<double> mean = recognitionMean(attemptsOf(id));
         return mean && *mean > settings_.slowRecognitionMs;
       }},
  };
}

void LearnPage::fillRow(QTreeWidgetItem *item, const std::string &caseId) {
  const CaseProgress *cp = cardOf(caseId);
  QColor muted = palette().color(QPalette::PlaceholderText);
  QColor text = palette().color(QPalette::Text);
  std::int64_t now = unixNow();
  QString status;
  QColor colour = muted;
  if (!eligible(caseId)) {
    status = tr("no alg");
  } else if (cp && isQueued(cp->card)) {
    status = tr("queued");
    colour = kWarnColour;
  } else {
    switch (statusOf(cp)) {
    case CaseStatus::Unstarted:
      status = tr("new");
      break;
    case CaseStatus::Learning:
      status = tr("learning");
      colour = kWarnColour;
      break;
    case CaseStatus::Learned:
      status = tr("learned");
      colour = kLearned;
      break;
    }
  }
  if (cp && cp->card.suspended) {
    status = tr("suspended");
    colour = muted;
  } else if (cp && isBuried(cp->card, now)) {
    status = tr("buried");
    colour = muted;
  }
  QString tip;
  if (cp && isLeech(cp->card, settings_.srs)) {
    status += " " + tr("· leech");
    colour = kLeech;
    tip = tr("Forgotten %1 times: worth a new alg, a mnemonic or a closer "
             "look (right-click: History)")
              .arg(cp->card.lapses);
  }
  item->setText(1, status);
  item->setForeground(1, colour);
  item->setToolTip(1, tip);
  item->setText(2, cp && !isQueued(cp->card) ? dueText(cp, now, localDayEnd())
                                             : QString("-"));
  std::optional<double> recog = recognitionMean(attemptsOf(caseId));
  item->setText(3, secondsText(recog));
  item->setForeground(
      3, recog && *recog > settings_.slowRecognitionMs ? kWarnColour : text);
  item->setToolTip(3, tr("Mean recognition time of the last 5 attempts that "
                         "measured it, in Learn and on the virtual cube in "
                         "Drill: from the case being set up to the first "
                         "turn off U and D"));
  bool started = cp && cp->card.introduced != 0;
  item->setText(4, started ? QString::number(cp->card.lapses) : QString("-"));
  item->setText(5, started
                       ? QString("%1%").arg(std::lround(cp->card.ease * 100))
                       : QString("-"));
  item->setToolTip(4, tr("Times the case was forgotten after it was learned"));
  item->setToolTip(5, tr("How much further apart the reviews get each time "
                         "the case goes well"));
}

void LearnPage::updateHeader() {
  std::int64_t now = unixNow();
  LearnCounts n;
  if (acrossSets()) {
    for (const auto &s : library_.sets()) {
      const SetProgress &p = library_.progress(s.id);
      std::vector<std::string> ids;
      for (const auto &[id, cp] : p) {
        ids.push_back(id);
      }
      LearnCounts c = learnCounts(ids, p, now, localDayStart(), settings_.srs,
                                  localDayEnd());
      n.queued += c.queued;
      n.learning += c.learning;
      n.due += c.due;
      n.doneToday += c.doneToday;
      n.againToday += c.againToday;
    }
  } else if (set()) {
    n = learnCounts(pool(), progress(), now, localDayStart(), settings_.srs,
                    localDayEnd());
  }
  QString muted = palette().color(QPalette::PlaceholderText).name();
  auto part = [](const QString &label, int count, const QString &colour) {
    return QString("%1 <b style='color:%2'>%3</b>")
        .arg(label.toHtmlEscaped(), colour)
        .arg(count);
  };
  QStringList parts;
  if (!acrossSets()) {
    parts << part(tr("Queued"), n.queued, muted);
  }
  parts << part(tr("Learning"), n.learning, kWarnColour.name())
        << part(tr("Due"), n.due, roleColour(Role::Info).name())
        << part(tr("Done today"), n.doneToday, kLearned.name());
  if (n.againToday > 0) {
    parts.back() += QString(" <span style='color:%1'>(%2)</span>")
                        .arg(muted, tr("%1 Again").arg(n.againToday));
  }
  QString html = parts.join(" &nbsp;·&nbsp; ");
  if (acrossSets()) {
    html = "<span style='color:" + muted + "'>" +
           tr("All sets").toHtmlEscaped() + ":</span> " + html;
  }
  const LearnSession::QuizScore &quiz = session_.quizScore();
  if (practice() == Practice::Quiz && quiz.asked > 0) {
    html +=
        " &nbsp;·&nbsp; " +
        tr("Quiz %1/%2, mean %3 s")
            .arg(quiz.right)
            .arg(quiz.asked)
            .arg(secondsText(static_cast<double>(quiz.totalMs) / quiz.asked));
  }
  stage_->header->setText(html);
}

void LearnPage::updateSummary() {
  AlgSet *s = set();
  if (!s) {
    summary_->setText(tr("No alg sets. Create one in Library mode."));
    updateHeader();
    return;
  }
  LearnCounts n = learnCounts(pool(), progress(), unixNow(), localDayStart(),
                              settings_.srs, localDayEnd());
  // The header has the counts; here, what the list's ticks come to.
  QString text = tr("%1 of %2 ticked.").arg(tickedCount()).arg(s->caseCount());
  if (n.held > 0) {
    text += " " + tr("%1 suspended or buried.").arg(n.held);
  }
  if (int excluded = excludedCount()) {
    text += " " + skippedText(excluded);
  }
  summary_->setText(text);
  updateHeader();
  refreshSetLabels();
  if (!queueMessageTimer_.isActive()) {
    updateQueue();
  }
}

int LearnPage::dueIn(const AlgSet &s) const {
  return dueCards(library_.progress(s.id), unixNow(), localDayEnd(),
                  settings_.srs);
}

QString LearnPage::setItemText(const AlgSet &s) {
  int due = dueCards(library_.progress(s.id), unixNow(), localDayEnd(),
                     settings_.srs);
  QString text = CasePage::setItemText(s);
  return due > 0 ? tr("%1 · %2 due").arg(text).arg(due) : text;
}

QString LearnPage::acrossItemText() {
  int due = 0;
  for (const auto &s : library_.sets()) {
    due += dueCards(library_.progress(s.id), unixNow(), localDayEnd(),
                    settings_.srs);
  }
  return tr("All sets: due reviews (%1)").arg(due);
}

void LearnPage::extendCaseMenu(QMenu &menu,
                               const std::vector<std::string> &caseIds) {
  // The cards of the cases (a family's once); for several cases, only of
  // those that can be learned.
  std::vector<std::string> cards;
  for (const auto &id : caseIds) {
    if (caseIds.size() > 1 && !eligible(id)) {
      continue;
    }
    std::string card = cardId(id);
    if (std::find(cards.begin(), cards.end(), card) == cards.end()) {
      cards.push_back(card);
    }
  }
  if (cards.empty()) {
    menu.addAction(tr("None of these cases has an alg yet"))->setEnabled(false);
    return;
  }
  auto change = [this, cards, caseIds](auto &&apply) {
    for (const auto &card : cards) {
      apply(progress()[card]);
    }
    saveProgress();
    if (current_ && phase() == Phase::Done &&
        std::find(cards.begin(), cards.end(), attempt_.gradedCard()) !=
            cards.end()) {
      attempt_.replaceGrade(); // 1-4 would undo the change
      showGrade();
    }
    if (caseIds.size() > 1) {
      updateAllRows();
    } else {
      updateCardRows(caseIds.front());
    }
    if (phase() == Phase::NoCase) {
      nextCase();
    }
  };
  auto any = [this, &cards](auto &&test) {
    return std::any_of(cards.begin(), cards.end(), [&](const std::string &c) {
      const CaseProgress *cp = progressOf(c);
      return test(cp);
    });
  };
  bool single = caseIds.size() == 1;
  bool anyEligible =
      std::any_of(caseIds.begin(), caseIds.end(),
                  [this](const auto &id) { return eligible(id); });
  // "Start learning" queues them in the set's order for new cases.
  menu.addAction(single ? tr("Start learning") : tr("Start learning all"), this,
                 [this, caseIds] {
                   std::vector<std::string> order;
                   for (const auto &id : pool()) {
                     for (const auto &c : caseIds) {
                       if (cardId(c) == id && eligible(c)) {
                         order.push_back(id);
                         break;
                       }
                     }
                   }
                   std::int64_t now = unixNow();
                   std::int64_t i = 0;
                   for (const auto &card : order) {
                     CaseProgress &p = progress()[card];
                     if (statusOf(&p) == CaseStatus::Unstarted ||
                         caseIds.size() == 1) {
                       startLearning(p.card, now + i++, settings_.srs);
                     }
                   }
                   saveProgress();
                   updateAllRows();
                   if (phase() == Phase::NoCase) {
                     nextCase();
                   }
                 })
      ->setEnabled(anyEligible);
  menu.addAction(single ? tr("Mark as learned") : tr("Mark all as learned"),
                 this, [this, change] {
                   change([this](CaseProgress &p) {
                     markLearned(p.card, unixNow(), settings_.srs);
                   });
                 });
  menu.addAction(tr("Remove from learn queue"), this,
                 [change] {
                   change([](CaseProgress &p) {
                     if (isQueued(p.card)) {
                       resetCard(p.card);
                     }
                   });
                 })
      ->setEnabled(
          any([](const CaseProgress *cp) { return cp && isQueued(cp->card); }));
  menu.addSeparator();
  bool anySuspended =
      any([](const CaseProgress *cp) { return cp && cp->card.suspended; });
  bool allSuspended = std::all_of(cards.begin(), cards.end(), [this](auto &c) {
    const CaseProgress *cp = progressOf(c);
    return cp && cp->card.suspended;
  });
  // Suspending keeps the case ticked, so Drill still has it.
  menu.addAction(allSuspended ? tr("Unsuspend") : tr("Suspend"), this,
                 [change, allSuspended] {
                   change([allSuspended](CaseProgress &p) {
                     p.card.suspended = !allSuspended;
                   });
                 })
      ->setToolTip(tr("Leave it out of Learn (Drill still has it)"));
  if (anySuspended && !allSuspended) {
    menu.addAction(tr("Unsuspend"), this, [change] {
      change([](CaseProgress &p) { p.card.suspended = false; });
    });
  }
  bool anyBuried = any([](const CaseProgress *cp) {
    return cp && isBuried(cp->card, unixNow());
  });
  menu.addAction(tr("Bury until tomorrow"), this, [change] {
    change([](CaseProgress &p) { bury(p.card, localDayEnd()); });
  });
  if (anyBuried) {
    menu.addAction(tr("Unbury"), this, [change] {
      change([](CaseProgress &p) { p.card.buriedUntil = 0; });
    });
  }
  menu.addSeparator();
  // Its card and Learn attempts; its drill times stay.
  menu.addAction(
      single ? tr("Reset learning progress")
             : tr("Reset learning progress of all"),
      this, [change] { change([](CaseProgress &p) { resetLearning(p); }); });
  if (single) {
    std::string id = caseIds.front();
    menu.addAction(tr("History…"), this, [this, id] {
      const AlgSet *s = set();
      const AlgCase *c = s ? s->findCase(id) : nullptr;
      if (c) {
        showCaseHistory(this, qs(c->name), cardOf(id), attemptsOf(id),
                        byFamily() && cardId(id) != id, settings_.srs);
      }
    });
  }
}

std::vector<Attempt> LearnPage::attemptsOf(const std::string &caseId) {
  const CaseProgress *own = progressOf(caseId);
  const CaseProgress *card = cardOf(caseId);
  return caseAttempts(own, card != own ? card : nullptr);
}

std::vector<std::string> LearnPage::queueOrder() {
  std::vector<std::string> out;
  std::int64_t now = unixNow();
  for (const auto &id : pool()) {
    const CaseProgress *cp = progressOf(id);
    if (cp && isQueued(cp->card) && !isHeld(cp->card, now)) {
      out.push_back(id);
    }
  }
  return out;
}

void LearnPage::showQueueMessage(const QString &text) {
  queueLabel_->setText("<b>" + text.toHtmlEscaped() + "</b>");
  queueMessageTimer_.start(4000);
}

void LearnPage::updateQueue() {
  const AlgSet *s = set();
  std::vector<std::string> queue =
      s ? queueOrder() : std::vector<std::string>{};
  learnNextButton_->setEnabled(!queue.empty() && !context_);
  unqueueAction_->setEnabled(!queue.empty());
  if (!s) {
    queueLabel_->clear();
    return;
  }
  auto nameOf = [this, s](const std::string &card) {
    const AlgCase *c = s->findCase(card);
    if (!c) {
      if (const auto *members = families_.familyFor(card);
          members && !members->empty()) {
        c = s->findCase(members->front());
      }
    }
    return c ? qs(c->name).toHtmlEscaped() : QString("?");
  };
  QString muted = palette().color(QPalette::PlaceholderText).name();
  QString text;
  if (queue.empty()) {
    text = tr("The learn queue is empty.");
  } else {
    QStringList names;
    for (std::size_t i = 0; i < queue.size() && i < 3; ++i) {
      names << nameOf(queue[i]);
    }
    text = tr("Next: %1").arg(names.join(", "));
    if (queue.size() > 3) {
      text += " " + tr("(+%1)").arg(queue.size() - 3);
    }
  }
  int today = newCasesIntroducedSince(progress(), localDayStart());
  QString limits =
      tr("New today %1 of %2").arg(today).arg(settings_.srs.newPerDay);
  if (settings_.srs.maxLearning > 0) {
    limits += ", " + tr("learning %1 of %2")
                         .arg(learningCount(pool(), progress()))
                         .arg(settings_.srs.maxLearning);
  }
  queueLabel_->setText(text + "<br><span style='color:" + muted + "'>" +
                       limits + "</span>");
}

void LearnPage::learnQueueHead() {
  ready_ = false; // asked for with a button: it waits for Space
  std::vector<std::string> queue = queueOrder();
  if (queue.empty() || context_) {
    return;
  }
  std::string member = pickMember(queue.front());
  if (eligible(member)) {
    begin(member, true);
  }
}

void LearnPage::unqueueHead() {
  std::vector<std::string> queue = queueOrder();
  if (queue.empty()) {
    return;
  }
  resetCard(progress()[queue.front()].card);
  saveProgress();
  updateCardRows(queue.front());
  if (current_ && cardId(current_->caseId) == queue.front() &&
      current_->isNew && phase() == Phase::Demo) {
    nextCase();
  }
}

void LearnPage::learnNext() {
  ready_ = false; // asked for with a button: it waits for Space
  int n = learnCount_->value();
  learnCount_->clearFocus();
  std::int64_t now = unixNow();
  int started = 0;
  // In the set's order for new cases; each queued a second apart, so that
  // "in the order queued" keeps this order.
  for (const auto &id : pool()) {
    if (started >= n) {
      break;
    }
    if (statusOf(progressOf(id)) == CaseStatus::Unstarted) {
      startLearning(progress()[id].card, now + started, settings_.srs);
      ++started;
    }
  }
  saveProgress();
  updateAllRows();
  if (started) {
    showQueueMessage(
        started == 1 ? tr("Added 1 case to your learn queue")
                     : tr("Added %1 cases to your learn queue").arg(started));
  } else {
    showQueueMessage(tr("Every ticked case with an alg is started already"));
  }
  if (phase() == Phase::NoCase) {
    nextCase();
  }
}

} // namespace vcube
