#include "app/case_page.hpp"

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
#include <QSettings>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>

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
  // Truncated to hundredths, as the timer shows times.
  return ms ? QString::number(std::floor(*ms / 10.0) / 100.0, 'f', 2)
            : QString("-");
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
  filter_ = new CaseFilter(tree_, left_);
  leftLayout_->addWidget(filter_);
  leftLayout_->addWidget(tree_, 1);

  tickRow_ = new QHBoxLayout;
  tickAllButton_ = new QPushButton(left_);
  tickNoneButton_ = new QPushButton(left_);
  for (auto *b : {tickAllButton_, tickNoneButton_}) {
    b->setFocusPolicy(Qt::NoFocus);
    tickRow_->addWidget(b);
  }
  filterChanged();
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
  connect(tree_, &QTreeWidget::itemDoubleClicked, this,
          [this](QTreeWidgetItem *item) {
            QVariant id = item->data(0, kCaseIdRole);
            if (!id.isNull() && eligible(ss(id.toString()))) {
              startCase(ss(id.toString()));
            }
          });
  connect(tickAllButton_, &QPushButton::clicked, this,
          [this] { tickShown(true); });
  connect(tickNoneButton_, &QPushButton::clicked, this,
          [this] { tickShown(false); });
  connect(filter_, &CaseFilter::changed, this, &CasePage::filterChanged);
  neutrality_ = settings_.neutrality;
}

void CasePage::tickShown(bool on) {
  Qt::CheckState state = on ? Qt::Checked : Qt::Unchecked;
  for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
    QTreeWidgetItem *group = tree_->topLevelItem(g);
    if (!filter_->isActive()) {
      group->setCheckState(0, state); // its cases follow
      continue;
    }
    for (int i = 0; i < group->childCount(); ++i) {
      if (!group->child(i)->isHidden()) {
        group->child(i)->setCheckState(0, state);
      }
    }
  }
}

void CasePage::filterChanged() {
  bool filtered = filter_->isActive();
  tickAllButton_->setText(filtered ? tr("Tick shown") : tr("Tick all"));
  tickNoneButton_->setText(filtered ? tr("Untick shown") : tr("Tick none"));
  tickAllButton_->setToolTip(filtered ? tr("Tick the cases shown")
                                      : tr("Tick every case"));
  tickNoneButton_->setToolTip(filtered ? tr("Untick the cases shown")
                                       : tr("Untick every case"));
}

void CasePage::init() {
  auto byId = [this](std::function<bool(const std::string &)> test) {
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

std::string CasePage::cardId(const std::string &caseId) const {
  return byFamily() ? families_.representative(caseId) : caseId;
}

const CaseProgress *CasePage::cardOf(const std::string &caseId) {
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
  for (const auto &g : s->groups) {
    auto *groupItem = new QTreeWidgetItem(tree_, {qs(g.name)});
    groupItem->setFlags(groupItem->flags() | Qt::ItemIsUserCheckable |
                        Qt::ItemIsAutoTristate);
    const StepSpec &spec = s->specFor(g);
    for (const auto &c : g.cases) {
      auto *item = new QTreeWidgetItem(groupItem, {qs(c.name)});
      item->setData(0, kCaseIdRole, qs(c.id));
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      bool on = !unticked.contains(qs(c.id));
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

void CasePage::updateRow(const std::string &caseId) {
  if (auto *item = itemFor(caseId)) {
    QSignalBlocker block(tree_);
    fillRow(item, caseId);
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

double CasePage::yawFor(const std::string &caseId, Mirror world) {
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
  left = left != world.leftRight;
  back = back != world.frontBack;
  DiagramView seen = kViews[back ? (left ? 2 : 3) : (left ? 1 : 0)];
  return cube_painter::yawFor(seen, CubeLook{}.yaw);
}

Hold CasePage::pickHold() { return settings_.neutrality.pick(rng_); }

// --- reveal ----------------------------------------------------------------

namespace {

QString caseHeaderHtml(const AlgCase &c, const CaseGroup &group) {
  return QString("<p style='font-size:large'><b>%1</b> <span "
                 "style='color:gray'>· %2</span></p>")
      .arg(qs(c.name).toHtmlEscaped(), qs(group.name).toHtmlEscaped());
}

QString greyHtml(const QString &text) {
  return "<span style='color:gray'>" + text + "</span>";
}

// " → Sune" after a step that stops at that case.
QString reachedHtml(const AlgSet &set, const std::string &caseId) {
  const AlgCase *next = set.findCase(caseId);
  return next ? " " + greyHtml("→ " + qs(next->name).toHtmlEscaped())
              : QString();
}

} // namespace

QString viewAlgsHtml(const AlgSet &set, const CaseView &view,
                     const CubieCube &state, Mirror world) {
  const AlgCase *c = set.findCase(view.caseId);
  const CaseGroup *g = set.groupOf(view.caseId);
  if (!c || !g) {
    return {};
  }
  QString html = caseHeaderHtml(*c, *g);
  std::vector<ViewAlg> algs =
      algsFor(set, *g, *c, analyzeCase(set, *g, *c), view, state, world);
  if (algs.empty()) {
    return html + "<p style='color:gray'>" +
           QObject::tr("No alg solves this case yet. Add one in the Library.")
               .toHtmlEscaped() +
           "</p>";
  }
  bool first = true;
  for (const ViewAlg &alg : algs) {
    QString line = qs(alg.text).toHtmlEscaped();
    line = first ? "<b>" + line + "</b>" : greyHtml(line);
    html += "<p>" + line + reachedHtml(set, alg.reducesTo) + "</p>";
    first = false;
  }
  return html;
}

QString chainHtml(const AlgSet &set, const CubieCube &state, Mirror world) {
  Chain chain = solveChain(set, state, world);
  if (chain.steps.empty()) {
    return {};
  }
  if (chain.steps.size() == 1) {
    return viewAlgsHtml(set, chain.steps.front().view, state, world);
  }
  const std::string &firstId = chain.steps.front().view.caseId;
  const AlgCase *c = set.findCase(firstId);
  const CaseGroup *g = set.groupOf(firstId);
  if (!c || !g) {
    return {};
  }
  QStringList lines;
  for (std::size_t i = 0; i < chain.steps.size(); ++i) {
    QString line = "<b>" + qs(chain.steps[i].alg).toHtmlEscaped() + "</b>";
    if (i + 1 < chain.steps.size()) {
      line += reachedHtml(set, chain.steps[i + 1].view.caseId);
    }
    lines << line;
  }
  QString html = caseHeaderHtml(*c, *g) + "<p>" + lines.join("<br>") + "</p>";
  if (!chain.complete) {
    html += "<p style='color:gray'>" +
            QObject::tr("The set's algs don't reach the end from there.")
                .toHtmlEscaped() +
            "</p>";
  }
  return html;
}

QString caseAlgsHtml(const AlgSet &set, const std::string &caseId,
                     const CubieCube &state) {
  // The scramble may show the case in one of the set's mirror worlds.
  std::optional<WorldView> seen = worldOf(set, state);
  if (!seen) {
    const AlgCase *c = set.findCase(caseId);
    const CaseGroup *g = set.groupOf(caseId);
    seen = WorldView{Mirror{}, CaseView{caseId, false, 0}};
    if (c && g) {
      if (auto view = viewOfCase(set, *g, *c, state, Mirror{})) {
        seen->view = *view;
      }
    }
  }
  return set.reduction ? chainHtml(set, state, seen->world)
                       : viewAlgsHtml(set, seen->view, state, seen->world);
}

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
  writeInBackground(library_.progressPath(setId_),
                    library_.progressWriter(setId_), this,
                    [this, id = qs(setId_)](const QString &error) {
                      emit message(tr("Could not save progress for %1: %2")
                                       .arg(id, error));
                    });
}

} // namespace vcube
