#include "app/alglab_page.hpp"
#include "app/alglab_page_private.hpp"
#include "app/ui_state.hpp"

#include "app/alglab_dialogs.hpp"
#include "app/app_settings.hpp"
#include "app/grip_view.hpp"
#include "app/lab_test_panel.hpp"
#include "app/ui_text.hpp"
#include "app/widgets.hpp"
#include "io/algset_json.hpp"
#include "train/ergonomics.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace vcube {

namespace {

QString sourceText(const CandidateRow &r) {
  if (r.inSet) {
    return QObject::tr("set");
  }
  switch (r.source) {
  case CandidateSource::Generated:
    return QObject::tr("generated");
  case CandidateSource::Pasted:
    return QObject::tr("pasted");
  case CandidateSource::Typed:
    return QObject::tr("typed");
  case CandidateSource::Set:
    return QObject::tr("was in set");
  }
  return {};
}

} // namespace

void AlgLabPage::fillTable(bool resize) {
  QString selected;
  if (auto *it = table_->currentItem()) {
    selected = table_->item(it->row(), ColAlg)->data(Qt::UserRole).toString();
  }
  // The guidance follows the selection once the table is filled again.
  QSignalBlocker blockSelection(table_);
  struct UpdateAfter {
    AlgLabPage *page;
    ~UpdateAfter() { page->updateGrips(); }
  } updateAfter{this};
  table_->setSortingEnabled(false);
  table_->setRowCount(0);
  if (!case_) {
    for (int c = ColCount; c < ColumnCount; ++c) {
      table_->setColumnHidden(c, true);
    }
    return;
  }
  rows_ = candidateRows(*case_, store_, candidateCache_);
  const std::string mainAlg =
      case_->setAlgs.empty()
          ? std::string()
          : candidateCache_.of(*case_, case_->setAlgs.front()).key;
  const LabAttemptGroups attempts(store_.attempts(), case_->key);
  const ErgoProfile &prof = profile();
  CubeKind cube =
      statsCube_->currentIndex() == 1 ? CubeKind::Virtual : CubeKind::Physical;
  LabWindow window;
  if (statsWindow_->currentIndex() == 1) {
    window.kind = LabWindow::Kind::Session;
    window.session = store_.current().id;
  } else if (statsWindow_->currentIndex() == 2) {
    window.kind = LabWindow::Kind::Recent;
    window.recent = 50;
  }
  WarmupOf warm = [this](std::string_view id) { return store_.warmupOf(id); };
  const int trim = store_.current().trimPercent;
  const int d = settings_.decimals;
  int hidden = 0;
  bool anyTimes = false; // an alg listed has times: the time columns show
  for (const CandidateRow &r : rows_) {
    const Alg &alg = r.moves;
    const MoveCounts &mc = r.counts;
    if ((filterOn_ && !filterSet_.contains(alg)) ||
        (noRotations_->isChecked() && r.rotations) ||
        (maxHtm_->value() > 0 && mc.htm > maxHtm_->value())) {
      ++hidden;
      continue;
    }
    int row = table_->rowCount();
    table_->insertRow(row);
    auto *algItem = new QTableWidgetItem(r.shown);
    algItem->setFlags(algItem->flags() & ~Qt::ItemIsEditable);
    algItem->setData(Qt::UserRole, qs(r.alg));
    if (!r.fit) {
      algItem->setForeground(palette().color(QPalette::PlaceholderText));
    }
    if (r.inSet && !case_->setAlgs.empty() && mainAlg == r.alg) {
      QFont f = algItem->font();
      f.setBold(true);
      algItem->setFont(f);
    }
    table_->setItem(row, ColAlg, algItem);
    auto num = [&](int col, const QString &text, std::optional<double> k) {
      table_->setItem(row, col, new SortItem(text, k));
    };
    num(ColHtm, QString::number(mc.htm), mc.htm);
    num(ColStm, QString::number(mc.stm), mc.stm);
    num(ColQtm, QString::number(mc.qtm), mc.qtm);
    num(ColEtm, QString::number(mc.etm), mc.etm);
    auto cached = scoreCache_.find(r.alg);
    if (cached == scoreCache_.end()) {
      cached = scoreCache_.emplace(r.alg, ergoTotal(alg, prof)).first;
    }
    double score = cached->second;
    num(ColScore, QString::number(score, 'f', 1), score);
    auto *src = new QTableWidgetItem(sourceText(r));
    src->setFlags(src->flags() & ~Qt::ItemIsEditable);
    table_->setItem(row, ColSource, src);
    LabStats st = labStats(attempts.of(case_->key, r.alg), case_->key, r.alg,
                           cube, window, trim, warm);
    auto ms = [](std::optional<std::int64_t> v) -> std::optional<double> {
      if (!v) {
        return std::nullopt;
      }
      return static_cast<double>(*v);
    };
    anyTimes = anyTimes || st.used + st.dnfs > 0;
    num(ColCount, QString::number(st.used), st.used);
    num(ColMean, labTime(st.mean, d), ms(st.mean));
    num(ColMedian, labTime(st.median, d), ms(st.median));
    num(ColBest, labTime(st.best, d), ms(st.best));
    num(ColAo5, labTime(st.ao5, d), ms(st.ao5));
    num(ColAo12, labTime(st.ao12, d), ms(st.ao12));
    num(ColSd, st.sd ? QString::number(*st.sd / 1000.0, 'f', d) : "–", st.sd);
    num(ColDnf,
        st.used + st.dnfs ? QString::number(st.dnfRate() * 100.0, 'f', 0) : "–",
        st.used + st.dnfs ? std::optional(st.dnfRate()) : std::nullopt);
    if (qs(r.alg) == selected) {
      table_->selectRow(row);
    }
  }
  table_->setSortingEnabled(true);
  if (resize) {
    for (int c = ColHtm; c < ColumnCount; ++c) {
      table_->resizeColumnToContents(c);
    }
  }
  // The eight time columns only once there are times to show.
  for (int c = ColCount; c < ColumnCount; ++c) {
    table_->setColumnHidden(c, !anyTimes);
  }
  if (hidden > 0 && !job_) {
    genStatus_->setText(plural(hidden, tr("%n alg hidden by the filters."),
                               tr("%n algs hidden by the filters.")));
  }
}

void AlgLabPage::updateGrips() {
  const ErgoProfile *g = labGuideProfile(store_, profileBox_->currentText());
  QTableWidgetItem *it = table_->currentItem();
  if (!it || !table_->item(it->row(), ColAlg) || !it->isSelected()) {
    grips_->clear(case_ ? tr("Select an alg to see how to turn it.")
                        : QString());
    return;
  }
  QString alg = table_->item(it->row(), ColAlg)->data(Qt::UserRole).toString();
  grips_->setAlg(alg, g ? &*g->grips : nullptr,
                 tr("No grip profile: make one with Profile… > New grip "
                    "profile, or restore the built-in profiles."));
}

std::vector<std::string> AlgLabPage::selectedAlgs() const {
  std::vector<std::string> out;
  for (const auto &index : table_->selectionModel()->selectedRows(ColAlg)) {
    out.push_back(index.data(Qt::UserRole).toString().toStdString());
  }
  return out;
}

void AlgLabPage::addTyped() {
  if (!case_) {
    return;
  }
  std::string text = cleanAlgText(addEdit_->text().toStdString());
  if (text.empty()) {
    return;
  }
  auto parsed = parseAlg(text);
  if (!parsed) {
    emit message(tr("Can't read that alg at character %1.")
                     .arg(parsed.error->position + 1));
    return;
  }
  std::string k = labAlgKey(text);
  if (!fitAlg(case_->state, parsed.alg, case_->free, aufsOf(*case_))) {
    AlgCheck check = checkAlg(case_->state, k, case_->free, aufsOf(*case_));
    emit message(tr("That alg doesn't solve the case: it %1. Added anyway, "
                    "greyed out.")
                     .arg(qs(describeCheck(check))));
  }
  store_.addCandidate(case_->key, {k, CandidateSource::Typed, unixNow()});
  saveStore();
  addEdit_->clear();
  fillTable();
}

void AlgLabPage::pasteCandidates() {
  if (!case_) {
    return;
  }
  bool ok = false;
  QString text = QInputDialog::getMultiLineText(
      this, tr("Paste algs"),
      tr("One alg per line (\"name: alg / alg\" lines and // comments are "
         "fine). Algs that don't solve the case are left out."),
      QApplication::clipboard()->text(), &ok);
  if (!ok) {
    return;
  }
  int added = 0, failed = 0, have = 0;
  for (const PasteItem &p : readPaste(text.toStdString())) {
    auto parsed = parseAlg(p.alg);
    if (!parsed || parsed.alg.empty() ||
        !fitAlg(case_->state, parsed.alg, case_->free, aufsOf(*case_))) {
      ++failed;
      continue;
    }
    if (store_.addCandidate(case_->key, {labAlgKey(p.alg),
                                         CandidateSource::Pasted, unixNow()})) {
      ++added;
    } else {
      ++have;
    }
  }
  saveStore();
  fillTable();
  emit message(tr("Added %1; %2 didn't solve the case, %3 there already.")
                   .arg(counted(added, tr("alg"), tr("algs")))
                   .arg(failed)
                   .arg(plural(have, tr("%n was"), tr("%n were"))));
}

void AlgLabPage::addToSet(bool makeMain) {
  if (!case_) {
    return;
  }
  if (case_->key.isLab()) {
    emit message(tr("A lab case isn't in a set."));
    return;
  }
  auto algs = selectedAlgs();
  if (algs.empty() || (makeMain && algs.size() != 1)) {
    emit message(makeMain ? tr("Select one alg to make it the main alg.")
                          : tr("Select the algs to add to the set."));
    return;
  }
  AlgSet *set = library_.find(case_->key.setId);
  AlgCase *c = set ? set->findCase(case_->key.caseId) : nullptr;
  if (!c) {
    return;
  }
  std::vector<std::string> newAlgs = c->algs;
  int added = 0;
  for (const std::string &a : algs) {
    auto it =
        std::find_if(newAlgs.begin(), newAlgs.end(),
                     [&](const std::string &x) { return labAlgKey(x) == a; });
    if (makeMain) {
      std::string text = it != newAlgs.end() ? *it : a;
      if (it != newAlgs.end()) {
        newAlgs.erase(it);
      }
      newAlgs.insert(newAlgs.begin(), text);
      ++added;
    } else if (it == newAlgs.end()) {
      newAlgs.push_back(a);
      ++added;
    }
  }
  if (added == 0 || newAlgs == c->algs) {
    emit message(tr("The set has those algs already."));
    return;
  }
  QString what = makeMain ? tr("Make main alg") : tr("Add algs");
  bool saved = false;
  if (algEditor_) {
    saved = algEditor_(qs(set->id), qs(c->id), newAlgs, what);
    setEdits_ += saved ? 1 : 0;
  } else {
    std::vector<std::string> old = c->algs;
    c->algs = newAlgs;
    saved = library_.save(*set);
    if (!saved) {
      c->algs = old;
    }
  }
  if (!saved) {
    emit message(tr("Could not save the set."));
    return;
  }
  emit message(makeMain ? tr("Made it the main alg of %1.").arg(case_->name)
                        : plural(added, tr("Added %n alg to %1."),
                                 tr("Added %n algs to %1."))
                              .arg(case_->name));
  if (!algEditor_) {
    emit setsChanged(qs(set->id));
  }
  showCase(case_->key);
}

void AlgLabPage::addToSession() {
  if (!case_) {
    return;
  }
  std::vector<LabEntry> entries;
  for (const std::string &a : selectedAlgs()) {
    auto parsed = parseAlg(a);
    if (parsed &&
        fitAlg(case_->state, parsed.alg, case_->free, aufsOf(*case_))) {
      entries.push_back({case_->key, a});
    }
  }
  if (entries.empty()) {
    emit message(tr("Select algs that solve the case to add them."));
    return;
  }
  int added = test_->addEntries(entries);
  emit message(tr("Added %1 to the session %2 (Test tab).")
                   .arg(counted(added, tr("alg"), tr("algs")))
                   .arg(test_->sessionName()));
}

void AlgLabPage::removeCandidates() {
  if (!case_) {
    return;
  }
  int removed = 0, inSet = 0;
  for (const std::string &a : selectedAlgs()) {
    if (store_.removeCandidate(case_->key, a)) {
      ++removed;
    } else {
      ++inSet;
    }
  }
  if (removed) {
    saveStore();
    fillTable();
  }
  if (inSet) {
    emit message(tr("Algs in the set are removed in Library."));
  }
}

void AlgLabPage::clearTimes() {
  auto algs = selectedAlgs();
  if (!case_ || algs.empty()) {
    return;
  }
  if (QMessageBox::question(
          this, tr("Clear times"),
          plural(static_cast<int>(algs.size()),
                 tr("Forget every time of the selected alg, on both cubes "
                    "and in every session?"),
                 tr("Forget every time of the %n selected algs, on both "
                    "cubes and in every session?"))) != QMessageBox::Yes) {
    return;
  }
  for (const std::string &a : algs) {
    store_.clearAttempts(case_->key, a);
  }
  saveStore();
  fillTable();
  test_->refresh();
}

void AlgLabPage::tableMenu(const QPoint &at) {
  if (!table_->itemAt(at)) {
    return;
  }
  QMenu menu(this);
  fillModeMenu(menu);
  menu.exec(table_->viewport()->mapToGlobal(at));
}

void AlgLabPage::fillModeMenu(QMenu &menu) {
  if (selectedAlgs().empty()) {
    menu.addAction(tr("Select an alg in the Search tab"))->setEnabled(false);
    return;
  }
  menu.addAction(tr("Add to session"), this, &AlgLabPage::addToSession);
  if (case_ && !case_->key.isLab()) {
    menu.addAction(tr("Add to set"), this, [this] { addToSet(false); });
    menu.addAction(tr("Make main alg"), this, [this] { addToSet(true); });
  }
  menu.addAction(tr("Copy"), this, [this] {
    QStringList lines;
    for (const auto &a : selectedAlgs()) {
      lines << qs(a);
    }
    QApplication::clipboard()->setText(lines.join("\n"));
  });
  menu.addSeparator();
  menu.addAction(tr("Remove"), this, &AlgLabPage::removeCandidates);
  menu.addAction(tr("Clear times…"), this, &AlgLabPage::clearTimes);
}

} // namespace vcube
