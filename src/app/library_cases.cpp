#include "app/library_page.hpp"
#include "app/library_private.hpp"

namespace vcube {

void LibraryPage::fillMoveToMenu() {
  moveToMenu_->clear();
  const AlgSet *s = set();
  if (!s) {
    return;
  }
  for (std::size_t g = 0; g < s->groups.size(); ++g) {
    moveToMenu_->addAction(qs(s->groups[g].name), this,
                           [this, g] { moveCasesTo(g); });
  }
}

// --- selection ---------------------------------------------------------------

std::vector<std::string> LibraryPage::selectedCases() const {
  std::vector<std::string> out;
  for (int g = 0; g < caseTree_->topLevelItemCount(); ++g) {
    QTreeWidgetItem *groupItem = caseTree_->topLevelItem(g);
    for (int c = 0; c < groupItem->childCount(); ++c) {
      QTreeWidgetItem *item = groupItem->child(c);
      if (item->isSelected() && !item->isHidden()) {
        out.push_back(ss(item->data(0, kCaseRole).toString()));
      }
    }
  }
  return out;
}

std::vector<std::size_t> LibraryPage::selectedGroups() const {
  std::vector<std::size_t> out;
  for (int g = 0; g < caseTree_->topLevelItemCount(); ++g) {
    QTreeWidgetItem *groupItem = caseTree_->topLevelItem(g);
    if (groupItem->isSelected() && !groupItem->isHidden()) {
      out.push_back(
          static_cast<std::size_t>(groupItem->data(0, kGroupRole).toInt()));
    }
  }
  return out;
}

std::optional<std::size_t> LibraryPage::currentGroup() const {
  QTreeWidgetItem *item = caseTree_->currentItem();
  if (!item) {
    return std::nullopt;
  }
  QTreeWidgetItem *groupItem = item->parent() ? item->parent() : item;
  return static_cast<std::size_t>(groupItem->data(0, kGroupRole).toInt());
}

void LibraryPage::selectItems(const std::vector<std::string> &cases,
                              const std::vector<std::size_t> &groups) {
  {
    QSignalBlocker block(caseTree_);
    caseTree_->clearSelection();
    QTreeWidgetItem *first = nullptr;
    for (int g = 0; g < caseTree_->topLevelItemCount(); ++g) {
      QTreeWidgetItem *groupItem = caseTree_->topLevelItem(g);
      auto gi =
          static_cast<std::size_t>(groupItem->data(0, kGroupRole).toInt());
      if (std::find(groups.begin(), groups.end(), gi) != groups.end()) {
        groupItem->setSelected(true);
        first = first ? first : groupItem;
      }
      for (int c = 0; c < groupItem->childCount(); ++c) {
        QTreeWidgetItem *item = groupItem->child(c);
        std::string id = ss(item->data(0, kCaseRole).toString());
        if (std::find(cases.begin(), cases.end(), id) != cases.end()) {
          item->setSelected(true);
          // Keep the shown case when it is one of them.
          if (!first || id == editor_.caseId) {
            first = item;
          }
        }
      }
    }
    if (first) {
      caseTree_->setCurrentItem(first, 0, QItemSelectionModel::Current);
      caseTree_->scrollToItem(first);
    }
  }
  updateActions();
}

// --- group and case actions --------------------------------------------------

void LibraryPage::newGroup() {
  AlgSet *s = set();
  if (!s) {
    return;
  }
  bool ok = false;
  QString name = QInputDialog::getText(this, tr("New group"), tr("Group name:"),
                                       QLineEdit::Normal, QString(), &ok);
  if (!ok || name.trimmed().isEmpty()) {
    return;
  }
  auto pending = editor_.begin();
  s->groups.push_back({ss(name.trimmed()), std::nullopt, {}});
  std::size_t added = s->groups.size() - 1;
  if (editor_.commit(tr("New group"), *pending)) {
    selectItems({}, {added});
  }
}

void LibraryPage::renameSelected() {
  if (algList_->hasFocus() && algList_->currentItem()) {
    algList_->editItem(algList_->currentItem(), 0);
    return;
  }
  AlgSet *s = set();
  QTreeWidgetItem *item = caseTree_->currentItem();
  if (!s || !item) {
    return;
  }
  if (item->parent()) {
    caseNameEdit_->setFocus();
    caseNameEdit_->selectAll();
    return;
  }
  auto gi = static_cast<std::size_t>(item->data(0, kGroupRole).toInt());
  if (gi >= s->groups.size()) {
    return;
  }
  bool ok = false;
  QString name =
      QInputDialog::getText(this, tr("Rename group"), tr("Group name:"),
                            QLineEdit::Normal, qs(s->groups[gi].name), &ok);
  if (!ok || name.trimmed().isEmpty() ||
      ss(name.trimmed()) == s->groups[gi].name) {
    return;
  }
  auto pending = editor_.begin();
  s->groups[gi].name = ss(name.trimmed());
  editor_.commit(tr("Rename group"), *pending);
}

void LibraryPage::deleteSelected() {
  if (algList_->hasFocus() && algList_->currentItem()) {
    removeAlg(algList_->indexOfTopLevelItem(algList_->currentItem()));
    return;
  }
  AlgSet *s = set();
  if (!s) {
    return;
  }
  std::vector<std::size_t> groups = selectedGroups();
  std::vector<std::string> cases = selectedCases();
  // Cases of the deleted groups go with them.
  std::vector<std::string> ids;
  for (std::size_t gi : groups) {
    for (const AlgCase &c : s->groups[gi].cases) {
      ids.push_back(c.id);
    }
  }
  for (const auto &id : cases) {
    if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
      ids.push_back(id);
    }
  }
  if (ids.empty() && groups.empty()) {
    return;
  }
  const SetProgress &progress = library_.progress(s->id);
  int withProgress = 0, algs = 0;
  for (const auto &id : ids) {
    withProgress += progress.contains(id) ? 1 : 0;
    algs += static_cast<int>(s->findCase(id)->algs.size());
  }
  QString what;
  if (!groups.empty()) {
    what = groups.size() == 1
               ? tr("group \"%1\"").arg(qs(s->groups[groups.front()].name))
               : tr("%1 groups").arg(groups.size());
    what += " " + tr("with %1").arg(counted(static_cast<int>(ids.size()),
                                            tr("case"), tr("cases")));
  } else {
    what = ids.size() == 1
               ? tr("case \"%1\"").arg(qs(s->findCase(ids.front())->name))
               : tr("%1 cases").arg(ids.size());
  }
  // Not asked first: Edit > Undo brings it all back, which the message
  // says.
  QString text =
      tr("Deleted %1 and %2").arg(what, counted(algs, tr("alg"), tr("algs")));
  if (withProgress > 0) {
    text += " " +
            tr("with your progress on %n of the cases", nullptr, withProgress);
  }
  emit message(text + tr(" (Ctrl+Z brings it all back)"));
  QString title = groups.empty() ? tr("Delete cases") : tr("Delete groups");
  auto pending = editor_.begin();
  for (auto &g : s->groups) {
    std::erase_if(g.cases, [&ids](const AlgCase &c) {
      return std::find(ids.begin(), ids.end(), c.id) != ids.end();
    });
  }
  std::vector<std::size_t> sorted = groups;
  std::sort(sorted.rbegin(), sorted.rend());
  for (std::size_t gi : sorted) {
    s->groups.erase(s->groups.begin() + static_cast<std::ptrdiff_t>(gi));
  }
  if (std::find(ids.begin(), ids.end(), editor_.caseId) != ids.end()) {
    editor_.caseId.clear();
  }
  editor_.commit(title, *pending, std::nullopt, ids);
}

void LibraryPage::moveSelected(int direction) {
  if (algList_->hasFocus() && algList_->currentItem()) {
    moveAlg(algList_->indexOfTopLevelItem(algList_->currentItem()), direction);
    return;
  }
  AlgSet *s = set();
  if (!s) {
    return;
  }
  std::vector<std::string> cases = selectedCases();
  std::vector<std::size_t> groups = selectedGroups();
  auto pending = editor_.begin();
  if (!cases.empty()) {
    if (shiftCases(*s, cases, direction) &&
        editor_.commit(tr("Move cases"), *pending)) {
      selectItems(cases, {});
    }
    return;
  }
  if (groups.size() != 1) {
    return;
  }
  std::size_t from = groups.front();
  auto to = static_cast<std::size_t>(static_cast<long>(from) + direction);
  if (moveGroup(*s, from, to) && editor_.commit(tr("Move group"), *pending)) {
    selectItems({}, {to});
  }
}

void LibraryPage::moveCasesTo(std::size_t group) {
  AlgSet *s = set();
  std::vector<std::string> cases = selectedCases();
  if (!s || cases.empty()) {
    return;
  }
  auto pending = editor_.begin();
  if (moveCases(*s, cases, group) &&
      editor_.commit(tr("Move cases"), *pending)) {
    selectItems(cases, {});
  }
}

void LibraryPage::dropOnTree(QTreeWidgetItem *target, int position) {
  AlgSet *s = set();
  if (!s || s->groups.empty()) {
    return;
  }
  // The tree is rebuilt by the move, so read the target first.
  std::optional<std::size_t> targetGroup;
  std::string targetCase;
  bool expanded = false;
  if (target) {
    QTreeWidgetItem *groupItem = target->parent() ? target->parent() : target;
    targetGroup =
        static_cast<std::size_t>(groupItem->data(0, kGroupRole).toInt());
    targetCase = ss(target->data(0, kCaseRole).toString());
    expanded = groupItem->isExpanded();
  }
  auto where = static_cast<CaseTreeWidget::Drop>(position);
  std::vector<std::string> cases = selectedCases();
  std::vector<std::size_t> groups = selectedGroups();
  auto pending = editor_.begin();
  if (!cases.empty()) {
    std::size_t group = s->groups.size() - 1;
    std::string before;
    if (targetGroup && *targetGroup < s->groups.size()) {
      group = *targetGroup;
      const auto &list = s->groups[group].cases;
      if (!targetCase.empty()) {
        // Above a case: before it; on or below it: after it.
        auto it = std::find_if(list.begin(), list.end(), [&](const AlgCase &c) {
          return c.id == targetCase;
        });
        if (where != CaseTreeWidget::DropAbove && it != list.end()) {
          ++it;
        }
        before = it == list.end() ? std::string() : it->id;
      } else if (where == CaseTreeWidget::DropAbove && group > 0) {
        --group; // between groups: the end of the one above
      } else if (where == CaseTreeWidget::DropAbove ||
                 (where == CaseTreeWidget::DropBelow && expanded)) {
        before = list.empty() ? std::string() : list.front().id;
      }
    }
    if (moveCases(*s, cases, group, before) &&
        editor_.commit(tr("Move cases"), *pending)) {
      selectItems(cases, {});
    }
    return;
  }
  if (groups.size() != 1) {
    return;
  }
  std::size_t from = groups.front();
  std::size_t to = s->groups.size() - 1;
  if (targetGroup) {
    to = *targetGroup;
    if (where == CaseTreeWidget::DropAbove && targetCase.empty()) {
      to = to > from ? to - 1 : to;
    } else {
      to = to < from ? to + 1 : to;
    }
  }
  to = std::min(to, s->groups.size() - 1);
  if (moveGroup(*s, from, to) && editor_.commit(tr("Move group"), *pending)) {
    selectItems({}, {to});
  }
}

void LibraryPage::newCaseFromAlg() {
  AlgSet *s = set();
  if (!s) {
    return;
  }
  bool ok = false;
  QString text = QInputDialog::getText(this, tr("New case"),
                                       tr("An alg that solves the new case:"),
                                       QLineEdit::Normal, QString(), &ok);
  if (!ok || text.trimmed().isEmpty()) {
    return;
  }
  std::string alg = cleanAlgText(ss(text));
  ParseResult parsed = parseAlg(alg);
  if (!parsed || parsed.alg.empty()) {
    QMessageBox::warning(
        this, tr("New case"),
        tr("Can't read that alg: %1")
            .arg(parsed.error ? qs(parsed.error->message) : tr("empty")));
    return;
  }
  if (auto ref = matchCase(*s, parsed.alg)) {
    AlgCase &existing = s->groups[ref->group].cases[ref->index];
    if (std::find(existing.algs.begin(), existing.algs.end(), alg) !=
        existing.algs.end()) {
      QMessageBox::information(
          this, tr("New case"),
          tr("The case \"%1\" already has that alg.").arg(qs(existing.name)));
      return;
    }
    auto answer =
        QMessageBox::question(this, tr("New case"),
                              tr("That alg solves the existing case \"%1\". "
                                 "Add it to that case instead?")
                                  .arg(qs(existing.name)));
    if (answer == QMessageBox::Yes) {
      auto pending = editor_.begin();
      existing.algs.push_back(alg);
      editor_.caseId = existing.id;
      editor_.commit(tr("Add alg"), *pending,
                     std::vector<std::string>{existing.id});
      buildCaseTree();
    }
    return;
  }
  QString name = QInputDialog::getText(
      this, tr("New case"), tr("Case name:"), QLineEdit::Normal,
      tr("Case %1").arg(s->caseCount() + 1), &ok);
  if (!ok) {
    return;
  }
  auto pending = editor_.begin();
  if (s->groups.empty()) {
    s->groups.push_back({"Cases", std::nullopt, {}});
  }
  // Add to the selected group, or the last one.
  std::size_t gi = std::min(currentGroup().value_or(s->groups.size() - 1),
                            s->groups.size() - 1);
  AlgCase c;
  c.name = ss(name.trimmed().isEmpty() ? tr("Case %1").arg(s->caseCount() + 1)
                                       : name.trimmed());
  c.id = s->uniqueCaseId(c.name);
  c.state = stateSolvedBy(parsed.alg);
  c.algs.push_back(alg);
  editor_.caseId = c.id;
  std::string id = c.id;
  bool solved = stepSolved(c.state, s->specFor(s->groups[gi]).free);
  s->groups[gi].cases.push_back(std::move(c));
  if (editor_.commit(tr("New case"), *pending, std::nullopt, {id}) && solved) {
    emit message(tr("Note: with this set's \"left unsolved\" setting, that "
                    "case already counts as solved."));
  }
}

void LibraryPage::pasteAlgs() {
  AlgSet *s = set();
  if (!s) {
    return;
  }
  std::optional<std::size_t> group = currentGroup();
  PasteDialog dialog(*s, group ? static_cast<int>(*group) : -1,
                     pasteLeftOver_[s->id], this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  PastePlan plan = dialog.plan();
  pasteLeftOver_[s->id] = dialog.leftOver();
  std::vector<std::string> created, changed;
  for (const PasteResult &r : plan.results) {
    if (r.outcome == PasteOutcome::NewCase) {
      created.push_back(r.caseId);
    } else if (r.outcome == PasteOutcome::Added) {
      changed.push_back(r.caseId);
    }
  }
  auto pending = editor_.begin();
  *s = std::move(plan.set);
  if (editor_.caseId.empty() && !created.empty()) {
    editor_.caseId = created.front();
  }
  if (!editor_.commit(tr("Paste algs"), *pending,
                      created.empty() ? std::optional(changed) : std::nullopt,
                      created)) {
    return;
  }
  const ImportReport &r = plan.report;
  QString text = tr("Added %1").arg(counted(r.added, tr("alg"), tr("algs")));
  if (r.created) {
    text += tr(", made %1")
                .arg(counted(r.created, tr("new case"), tr("new cases")));
  }
  int left = static_cast<int>(r.unmatched.size() + r.ambiguous.size() +
                              r.invalid.size());
  if (left) {
    text += plural(left, tr("; %n not added is kept for the next Paste algs"),
                   tr("; %n not added are kept for the next Paste algs"));
  }
  emit message(text + ".");
}

} // namespace vcube
