#include "app/library_page.hpp"
#include "app/library_private.hpp"
#include "app/page_parts.hpp"

namespace vcube {

void LibraryPage::fillRestoreSetMenu() {
  restoreSetMenu_->clear();
  std::vector<AlgSet> missing = library_.missingBuiltins();
  if (missing.empty()) {
    restoreSetMenu_->addAction(tr("(every built-in set is here)"))
        ->setEnabled(false);
  }
  for (const AlgSet &s : missing) {
    std::string id = s.id;
    restoreSetMenu_->addAction(
        tr("%1 (%2 cases)").arg(qs(s.name)).arg(s.caseCount()), this,
        [this, id] { restoreBuiltinSet(id); });
  }
}

// --- set actions -------------------------------------------------------------

void LibraryPage::newSet() {
  bool ok = false;
  QString name = QInputDialog::getText(this, tr("New alg set"), tr("Name:"),
                                       QLineEdit::Normal, QString(), &ok);
  if (!ok || name.trimmed().isEmpty()) {
    return;
  }
  QStringList kinds = {
      tr("Last layer, everything solved (like PLL, ZBLL, 1LLL)"),
      tr("Last layer, permutation left (like OLL, COLL-free OLLCP)"),
      tr("First two layers / last slot (LL left, 3D diagrams)"),
      tr("Anything (everything solved, 3D diagrams)")};
  QString kind = QInputDialog::getItem(this, tr("New alg set"),
                                       tr("What do the algs solve?"), kinds, 0,
                                       false, &ok);
  if (!ok) {
    return;
  }
  AlgSet s;
  s.name = ss(name.trimmed());
  s.id = library_.uniqueId(s.name);
  int k = static_cast<int>(kinds.indexOf(kind));
  if (k == 1) {
    s.spec.free = *parseFree("LL:p");
    s.spec.mask = {StickerMode::Orientation, StickerMode::Orientation,
                   StickerMode::Full};
  } else if (k == 2) {
    s.spec.free = *parseFree("LL:po");
    s.spec.mask = {StickerMode::Hidden, StickerMode::Hidden, StickerMode::Full,
                   true};
    s.spec.view = DiagramView::Iso3D;
    s.aufBefore = true;
    s.aufAfter = false;
  } else if (k == 3) {
    s.spec.view = DiagramView::Iso3D;
    s.aufBefore = false;
    s.aufAfter = false;
  }
  s.groups.push_back({"Cases", std::nullopt, {}});
  QString error;
  AlgSet *added = library_.add(std::move(s), &error);
  if (!added) {
    QMessageBox::warning(this, tr("New alg set"),
                         tr("Could not create the set: %1").arg(error));
    return;
  }
  editor_.added(*added, tr("New set"));
  emit message(tr("Created \"%1\". Add cases with Cases > New case from "
                  "alg, or Paste algs.")
                   .arg(name));
}

void LibraryPage::duplicateSet() {
  AlgSet *s = set();
  if (!s) {
    return;
  }
  AlgSet copy = *s;
  copy.builtin = false;
  copy.name += " (copy)";
  copy.id = library_.uniqueId(s->id + "-copy");
  QString error;
  AlgSet *added = library_.add(std::move(copy), &error);
  if (!added) {
    QMessageBox::warning(this, tr("Duplicate alg set"),
                         tr("Could not save the copy: %1").arg(error));
    return;
  }
  editor_.added(*added, tr("Duplicate set"), true);
}

void LibraryPage::deleteSet() {
  AlgSet *s = set();
  if (!s) {
    return;
  }
  SetProgress progress = library_.progress(s->id);
  int algs = 0;
  for (const auto &g : s->groups) {
    for (const auto &c : g.cases) {
      algs += static_cast<int>(c.algs.size());
    }
  }
  QString text = tr("Delete \"%1\" and its %2?")
                     .arg(qs(s->name), counted(algs, tr("alg"), tr("algs")));
  if (!progress.empty()) {
    text += " " + tr("Your Learn and Drill progress on %n of its cases goes "
                     "too.",
                     nullptr, static_cast<int>(progress.size()));
  }
  text += "\n\n" + tr("Edit > Undo brings it back while v-cube is open, and "
                      "its files are kept in the alg sets' deleted folder.");
  if (s->builtin) {
    text += " " + tr("After that, Set > Restore a built-in set brings back "
                     "its cases, but not your algs.");
  }
  if (QMessageBox::question(this, tr("Delete alg set"), text) !=
      QMessageBox::Yes) {
    return;
  }
  Edit edit{s->id,
            *s,
            std::nullopt,
            {},
            std::move(progress),
            saved::setSettings(s->id),
            editor_.caseId,
            {}};
  QString error;
  if (!library_.remove(edit.setId, &error)) {
    QMessageBox::warning(this, tr("Delete alg set"),
                         tr("Could not delete the set: %1").arg(error));
    return;
  }
  if (!error.isEmpty()) {
    emit message(tr("Deleted the set, but %1").arg(error));
  }
  editor_.removed(std::move(edit), tr("Delete set"));
}

void LibraryPage::restoreSet() {
  AlgSet *s = set();
  if (!s || !library_.builtinIds().contains(qs(s->id))) {
    return;
  }
  auto answer = QMessageBox::question(
      this, tr("Restore built-in cases"),
      tr("Restore the built-in cases of %1? Cases you deleted come back, and "
         "the shipped names and states replace yours. Your algs, the cases "
         "and groups you added and the set's settings are kept.")
          .arg(qs(s->name)));
  if (answer != QMessageBox::Yes) {
    return;
  }
  auto pending = editor_.begin();
  QString error;
  if (!library_.restoreBuiltin(editor_.setId, &error)) {
    QMessageBox::warning(this, tr("Restore"), error);
    return;
  }
  if (editor_.commit(tr("Restore built-in cases"), *pending)) {
    emit message(tr("Restored the built-in cases of %1; your algs, cases and "
                    "settings were kept.")
                     .arg(qs(set()->name)));
  }
}

void LibraryPage::restoreBuiltinSet(const std::string &id) {
  QString error;
  if (!library_.restoreBuiltin(id, &error)) {
    QMessageBox::warning(this, tr("Restore a built-in set"), error);
    return;
  }
  AlgSet *s = library_.find(id);
  if (!s) {
    return;
  }
  editor_.added(*s, tr("Restore %1").arg(qs(s->name)));
  emit message(tr("Added the built-in set %1 again, with no algs yet.")
                   .arg(qs(s->name)));
}

void LibraryPage::editProperties() {
  AlgSet *s = set();
  if (!s) {
    return;
  }
  // Start on the selected group if it has its own settings.
  int group = -1;
  if (QTreeWidgetItem *item = caseTree_->currentItem()) {
    QTreeWidgetItem *groupItem = item->parent() ? item->parent() : item;
    std::size_t gi = groupIndexOf(groupItem);
    if (gi < s->groups.size() &&
        (item == groupItem || s->groups[gi].spec.has_value())) {
      group = static_cast<int>(gi);
    }
  }
  SetPropertiesDialog dialog(*s, group, look(), editor_.caseId, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  const AlgSet &r = dialog.result();
  auto pending = editor_.begin();
  bool renamed = r.name != s->name;
  s->name = r.name;
  s->description = r.description;
  s->aufBefore = r.aufBefore;
  s->aufAfter = r.aufAfter;
  s->targetSeconds = r.targetSeconds;
  s->guided = r.guided;
  s->families = r.families;
  s->reduction = r.reduction;
  s->mirrors = r.mirrors;
  s->spec = r.spec;
  for (std::size_t i = 0; i < s->groups.size() && i < r.groups.size(); ++i) {
    s->groups[i].spec = r.groups[i].spec;
  }
  if (algChanges(pending->before, *s)) {
    return; // nothing changed
  }
  editor_.commit(tr("Set properties"), *pending);
  if (renamed) {
    emit setsChanged({});
  }
}

void LibraryPage::checkAlgs() {
  AlgSet *s = set();
  if (!s) {
    return;
  }
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Check algs: %1").arg(qs(s->name)));
  auto *layout = new QVBoxLayout(&dialog);
  auto *tree = new QTreeWidget(&dialog);
  tree->setColumnCount(3);
  tree->setHeaderLabels({tr("Case"), tr("Alg"), tr("Problem")});
  tree->setRootIsDecorated(false);
  int total = 0, failing = 0, noPassing = 0;
  for (const auto &g : s->groups) {
    for (const auto &c : g.cases) {
      const CaseAnalysis *a = editor_.analysisOf(c.id);
      if (!a) {
        continue;
      }
      total += static_cast<int>(c.algs.size());
      noPassing += !c.algs.empty() && !a->main ? 1 : 0;
      for (std::size_t i = 0; i < c.algs.size(); ++i) {
        if (a->checks[i].passes()) {
          continue;
        }
        ++failing;
        auto *item = new QTreeWidgetItem(
            tree, {qs(c.name), qs(c.algs[i]), qs(describeCheck(a->checks[i]))});
        item->setData(0, kCaseRole, qs(c.id));
        item->setForeground(2, kFailColour);
      }
    }
  }
  for (int c = 0; c < 3; ++c) {
    tree->resizeColumnToContents(c);
  }
  QString summary =
      failing == 0
          ? tr("All %1 algs solve their cases under the current step "
               "settings.")
                .arg(total)
          : tr("%1 of %2 algs don't solve their cases under the current step "
               "settings%3. Double-click one to open its case.")
                .arg(failing)
                .arg(total)
                .arg(noPassing
                         ? plural(noPassing, tr("; %n case has no passing alg"),
                                  tr("; %n cases have no passing alg"))
                         : QString());
  auto *label = new QLabel(summary, &dialog);
  label->setWordWrap(true);
  layout->addWidget(label);
  layout->addWidget(tree, 1);
  tree->setVisible(failing > 0);
  addButtonBox(&dialog, layout, QDialogButtonBox::Close);
  connect(tree, &QTreeWidget::itemDoubleClicked, &dialog,
          [this, &dialog](QTreeWidgetItem *item) {
            editor_.caseId = caseIdOf(item);
            buildCaseTree();
            loadCase();
            dialog.accept();
          });
  dialog.resize(failing > 0 ? 760 : 420, failing > 0 ? 420 : 120);
  dialog.exec();
}

void LibraryPage::importSetFile() {
  QString path = QFileDialog::getOpenFileName(
      this, tr("Import alg set"), QString(), tr("v-cube alg set (*.json)"));
  if (path.isEmpty()) {
    return;
  }
  QString error;
  auto imported = readAlgSetFile(path, &error);
  if (!imported) {
    warnPlain(this, tr("Import alg set"),
              tr("Could not import: %1").arg(error));
    return;
  }
  if (library_.idTaken(imported->id)) {
    imported->id = library_.uniqueId(imported->id);
    imported->name += " (imported)";
  }
  // A built-in set exported and imported again stays a built-in one, so
  // that its shipped cases can still be restored.
  imported->builtin =
      imported->builtin && library_.builtinIds().contains(qs(imported->id));
  AlgSet *added = library_.add(std::move(*imported), &error);
  if (!added) {
    warnPlain(this, tr("Import alg set"),
              tr("Could not import: %1").arg(error));
    return;
  }
  editor_.added(*added, tr("Import set"));
}

void LibraryPage::exportSetFile() {
  AlgSet *s = set();
  if (!s) {
    return;
  }
  QString path = QFileDialog::getSaveFileName(this, tr("Export alg set"),
                                              qs(s->id) + ".json",
                                              tr("v-cube alg set (*.json)"));
  if (path.isEmpty()) {
    return;
  }
  QString error;
  if (!writeAlgSetFile(*s, path, &error)) {
    QMessageBox::warning(this, tr("Export alg set"), error);
    return;
  }
  emit message(tr("Exported %1 to %2").arg(qs(s->name), path));
}

} // namespace vcube
