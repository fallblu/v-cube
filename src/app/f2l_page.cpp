#include "app/f2l_page.hpp"

#include "app/cube_painter.hpp"
#include "app/ui_state.hpp"
#include "app/ui_text.hpp"

#include "io/algset_json.hpp"
#include "train/solve_review.hpp"

#include <QComboBox>
#include <QDir>
#include <QFutureWatcher>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSignalBlocker>
#include <QTextDocumentFragment>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <limits>

namespace vcube {

namespace {

const char *const kWhole = "whole";
// Costs closer than this are the same (they are shown to a tenth).
constexpr double kSameCost = 0.05;
// A cost this much over the best (a pair's) is far over.
constexpr double kFarOver = 2.0;

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

// The corner a back card is best seen from.
DiagramView backViewOf(PairCase c) {
  for (std::optional<Slot> s : {c.cornerSlot(), c.edgeSlot()}) {
    if (s && slotSeenFrom(*s, Slot::BR) == Slot::BL) {
      return DiagramView::Iso3DBackLeft;
    }
  }
  return DiagramView::Iso3DBackRight;
}

// A cost over the best one, coloured by how much it is.
QString overText(double over, const QString &best) {
  return over < kSameCost
             ? coloured(kPassColour, QObject::tr("as good as %1").arg(best))
             : overMarked(QObject::tr("+%1 over %2").arg(costText(over), best),
                          over >= kFarOver);
}

QString slotsText(const std::vector<Slot> &solved) {
  QStringList names;
  for (Slot s : solved) {
    names << slotText(s);
  }
  return names.join(" + ");
}

} // namespace

F2LPage::F2LPage(AppSettings &settings, AlgLibrary &library, AlgLabStore &lab,
                 const QString &dataDir, QWidget *parent)
    : PracticePage(settings, dataDir, "f2l.json", parent), library_(library),
      lab_(lab) {
  loadChoices();
  // Cards of before, by case alone, start afresh as front and back ones.
  const std::size_t before = data().cards.size();
  std::erase_if(editData().cards,
                [](const auto &entry) { return !cardFromKey(entry.first); });
  if (data().cards.size() != before) {
    save();
  }
  buildChooser();
  build();
}

F2LPage::~F2LPage() {
  for (const auto &stop : {stop_, laterStop_, candidateStop_}) {
    if (stop) {
      stop->store(true);
    }
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
  // Solutions are searched with every move the method allows.
  config_.dMoves = true;
  config_.halfFB = true;
  config_.wide = true;
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
}

void F2LPage::configChanged() {
  saveChoices();
  optionChanged();
}

void F2LPage::setConfig(const F2LConfig &config) {
  config_ = config;
  config_.dMoves = true;
  config_.halfFB = true;
  config_.wide = true;
  configChanged();
}

void F2LPage::reloadSets() {
  sheetLooked_ = false; // it may be there now
  importSheet();
}

void F2LPage::activated() {
  importSheet();
  PracticePage::activated();
}

// --- the sheet's algs --------------------------------------------------------

void F2LPage::importSheet() {
  if (sheetLooked_ || data().notes.contains("sheet")) {
    return;
  }
  // The set of pair cases F2L mode used to show beside its solutions: in
  // the library, or the newest copy of it deleted from there.
  const QString id = saved::reviewPairSet().get();
  std::optional<AlgSet> set;
  QString source;
  if (const AlgSet *s = library_.find(id.toStdString()); s && isPairSet(*s)) {
    set = *s;
    source = id;
  } else {
    QDir dir(library_.deletedDirectory());
    const QStringList files =
        dir.entryList({id + ".*.json"}, QDir::Files, QDir::Name);
    for (auto it = files.rbegin(); it != files.rend() && !set; ++it) {
      QString error;
      std::optional<AlgSet> read = readAlgSetFile(dir.filePath(*it), &error);
      if (read && isPairSet(*read)) {
        set = std::move(read);
        source = "deleted/" + *it;
      }
    }
  }
  if (!set) {
    sheetLooked_ = true;
    return;
  }
  for (const std::string &key : pairCardPool(true)) {
    std::optional<PairCard> card = cardFromKey(key);
    std::optional<CubieCube> state =
        card ? cardState(*card, true, rng()) : std::nullopt;
    if (!state) {
      continue;
    }
    for (const PairOption &o : pairOptions(*state, &*set)) {
      if (o.slot != sideSlot(card->side) || o.caseId.empty()) {
        continue;
      }
      Alg alg = simplify(parseAlgOrEmpty(o.alg));
      if (!alg.empty() && solvesPairWithAuf(*state, o.slot, alg, config_)) {
        editData().notes["sheet/" + key] = toString(alg);
      }
    }
  }
  editData().notes["sheet"] = source.toStdString();
  save();
}

std::optional<std::string> F2LPage::sheetAlg(const std::string &card) const {
  auto it = data().notes.find("sheet/" + card);
  return it == data().notes.end() ? std::nullopt
                                  : std::optional<std::string>(it->second);
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
  stop_ = std::make_shared<std::atomic<bool>>(false);
  ++requests_; // what is still being worked out is for another state
  pair_.reset();
  target_.reset();
  card_.reset();
  ranks_.clear();
  asked_.clear();
  if (whole) {
    start_ = randomFullState(config_, rng());
  } else {
    std::optional<PairCard> c = cardFromKey(card);
    if (!c) {
      return std::nullopt;
    }
    // A case whose solutions are still to be chosen has the other slots
    // solved; afterwards the last slot may be open too, as in a solve.
    const bool leaveOpen = !chosenSolutions(card).empty() &&
                           std::uniform_int_distribution<int>(0, 1)(rng()) == 1;
    std::optional<PairSetup> setup = cardSetup(*c, config_, leaveOpen, rng());
    if (!setup) {
      return std::nullopt;
    }
    start_ = setup->state;
    target_ = setup->target;
    card_ = c;
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
  } else {
    requestRanks(0, start_); // the cheapest way from the start
  }
  return s;
}

void F2LPage::restarted() {
  hintMoves_ = 0;
  segments_.clear();
  offerAdd_ = false;
  // From the start again: the ways on from later states are of no use.
  std::erase_if(ranks_, [](const auto &e) { return e.first != 0; });
  std::erase_if(asked_, [](const auto &e) { return e.first != 0; });
  // What is still being worked out for them is for states that are gone.
  if (laterStop_) {
    laterStop_->store(true);
  }
  laterStop_ = std::make_shared<std::atomic<bool>>(false);
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
  if (target_) {
    return slotSolved(*c, *target_) && initiallySolvedKept(*c);
  }
  std::vector<F2LSegment> segments = f2lSegments(start_, item().moves, config_);
  const bool done = f2lDone(*c, config_);
  if (segments.size() > segments_.size()) {
    segments_ = std::move(segments);
    // The next pair is begun from here: the way on is worked out now, for
    // its line once it is in. Timed has no lines: it waits for the end,
    // so as not to slow the solve.
    if (!done && tab() != Tab::Timed) {
      requestRanks(segments_.size(), *c);
    }
    updateProgress();
  }
  if (done && tab() == Tab::Timed) {
    for (std::size_t i = 1; i < segments_.size(); ++i) {
      requestRanks(i, segments_[i].before);
    }
  }
  return done;
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
  return o;
}

double F2LPage::compareCost(const Alg &moves, const AnalysisOptions &options) {
  return solutionCost(comparableSolution(moves), options);
}

bool F2LPage::wholeReady() const {
  const std::size_t needed = std::max<std::size_t>(1, segments_.size());
  for (std::size_t i = 0; i < needed; ++i) {
    if (!ranks_.contains(i)) {
      return false;
    }
  }
  return true;
}

bool F2LPage::analysisReady() const {
  return target_ ? pair_.has_value() : wholeReady();
}

void F2LPage::requestPair() {
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

void F2LPage::requestRanks(std::size_t segment, const CubieCube &before) {
  if (asked_.contains(segment)) {
    return;
  }
  const int ask = ++asks_;
  asked_[segment] = ask;
  auto *watcher = new QFutureWatcher<Analysis>(this);
  connect(watcher, &QFutureWatcher<Analysis>::finished, this, [this, watcher] {
    arrived(watcher->result());
    watcher->deleteLater();
  });
  watcher->setFuture(QtConcurrent::run(
      [request = requests_, ask, segment, before, options = analysisOptions(),
       stop = segment == 0 ? stop_ : laterStop_] {
        Analysis a;
        a.request = request;
        a.ask = ask;
        a.segment = segment;
        a.ranks = rankPairs(before, options, stop.get());
        return a;
      }));
}

void F2LPage::arrived(const Analysis &a) {
  if (a.request != requests_) {
    return; // for a state that is gone
  }
  if (a.pair) {
    pair_ = a.pair;
  } else if (a.segment) {
    // Asked again since (after a restart), from another state: the newer
    // asking's answer is the one wanted.
    auto asked = asked_.find(*a.segment);
    if (asked == asked_.end() || asked->second != a.ask) {
      return;
    }
    ranks_[*a.segment] = a.ranks;
    updateProgress();
  }
  analysisArrived(item().id);
}

std::vector<RankedSolution> F2LPage::shownSolutions() const {
  return pair_ ? pair_->solutions : std::vector<RankedSolution>{};
}

std::vector<Alg> F2LPage::chosenSolutions(const std::string &card) const {
  std::vector<Alg> out;
  auto it = data().choices.find(card);
  if (it != data().choices.end()) {
    for (const std::string &text : it->second) {
      Alg alg = parseAlgOrEmpty(text);
      if (!alg.empty()) {
        out.push_back(std::move(alg));
      }
    }
  }
  return out;
}

std::vector<Alg> F2LPage::usableSolutions() const {
  std::vector<Alg> out = chosenSolutions(item().card);
  std::erase_if(out, [this](const Alg &alg) { return !chosenHere(alg); });
  return out;
}

std::optional<Alg> F2LPage::chosenHere(const Alg &chosen) const {
  if (!target_) {
    return std::nullopt;
  }
  return chosenFor(start_, *target_, chosen, config_);
}

// --- the pairs of a whole solve ----------------------------------------------

QString F2LPage::pairLine(std::size_t i, const AnalysisOptions &options) const {
  const F2LSegment &seg = segments_[i];
  const QString name = "<b>" + slotsText(seg.solved) + "</b>";
  Alg moves(item().moves.begin() + static_cast<std::ptrdiff_t>(seg.begin),
            item().moves.begin() + static_cast<std::ptrdiff_t>(seg.end));
  const double cost = solutionCost(moves, options);
  if (seg.solved.size() != 1) {
    return tr("%1 together, cost %2").arg(name, costText(cost));
  }
  auto ranks = ranks_.find(i);
  if (ranks == ranks_.end()) {
    return tr("%1, cost %2 · …").arg(name, costText(cost));
  }
  const PairRank *r = ranks->second.empty()
                          ? nullptr
                          : rankOf(ranks->second, seg.solved.front());
  if (!r || !r->pair.best()) {
    return tr("%1, cost %2").arg(name, costText(cost));
  }
  const double regret = r->total - ranks->second.front().total;
  const QString pick =
      regret < kSameCost
          ? coloured(kPassColour, tr("the best pair"))
          : overMarked(tr("%1 was better (−%2)")
                           .arg(slotText(ranks->second.front().pair.slot),
                                costText(regret)),
                       regret >= kFarOver);
  return tr("%1: %2 · %3")
      .arg(name, pick, overText(cost - r->pair.best()->cost, tr("its best")));
}

void F2LPage::updateProgress() {
  if (tab() != Tab::Untimed) {
    return;
  }
  const AnalysisOptions options = analysisOptions();
  QStringList lines;
  for (std::size_t i = 0; i < segments_.size(); ++i) {
    lines << pairLine(i, options);
  }
  setProgress(lines);
}

std::vector<PracticePage::Split> F2LPage::splits() {
  std::vector<Split> out;
  if (target_) {
    return out;
  }
  for (const F2LSegment &seg : segments_) {
    out.push_back({slotsText(seg.solved), seg.end});
  }
  return out;
}

// --- what is said ------------------------------------------------------------

QString F2LPage::cardName(const std::string &card) const {
  std::optional<PairCard> c = cardFromKey(card);
  if (!c) {
    return qs(card);
  }
  return qs(caseName(c->pcase)).toHtmlEscaped() + " · " +
         (c->side == PairSide::Front ? tr("front") : tr("back"));
}

QString F2LPage::task() const {
  return target_ ? tr("Solve the ringed pair")
                 : tr("Solve the %1").arg(qs(methodName(methodOf(config_))));
}

QString F2LPage::taskNote() const {
  return target_ ? tr("%1 slot").arg(slotText(*target_)) : QString();
}

QString F2LPage::hint() {
  const std::vector<Alg> chosen = usableSolutions();
  std::optional<Alg> best =
      chosen.empty() ? std::nullopt : chosenHere(chosen.front());
  if (!best && pair_ && pair_->best()) {
    best = pair_->best()->alg;
  }
  if (!best || best->empty()) {
    return {};
  }
  // A move more each time.
  hintMoves_ = std::min(hintMoves_ + 1, static_cast<int>(best->size()));
  Alg shown(best->begin(), best->begin() + hintMoves_);
  return hintMoves_ == static_cast<int>(best->size())
             ? tr("Hint: %1").arg(qs(toString(shown)))
             : tr("Hint: it starts %1").arg(qs(toString(shown)));
}

Alg F2LPage::demoAlg() {
  const std::vector<Alg> chosen = usableSolutions();
  if (!chosen.empty()) {
    if (std::optional<Alg> here = chosenHere(chosen.front())) {
      return *here;
    }
  }
  return pair_ && pair_->best() ? pair_->best()->alg : Alg{};
}

QString F2LPage::demoText() {
  const Alg alg = demoAlg();
  if (alg.empty()) {
    return tr("Working out the best solutions…");
  }
  return tr("%1; your solution: %2. Enter shows it; then do it.")
      .arg(taskNote(), "<b>" + algText(alg) + "</b>");
}

PracticePage::Outcome F2LPage::judge() {
  Outcome o;
  const AnalysisOptions options = analysisOptions();
  if (target_) {
    // One pair, against the solutions chosen for it (that the method
    // allows).
    const std::vector<Alg> chosen = usableSolutions();
    offerAdd_ = false;
    if (chosen.empty() || !card_) {
      o.verdict = item().gaveUp ? coloured(kFailColour, tr("Gave up"))
                                : tr("No solutions are chosen for this case.");
      o.good = !item().gaveUp;
      return o;
    }
    double cheapest = std::numeric_limits<double>::infinity();
    for (const Alg &c : chosen) {
      cheapest = std::min(cheapest, compareCost(c, options));
    }
    if (std::optional<Alg> first = chosenHere(chosen.front())) {
      o.best = *first;
      o.bestLabel = chosen.size() > 1 ? tr("Your first") : tr("Yours");
      o.bestText = algText(*first) +
                   QString(" <span style='font-weight:normal'>(%1, %2)</span>")
                       .arg(movesText(htmCount(*first)),
                            tr("cost %1").arg(costText(
                                compareCost(chosen.front(), options))));
      o.bestMoves = htmCount(*first);
    }
    if (item().gaveUp) {
      o.verdict = coloured(kFailColour, tr("Gave up"));
      return o;
    }
    tryAsChosen_ = comparableSolution(
        slotToSide(fixedFrame(item().moves), card_->side, *target_));
    const std::optional<std::size_t> match =
        matchChosen(item().moves, card_->side, *target_, chosen);
    const double mine = solutionCost(tryAsChosen_, options);
    o.moves = htmCount(simplify(item().moves));
    o.extra = std::max(0.0, mine - cheapest);
    o.good = match.has_value() || mine <= cheapest + kSameCost;
    offerAdd_ = !match && o.good && !tryAsChosen_.empty();
    if (match) {
      o.verdict =
          coloured(kPassColour, chosen.size() > 1 ? tr("Your solution %1 of %2")
                                                        .arg(*match + 1)
                                                        .arg(chosen.size())
                                                  : tr("Your solution")) +
          ", " + tr("cost %1").arg(costText(mine));
    } else if (o.good) {
      o.verdict = coloured(kPassColour, tr("Not one of yours, but as cheap")) +
                  " " +
                  tr("(cost %1, yours from %2)")
                      .arg(costText(mine), costText(cheapest));
    } else {
      o.verdict =
          coloured(kFailColour, tr("Not one of your solutions")) + " · " +
          tr("cost %1, yours from %2").arg(costText(mine), costText(cheapest));
    }
    if (item().hint) {
      o.verdict += " · " + tr("with a hint");
    }
    return o;
  }
  // The whole F2L.
  const bool solved = !item().moves.empty();
  if (solved && segments_.empty()) {
    segments_ = f2lSegments(start_, item().moves, config_);
  }
  auto start = ranks_.find(0);
  const PairRank *plan = start != ranks_.end() && !start->second.empty()
                             ? &start->second.front()
                             : nullptr;
  if (plan) {
    // The cheapest way found from the start.
    QStringList order;
    for (Slot s : plan->order) {
      order << slotText(s);
    }
    o.bestMoves = plan->moves;
    o.bestText =
        order.join(" → ") +
        QString(" <span style='font-weight:normal'>(%1, %2)</span>")
            .arg(movesText(plan->moves),
                 tr("cost %1").arg(costText(plan->total - plan->hidden)));
  }
  if (!solved) {
    return o; // a physical cube: only the time, and the best way
  }
  o.moves = htmCount(simplify(item().moves));
  if (item().gaveUp) {
    o.verdict = coloured(kFailColour, tr("Not finished"));
    return o;
  }
  const double cost = solutionCost(item().moves, options);
  o.verdict = tr("F2L in %1, cost %2").arg(movesText(o.moves), costText(cost));
  if (plan) {
    // Against the cheapest way found from the start: what the pairs
    // chosen and their solutions cost over it together.
    const double over = std::max(0.0, cost - (plan->total - plan->hidden));
    o.extra = over;
    o.good = over < kSameCost;
    const double pairs =
        static_cast<double>(std::max(1, pairsLeft(start_, config_)));
    o.verdict +=
        " · " +
        (over < kSameCost
             ? coloured(kPassColour, tr("as good as the cheapest way found"))
             : overMarked(
                   tr("+%1 over the cheapest way found").arg(costText(over)),
                   over >= kFarOver * pairs));
  }
  return o;
}

bool F2LPage::doneKey(QKeyEvent *e) {
  if (!offerAdd_ || (e->key() != Qt::Key_Plus && e->key() != Qt::Key_Equal)) {
    return false;
  }
  std::vector<std::string> &chosen = editData().choices[item().card];
  chosen.push_back(toString(tryAsChosen_));
  offerAdd_ = false;
  save();
  emit message(tr("Added to your solutions for %1")
                   .arg(QTextDocumentFragment::fromHtml(cardName(item().card))
                            .toPlainText()));
  analysisArrived(item().id); // said again
  return true;
}

QList<KeyHint> F2LPage::doneKeys() const {
  if (offerAdd_) {
    return {{"+", tr("add yours to your solutions")}};
  }
  return {};
}

QString
F2LPage::solutionsHtml(const std::vector<RankedSolution> &solutions) const {
  const QString muted = mutedColour();
  if (solutions.empty()) {
    return "<p>" + tr("No solution was found.") + "</p>";
  }
  QString html = "<table align='center' cellspacing='0' cellpadding='3'>";
  for (std::size_t i = 0; i < solutions.size(); ++i) {
    const RankedSolution &s = solutions[i];
    QString alg = playLink(s.alg, qs(s.text).toHtmlEscaped());
    html += QString("<tr><td>%1</td><td align='right'>%2</td>"
                    "<td align='right' style='color:%3'>%4</td></tr>")
                .arg(i == 0 ? "<b>" + alg + "</b>" : alg, movesText(s.htm),
                     muted, costText(s.cost));
  }
  return html + "</table>";
}

QString F2LPage::chosenHtml() const {
  const std::vector<Alg> chosen = usableSolutions();
  if (chosen.empty() || !card_) {
    return {};
  }
  const std::size_t unusable =
      chosenSolutions(item().card).size() - chosen.size();
  const QString muted = mutedColour();
  const AnalysisOptions options = analysisOptions();
  const std::optional<std::size_t> match =
      item().gaveUp ? std::nullopt
                    : matchChosen(item().moves, card_->side, *target_, chosen);
  QString html =
      "<p>" +
      tr("Your solutions for this case (0 changes them), as done here:") +
      "</p><table align='center' cellspacing='0' cellpadding='3'>";
  for (std::size_t i = 0; i < chosen.size(); ++i) {
    std::optional<Alg> here = chosenHere(chosen[i]);
    const Alg shown =
        here.value_or(sideToSlot(chosen[i], card_->side, *target_));
    html +=
        QString("<tr><td>%1</td><td align='right'>%2</td>"
                "<td align='right' style='color:%3'>%4</td>"
                "<td style='color:%3'>%5</td></tr>")
            .arg(playLink(shown, algText(shown)), movesText(htmCount(shown)),
                 muted, costText(compareCost(chosen[i], options)),
                 match == i ? tr("yours this time") : QString());
  }
  html += "</table>";
  if (unusable > 0) {
    html += "<p style='color:" + muted + "'>" +
            tr("%n chosen with another method, kept for it.", "",
               static_cast<int>(unusable)) +
            "</p>";
  }
  return html;
}

QString F2LPage::wholeHtml() const {
  const QString muted = mutedColour();
  if (!wholeReady()) {
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
  const AnalysisOptions options = analysisOptions();
  for (std::size_t i = 0; i < segments_.size(); ++i) {
    const F2LSegment &seg = segments_[i];
    Alg moves(item().moves.begin() + static_cast<std::ptrdiff_t>(seg.begin),
              item().moves.begin() + static_cast<std::ptrdiff_t>(seg.end));
    Alg simple = simplify(moves);
    double cost = solutionCost(moves, options);
    QString best, over, pick;
    auto found = ranks_.find(i);
    if (found != ranks_.end() && !found->second.empty()) {
      const std::vector<PairRank> &ranks = found->second;
      const PairRank *r =
          seg.solved.size() == 1 ? rankOf(ranks, seg.solved[0]) : nullptr;
      if (r && r->pair.best()) {
        best = QString("%1 <span style='color:%2'>(%3, %4)</span>")
                   .arg(qs(r->pair.best()->text).toHtmlEscaped(), muted,
                        movesText(r->pair.best()->htm),
                        costText(r->pair.best()->cost));
        double d = cost - r->pair.best()->cost;
        over =
            d < kSameCost ? tick : overMarked("+" + costText(d), d >= kFarOver);
        double regret = r->total - ranks.front().total;
        pick = regret < kSameCost
                   ? tick
                   : slotText(ranks.front().pair.slot) + " " +
                         overMarked("(" + tr("−%1").arg(costText(regret)) + ")",
                                    regret >= kFarOver);
      }
    }
    html +=
        QString("<tr><td><b>%1</b></td><td>%2 <span style='color:%3'>(%4, "
                "%5)</span></td><td>%6</td><td>%7</td><td>%8</td></tr>")
            .arg(slotsText(seg.solved), qs(toString(simple)).toHtmlEscaped(),
                 muted, movesText(htmCount(simple)), costText(cost), over, best,
                 pick);
  }
  if (!segments_.empty()) {
    html += "</table>";
    html += QString("<p style='color:%1'>%2</p>")
                .arg(muted,
                     tr("Costs are by your Alg Lab score profile. \"Best for "
                        "that pair\" is played from where the pair was begun "
                        "only after the pairs before it; \"Best pair then\" "
                        "is the pair that starts the cheapest way through "
                        "the pairs left, with what starting with yours cost "
                        "over it."));
  }
  return html;
}

QString F2LPage::detail() {
  if (!target_) {
    return wholeHtml();
  }
  QString html;
  if (!item().gaveUp && !item().moves.empty()) {
    html +=
        "<p>" +
        tr("You: %1").arg("<b>" + algText(simplify(item().moves)) + "</b>") +
        "</p>";
  }
  html += chosenHtml();
  html += "<p>" +
          tr("The best solutions from this state, cost by your Alg Lab score "
             "profile (other open slots may make some cheaper than in your "
             "choice):") +
          "</p>" +
          (pair_ ? solutionsHtml(pair_->solutions)
                 : "<p>" + tr("Working out the best solutions…") + "</p>");
  if (card_) {
    html += QString("<p style='color:%1'>%2</p>")
                .arg(mutedColour(), cardName(item().card));
  }
  return html;
}

// --- choosing a case's solutions ---------------------------------------------

void F2LPage::buildChooser() {
  chooser_ = new QWidget(this);
  auto *layout = new QVBoxLayout(chooser_);
  layout->setContentsMargins(0, 0, 0, 0);
  chooserList_ = new QLabel(chooser_);
  chooserList_->setObjectName("f2lChooser");
  chooserList_->setTextFormat(Qt::RichText);
  chooserList_->setWordWrap(true);
  chooserList_->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
  chooserList_->setTextInteractionFlags(Qt::TextBrowserInteraction);
  chooserList_->setFocusPolicy(Qt::NoFocus);
  chooserList_->setOpenExternalLinks(false);
  ownField_ = new QLineEdit(chooser_);
  ownField_->setObjectName("f2lOwnSolution");
  ownField_->setFocusPolicy(Qt::ClickFocus);
  ownField_->setPlaceholderText(
      tr("A solution of your own, as done for the slot shown (Enter adds "
         "it)"));
  ownNote_ = new QLabel(chooser_);
  ownNote_->setWordWrap(true);
  ownNote_->setAlignment(Qt::AlignHCenter);
  layout->addWidget(chooserList_);
  layout->addWidget(ownField_);
  layout->addWidget(ownNote_);
  layout->addStretch();
  connect(chooserList_, &QLabel::linkActivated, this, &F2LPage::candidateLink);
  connect(ownField_, &QLineEdit::returnPressed, this, [this] {
    const QString error = addOwnSolution(ownField_->text());
    ownNote_->setText(error);
    if (error.isEmpty()) {
      ownField_->clear();
      ownField_->clearFocus(); // the keys are the page's again
    }
  });
}

QWidget *F2LPage::choicePanel() { return chooser_; }

bool F2LPage::needsChoice() const {
  return card_.has_value() && !item().card.empty() && usableSolutions().empty();
}

QString F2LPage::choosingText() const {
  if (!card_ || !target_) {
    return {};
  }
  return tr("Choose the solutions you'll use for the %1 slots (shown for "
            "%2): doing one of them, or anything as cheap, is Good.")
      .arg(card_->side == PairSide::Front ? tr("front") : tr("back"),
           slotText(*target_));
}

void F2LPage::startChoosing() {
  candidates_.clear();
  candidatesReady_ = false;
  ownField_->clear();
  ownNote_->clear();
  auto it = searched_.find(searchKey());
  if (it != searched_.end()) {
    fillCandidates(it->second);
  } else {
    requestCandidates();
    showCandidates();
  }
}

std::string F2LPage::searchKey() const {
  return item().card + (config_.eo ? " eo " : " any ") +
         analysisOptions().profile.name;
}

void F2LPage::requestCandidates() {
  const std::string key = searchKey();
  if (searching_ == key) {
    return; // already being searched for
  }
  // One for another card, method or profile isn't wanted any more.
  if (candidateStop_) {
    candidateStop_->store(true);
  }
  auto stop = std::make_shared<std::atomic<bool>>(false);
  candidateStop_ = stop;
  searching_ = key;
  auto *watcher = new QFutureWatcher<std::vector<RankedSolution>>(this);
  connect(watcher, &QFutureWatcher<std::vector<RankedSolution>>::finished, this,
          [this, watcher, stop, key] {
            std::vector<RankedSolution> found = watcher->result();
            watcher->deleteLater();
            if (stop->load()) {
              return; // cut short
            }
            searching_.clear();
            if (!found.empty()) {
              searched_[key] = found;
            }
            if (phase() == Phase::Choose && searchKey() == key) {
              fillCandidates(found);
            }
          });
  AnalysisOptions options = analysisOptions();
  options.keep = kCandidates;
  options.extra = 2;
  watcher->setFuture(QtConcurrent::run([card = item().card, options, stop] {
    std::optional<PairCard> c = cardFromKey(card);
    Rng rng = makeRng();
    std::optional<CubieCube> state =
        c ? cardState(*c, options.config.eo, rng) : std::nullopt;
    if (!state) {
      return std::vector<RankedSolution>{};
    }
    return analysePair(*state, sideSlot(c->side), options, stop.get())
        .solutions;
  }));
}

void F2LPage::fillCandidates(const std::vector<RankedSolution> &searched) {
  const std::vector<Alg> chosen = usableSolutions();
  const bool first = chosen.empty();
  // Typed in (and ticked) while the search was going: kept.
  const std::vector<Candidate> before = std::move(candidates_);
  candidates_.clear();
  auto find = [this](const Alg &alg) {
    const Alg key = comparableSolution(alg);
    return std::find_if(candidates_.begin(), candidates_.end(),
                        [&key](const Candidate &c) {
                          return comparableSolution(c.solution.alg) == key;
                        });
  };
  const AnalysisOptions options = analysisOptions();
  auto add = [this, &options](const Alg &alg, bool sheet, bool custom) {
    Candidate c;
    c.solution.alg = alg;
    c.solution.text = toString(alg);
    c.solution.htm = htmCount(comparableSolution(alg));
    c.solution.cost = compareCost(alg, options);
    c.sheet = sheet;
    c.custom = custom;
    auto at = std::find_if(
        candidates_.begin(), candidates_.end(),
        [&c](const Candidate &o) { return c.solution.cost < o.solution.cost; });
    return candidates_.insert(at, std::move(c));
  };
  for (const RankedSolution &s : searched) {
    add(s.alg, false, false);
  }
  if (std::optional<std::string> sheet = sheetAlg(item().card)) {
    Alg alg = parseAlgOrEmpty(*sheet);
    if (!alg.empty()) {
      auto it = find(alg);
      if (it == candidates_.end()) {
        it = add(alg, true, false);
      }
      it->sheet = true;
      it->ticked = first; // to start with
    }
  }
  for (const Alg &alg : chosen) {
    auto it = find(alg);
    if (it == candidates_.end()) {
      it = add(alg, false, true);
    }
    it->ticked = true;
  }
  for (const Candidate &c : before) {
    auto it = find(c.solution.alg);
    if (it == candidates_.end()) {
      it = add(c.solution.alg, false, true);
    }
    it->ticked = it->ticked || c.ticked;
  }
  candidatesReady_ = true;
  showCandidates();
  showChoosing();
}

void F2LPage::showCandidates() {
  if (!candidatesReady_) {
    chooserList_->setText("<p>" + tr("Working out the best solutions…") +
                          "</p>");
    return;
  }
  const QString muted = mutedColour();
  QString html =
      "<p>" +
      tr("Tick the solutions to use (1-9, or click the box); click one to "
         "watch it. Costs are by your Alg Lab score profile.") +
      "</p><table align='center' cellspacing='0' cellpadding='3'>";
  for (std::size_t i = 0; i < candidates_.size(); ++i) {
    const Candidate &c = candidates_[i];
    std::optional<Alg> here = chosenHere(c.solution.alg);
    const Alg shown = here.value_or(c.solution.alg);
    QStringList notes;
    if (c.sheet) {
      notes << tr("your sheet");
    }
    if (c.custom) {
      notes << tr("yours");
    }
    html += QString("<tr><td><a href='tick:%1' style='text-decoration:none'>"
                    "%2</a></td><td style='color:%3'>%4</td><td>%5</td>"
                    "<td align='right'>%6</td><td align='right' "
                    "style='color:%3'>%7</td><td style='color:%3'>%8</td>"
                    "</tr>")
                .arg(i)
                .arg(c.ticked ? QStringLiteral("☑") : QStringLiteral("☐"),
                     muted, i < 9 ? QString::number(i + 1) : QString(),
                     QString("<a href='play:%1'>%2</a>")
                         .arg(i)
                         .arg(c.ticked ? "<b>" + algText(shown) + "</b>"
                                       : algText(shown)),
                     movesText(c.solution.htm), costText(c.solution.cost),
                     notes.join(", "));
  }
  chooserList_->setText(html + "</table>");
}

void F2LPage::candidateLink(const QString &link) {
  if (phase() != Phase::Choose) {
    return;
  }
  bool ok = false;
  const auto i = static_cast<std::size_t>(link.mid(5).toInt(&ok));
  if (!ok || i >= candidates_.size()) {
    return;
  }
  if (link.startsWith("tick:")) {
    choiceKey(static_cast<int>(i) + 1);
  } else if (link.startsWith("play:")) {
    // Only the picture turns.
    const Alg alg = chosenHere(candidates_[i].solution.alg).value_or(Alg{});
    cubeView()->finishAnimations();
    cubeView()->setState(startCube());
    cubeView()->animateSequence(alg);
  }
}

void F2LPage::choiceKey(int n) {
  if (n < 1 || static_cast<std::size_t>(n) > candidates_.size()) {
    return;
  }
  candidates_[static_cast<std::size_t>(n - 1)].ticked ^= true;
  showCandidates();
}

QString F2LPage::addOwnSolution(const QString &text) {
  if (!target_ || !card_) {
    return {};
  }
  ParseResult parsed = parseAlg(text.toStdString());
  if (!parsed || parsed.alg.empty()) {
    return tr("That isn't an alg.");
  }
  // As done for the slot shown, from some U turn.
  const Alg done = fixedFrame(parsed.alg);
  if (!solvesPairWithAuf(start_, *target_, done, config_)) {
    return tr("That doesn't solve the pair from here (keeping the slots "
              "that are solved).");
  }
  const Alg asChosen =
      comparableSolution(slotToSide(done, card_->side, *target_));
  auto it = std::find_if(
      candidates_.begin(), candidates_.end(), [&asChosen](const Candidate &c) {
        return comparableSolution(c.solution.alg) == asChosen;
      });
  if (it == candidates_.end()) {
    Candidate c;
    c.solution.alg = asChosen;
    c.solution.text = toString(asChosen);
    c.solution.htm = htmCount(asChosen);
    c.solution.cost = compareCost(asChosen, analysisOptions());
    c.custom = true;
    candidates_.push_back(std::move(c));
    it = candidates_.end() - 1;
  }
  it->ticked = true;
  candidatesReady_ = true;
  showCandidates();
  return {};
}

bool F2LPage::confirmChoice() {
  std::vector<std::string> chosen;
  for (const Candidate &c : candidates_) {
    if (c.ticked) {
      chosen.push_back(toString(c.solution.alg));
    }
  }
  if (chosen.empty()) {
    emit message(candidatesReady_ ? tr("Tick one solution at least (1-9)")
                                  : tr("Still working out the best solutions"));
    return false;
  }
  // Those chosen with another method, which aren't offered with this one,
  // are kept for it.
  for (const Alg &alg : chosenSolutions(item().card)) {
    if (!chosenHere(alg)) {
      chosen.push_back(toString(alg));
    }
  }
  editData().choices[item().card] = std::move(chosen);
  save();
  return true;
}

// --- the lists ---------------------------------------------------------------

std::vector<PracticePage::ListRow> F2LPage::cardRows() {
  CubeLook look = cubeView()->look();
  look.map = kIdentityColourMap;
  const int size = settings().thumbnailSize;
  std::vector<ListRow> rows;
  // The back cards' pictures, worked out once.
  static std::map<std::string, FaceletCube> backPictures;
  Rng pictures(1);
  for (PairCase c : pairCases(config_.eo)) {
    for (PairSide side : {PairSide::Front, PairSide::Back}) {
      ListRow r;
      r.key = cardKey({side, c});
      r.name = qs(caseName(c)) + " · " +
               (side == PairSide::Front ? tr("front") : tr("back"));
      r.section = qs(groupName(groupOf(c)));
      if (side == PairSide::Front) {
        r.icon = cube_painter::caseIcon(cube_painter::cachedDiagram(
            caseDiagram(c), viewOf(c), size, look, devicePixelRatioF()));
      } else {
        auto picture = backPictures.find(r.key);
        if (picture == backPictures.end()) {
          // With EO unless the case has the edge flipped.
          std::optional<CubieCube> state =
              cardState({side, c}, c.edge % 2 == 0, pictures);
          picture = backPictures
                        .emplace(r.key, state ? pairDiagram(*state, Slot::BR)
                                              : caseDiagram(c))
                        .first;
        }
        r.icon = cube_painter::caseIcon(cube_painter::cachedDiagram(
            picture->second, backViewOf(c), size, look, devicePixelRatioF()));
      }
      rows.push_back(std::move(r));
    }
  }
  return rows;
}

std::vector<PracticePage::ListRow> F2LPage::untimedGroups() {
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
  switch (tab()) {
  case Tab::Learn:
    return tr("The pair cases, each a card (F1 explains more). Double-click "
              "one to practise it, ungraded.");
  case Tab::Untimed:
    return tr("Your untimed F2L solves (F1 explains the columns).");
  default:
    return tr("Timed solves by method. Double-click one to switch to it.");
  }
}

} // namespace vcube
