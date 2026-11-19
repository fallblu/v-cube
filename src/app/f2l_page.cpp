#include "app/f2l_page.hpp"

#include "app/cube_painter.hpp"
#include "app/ui_state.hpp"
#include "app/ui_text.hpp"

#include <QComboBox>
#include <QFutureWatcher>
#include <QMenu>
#include <QSignalBlocker>
#include <QToolButton>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace vcube {

namespace {

const char *const kWhole = "whole";

QString costText(double cost) { return QString::number(cost, 'f', 1); }

// The stickers of a slot's corner and edge, wherever they are on a cube
// held any way.
std::vector<int> pairFacelets(const FaceletCube &cube, Slot slot) {
  std::vector<int> out;
  auto colours = [](auto facelets) {
    std::vector<int> c;
    for (std::uint8_t f : facelets) {
      c.push_back(f / 9);
    }
    std::sort(c.begin(), c.end());
    return c;
  };
  auto shown = [&cube](auto facelets) {
    std::vector<int> c;
    for (std::uint8_t f : facelets) {
      c.push_back(cube[f]);
    }
    std::sort(c.begin(), c.end());
    return c;
  };
  std::vector<int> corner = colours(kCornerFacelets[slotCorner(slot)]);
  for (const auto &facelets : kCornerFacelets) {
    if (shown(facelets) == corner) {
      out.insert(out.end(), facelets.begin(), facelets.end());
    }
  }
  std::vector<int> edge = colours(kEdgeFacelets[slotEdge(slot)]);
  for (const auto &facelets : kEdgeFacelets) {
    if (shown(facelets) == edge) {
      out.insert(out.end(), facelets.begin(), facelets.end());
    }
  }
  return out;
}

const PairRank *rankOf(const std::vector<PairRank> &ranks, Slot slot) {
  auto it = std::find_if(ranks.begin(), ranks.end(), [slot](const PairRank &r) {
    return r.pair.slot == slot;
  });
  return it == ranks.end() ? nullptr : &*it;
}

// The corner a case is best seen from: that of a back slot it uses.
DiagramView viewOf(PairCase c) {
  for (std::optional<Slot> s : {c.cornerSlot(), c.edgeSlot()}) {
    if (s == Slot::BL) {
      return DiagramView::Iso3DBackLeft;
    }
    if (s == Slot::BR) {
      return DiagramView::Iso3DBackRight;
    }
  }
  for (std::optional<Slot> s : {c.cornerSlot(), c.edgeSlot()}) {
    if (s == Slot::FL) {
      return DiagramView::Iso3DFrontLeft;
    }
  }
  return DiagramView::Iso3D;
}

// A cost over the best one, coloured by how much it is.
QString overText(double over) {
  return over < 0.05
             ? coloured(kPassColour, QObject::tr("as good as the best"))
             : coloured(over >= 2.0 ? QColor(kFailColour) : QColor(kWarnColour),
                        QObject::tr("+%1 over the best").arg(costText(over)));
}

} // namespace

F2LPage::F2LPage(AppSettings &settings, AlgLibrary &library, AlgLabStore &lab,
                 const QString &dataDir, QWidget *parent)
    : PracticePage(settings, dataDir, "f2l.json", parent), library_(library),
      lab_(lab) {
  loadChoices();
  build();
}

F2LPage::~F2LPage() {
  if (stop_) {
    stop_->store(true);
  }
}

// --- options -----------------------------------------------------------------

void F2LPage::addOptions(OptionBar *bar) {
  methodBox_ = new QComboBox(bar);
  methodBox_->setFocusPolicy(Qt::NoFocus);
  for (int m = 0; m < kF2LMethods; ++m) {
    auto method = static_cast<F2LMethod>(m);
    methodBox_->addItem(qs(methodName(method)), qs(methodId(method)));
  }
  bar->addPicker(tr("F2L:"), methodBox_);

  leaveButton_ = new QToolButton(bar);
  leaveButton_->setFocusPolicy(Qt::NoFocus);
  leaveButton_->setPopupMode(QToolButton::InstantPopup);
  auto *leaveMenu = new QMenu(leaveButton_);
  for (Slot s : kF2LSlots) {
    QAction *a = leaveMenu->addAction(slotText(s));
    a->setCheckable(true);
    a->setData(static_cast<int>(s));
    connect(a, &QAction::toggled, this, [this, s, a](bool on) {
      config_.lsSlots[static_cast<std::size_t>(s)] = on;
      if (std::none_of(config_.lsSlots.begin(), config_.lsSlots.end(),
                       [](bool b) { return b; })) {
        // One slot at least can be left.
        config_.lsSlots[static_cast<std::size_t>(s)] = true;
        QSignalBlocker block(a);
        a->setChecked(true);
        return;
      }
      configChanged();
    });
  }
  leaveButton_->setMenu(leaveMenu);
  leaveItem_ = bar->addPicker(tr("Last slot:"), leaveButton_);

  movesButton_ = new QToolButton(bar);
  movesButton_->setFocusPolicy(Qt::NoFocus);
  movesButton_->setPopupMode(QToolButton::InstantPopup);
  auto *movesMenu = new QMenu(movesButton_);
  auto addMove = [&](const QString &text, const char *name,
                     bool F2LConfig::*f) {
    QAction *a = movesMenu->addAction(text);
    a->setCheckable(true);
    a->setObjectName(name);
    connect(a, &QAction::toggled, this, [this, f](bool on) {
      config_.*f = on;
      configChanged();
    });
  };
  addMove(tr("D turns"), "d", &F2LConfig::dMoves);
  addMove(tr("F2 and B2"), "halfFB", &F2LConfig::halfFB);
  addMove(tr("Wide turns (r, l, u, d)"), "wide", &F2LConfig::wide);
  movesButton_->setMenu(movesMenu);
  bar->addPicker(tr("Solutions use:"), movesButton_);

  connect(methodBox_, &QComboBox::activated, this, [this] {
    if (auto m =
            methodFromId(methodBox_->currentData().toString().toStdString())) {
      applyMethod(config_, *m);
      configChanged();
    }
  });
}

void F2LPage::loadChoices() {
  if (auto m = methodFromId(saved::practice("f2l", "method", "zz-1")
                                .get()
                                .toString()
                                .toStdString())) {
    applyMethod(config_, *m);
  }
  QStringList leave = saved::practice("f2l", "leave", QStringList{"FR", "FL"})
                          .get()
                          .toStringList();
  bool any = false;
  for (Slot s : kF2LSlots) {
    bool on = leave.contains(slotText(s));
    config_.lsSlots[static_cast<std::size_t>(s)] = on;
    any = any || on;
  }
  if (!any) {
    config_.lsSlots = F2LConfig().lsSlots;
  }
  config_.dMoves = saved::practice("f2l", "dMoves", true).get().toBool();
  config_.halfFB = saved::practice("f2l", "halfFB", true).get().toBool();
  config_.wide = saved::practice("f2l", "wide", true).get().toBool();
}

void F2LPage::saveChoices() const {
  saved::practice("f2l", "method").set(qs(methodId(methodOf(config_))));
  QStringList leave;
  for (Slot s : kF2LSlots) {
    if (config_.lsSlots[static_cast<std::size_t>(s)]) {
      leave << slotText(s);
    }
  }
  saved::practice("f2l", "leave").set(leave);
  saved::practice("f2l", "dMoves").set(config_.dMoves);
  saved::practice("f2l", "halfFB").set(config_.halfFB);
  saved::practice("f2l", "wide").set(config_.wide);
}

void F2LPage::showOptions() {
  {
    QSignalBlocker block(methodBox_);
    methodBox_->setCurrentIndex(
        methodBox_->findData(qs(methodId(methodOf(config_)))));
  }
  QStringList leave;
  for (QAction *a : leaveButton_->menu()->actions()) {
    QSignalBlocker block(a);
    bool on = config_.lsSlots[static_cast<std::size_t>(a->data().toInt())];
    a->setChecked(on);
    if (on) {
      leave << a->text();
    }
  }
  leaveButton_->setText(leave.join(" / "));
  leaveItem_->setVisible(config_.lastSlot);
  for (QAction *a : movesButton_->menu()->actions()) {
    QSignalBlocker block(a);
    if (a->objectName() == "d") {
      a->setChecked(config_.dMoves);
    } else if (a->objectName() == "halfFB") {
      // Without EO every F and B turn is used.
      a->setChecked(config_.eo ? config_.halfFB : true);
      a->setEnabled(config_.eo);
    } else {
      a->setChecked(!config_.eo && config_.wide);
      a->setEnabled(!config_.eo);
    }
  }
  PairMoves moves = config_.moves();
  QString text = "R U L";
  text += moves.d ? " D" : "";
  text += moves.quarterFB ? " F B" : moves.halfFB ? " F2 B2" : "";
  text += moves.wide ? " r l u d" : "";
  movesButton_->setText(text);
}

void F2LPage::configChanged() {
  saveChoices();
  optionChanged();
}

void F2LPage::setConfig(const F2LConfig &config) {
  config_ = config;
  configChanged();
}

void F2LPage::reloadSets() {
  reference_.reset();
  const AlgSet *set = library_.find(saved::reviewPairSet().get().toStdString());
  if (set && isPairSet(*set)) {
    reference_ = std::make_shared<const AlgSet>(*set);
  }
}

void F2LPage::activated() {
  reloadSets();
  PracticePage::activated();
}

// --- states ------------------------------------------------------------------

std::vector<std::string> F2LPage::cardPool() const {
  return pairCardPool(config_.eo);
}

std::optional<PracticePage::Setup> F2LPage::setUp(bool whole,
                                                  const std::string &card) {
  if (stop_) {
    stop_->store(true);
  }
  ++requests_; // what is still being worked out is for another state
  pair_.reset();
  target_.reset();
  if (whole) {
    start_ = randomFullState(config_, rng());
  } else {
    // The pair anywhere in a solve: with other slots done or not.
    std::optional<PairSetup> setup =
        randomPairSetup(config_, 0, caseFromKey(card), rng());
    if (!setup) {
      return std::nullopt;
    }
    start_ = setup->state;
    target_ = setup->target;
    pcase_ = setup->pcase;
  }
  restarted();
  Setup s;
  s.state = start_;
  s.hold = settings().neutrality.pick(rng());
  s.colours = s.hold;
  s.cube = start_.toFacelets();
  s.group =
      tab() == Tab::Timed ? std::string(methodId(methodOf(config_))) : kWhole;
  if (target_) {
    requestPair();
  }
  return s;
}

void F2LPage::restarted() {
  hintMoves_ = 0;
  segments_.clear();
  ranks_.clear();
  wholeAsked_ = false;
  wholeReady_ = false;
}

bool F2LPage::initiallySolvedKept(const CubieCube &c) const {
  return std::all_of(kF2LSlots.begin(), kF2LSlots.end(), [&](Slot s) {
    return !slotSolved(start_, s) || slotSolved(c, s);
  });
}

bool F2LPage::turned() {
  auto c = centredCubie(cube());
  if (!c || !f2lBase(*c, config_)) {
    return false;
  }
  return target_ ? slotSolved(*c, *target_) && initiallySolvedKept(*c)
                 : f2lDone(*c, config_);
}

void F2LPage::cubeChanged() {
  if (!target_) {
    cubeView()->clearMarks();
    return;
  }
  StickerMarks marks;
  marks.rings = pairFacelets(cube(), *target_);
  cubeView()->setMarks(marks);
}

// --- analysis ----------------------------------------------------------------

AnalysisOptions F2LPage::analysisOptions() const {
  AnalysisOptions o;
  o.config = config_;
  o.profile =
      lab_.profile(saved::lab("scoreProfile").get().toString().toStdString());
  o.reference = reference_;
  return o;
}

double F2LPage::costOf(const Alg &moves) const {
  return solutionCost(moves, analysisOptions());
}

bool F2LPage::analysisReady() const {
  return target_ ? pair_.has_value() : !wholeAsked_ || wholeReady_;
}

bool F2LPage::waitsForAnalysis() const {
  // A pair's verdict (and its card's grade) needs the best solution.
  return target_ && !pair_;
}

void F2LPage::requestPair() {
  stop_ = std::make_shared<std::atomic<bool>>(false);
  auto *watcher = new QFutureWatcher<Analysis>(this);
  connect(watcher, &QFutureWatcher<Analysis>::finished, this, [this, watcher] {
    arrived(watcher->result());
    watcher->deleteLater();
  });
  watcher->setFuture(
      QtConcurrent::run([request = requests_, start = start_, target = *target_,
                         options = analysisOptions(), stop = stop_] {
        Analysis a;
        a.request = request;
        a.pair = analysePair(start, target, options, stop.get());
        return a;
      }));
}

void F2LPage::requestWhole() {
  if (stop_) {
    stop_->store(true);
  }
  stop_ = std::make_shared<std::atomic<bool>>(false);
  wholeAsked_ = true;
  wholeReady_ = false;
  // Each pair from where it was begun; with none solved, the start alone,
  // for the cheapest way through.
  std::vector<CubieCube> befores;
  for (const F2LSegment &s : segments_) {
    befores.push_back(s.before);
  }
  if (befores.empty()) {
    befores.push_back(start_);
  }
  auto *watcher = new QFutureWatcher<Analysis>(this);
  connect(watcher, &QFutureWatcher<Analysis>::finished, this, [this, watcher] {
    arrived(watcher->result());
    watcher->deleteLater();
  });
  watcher->setFuture(
      QtConcurrent::run([request = ++requests_, befores,
                         options = analysisOptions(), stop = stop_] {
        Analysis a;
        a.request = request;
        for (const CubieCube &before : befores) {
          a.segments.push_back(rankPairs(before, options, stop.get()));
        }
        return a;
      }));
}

void F2LPage::arrived(const Analysis &a) {
  if (a.request != requests_) {
    return; // for a state that is gone
  }
  if (a.pair) {
    pair_ = a.pair;
  } else {
    ranks_ = a.segments;
    wholeReady_ = true;
  }
  analysisArrived(item().id);
}

std::vector<RankedSolution> F2LPage::shownSolutions() const {
  return pair_ ? pair_->solutions : std::vector<RankedSolution>{};
}

// --- what is said ------------------------------------------------------------

QString F2LPage::cardName(const std::string &card) const {
  std::optional<PairCase> c = caseFromKey(card);
  return c ? qs(caseName(*c)).toHtmlEscaped() : qs(card);
}

QString F2LPage::task() const {
  return target_ ? tr("Solve the ringed pair")
                 : tr("Solve the %1").arg(qs(methodName(methodOf(config_))));
}

QString F2LPage::taskNote() const {
  return target_ ? tr("%1 slot").arg(slotText(*target_)) : QString();
}

QString F2LPage::hint() {
  if (!pair_ || !pair_->best()) {
    return {};
  }
  // A move more of the best solution each time.
  const Alg &best = pair_->best()->alg;
  hintMoves_ = std::min(hintMoves_ + 1, static_cast<int>(best.size()));
  Alg shown(best.begin(), best.begin() + hintMoves_);
  return hintMoves_ == static_cast<int>(best.size())
             ? tr("Hint: the best is %1").arg(qs(toString(shown)))
             : tr("Hint: the best starts %1").arg(qs(toString(shown)));
}

Alg F2LPage::demoAlg() {
  return pair_ && pair_->best() ? pair_->best()->alg : Alg{};
}

QString F2LPage::demoText() {
  if (!pair_) {
    return tr("Working out the best solutions…");
  }
  if (!pair_->best()) {
    return tr("No solution was found with these moves.");
  }
  return tr("%1; best from here: %2. Enter shows it; then do it.")
      .arg(taskNote(),
           "<b>" + qs(pair_->best()->text).toHtmlEscaped() + "</b>");
}

PracticePage::Outcome F2LPage::judge() {
  Outcome o;
  if (target_) {
    // One pair.
    if (!pair_ || !pair_->best()) {
      o.verdict = tr("No solution was found with these moves.");
      o.good = !item().gaveUp;
      return o;
    }
    const RankedSolution &best = *pair_->best();
    o.best = best.alg;
    o.bestText =
        qs(best.text).toHtmlEscaped() +
        QString(" <span style='font-weight:normal'>(%1, %2)</span>")
            .arg(movesText(best.htm), tr("cost %1").arg(costText(best.cost)));
    o.bestMoves = best.htm;
    if (item().gaveUp) {
      o.verdict = coloured(kFailColour, tr("Gave up"));
      return o;
    }
    const double cost = costOf(item().moves);
    const double over = std::max(0.0, cost - best.cost);
    o.moves = htmCount(simplify(item().moves));
    o.extra = over;
    o.good = over <= kGoodOver;
    o.verdict = tr("%1, cost %2").arg(movesText(o.moves), costText(cost)) +
                " · " + overText(over) +
                (item().hint ? " · " + tr("with a hint") : QString());
    return o;
  }
  // The whole F2L.
  const bool solved = !item().moves.empty();
  if (!wholeAsked_) {
    if (solved) {
      segments_ = f2lSegments(start_, item().moves, config_);
    }
    requestWhole();
  }
  if (wholeReady_ && !ranks_.empty() && !ranks_.front().empty()) {
    // The cheapest way found from the start.
    const PairRank &plan = ranks_.front().front();
    QStringList order;
    for (Slot s : plan.order) {
      order << slotText(s);
    }
    o.bestMoves = plan.moves;
    o.bestText =
        order.join(" → ") +
        QString(" <span style='font-weight:normal'>(%1, %2)</span>")
            .arg(movesText(plan.moves),
                 tr("cost %1").arg(costText(plan.total - plan.hidden)));
  }
  if (!solved) {
    return o; // a physical cube: only the time, and the best way
  }
  o.moves = htmCount(simplify(item().moves));
  if (item().gaveUp) {
    o.verdict = coloured(kFailColour, tr("Not finished"));
    return o;
  }
  const double cost = costOf(item().moves);
  o.verdict = tr("F2L in %1, cost %2").arg(movesText(o.moves), costText(cost));
  if (wholeReady_ && !ranks_.empty() && !ranks_.front().empty()) {
    // Against the cheapest way found from the start: what the pairs
    // chosen and their solutions cost over it together.
    const PairRank &plan = ranks_.front().front();
    const double over = std::max(0.0, cost - (plan.total - plan.hidden));
    const double pairs =
        static_cast<double>(std::max(1, pairsLeft(start_, config_)));
    o.extra = over;
    o.good = over <= kGoodOver * pairs;
    o.verdict +=
        " · " +
        (over < 0.05
             ? coloured(kPassColour, tr("as good as the cheapest way found"))
             : coloured(
                   over >= 2.0 * pairs ? QColor(kFailColour)
                                       : QColor(kWarnColour),
                   tr("+%1 over the cheapest way found").arg(costText(over))));
  }
  return o;
}

QString F2LPage::solutionsHtml(const PairAnalysis &pair) const {
  const QString muted = mutedColour();
  if (pair.solutions.empty()) {
    return "<p>" + tr("No solution was found with these moves.") + "</p>";
  }
  QString html = "<table align='center' cellspacing='0' cellpadding='3'>";
  for (std::size_t i = 0; i < pair.solutions.size(); ++i) {
    const RankedSolution &s = pair.solutions[i];
    QString alg = playLink(s.alg, qs(s.text).toHtmlEscaped());
    QString note;
    if (s.fromSet()) {
      note = tr("your set: %1").arg(qs(s.setCase).toHtmlEscaped());
    }
    html += QString("<tr><td>%1</td><td align='right'>%2</td>"
                    "<td align='right'>%3</td><td style='color:%4'>%5</td>"
                    "</tr>")
                .arg(i == 0 ? "<b>" + alg + "</b>" : alg, movesText(s.htm),
                     costText(s.cost), muted, note);
  }
  return html + "</table>";
}

QString F2LPage::wholeHtml() const {
  const QString muted = mutedColour();
  if (!wholeReady_) {
    return "<p>" + tr("Working out the best solutions…") + "</p>";
  }
  QString html;
  if (!segments_.empty()) {
    html += "<table align='center' cellspacing='0' cellpadding='3'><tr "
            "style='color:" +
            muted + "'><th align='left'>" + tr("Pair") +
            "</th><th align='left'>" + tr("You") +
            "</th><th></th><th align='left'>" + tr("Best for that pair") +
            "</th><th align='left'>" + tr("Best pair then") + "</th></tr>";
  }
  const QString tick = coloured(kPassColour, QStringLiteral("✓"));
  for (std::size_t i = 0; i < segments_.size(); ++i) {
    const F2LSegment &seg = segments_[i];
    QStringList names;
    for (Slot s : seg.solved) {
      names << slotText(s);
    }
    Alg moves(item().moves.begin() + static_cast<std::ptrdiff_t>(seg.begin),
              item().moves.begin() + static_cast<std::ptrdiff_t>(seg.end));
    Alg simple = simplify(moves);
    double cost = costOf(moves);
    QString best, over, pick;
    if (i < ranks_.size() && !ranks_[i].empty()) {
      const std::vector<PairRank> &ranks = ranks_[i];
      const PairRank *r =
          seg.solved.size() == 1 ? rankOf(ranks, seg.solved[0]) : nullptr;
      if (r && r->pair.best()) {
        best = QString("%1 <span style='color:%2'>(%3, %4)</span>")
                   .arg(qs(r->pair.best()->text).toHtmlEscaped(), muted,
                        movesText(r->pair.best()->htm),
                        costText(r->pair.best()->cost));
        double d = cost - r->pair.best()->cost;
        over = d < 0.05 ? tick
                        : coloured(d >= 2.0 ? QColor(kFailColour)
                                            : QColor(kWarnColour),
                                   "+" + costText(d));
        double regret = r->total - ranks.front().total;
        pick = regret < 0.05
                   ? tick
                   : slotText(ranks.front().pair.slot) + " " +
                         coloured(regret >= 2.0 ? QColor(kFailColour)
                                                : QColor(kWarnColour),
                                  "(" + tr("−%1").arg(costText(regret)) + ")");
      }
    }
    html +=
        QString("<tr><td><b>%1</b></td><td>%2 <span style='color:%3'>(%4, "
                "%5)</span></td><td>%6</td><td>%7</td><td>%8</td></tr>")
            .arg(names.join(" + "), qs(toString(simple)).toHtmlEscaped(), muted,
                 movesText(htmCount(simple)), costText(cost), over, best, pick);
  }
  if (!segments_.empty()) {
    html += "</table>";
    html += QString("<p style='color:%1'>%2</p>")
                .arg(muted,
                     tr("Costs are by your Alg Lab score profile. \"Best "
                        "pair then\" is the pair that starts the cheapest way "
                        "through the pairs left, with what starting with "
                        "yours cost over it."));
  }
  return html;
}

QString F2LPage::detail() {
  if (!target_) {
    return wholeHtml();
  }
  if (!pair_) {
    return "<p>" + tr("Working out the best solutions…") + "</p>";
  }
  QString html;
  if (!item().gaveUp && !item().moves.empty()) {
    html +=
        "<p>" +
        tr("You: %1").arg("<b>" + algText(simplify(item().moves)) + "</b>") +
        "</p>";
  }
  html += "<p>" +
          tr("The best solutions from this state (cost by your Alg Lab score "
             "profile):") +
          "</p>" + solutionsHtml(*pair_);
  html += QString("<p style='color:%1'>%2</p>")
              .arg(mutedColour(), qs(caseName(pair_->pcase)).toHtmlEscaped());
  return html;
}

// --- the lists ---------------------------------------------------------------

std::vector<PracticePage::ListRow> F2LPage::cardRows() {
  CubeLook look = cubeView()->look();
  look.map = kIdentityColourMap;
  const int size = settings().thumbnailSize;
  std::vector<ListRow> rows;
  for (PairCase c : pairCases(config_.eo)) {
    ListRow r;
    r.key = caseKey(c);
    r.name = qs(caseName(c));
    r.section = qs(groupName(groupOf(c)));
    r.icon = cube_painter::cachedDiagram(caseDiagram(c), viewOf(c), size, look,
                                         devicePixelRatioF());
    rows.push_back(std::move(r));
  }
  return rows;
}

std::vector<PracticePage::ListRow> F2LPage::learnGroups() {
  return {{kWhole, tr("Whole F2L"), {}, {}}};
}

std::vector<PracticePage::ListRow> F2LPage::timedGroups() {
  std::vector<ListRow> rows;
  for (int m = 0; m < kF2LMethods; ++m) {
    auto method = static_cast<F2LMethod>(m);
    rows.push_back(
        {std::string(methodId(method)), qs(methodName(method)), {}, {}});
  }
  return rows;
}

void F2LPage::groupActivated(const std::string &key) {
  if (auto m = methodFromId(key); m && tab() == Tab::Timed) {
    applyMethod(config_, *m);
    configChanged();
  }
}

QString F2LPage::listHelp() const {
  return tab() == Tab::Learn
             ? tr("The pair cases by where the corner and edge are, seen for "
                  "the front-right slot. Each is a card: new ones come in a "
                  "few a day, and a try is Good when the pair is solved "
                  "unaided within %1 of the best solution's cost. "
                  "Double-click a case to practise it, ungraded.")
                   .arg(costText(kGoodOver))
             : tr("Timed solves by method. Double-click one to switch to it.");
}

} // namespace vcube
