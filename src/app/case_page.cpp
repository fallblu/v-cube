#include "app/case_page.hpp"

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
#include <QSettings>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace vcube {

namespace {

constexpr int kCaseIdRole = Qt::UserRole;
constexpr int kThumb = 38;

QComboBox *combo(QWidget *parent) {
  auto *c = new QComboBox(parent);
  c->setFocusPolicy(Qt::NoFocus);
  return c;
}

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
  return ms ? QString::number(*ms / 1000.0, 'f', 2) : QString("-");
}

CasePage::CasePage(AppSettings &settings, AlgLibrary &library, QString mode,
                   QWidget *parent)
    : ModePage(parent), settings_(settings), library_(library),
      mode_(std::move(mode)), rng_(makeRng()) {
  left_ = new QWidget(this);
  leftLayout_ = new QVBoxLayout(left_);
  leftLayout_->setContentsMargins(0, 0, 0, 0);
  form_ = new QFormLayout;
  setBox_ = combo(left_);
  form_->addRow(tr("Alg set:"), setBox_);
  leftLayout_->addLayout(form_);

  tree_ = new QTreeWidget(left_);
  tree_->setIconSize(QSize(kThumb, kThumb));
  tree_->setIndentation(14);
  tree_->setFocusPolicy(Qt::NoFocus);
  tree_->setContextMenuPolicy(Qt::CustomContextMenu);
  leftLayout_->addWidget(tree_, 1);

  tickRow_ = new QHBoxLayout;
  auto *allButton = new QPushButton(tr("Tick all"), left_);
  auto *noneButton = new QPushButton(tr("Tick none"), left_);
  for (auto *b : {allButton, noneButton}) {
    b->setFocusPolicy(Qt::NoFocus);
    tickRow_->addWidget(b);
  }
  tickRow_->addStretch();
  leftLayout_->addLayout(tickRow_);
  summary_ = new QLabel(left_);
  summary_->setWordWrap(true);
  leftLayout_->addWidget(summary_);

  connect(setBox_, &QComboBox::activated, this,
          [this](int i) { selectSet(ss(setBox_->itemData(i).toString())); });
  connect(tree_, &QTreeWidget::itemChanged, this,
          [this](QTreeWidgetItem *, int column) {
            if (column == 0) {
              ticksEdited();
            }
          });
  connect(tree_, &QTreeWidget::customContextMenuRequested, this,
          &CasePage::showCaseMenu);
  connect(allButton, &QPushButton::clicked, this, [this] {
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
      tree_->topLevelItem(i)->setCheckState(0, Qt::Checked);
    }
  });
  connect(noneButton, &QPushButton::clicked, this, [this] {
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
      tree_->topLevelItem(i)->setCheckState(0, Qt::Unchecked);
    }
  });
  neutrality_ = settings_.neutrality;
}

void CasePage::init() {
  QStringList headers = {tr("Case")};
  headers << columns();
  tree_->setColumnCount(static_cast<int>(headers.size()));
  tree_->setHeaderLabels(headers);
  tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  for (int c = 1; c < headers.size(); ++c) {
    tree_->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
  }
  applySettings();
  populateSets();
  QSettings q;
  QString last = q.value(mode_ + "/set", q.value("trainer/set")).toString();
  selectSet(last.isEmpty() && !library_.sets().empty()
                ? library_.sets().front().id
                : ss(last));
}

// --- set and cases ----------------------------------------------------------

AlgSet *CasePage::set() { return library_.find(setId_); }

SetProgress &CasePage::progress() { return library_.progress(setId_); }

const CaseProgress *CasePage::progressOf(const std::string &caseId) {
  const SetProgress &p = progress();
  auto it = p.find(caseId);
  return it == p.end() ? nullptr : &it->second;
}

const CasePage::CaseInfo *CasePage::infoOf(const std::string &caseId) const {
  auto it = info_.find(caseId);
  return it == info_.end() ? nullptr : &it->second;
}

bool CasePage::eligible(const std::string &caseId) const {
  const CaseInfo *i = infoOf(caseId);
  return i && !i->passing.empty();
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
    n += i.passing.empty() ? 1 : 0;
  }
  return n;
}

void CasePage::applySettings() {
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

void CasePage::setFocusMode(bool on) { left_->setVisible(!on); }

void CasePage::populateSets() {
  QSignalBlocker block(setBox_);
  setBox_->clear();
  for (const auto &s : library_.sets()) {
    setBox_->addItem(QString("%1 (%2)").arg(qs(s.name)).arg(s.caseCount()),
                     qs(s.id));
  }
}

void CasePage::reloadSets() {
  if (!isVisible()) {
    reloadPending_ = true;
    return;
  }
  populateSets();
  std::string id = setId_;
  if (!library_.find(id)) {
    id = library_.sets().empty() ? std::string() : library_.sets().front().id;
  }
  if (id == setId_ && set()) {
    setBox_->setCurrentIndex(setBox_->findData(qs(id)));
    computeInfo();
    buildTree();
    setReloaded();
  } else {
    selectSet(id);
  }
}

void CasePage::selectSet(const std::string &id) {
  setId_ = id;
  if (!library_.find(id) && !library_.sets().empty()) {
    setId_ = library_.sets().front().id;
  }
  QSettings().setValue(mode_ + "/set", qs(setId_));
  setBox_->setCurrentIndex(setBox_->findData(qs(setId_)));
  computeInfo();
  buildTree();
  startSet();
}

void CasePage::computeInfo() {
  info_.clear();
  AlgSet *s = set();
  if (!s) {
    return;
  }
  for (const auto &g : s->groups) {
    for (const auto &c : g.cases) {
      CaseAnalysis a = analyzeCase(*s, g, c);
      info_[c.id] = CaseInfo{a.passing, a.display, a.mainAlg};
    }
  }
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
  std::optional<QStringList> ticked = tickedCases(setId_);
  CubeLook look;
  look.scheme = settings_.scheme;
  look.map = colourMapOf(settings_.neutrality.main);
  qreal dpr = devicePixelRatioF();
  QColor muted = palette().color(QPalette::PlaceholderText);
  for (const auto &g : s->groups) {
    auto *groupItem = new QTreeWidgetItem(tree_, {qs(g.name)});
    groupItem->setFlags(groupItem->flags() | Qt::ItemIsUserCheckable |
                        Qt::ItemIsAutoTristate);
    const StepSpec &spec = s->specFor(g);
    for (const auto &c : g.cases) {
      auto *item = new QTreeWidgetItem(groupItem, {qs(c.name)});
      item->setData(0, kCaseIdRole, qs(c.id));
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      bool on = !ticked || ticked->contains(qs(c.id));
      item->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
      const CaseInfo *info = infoOf(c.id);
      FaceletCube masked =
          maskedFacelets(info ? info->state : c.state, spec.mask, spec.free);
      item->setIcon(0, QIcon(cube_painter::cachedDiagram(masked, spec.view,
                                                         kThumb, look, dpr)));
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
  tree_->verticalScrollBar()->setValue(scroll);
  updateSummary();
}

void CasePage::syncRows() {
  QSignalBlocker block(tree_);
  std::optional<QStringList> ticked = tickedCases(setId_);
  for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
    auto *group = tree_->topLevelItem(g);
    for (int i = 0; i < group->childCount(); ++i) {
      QTreeWidgetItem *item = group->child(i);
      QString id = item->data(0, kCaseIdRole).toString();
      bool on = !ticked || ticked->contains(id);
      if ((item->checkState(0) == Qt::Checked) != on) {
        item->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
      }
      fillRow(item, ss(id));
    }
  }
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

void CasePage::updateRow(const std::string &caseId) {
  if (auto *item = itemFor(caseId)) {
    QSignalBlocker block(tree_);
    fillRow(item, caseId);
  }
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
  updateSummary();
}

void CasePage::saveTicks() {
  QStringList ids;
  for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
    auto *group = tree_->topLevelItem(g);
    for (int i = 0; i < group->childCount(); ++i) {
      if (group->child(i)->checkState(0) == Qt::Checked) {
        ids << group->child(i)->data(0, kCaseIdRole).toString();
      }
    }
  }
  setTickedCases(setId_, ids);
}

void CasePage::showCaseMenu(const QPoint &pos) {
  QTreeWidgetItem *item = tree_->itemAt(pos);
  if (!item || item->data(0, kCaseIdRole).isNull()) {
    return;
  }
  std::string id = ss(item->data(0, kCaseIdRole).toString());
  const AlgCase *c = set() ? set()->findCase(id) : nullptr;
  if (!c) {
    return;
  }
  QMenu menu(this);
  QAction *now =
      menu.addAction(startCaseLabel(id), this, [this, id] { startCase(id); });
  now->setEnabled(eligible(id));
  menu.addSeparator();
  extendCaseMenu(menu, id);
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
  menu.exec(tree_->viewport()->mapToGlobal(pos));
}

Hold CasePage::pickHold() { return settings_.neutrality.pick(rng_); }

// --- reveal ----------------------------------------------------------------

namespace {

// The rich text of CasePage::algsHtml() and caseAlgsHtml(): `passing` are
// the indices of the passing algs and `mainAlg` the main alg as done from
// the display orientation.
QString algsHtmlFor(const AlgSet &set, const CaseGroup &group, const AlgCase &c,
                    const std::vector<std::size_t> &passing,
                    const std::string &mainAlg, const CubieCube &state) {
  const PieceMask &free = set.specFor(group).free;
  QString html =
      QString("<p style='font-size:large'><b>%1</b> <span style='color:gray'>· "
              "%2</span></p>")
          .arg(qs(c.name).toHtmlEscaped(), qs(group.name).toHtmlEscaped());
  if (passing.empty()) {
    return html + "<p style='color:gray'>" +
           QObject::tr("No alg solves this case yet. Add one in the Library.")
               .toHtmlEscaped() +
           "</p>";
  }
  bool first = true;
  for (std::size_t i : passing) {
    const std::string &text = first ? mainAlg : c.algs[i];
    std::optional<AufFit> fit =
        fitAlg(state, parseAlgOrEmpty(text), free, set.aufBefore, set.aufAfter);
    QString line = qs(fit ? withAufs(text, *fit) : text).toHtmlEscaped();
    line = first ? "<b>" + line + "</b>"
                 : "<span style='color:gray'>" + line + "</span>";
    html += "<p>" + line + "</p>";
    first = false;
  }
  return html;
}

} // namespace

QString caseAlgsHtml(const AlgSet &set, const std::string &caseId,
                     const CubieCube &state) {
  const AlgCase *c = set.findCase(caseId);
  const CaseGroup *g = set.groupOf(caseId);
  if (!c || !g) {
    return {};
  }
  CaseAnalysis a = analyzeCase(set, *g, *c);
  return algsHtmlFor(set, *g, *c, a.passing, a.mainAlg, state);
}

QString CasePage::algsHtml(const std::string &caseId, const CubieCube &state,
                           std::optional<Hold> hold) {
  const AlgSet *s = set();
  const AlgCase *c = s ? s->findCase(caseId) : nullptr;
  const CaseGroup *g = s ? s->groupOf(caseId) : nullptr;
  const CaseInfo *info = infoOf(caseId);
  if (!c || !g || !info) {
    return {};
  }
  QString html = algsHtmlFor(*s, *g, *c, info->passing, info->mainAlg, state);
  if (hold && !info->passing.empty()) {
    html += "<p style='color:gray'>" +
            tr("Held with %1").arg(holdHtml(*hold, settings_.scheme)) + "</p>";
  }
  return html;
}

QPixmap CasePage::caseDiagram(const std::string &caseId, const CubieCube &state,
                              int size, const ColourMap &colours) {
  const AlgSet *s = set();
  const CaseGroup *g = s ? s->groupOf(caseId) : nullptr;
  if (!g) {
    return {};
  }
  const StepSpec &spec = s->specFor(*g);
  CubeLook look;
  look.scheme = settings_.scheme;
  look.map = colours;
  return cube_painter::diagram(maskedFacelets(state, spec.mask, spec.free),
                               spec.view, size, look, devicePixelRatioF());
}

void CasePage::saveProgress() {
  if (!library_.saveProgress(setId_)) {
    emit message(tr("Could not save progress for %1").arg(qs(setId_)));
  }
}

} // namespace vcube
