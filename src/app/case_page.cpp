#include "app/case_page.hpp"

#include "app/ui_state.hpp"
#include "io/data_file.hpp"

#include "app/cube_painter.hpp"
#include "app/widgets.hpp"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>
#include <utility>

namespace vcube {

namespace {

constexpr int kCaseIdRole = Qt::UserRole;
constexpr int kOrderRole = Qt::UserRole + 31; // place in list order

// A row that sorts by its kSortRole numbers, or in list order.
class SortItem : public QTreeWidgetItem {
public:
  using QTreeWidgetItem::QTreeWidgetItem;
  bool operator<(const QTreeWidgetItem &other) const override {
    int column = treeWidget() ? treeWidget()->sortColumn() : 0;
    if (column > 0) {
      QVariant va = data(column, CasePage::kSortRole);
      QVariant vb = other.data(column, CasePage::kSortRole);
      double a = va.isValid() ? va.toDouble() : -1;
      double b = vb.isValid() ? vb.toDouble() : -1;
      if (a != b) {
        return a < b;
      }
      // Ties stay in list order either way round.
      if (treeWidget()->header()->sortIndicatorOrder() == Qt::DescendingOrder) {
        return data(0, kOrderRole).toInt() > other.data(0, kOrderRole).toInt();
      }
    }
    return data(0, kOrderRole).toInt() < other.data(0, kOrderRole).toInt();
  }
};

} // namespace

QString dueText(const CaseProgress *p, std::int64_t now, std::int64_t dayEnd) {
  if (!p || p->card.state == CardState::New) {
    return "-";
  }
  std::int64_t d = p->card.due - now;
  if (d <= 0) {
    return QObject::tr("now");
  }
  if (p->card.state == CardState::Review && p->card.due < dayEnd) {
    return QObject::tr("today");
  }
  if (d < 3600) {
    return QObject::tr("%1 min").arg((d + 59) / 60);
  }
  if (d < 86400) {
    return QObject::tr("%1 h").arg((d + 1799) / 3600);
  }
  return QObject::tr("%1 d").arg((d + 43199) / 86400);
}

QString skippedText(int cases) {
  return cases == 1 ? QObject::tr("1 case without a passing alg is skipped.")
                    : QObject::tr("%1 cases without a passing alg are skipped.")
                          .arg(cases);
}

QString secondsText(std::optional<double> ms) {
  // Truncated to hundredths, as the timer shows times.
  return ms ? QString::number(std::floor(*ms / 10.0) / 100.0, 'f', 2)
            : QString("-");
}

CasePage::CasePage(AppSettings &settings, AlgLibrary &library, QString mode,
                   QWidget *parent)
    : ModePage(parent), settings_(settings), library_(library),
      mode_(std::move(mode)), rng_(makeRng()) {
  panel_ = new CaseListPanel(mode_, settings_.thumbnailSize, this);
  left_ = panel_;
  leftLayout_ = panel_->layout;
  form_ = panel_->form;
  setBox_ = panel_->setBox;
  filter_ = panel_->filter;
  tree_ = panel_->tree;
  summary_ = panel_->summary;

  connect(setBox_, &QComboBox::activated, this, [this](int i) {
    std::string id = ss(setBox_->itemData(i).toString());
    if (id == kAcrossSetsId) {
      selectAcrossSets();
    } else {
      selectSet(id);
    }
  });
  connect(tree_, &QTreeWidget::itemChanged, this,
          [this](QTreeWidgetItem *, int column) {
            if (column == 0) {
              ticksEdited();
            }
          });
  connect(tree_, &QTreeWidget::customContextMenuRequested, this,
          &CasePage::showCaseMenu);
  connect(tree_, &QTreeWidget::itemDoubleClicked, this,
          [this](QTreeWidgetItem *item) {
            QVariant id = item->data(0, kCaseIdRole);
            if (!id.isNull() && eligible(ss(id.toString()))) {
              startCaseFromList(ss(id.toString()));
            }
          });
  neutrality_ = settings_.neutrality;
}

void CasePage::init() {
  auto byId = [](std::function<bool(const std::string &)> test) {
    return [test = std::move(test)](const QTreeWidgetItem *item) {
      return test(ss(item->data(0, kCaseIdRole).toString()));
    };
  };
  std::vector<CaseFilter::Status> statuses;
  statuses.push_back({tr("Ticked"), [](const QTreeWidgetItem *item) {
                        return item->checkState(0) == Qt::Checked;
                      }});
  statuses.push_back({tr("Not ticked"), [](const QTreeWidgetItem *item) {
                        return item->checkState(0) != Qt::Checked;
                      }});
  for (CaseStatusFilter &f : statusFilters()) {
    statuses.push_back({f.name, byId(std::move(f.test))});
  }
  statuses.push_back({tr("No alg"), byId([this](const std::string &id) {
                        return !eligible(id);
                      })});
  filter_->setStatuses(std::move(statuses));

  panel_->setColumns(columns(), defaultHiddenColumns());
  panel_->setSortable(sortableColumns());
  applySettings();
  populateSets();
  QString last = saved::caseSet(mode_);
  if (last == kAcrossSetsId && offersAcrossSets()) {
    setId_ = ss(saved::shownSet(mode_).get());
    selectAcrossSets();
    return;
  }
  selectSet(last.isEmpty() && !library_.sets().empty()
                ? library_.sets().front().id
                : ss(last));
}

void CasePage::applySort() { panel_->applySort(); }

void CasePage::fillGroupRows() {
  QSignalBlocker block(tree_);
  for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
    QTreeWidgetItem *group = tree_->topLevelItem(g);
    std::vector<std::string> ids;
    for (int i = 0; i < group->childCount(); ++i) {
      ids.push_back(ss(group->child(i)->data(0, kCaseIdRole).toString()));
    }
    fillGroupRow(group, ids);
  }
}

// --- set and cases ----------------------------------------------------------

AlgSet *CasePage::set() { return library_.find(setId_); }

const AlgSet *CasePage::set() const {
  return std::as_const(library_).find(setId_);
}

SetProgress &CasePage::progress() { return library_.progress(setId_); }

const SetProgress &CasePage::progress() const {
  return std::as_const(library_).progress(setId_);
}

const CaseProgress *CasePage::progressOf(const std::string &caseId) const {
  const SetProgress &p = progress();
  auto it = p.find(caseId);
  return it == p.end() ? nullptr : &it->second;
}

std::string CasePage::cardId(const std::string &caseId) const {
  return byFamily() ? families_.representative(caseId) : caseId;
}

const CaseProgress *CasePage::cardOf(const std::string &caseId) const {
  return progressOf(cardId(caseId));
}

const CasePage::CaseInfo *CasePage::infoOf(const std::string &caseId) const {
  auto it = info_.find(caseId);
  return it == info_.end() ? nullptr : &it->second;
}

bool CasePage::eligible(const std::string &caseId) const {
  const CaseInfo *i = infoOf(caseId);
  return i && !i->passing.empty() && steps_.contains(caseId);
}

int CasePage::stepsFrom(const std::string &caseId) const {
  auto it = steps_.find(caseId);
  return it == steps_.end() ? 0 : it->second;
}

Mirror CasePage::pickWorld(const std::string &caseId) {
  const AlgSet *s = library_.find(setId_);
  const CaseGroup *g = s ? s->groupOf(caseId) : nullptr;
  const CaseInfo *info = infoOf(caseId);
  std::vector<Mirror> worlds{Mirror{}};
  if (g && info) {
    // A mirror image of a case that doesn't depend on the slot would only
    // be the same case seen from another corner.
    for (Mirror m : s->worlds()) {
      if (!m.none() && !slotIndependent(info->state, s->specFor(*g).free, m)) {
        worlds.push_back(m);
      }
    }
  }
  std::uniform_int_distribution<std::size_t> pick(0, worlds.size() - 1);
  return worlds[pick(rng_)];
}

std::vector<std::string> CasePage::shownEligible() const {
  std::vector<std::string> out;
  for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
    auto *group = tree_->topLevelItem(g);
    if (group->isHidden()) {
      continue;
    }
    for (int i = 0; i < group->childCount(); ++i) {
      std::string id = ss(group->child(i)->data(0, kCaseIdRole).toString());
      if (!group->child(i)->isHidden() && eligible(id)) {
        out.push_back(id);
      }
    }
  }
  return out;
}

std::vector<std::string>
CasePage::groupEligible(const std::string &groupName) const {
  std::vector<std::string> out;
  for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
    auto *group = tree_->topLevelItem(g);
    if (group->text(0) != qs(groupName)) {
      continue;
    }
    for (int i = 0; i < group->childCount(); ++i) {
      std::string id = ss(group->child(i)->data(0, kCaseIdRole).toString());
      if (eligible(id)) {
        out.push_back(id);
      }
    }
  }
  return out;
}

std::vector<std::string> CasePage::tickedEligible() const {
  std::vector<std::string> out;
  for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
    auto *group = tree_->topLevelItem(g);
    for (int i = 0; i < group->childCount(); ++i) {
      std::string id = ss(group->child(i)->data(0, kCaseIdRole).toString());
      if (group->child(i)->checkState(0) == Qt::Checked && eligible(id)) {
        out.push_back(id);
      }
    }
  }
  return out;
}

int CasePage::tickedCount() const {
  int n = 0;
  for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
    auto *group = tree_->topLevelItem(g);
    for (int i = 0; i < group->childCount(); ++i) {
      n += group->child(i)->checkState(0) == Qt::Checked ? 1 : 0;
    }
  }
  return n;
}

int CasePage::excludedCount() const {
  int n = 0;
  for (const auto &[id, i] : info_) {
    n += eligible(id) ? 0 : 1;
  }
  return n;
}

void CasePage::applySettings() {
  tree_->setIconSize(QSize(settings_.thumbnailSize, settings_.thumbnailSize));
  if (!isVisible()) {
    rebuildPending_ = true;
  } else if (set()) {
    buildTree();
  }
  if (settings_.neutrality != neutrality_) {
    neutrality_ = settings_.neutrality;
    neutralityChanged();
  }
}

void CasePage::activated() {
  if (reloadPending_) {
    reloadPending_ = false;
    rebuildPending_ = false;
    reloadSets();
  } else if (rebuildPending_) {
    rebuildPending_ = false;
    buildTree();
  } else if (set()) {
    syncRows(); // ticks and progress are shared with the other mode
  }
}

void CasePage::deactivated() { filter_->release(); }

void CasePage::focusModeChanged() { left_->setVisible(!focusMode()); }

QString CasePage::setItemText(const AlgSet &s) const {
  return QString("%1 (%2)").arg(qs(s.name)).arg(s.caseCount());
}

void CasePage::populateSets() {
  QSignalBlocker block(setBox_);
  setBox_->clear();
  if (offersAcrossSets()) {
    setBox_->addItem(acrossItemText(), QString(kAcrossSetsId));
    setBox_->insertSeparator(1);
  }
  for (const auto &s : library_.sets()) {
    setBox_->addItem(setItemText(s), qs(s.id));
  }
}

void CasePage::refreshSetLabels() {
  for (int i = 0; i < setBox_->count(); ++i) {
    std::string id = ss(setBox_->itemData(i).toString());
    if (id == kAcrossSetsId) {
      setBox_->setItemText(i, acrossItemText());
    } else if (const AlgSet *s = library_.find(id)) {
      setBox_->setItemText(i, setItemText(*s));
    }
  }
}

void CasePage::reloadSets() {
  if (!isVisible()) {
    reloadPending_ = true;
    return;
  }
  infoCache_.clear();
  populateSets();
  std::string id = setId_;
  if (!library_.find(id)) {
    id = library_.sets().empty() ? std::string() : library_.sets().front().id;
  }
  if (acrossSets_) {
    setBox_->setCurrentIndex(setBox_->findData(QString(kAcrossSetsId)));
    setId_ = id;
    computeInfo();
    buildTree();
    setReloaded();
  } else if (id == setId_ && set()) {
    setBox_->setCurrentIndex(setBox_->findData(qs(id)));
    computeInfo();
    buildTree();
    setReloaded();
  } else {
    selectSet(id);
  }
}

void CasePage::selectSet(const std::string &id) {
  acrossSets_ = false;
  if (id != setId_) {
    filter_->clearSearch(); // another set's cases
  }
  setId_ = id;
  if (!library_.find(id) && !library_.sets().empty()) {
    setId_ = library_.sets().front().id;
  }
  saved::setCaseSet(mode_, qs(setId_));
  setBox_->setCurrentIndex(setBox_->findData(qs(setId_)));
  computeInfo();
  buildTree();
  startSet();
}

void CasePage::selectAcrossSets() {
  acrossSets_ = true;
  if (!library_.find(setId_)) {
    setId_ =
        library_.sets().empty() ? std::string() : library_.sets().front().id;
  }
  saved::setCaseSet(mode_, QString(kAcrossSetsId));
  setBox_->setCurrentIndex(setBox_->findData(QString(kAcrossSetsId)));
  computeInfo(true);
  buildTree();
  startSet();
}

void CasePage::showSet(const std::string &id) {
  if (id == setId_ || !library_.find(id)) {
    return;
  }
  filter_->clearSearch();
  setId_ = id;
  saved::shownSet(mode_).set(qs(setId_));
  computeInfo(true);
  buildTree();
}

void CasePage::showCase(const QString &setId, const QString &caseId) {
  if (acrossSets_ || ss(setId) != setId_) {
    selectSet(ss(setId));
  }
  selectInTree(ss(caseId));
}

void CasePage::releaseKeys() {
  filter_->release();
  if (QWidget *focus = QApplication::focusWidget();
      focus && isAncestorOf(focus)) {
    focus->clearFocus();
  }
}

void CasePage::startCaseFromList(const std::string &id) {
  releaseKeys();
  startCase(id);
}

void CasePage::computeInfo(bool cached) {
  if (cached) {
    if (auto it = infoCache_.find(setId_); it != infoCache_.end()) {
      info_ = it->second.info;
      families_ = it->second.families;
      steps_ = it->second.steps;
      return;
    }
  }
  info_.clear();
  families_ = {};
  steps_.clear();
  AlgSet *s = set();
  if (!s) {
    return;
  }
  if (s->families) {
    families_ = findFamilies(*s);
    if (adoptFamilyCards(families_, progress())) {
      saveProgress();
    }
  }
  for (const auto &g : s->groups) {
    for (const auto &c : g.cases) {
      CaseAnalysis a = analyzeCase(*s, g, c);
      if (!s->reduction && !a.passing.empty()) {
        steps_[c.id] = 1;
      }
      info_[c.id] = CaseInfo{a.passing, a.display, a.mainAlg, std::move(a)};
    }
  }
  if (s->reduction) {
    steps_ = stepsLeft(*s);
  }
  infoCache_[setId_] = InfoCache{info_, families_, steps_};
}

void CasePage::buildTree() {
  QSignalBlocker block(tree_);
  std::string current;
  if (auto *item = tree_->currentItem()) {
    current = ss(item->data(0, kCaseIdRole).toString());
  }
  int scroll = tree_->verticalScrollBar()->value();
  QSet<QString> collapsed;
  for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
    if (!tree_->topLevelItem(g)->isExpanded()) {
      collapsed.insert(tree_->topLevelItem(g)->text(0));
    }
  }
  tree_->clear();
  AlgSet *s = set();
  if (!s) {
    updateSummary();
    return;
  }
  QStringList unticked = untickedCases(*s);
  CubeLook look;
  look.scheme = settings_.scheme;
  look.map = colourMapOf(settings_.neutrality.main);
  qreal dpr = devicePixelRatioF();
  QColor muted = palette().color(QPalette::PlaceholderText);
  int order = 0;
  for (const auto &g : s->groups) {
    auto *groupItem = new SortItem(tree_, QStringList{qs(g.name)});
    groupItem->setData(0, kOrderRole, order++);
    groupItem->setFlags(groupItem->flags() | Qt::ItemIsUserCheckable |
                        Qt::ItemIsAutoTristate);
    const StepSpec &spec = s->specFor(g);
    for (const auto &c : g.cases) {
      auto *item = new SortItem(groupItem, QStringList{qs(c.name)});
      item->setData(0, kCaseIdRole, qs(c.id));
      item->setData(0, kOrderRole, order++);
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      bool on = !unticked.contains(qs(c.id));
      item->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
      const CaseInfo *info = infoOf(c.id);
      FaceletCube masked =
          maskedFacelets(info ? info->state : c.state, spec.mask, spec.free);
      item->setIcon(
          0, cube_painter::caseIcon(cube_painter::cachedDiagram(
                 masked, spec.view, settings_.thumbnailSize, look, dpr)));
      QStringList algs;
      for (const auto &a : c.algs) {
        algs << qs(a);
      }
      item->setData(0, CaseFilter::kSearchRole, algs.join('\n'));
      if (eligible(c.id)) {
        item->setToolTip(0, qs(info->mainAlg));
      } else {
        item->setForeground(0, muted);
        item->setToolTip(0, tr("No alg solves this case yet, so it is "
                               "skipped. Add one in the Library."));
      }
      fillRow(item, c.id);
      if (c.id == current) {
        tree_->setCurrentItem(item);
      }
    }
    groupItem->setExpanded(!collapsed.contains(qs(g.name)));
  }
  fillGroupRows();
  applySort();
  filter_->apply();
  tree_->verticalScrollBar()->setValue(scroll);
  updateSummary();
}

void CasePage::syncRows() {
  QSignalBlocker block(tree_);
  QStringList unticked;
  if (const AlgSet *s = set()) {
    unticked = untickedCases(*s);
  }
  for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
    auto *group = tree_->topLevelItem(g);
    for (int i = 0; i < group->childCount(); ++i) {
      QTreeWidgetItem *item = group->child(i);
      QString id = item->data(0, kCaseIdRole).toString();
      bool on = !unticked.contains(id);
      if ((item->checkState(0) == Qt::Checked) != on) {
        item->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
      }
      fillRow(item, ss(id));
    }
  }
  fillGroupRows();
  applySort();
  filter_->apply();
  updateSummary();
}

void CasePage::ticksEdited() {
  if (ticksPending_) {
    return;
  }
  ticksPending_ = true;
  QTimer::singleShot(0, this, [this] {
    ticksPending_ = false;
    saveTicks();
    filter_->apply();
    updateSummary();
    ticksChanged();
  });
}

QTreeWidgetItem *CasePage::itemFor(const std::string &caseId) const {
  for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
    auto *group = tree_->topLevelItem(g);
    for (int i = 0; i < group->childCount(); ++i) {
      if (ss(group->child(i)->data(0, kCaseIdRole).toString()) == caseId) {
        return group->child(i);
      }
    }
  }
  return nullptr;
}

void CasePage::selectInTree(const std::string &caseId) {
  if (auto *item = itemFor(caseId)) {
    QSignalBlocker block(tree_);
    tree_->setCurrentItem(item);
    tree_->scrollToItem(item);
  }
}

void CasePage::clearCaseSelection() {
  QSignalBlocker block(tree_);
  tree_->clearSelection();
  tree_->setCurrentItem(nullptr);
}

void CasePage::updateRow(const std::string &caseId) {
  if (auto *item = itemFor(caseId)) {
    QSignalBlocker block(tree_);
    fillRow(item, caseId);
    if (QTreeWidgetItem *group = item->parent()) {
      std::vector<std::string> ids;
      for (int i = 0; i < group->childCount(); ++i) {
        ids.push_back(ss(group->child(i)->data(0, kCaseIdRole).toString()));
      }
      fillGroupRow(group, ids);
    }
  }
  filter_->apply();
  updateSummary();
}

void CasePage::updateAllRows() {
  QSignalBlocker block(tree_);
  for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
    auto *group = tree_->topLevelItem(g);
    for (int i = 0; i < group->childCount(); ++i) {
      fillRow(group->child(i),
              ss(group->child(i)->data(0, kCaseIdRole).toString()));
    }
  }
  fillGroupRows();
  applySort();
  filter_->apply();
  updateSummary();
}

void CasePage::saveTicks() {
  QStringList ids;
  for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
    auto *group = tree_->topLevelItem(g);
    for (int i = 0; i < group->childCount(); ++i) {
      if (group->child(i)->checkState(0) != Qt::Checked) {
        ids << group->child(i)->data(0, kCaseIdRole).toString();
      }
    }
  }
  setUntickedCases(setId_, ids);
}

void CasePage::showCaseMenu(const QPoint &pos) {
  QMenu menu(this);
  if (fillCaseMenu(menu, tree_->itemAt(pos))) {
    menu.exec(tree_->viewport()->mapToGlobal(pos));
  }
}

void CasePage::fillModeMenu(QMenu &menu) {
  QTreeWidgetItem *item = tree_->currentItem();
  if (!item || !item->isSelected()) {
    const auto selected = tree_->selectedItems();
    item = selected.isEmpty() ? nullptr : selected.front();
  }
  if (!fillCaseMenu(menu, item)) {
    menu.addAction(tr("Select a case in the list"))->setEnabled(false);
  }
  // What the list offers on its buttons and header, so that the command
  // list finds it too.
  if (!set()) {
    return;
  }
  menu.addSeparator();
  menu.addAction(panel_->tickAllText(), this,
                 [this] { panel_->tickShown(true); });
  menu.addAction(panel_->tickNoneText(), this,
                 [this] { panel_->tickShown(false); });
  panel_->fillColumnMenu(*menu.addMenu(tr("Columns")));
  extendModeMenu(menu);
}

bool CasePage::fillCaseMenu(QMenu &menu, QTreeWidgetItem *item) {
  if (!item || !set()) {
    return false;
  }
  // The cases of a group's row, or of every selected row when the row
  // right-clicked is one of them.
  std::vector<std::string> ids;
  auto add = [&ids](const QTreeWidgetItem *it) {
    if (it->isHidden()) {
      return;
    }
    if (it->data(0, kCaseIdRole).isNull()) {
      for (int i = 0; i < it->childCount(); ++i) {
        if (!it->child(i)->isHidden()) {
          std::string id = ss(it->child(i)->data(0, kCaseIdRole).toString());
          if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
            ids.push_back(id);
          }
        }
      }
      return;
    }
    std::string id = ss(it->data(0, kCaseIdRole).toString());
    if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
      ids.push_back(id);
    }
  };
  if (item->isSelected() && tree_->selectedItems().size() > 1) {
    for (const QTreeWidgetItem *it : tree_->selectedItems()) {
      add(it);
    }
  } else {
    add(item);
  }
  if (ids.empty()) {
    return false;
  }
  if (ids.size() > 1) {
    menu.addSection(tr("%1 cases").arg(ids.size()));
    extendCaseMenu(menu, ids);
    return menu.actions().size() > 1; // more than the section
  }
  std::string id = ids.front();
  const AlgCase *c = set()->findCase(id);
  if (!c) {
    return false;
  }
  QAction *now = menu.addAction(startCaseLabel(id), this,
                                [this, id] { startCaseFromList(id); });
  now->setEnabled(eligible(id));
  menu.addSeparator();
  extendCaseMenu(menu, ids);
  menu.addSeparator();
  menu.addAction(tr("Edit algs in Library…"), this,
                 [this, id] { emit editCaseRequested(qs(setId_), qs(id)); });
  QAction *copy = menu.addAction(tr("Copy main alg"), this, [this, id] {
    const AlgSet *s = set();
    const AlgCase *ac = s ? s->findCase(id) : nullptr;
    if (ac && eligible(id)) {
      QApplication::clipboard()->setText(qs(infoOf(id)->mainAlg));
    }
  });
  copy->setEnabled(eligible(id));
  return true;
}

double CasePage::yawFor(const std::string &caseId, Mirror world) const {
  const AlgSet *s = set();
  const CaseGroup *g = s ? s->groupOf(caseId) : nullptr;
  DiagramView view = g ? s->specFor(*g).view : DiagramView::Iso3D;
  // Front-right, front-left, back-left, back-right, going round.
  static constexpr DiagramView kViews[] = {
      DiagramView::Iso3D, DiagramView::Iso3DFrontLeft,
      DiagramView::Iso3DBackLeft, DiagramView::Iso3DBackRight};
  int corner = 0;
  for (int i = 0; i < 4; ++i) {
    if (kViews[i] == view) {
      corner = i;
    }
  }
  bool left = corner == 1 || corner == 2, back = corner >= 2;
  // A left-right mirror is followed round to the other side. A front-back
  // one is not: a back slot is done with the cube held as usual (R U R'
  // becomes R' U' R), and seen from behind the R face would be on the left
  // and the U layer upside down from the case's diagram.
  left = left != world.leftRight;
  DiagramView seen = kViews[back ? (left ? 2 : 3) : (left ? 1 : 0)];
  return cube_painter::yawFor(seen, CubeLook{}.yaw);
}

Hold CasePage::pickHold() { return settings_.neutrality.pick(rng_); }

QString CasePage::algsHtml(const std::string &caseId, const CubieCube &state,
                           Mirror world) {
  const AlgSet *s = set();
  const AlgCase *c = s ? s->findCase(caseId) : nullptr;
  const CaseGroup *g = s ? s->groupOf(caseId) : nullptr;
  if (!c || !g) {
    return {};
  }
  if (s->reduction) {
    return chainHtml(*s, state, world);
  }
  CaseView view =
      viewOfCase(*s, *g, *c, state, world).value_or(CaseView{caseId, false, 0});
  return viewAlgsHtml(*s, view, state, world);
}

QPixmap CasePage::caseDiagram(const std::string &caseId, const CubieCube &state,
                              int size, const ColourMap &colours,
                              Mirror world) {
  const AlgSet *s = set();
  const CaseGroup *g = s ? s->groupOf(caseId) : nullptr;
  if (!g) {
    return {};
  }
  const StepSpec &spec = s->specFor(*g);
  CubeLook look;
  look.scheme = settings_.scheme;
  look.map = colours;
  return cube_painter::diagram(
      maskedFacelets(state, spec.mask, mirrorMask(spec.free, world)), spec.view,
      size, look, devicePixelRatioF());
}

void CasePage::saveProgress() {
  // Written in the background from a copy, so that saving never holds up
  // the next attempt.
  writeInBackground(
      library_.progressPath(setId_), library_.progressWriter(setId_), this,
      [this, id = qs(setId_)](const QString &error) {
        emit message(tr("Could not save progress for %1: %2").arg(id, error));
      });
}

} // namespace vcube
