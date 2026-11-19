#include "app/library_page.hpp"
#include "app/library_private.hpp"
#include "app/ui_text.hpp"

#include "app/alglab_common.hpp"
#include "app/case_filter.hpp"
#include "app/case_history_dialog.hpp"
#include "app/cube_painter.hpp"
#include "app/drill_history_dialog.hpp"
#include "app/grip_view.hpp"
#include "app/mode_table.hpp"
#include "app/paste_dialog.hpp"
#include "app/set_properties_dialog.hpp"
#include "app/ui_state.hpp"
#include "train/reduction.hpp"

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDropEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QSplitter>
#include <QTabWidget>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace vcube {

namespace {

QString drillText(const CaseProgress &p, AttemptMode mode) {
  std::size_t n = p.drillCount(mode);
  if (n == 0) {
    return {};
  }
  QString text =
      counted(static_cast<int>(n), QObject::tr("drill"), QObject::tr("drills"));
  if (auto best = p.best(mode)) {
    text += QObject::tr(", best %1 s")
                .arg(static_cast<double>(*best) / 1000.0, 0, 'f', 2);
  }
  if (auto mean = p.recentMean(mode)) {
    text += QObject::tr(", recent mean %1 s").arg(*mean / 1000.0, 0, 'f', 2);
  }
  return text;
}

} // namespace

QString LibraryPage::historyHtml(const AlgCase &c) const {
  const SetProgress &p = library_.progress(editor_.setId);
  auto it = p.find(c.id);
  if (it == p.end()) {
    return tr("Not learned yet, no drills.");
  }
  const CaseProgress &cp = it->second;
  auto row = [](const QString &name, const QString &value) {
    return QString("<tr><td>%1&nbsp;&nbsp;</td><td>%2</td></tr>")
        .arg(name.toHtmlEscaped(), value.toHtmlEscaped());
  };
  auto secs = [this](double ms) {
    return QString::number(ms / 1000.0, 'f', settings_.decimals) + " s";
  };
  QString html = "<p><b>" + tr("Learn") + "</b></p><table>";
  const SrsCard &card = cp.card;
  QString status = statusOf(&cp) == CaseStatus::Learned    ? tr("learned")
                   : statusOf(&cp) == CaseStatus::Learning ? tr("learning")
                                                           : tr("not started");
  if (card.suspended) {
    status += tr(", suspended");
  }
  html += row(tr("Status"), status);
  if (card.due > 0) {
    html += row(tr("Due"), dateTimeText(card.due));
  }
  if (card.reps > 0) {
    html += row(tr("Reviews"), QString::number(card.reps));
    html += row(tr("Misses"), QString::number(card.lapses));
    html +=
        row(tr("Interval"), tr("%1 days").arg(card.intervalDays, 0, 'f', 1));
    html += row(tr("Ease"), QString::number(card.ease, 'f', 2));
  }
  html += "</table>";
  const std::pair<AttemptMode, QString> modes[] = {
      {AttemptMode::VirtualDrill, tr("Drill, virtual cube")},
      {AttemptMode::PhysicalDrill, tr("Drill, physical cube")}};
  for (const auto &[mode, name] : modes) {
    std::size_t n = cp.drillCount(mode);
    if (n == 0) {
      continue;
    }
    html += "<p><b>" + name.toHtmlEscaped() + "</b></p><table>";
    html += row(tr("Attempts"), QString::number(n));
    if (auto best = cp.best(mode)) {
      html += row(tr("Best"), secs(static_cast<double>(*best)));
    }
    if (auto mean = cp.recentMean(mode)) {
      html += row(tr("Recent mean"), secs(*mean));
    }
    html += row(tr("Fail rate, last 10"),
                QString("%1%").arg(std::lround(100.0 * cp.failRate(mode))));
    html += "</table>";
  }
  return html;
}

void LibraryPage::loadCase() {
  AlgCase *c = currentCase();
  caseEmpty_->setVisible(c == nullptr);
  caseContent_->setVisible(c != nullptr);
  loading_ = true;
  caseNameEdit_->setText(c ? qs(c->name) : QString());
  loading_ = false;
  if (!c) {
    caseDiagram_->clear();
    caseSummary_->clear();
    caseGroup_->clear();
    caseAlgCount_->clear();
    caseProgress_->clear();
    caseHistory_->clear();
    QSignalBlocker block(algList_);
    algList_->clear();
    caseBox_->setTitle(tr("Case"));
    updateActions();
    return;
  }
  caseBox_->setTitle(qs(c->name));
  AlgSet *s = set();
  const CaseGroup &group = *s->groupOf(c->id);
  const StepSpec &spec = s->specFor(group);
  const CaseAnalysis *a = editor_.analysisOf(c->id);
  caseDiagram_->setPixmap(cube_painter::diagram(
      maskedFacelets(a ? a->display : c->state, spec.mask, spec.free),
      spec.view, diagramSize(), look(), devicePixelRatioF()));
  caseGroup_->setText(qs(group.name) +
                      (group.spec ? tr(" (its own step settings)") : ""));
  int passing = a ? static_cast<int>(a->passing.size()) : 0;
  caseAlgCount_->setText(
      c->algs.empty() ? tr("none yet")
                      : tr("%1, %2 passing").arg(c->algs.size()).arg(passing));
  // What Learn and Drill have recorded for it.
  QStringList progress;
  const SetProgress &p = library_.progress(s->id);
  if (auto it = p.find(c->id); it != p.end()) {
    switch (statusOf(&it->second)) {
    case CaseStatus::Unstarted:
      progress << tr("not learned yet");
      break;
    case CaseStatus::Learning:
      progress << tr("learning");
      break;
    case CaseStatus::Learned:
      progress << tr("learned");
      break;
    }
    const std::pair<AttemptMode, QString> modes[] = {
        {AttemptMode::PhysicalDrill, tr("physical cube")},
        {AttemptMode::VirtualDrill, tr("virtual cube")}};
    for (const auto &[mode, name] : modes) {
      QString text = drillText(it->second, mode);
      if (!text.isEmpty()) {
        progress << tr("%1 (%2)").arg(text, name);
      }
    }
  } else {
    progress << tr("not learned yet, no drills");
  }
  caseProgress_->setText(progress.join("; "));
  caseHistory_->setText(historyHtml(*c));
  if (a && a->main && s->reduction) {
    // Every step from this case to the end, by the set's main algs.
    Chain chain = solveChain(*s, c->state);
    QStringList steps;
    for (const ChainStep &step : chain.steps) {
      steps << qs(step.alg);
    }
    caseSummary_->setText(
        chain.complete
            ? (steps.size() == 1 ? tr("Finishes the step: %1")
                                 : tr("To the end, a step at a time: %1"))
                  .arg(steps.join("  ·  "))
            : tr("Its steps don't reach the end: %1").arg(steps.join("  ·  ")));
  } else if (a && a->main) {
    caseSummary_->setText(
        tr("Shown as the main alg starts: %1").arg(qs(a->mainAlg)));
  } else {
    caseSummary_->setText(c->algs.empty() ? tr("No algs yet.")
                                          : tr("No alg solves this case yet."));
  }
  refreshAlgList();
  updateActions();
}

void LibraryPage::refreshAlgList() {
  AlgCase *c = currentCase();
  QSignalBlocker block(algList_);
  loading_ = true;
  int row = algList_->currentItem()
                ? algList_->indexOfTopLevelItem(algList_->currentItem())
                : -1;
  algList_->clear();
  if (c) {
    const CaseAnalysis *a = editor_.analysisOf(c->id);
    for (std::size_t i = 0; i < c->algs.size(); ++i) {
      auto *item = new QTreeWidgetItem(algList_, {qs(c->algs[i])});
      // Items can be dragged and edited but not dropped onto (no nesting).
      item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled |
                     Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
      if (!a || i >= a->checks.size()) {
        continue;
      }
      const AlgCheck &check = a->checks[i];
      QString result = qs(describeCheck(check));
      if (check.passes()) {
        bool isMain = a->main == i;
        item->setText(1, (isMain ? tr("✓ main · %1") : tr("✓ %1")).arg(result));
        item->setForeground(1, kPassColour);
        if (isMain) {
          QFont f = item->font(0);
          f.setBold(true);
          item->setFont(0, f);
        }
      } else {
        item->setText(1, tr("✗ %1").arg(result));
        item->setForeground(1, kFailColour);
        item->setForeground(0, kFailColour);
      }
      if (check.parseError) {
        item->setToolTip(1, qs(check.parseError->message));
      }
    }
    if (lab_) {
      // Each alg's times from Alg Lab, on each cube it was timed on.
      LabCaseKey key{editor_.setId, c->id};
      WarmupOf warm = [this](std::string_view id) {
        return lab_->warmupOf(id);
      };
      for (int i = 0; i < algList_->topLevelItemCount(); ++i) {
        std::string alg = labAlgKey(c->algs[static_cast<std::size_t>(i)]);
        QStringList parts;
        for (CubeKind cube : {CubeKind::Physical, CubeKind::Virtual}) {
          LabStats st = labStats(lab_->attempts(), key, alg, cube, {}, 0, warm);
          if (st.mean) {
            parts << tr("%1 mean of %2%3")
                         .arg(qs(formatTime(*st.mean, settings_.decimals)))
                         .arg(st.used)
                         .arg(cube == CubeKind::Virtual ? tr(" (virtual)")
                                                        : QString());
          }
        }
        algList_->topLevelItem(i)->setText(2, parts.join(" · "));
      }
    }
  }
  if (row >= 0 && row < algList_->topLevelItemCount()) {
    algList_->setCurrentItem(algList_->topLevelItem(row));
  }
  loading_ = false;
  updateGrips();
}

void LibraryPage::updateGrips() {
  const ErgoProfile *g = lab_ ? labGuideProfile(*lab_) : nullptr;
  gripTitle_->setVisible(lab_ != nullptr);
  grips_->setVisible(lab_ != nullptr);
  QTreeWidgetItem *item = algList_->currentItem();
  if (!item) {
    grips_->clear(currentCase() && !currentCase()->algs.empty()
                      ? tr("Select an alg to see how to turn it.")
                      : QString());
    return;
  }
  grips_->setAlg(item->text(0), g ? &*g->grips : nullptr,
                 g ? tr("Can't read this alg.")
                   : tr("No grip profile: make one in Alg Lab (Profile…)."));
}

void LibraryPage::storeAlgEdit(const std::string &setId,
                               const std::string &caseId,
                               const QStringList &texts, int row) {
  if (setId == editor_.setId) {
    storeAlgTexts(tr("Edit alg"), caseId, texts, row);
    return;
  }
  if (!library_.find(setId)) {
    return;
  }
  // Another set is shown by now: store the edit in its own set, then show
  // the chosen one again.
  const std::string shownSet = editor_.setId;
  const std::string shownCase = editor_.caseId;
  editor_.setId = setId;
  editor_.caseId = caseId;
  storeAlgTexts(tr("Edit alg"), caseId, texts, row);
  editor_.setId = shownSet;
  editor_.caseId = shownCase;
  refresh({shownSet, std::nullopt, true, std::nullopt});
}

QStringList LibraryPage::algTexts() const {
  QStringList texts;
  for (int i = 0; i < algList_->topLevelItemCount(); ++i) {
    texts << algList_->topLevelItem(i)->text(0);
  }
  return texts;
}

void LibraryPage::storeAlgs(const QString &text, std::optional<int> edited) {
  storeAlgTexts(text, editor_.caseId, algTexts(), edited);
}

void LibraryPage::storeAlgTexts(const QString &text, const std::string &caseId,
                                const QStringList &texts,
                                std::optional<int> edited) {
  AlgSet *s = editor_.set();
  AlgCase *c = s && !caseId.empty() ? s->findCase(caseId) : nullptr;
  if (!c) {
    return;
  }
  std::vector<std::string> algs;
  std::optional<std::size_t> editedIndex;
  for (int i = 0; i < texts.size(); ++i) {
    std::string alg = cleanAlgText(ss(texts[i]));
    if (alg.empty()) {
      continue;
    }
    if (std::find(algs.begin(), algs.end(), alg) != algs.end()) {
      emit message(tr("%1 already has \"%2\"").arg(qs(c->name), qs(alg)));
      refreshAlgList(); // back to what it was
      return;
    }
    if (edited == i) {
      editedIndex = algs.size();
    }
    algs.push_back(alg);
  }
  if (algs == c->algs) {
    refreshAlgList(); // e.g. only spaces were tidied
    return;
  }
  auto pending = editor_.begin();
  c->algs = std::move(algs);
  editor_.commit(text, *pending, std::vector<std::string>{caseId});
  // The edited alg as stored, which may have been tidied up.
  if (editedIndex && caseId == editor_.caseId) {
    warnIfFailing(*editedIndex);
  }
}

void LibraryPage::warnIfFailing(std::size_t index) {
  const AlgCase *c = currentCase();
  const CaseAnalysis *a = c ? editor_.analysisOf(c->id) : nullptr;
  if (!a || index >= a->checks.size() || index >= c->algs.size() ||
      a->checks[index].passes()) {
    return;
  }
  emit message(
      tr("Kept \"%1\", but it doesn't pass: %2")
          .arg(qs(c->algs[index]), qs(describeCheck(a->checks[index]))));
}

void LibraryPage::showAlgMenu(const QPoint &pos) {
  QTreeWidgetItem *item = algList_->itemAt(pos);
  if (!item) {
    return;
  }
  int row = algList_->indexOfTopLevelItem(item);
  algList_->setCurrentItem(item);
  QMenu menu(this);
  menu.addAction(tr("Make main alg"), this, [this, row] { makeMain(row); })
      ->setEnabled(row > 0);
  menu.addAction(tr("Edit"), QKeySequence(Qt::Key_F2), this,
                 [this, item] { algList_->editItem(item, 0); });
  menu.addAction(tr("Move up"), QKeySequence(Qt::ALT | Qt::Key_Up), this,
                 [this, row] { moveAlg(row, -1); })
      ->setEnabled(row > 0);
  menu.addAction(tr("Move down"), QKeySequence(Qt::ALT | Qt::Key_Down), this,
                 [this, row] { moveAlg(row, 1); })
      ->setEnabled(row + 1 < algList_->topLevelItemCount());
  menu.addAction(tr("Open in Alg Lab"), this, [this] {
    if (currentCase()) {
      emit algLabRequested(qs(editor_.setId), qs(currentCase()->id));
    }
  });
  menu.addSeparator();
  menu.addAction(tr("Remove"), QKeySequence(QKeySequence::Delete), this,
                 [this, row] { removeAlg(row); });
  menu.exec(algList_->viewport()->mapToGlobal(pos));
}

void LibraryPage::addAlg() {
  AlgCase *c = currentCase();
  std::string text = cleanAlgText(ss(newAlgEdit_->text()));
  if (!c || text.empty()) {
    return;
  }
  if (std::find(c->algs.begin(), c->algs.end(), text) != c->algs.end()) {
    emit message(tr("%1 already has \"%2\"").arg(qs(c->name), qs(text)));
    return;
  }
  // Failing algs are kept, with a warning: the check shows what's wrong.
  auto pending = editor_.begin();
  c->algs.push_back(text);
  std::size_t index = c->algs.size() - 1;
  newAlgEdit_->clear();
  if (editor_.commit(tr("Add alg"), *pending,
                     std::vector<std::string>{editor_.caseId})) {
    warnIfFailing(index);
  }
}

void LibraryPage::removeAlg(int row) {
  AlgCase *c = currentCase();
  if (!c || row < 0 || row >= static_cast<int>(c->algs.size())) {
    return;
  }
  auto pending = editor_.begin();
  std::string alg = c->algs[static_cast<std::size_t>(row)];
  c->algs.erase(c->algs.begin() + row);
  if (editor_.commit(tr("Remove alg"), *pending,
                     std::vector<std::string>{editor_.caseId})) {
    emit message(tr("Removed \"%1\"; %2 brings it back")
                     .arg(qs(alg), undoAction_->shortcut().toString(
                                       QKeySequence::NativeText)));
  }
}

void LibraryPage::moveAlg(int row, int direction) {
  AlgCase *c = currentCase();
  int to = row + direction;
  if (!c || row < 0 || to < 0 || row >= static_cast<int>(c->algs.size()) ||
      to >= static_cast<int>(c->algs.size())) {
    return;
  }
  auto pending = editor_.begin();
  std::swap(c->algs[static_cast<std::size_t>(row)],
            c->algs[static_cast<std::size_t>(to)]);
  if (editor_.commit(tr("Reorder algs"), *pending,
                     std::vector<std::string>{editor_.caseId})) {
    algList_->setCurrentItem(algList_->topLevelItem(to));
  }
}

void LibraryPage::makeMain(int row) {
  AlgCase *c = currentCase();
  if (!c || row <= 0 || row >= static_cast<int>(c->algs.size())) {
    return;
  }
  const CaseAnalysis *a = editor_.analysisOf(c->id);
  if (a && static_cast<std::size_t>(row) < a->checks.size() &&
      !a->checks[static_cast<std::size_t>(row)].passes()) {
    auto answer = QMessageBox::question(
        this, tr("Make main alg"),
        tr("This alg doesn't pass the check (%1). It moves to the top of the "
           "list but only becomes the main alg once it passes. Move it "
           "anyway?")
            .arg(qs(describeCheck(a->checks[static_cast<std::size_t>(row)]))));
    if (answer != QMessageBox::Yes) {
      return;
    }
  }
  auto pending = editor_.begin();
  std::string alg = c->algs[static_cast<std::size_t>(row)];
  c->algs.erase(c->algs.begin() + row);
  c->algs.insert(c->algs.begin(), alg);
  if (editor_.commit(tr("Make main alg"), *pending,
                     std::vector<std::string>{editor_.caseId})) {
    algList_->setCurrentItem(algList_->topLevelItem(0));
  }
}

} // namespace vcube
