#include "app/tsle_page.hpp"

#include "app/cube_painter.hpp"
#include "app/session_panel.hpp"
#include "app/ui_state.hpp"
#include "app/ui_text.hpp"
#include "train/f2l.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace vcube {

namespace {

// Whether the moves turn anything but the top layer and the whole cube.
bool turnsTheSlot(const Alg &moves) {
  Alg simple = simplify(moves);
  return std::any_of(simple.begin(), simple.end(), [](Move m) {
    return m.type != MoveType::U && !isRotation(m.type);
  });
}

// The section a case is listed in: 0 the OCLLs, else its steps left.
int sectionOf(TsleState c) { return c.ocll() ? 0 : tsleStepsLeft(c); }

const char *const kWhole = "whole";

} // namespace

TslePage::TslePage(AppSettings &settings, PracticeStore &store, QWidget *parent)
    : PracticePage(settings, store, parent) {
  QStringList chosen = saved::practice("tsle", "slots", QStringList{"FR", "FL"})
                           .get()
                           .toStringList();
  bool any = false;
  for (Slot s : kF2LSlots) {
    bool on = chosen.contains(slotText(s));
    slots_[static_cast<std::size_t>(s)] = on;
    any = any || on;
  }
  if (!any) {
    slots_ = {true, true, false, false};
  }
  rebuildSolvers();
  build();
}

TslePage::~TslePage() = default;

// --- options -----------------------------------------------------------------

void TslePage::addOptions(OptionBar *bar) {
  slotsButton_ = new QToolButton(bar);
  slotsButton_->setFocusPolicy(Qt::NoFocus);
  slotsButton_->setPopupMode(QToolButton::InstantPopup);
  auto *menu = new QMenu(slotsButton_);
  for (Slot s : kF2LSlots) {
    QAction *a = menu->addAction(slotText(s));
    a->setCheckable(true);
    a->setData(static_cast<int>(s));
    connect(a, &QAction::toggled, this, [this, s](bool on) {
      std::array<bool, kSlots> chosen = slots_;
      chosen[static_cast<std::size_t>(s)] = on;
      setLastSlots(chosen);
    });
  }
  slotsButton_->setMenu(menu);
  bar->addPicker(tr("Last slot:"), slotsButton_);
}

void TslePage::addButtons(OptionBar *bar) {
  ocllButton_ = flatButton(tr("OCLL algs…"), bar);
  bar->addWidget(ocllButton_);
  connect(ocllButton_, &QPushButton::clicked, this, &TslePage::editOcllAlgs);
}

void TslePage::showOptions() {
  QStringList names;
  for (QAction *a : slotsButton_->menu()->actions()) {
    QSignalBlocker block(a);
    bool on = slots_[static_cast<std::size_t>(a->data().toInt())];
    a->setChecked(on);
    if (on) {
      names << a->text();
    }
  }
  slotsButton_->setText(names.join(" / "));
}

void TslePage::setLastSlots(const std::array<bool, kSlots> &chosen) {
  if (std::none_of(chosen.begin(), chosen.end(), [](bool b) { return b; })) {
    showOptions(); // one slot at least
    return;
  }
  slots_ = chosen;
  QStringList names;
  for (Slot s : kF2LSlots) {
    if (slots_[static_cast<std::size_t>(s)]) {
      names << slotText(s);
    }
  }
  saved::practice("tsle", "slots").set(names);
  optionChanged();
}

std::vector<OcllAlg> TslePage::ocllAlgs() const {
  std::vector<OcllAlg> algs = defaultOcllAlgs();
  for (OcllAlg &a : algs) {
    auto it = data().notes.find("ocll/" + a.id);
    if (it != data().notes.end() &&
        ocllAlgWorks(a.id, parseAlgOrEmpty(it->second))) {
      a.alg = it->second;
    }
  }
  return algs;
}

void TslePage::rebuildSolvers() {
  std::vector<OcllAlg> algs = ocllAlgs();
  for (Slot s : kF2LSlots) {
    solvers_[static_cast<std::size_t>(s)] =
        std::make_unique<TsleSolver>(s, algs);
  }
}

const TsleSolver &TslePage::solver(Slot slot) const {
  return *solvers_[static_cast<std::size_t>(slot)];
}

Slot TslePage::randomSlot() {
  std::vector<Slot> allowed;
  for (Slot s : kF2LSlots) {
    if (slots_[static_cast<std::size_t>(s)]) {
      allowed.push_back(s);
    }
  }
  return allowed[std::uniform_int_distribution<std::size_t>(0, allowed.size() -
                                                                   1)(rng())];
}

// --- states ------------------------------------------------------------------

std::vector<std::string> TslePage::cardPool() const { return tsleCardPool(); }

std::optional<PracticePage::Setup> TslePage::setUp(const std::string &card) {
  TsleState state;
  if (!card.empty()) {
    std::optional<TsleState> c = tsleFromKey(card);
    if (!c) {
      return std::nullopt;
    }
    state = tsleTurned(*c, static_cast<int>(rng()() % 4));
  } else {
    // Any last-slot state, as a solve leaves them.
    do {
      state = TsleState::fromIndex(
          std::uniform_int_distribution<int>(0, kTsleStates - 1)(rng()));
    } while (state.done());
  }
  slot_ = randomSlot();
  start_ = state;
  restarted();
  Setup s;
  s.state = randomTsleCube(state, slot_, rng());
  s.hold = settings().neutrality.pick(rng());
  s.cube = s.state.toFacelets();
  // Timed attempts are kept by the case they start from.
  s.group = tab() == Tab::Timed ? tsleKey(tsleCase(state)) : kWhole;
  return s;
}

void TslePage::restarted() {
  steps_.clear();
  stepBegin_ = 0;
  stepState_ = start_;
  stepHint_ = false;
}

std::optional<TsleState> TslePage::currentState() const {
  auto c = centredCubie(cube());
  return c ? tsleStateOf(*c, slot_) : std::nullopt;
}

TslePage::Step TslePage::judged(TsleState before, TsleState after,
                                const Alg &moves) const {
  const TsleSolver &s = solver(slot_);
  Step step;
  step.before = before;
  step.after = after;
  step.moves = htmCount(simplify(moves));
  if (before.ocll()) {
    step.good = after.done();
    step.extra = std::max(0, step.moves - s.movesLeft(before));
    return step;
  }
  step.good = tsleStepsLeft(after) < tsleStepsLeft(before);
  // The way on from here, as the trigger that leaves this state has it
  // (it knows what cancels into an OCLL alg).
  int total = step.moves + s.movesLeft(after);
  for (const TsleOption &o : s.options(before)) {
    if (o.next == after && step.moves >= o.moves) {
      total = std::min(total, o.total + step.moves - o.moves);
    }
  }
  step.extra = std::max(0, total - s.movesLeft(before));
  return step;
}

bool TslePage::turned() {
  std::optional<TsleState> state = currentState();
  if (!state) {
    return false; // in the middle of a trigger
  }
  const Alg &moves = item().moves;
  Alg since(moves.begin() + static_cast<std::ptrdiff_t>(stepBegin_),
            moves.end());
  if (!turnsTheSlot(since)) {
    return false; // only U turns so far
  }
  if (stepState_.ocll() && !state->done()) {
    return false; // an OCLL alg passes through states on its way
  }
  Step step = judged(stepState_, *state, since);
  step.begin = stepBegin_;
  step.end = moves.size();
  step.hint = stepHint_;
  steps_.push_back(step);
  stepBegin_ = moves.size();
  stepState_ = *state;
  stepHint_ = false;
  if (tab() == Tab::Untimed) {
    // A line on each step as it is done.
    QStringList lines;
    for (const Step &done : steps_) {
      Alg stepMoves(moves.begin() + static_cast<std::ptrdiff_t>(done.begin),
                    moves.begin() + static_cast<std::ptrdiff_t>(done.end));
      lines << "<b>" + algText(simplify(stepMoves)) + "</b> · " +
                   stepVerdict(done);
    }
    setProgress(lines);
  }
  if (item().isNew) {
    // A new case is done once a good trigger was done from where it was
    // shown.
    if (steps_.size() == 1 && step.good) {
      return true;
    }
    emit message(tr("Not a good trigger from there: Esc starts again"));
    return false;
  }
  return item().card.empty() ? state->done() : true;
}

// --- what is said ------------------------------------------------------------

QString TslePage::caseText(TsleState state) const {
  if (state.done()) {
    return tr("TSLE done");
  }
  QString name = qs(tsleName(tsleCase(state))).toHtmlEscaped();
  if (state.ocll()) {
    return tr("%1 (an OCLL)").arg(name);
  }
  int steps = tsleStepsLeft(state);
  return steps == 1 ? tr("%1 (1 step left)").arg(name)
                    : tr("%1 (%2 steps left)").arg(name).arg(steps);
}

QString TslePage::cardName(const std::string &card) const {
  std::optional<TsleState> c = tsleFromKey(card);
  return c ? qs(tsleName(*c)).toHtmlEscaped() : qs(card);
}

QString TslePage::task() const {
  return item().card.empty() ? tr("Solve TSLE") : tr("Do the next step");
}

QString TslePage::taskNote() const {
  return tr("last slot: %1").arg(slotText(slot_));
}

QString TslePage::hint() {
  const TsleSolver &s = solver(slot_);
  stepHint_ = true;
  if (stepState_.ocll()) {
    auto f = s.finish(stepState_);
    return f ? tr("Hint: %1, %2").arg(qs(f->name), qs(toString(f->alg)))
             : QString();
  }
  std::vector<TsleOption> options = s.options(stepState_);
  return options.empty()
             ? QString()
             : tr("Hint: %1").arg(qs(toString(options.front().alg)));
}

Alg TslePage::demoAlg() {
  std::vector<TsleOption> options = solver(slot_).options(start_);
  return options.empty() ? Alg{} : options.front().alg;
}

QString TslePage::demoText() {
  QStringList good;
  for (const TsleOption &o : solver(slot_).options(start_)) {
    if (o.good) {
      good << (o.best ? "<b>" + algText(o.alg) + "</b>" : algText(o.alg));
    }
  }
  return tr("%1; good from this AUF: %2. Enter shows the best one; then do "
            "a good one.")
      .arg(taskNote(), good.join(QStringLiteral(" &nbsp;·&nbsp; ")));
}

QString TslePage::stepVerdict(const Step &step) const {
  if (step.before.ocll()) {
    return step.extra == 0 ? coloured(kPassColour, tr("OCLL solved"))
                           : overMarked(tr("OCLL solved, +%1 over your alg")
                                            .arg(movesText(step.extra)));
  }
  if (!step.good) {
    int before = tsleStepsLeft(step.before), after = tsleStepsLeft(step.after);
    return coloured(
        kFailColour,
        tr("Not a good trigger: %1 left after it (%2 before)")
            .arg(after == 1 ? tr("1 step") : tr("%1 steps").arg(after))
            .arg(before));
  }
  return step.extra == 0
             ? coloured(kPassColour, tr("The best trigger from this AUF"))
             : overMarked(tr("A good trigger, but +%1 over the best "
                             "from this AUF")
                              .arg(movesText(step.extra)));
}

PracticePage::Outcome TslePage::judge() {
  const TsleSolver &s = solver(slot_);
  Outcome o;
  if (!item().card.empty()) {
    // One step.
    if (start_.ocll()) {
      if (auto f = s.finish(start_)) {
        o.best = f->alg;
        o.result.bestMoves = f->moves;
      }
    } else if (auto options = s.options(start_); !options.empty()) {
      o.best = options.front().alg;
      o.result.bestMoves = options.front().moves;
    }
    if (item().gaveUp || steps_.empty()) {
      o.verdict = coloured(kFailColour, tr("Gave up"));
      return o;
    }
    const Step &step = steps_.front();
    o.verdict = stepVerdict(step) +
                (item().hint ? " · " + tr("with a hint") : QString());
    o.result.moves = step.moves;
    o.result.extra = step.good ? step.extra : kWrongTrigger;
    o.result.good = step.good;
    return o;
  }
  // The whole step.
  o.result.bestMoves = s.movesLeft(start_);
  for (const Alg &step : s.bestPath(start_)) {
    o.best.insert(o.best.end(), step.begin(), step.end());
  }
  if (item().moves.empty()) {
    // A physical cube: only the best way is known.
    o.verdict = tr("%1, best way %2")
                    .arg(caseText(start_), movesText(o.result.bestMoves));
    return o;
  }
  // Step by step, as done: a trigger undone by the next one still counts,
  // but an OCLL alg joined on to the trigger before it (R' R cancelling)
  // is counted joined, as the best way is.
  for (std::size_t i = 0; i < steps_.size(); ++i) {
    o.result.moves += steps_[i].moves;
    if (i > 0 && steps_[i].before.ocll()) {
      const Step &prev = steps_[i - 1];
      const Alg &moves = item().moves;
      Alg both(moves.begin() + static_cast<std::ptrdiff_t>(prev.begin),
               moves.begin() + static_cast<std::ptrdiff_t>(steps_[i].end));
      o.result.moves -= prev.moves + steps_[i].moves - htmCount(simplify(both));
    }
  }
  if (item().gaveUp) {
    o.verdict = coloured(kFailColour, tr("Not finished")) + " · " +
                tr("best way %1").arg(movesText(o.result.bestMoves));
    return o;
  }
  o.result.extra = std::max(0, o.result.moves - o.result.bestMoves);
  o.result.good = o.result.extra == 0;
  o.verdict = tr("TSLE in %1").arg(movesText(o.result.moves)) + " · " +
              (o.result.moves <= o.result.bestMoves
                   ? coloured(kPassColour, tr("as short as the best way"))
                   : overMarked(tr("+%1 over the best way")
                                    .arg(o.result.moves - o.result.bestMoves),
                                o.result.moves - o.result.bestMoves >= 3));
  return o;
}

QString TslePage::optionsHtml(TsleState state) const {
  const TsleSolver &s = solver(slot_);
  const QString muted = mutedColour();
  if (state.done()) {
    return "<p>" + tr("TSLE is done.") + "</p>";
  }
  if (state.ocll()) {
    auto f = s.finish(state);
    if (!f) {
      return {};
    }
    return "<p>" +
           tr("An OCLL is left, %1: %2 (%3)")
               .arg(qs(f->name).toHtmlEscaped(),
                    playLink(f->alg, algText(f->alg)), movesText(f->moves)) +
           "</p>";
  }
  const int best = s.movesLeft(state);
  QString html = "<table align='center' cellspacing='0' cellpadding='3'><tr "
                 "style='color:" +
                 muted + "'><th align='left'>" + tr("Trigger") +
                 "</th><th align='left'>" + tr("Leaves") + "</th><th>" +
                 tr("To the end") + "</th><th></th></tr>";
  for (const TsleOption &o : s.options(state)) {
    // The good ones, and any with more steps but fewer moves.
    if (!o.good && o.total >= best) {
      continue;
    }
    QString leaves = caseText(o.next);
    if (o.finish) {
      leaves += " · " + algText(o.finish->alg);
    }
    QString mark = o.best ? coloured(kPassColour, tr("best from this AUF"))
                   : o.good
                       ? overMarked(tr("good, +%1").arg(o.total - best))
                       : QString("<span style='color:%1'>%2</span>")
                             .arg(muted, tr("a step more, %1 fewer")
                                             .arg(movesText(best - o.total)));
    QString alg = playLink(o.alg, algText(o.alg));
    html += QString("<tr><td>%1</td><td>%2</td><td align='right'>%3</td>"
                    "<td>%4</td></tr>")
                .arg(o.best ? "<b>" + alg + "</b>" : alg, leaves,
                     movesText(o.total), mark);
  }
  return html + "</table>";
}

QString TslePage::pathHtml(TsleState state) const {
  if (state.done()) {
    return {};
  }
  const TsleSolver &s = solver(slot_);
  QStringList steps;
  for (const Alg &step : s.bestPath(state)) {
    steps << algText(step);
  }
  return "<p>" +
         tr("The best way to the end, %1: %2")
             .arg(movesText(s.movesLeft(state)),
                  steps.join(QStringLiteral(" &nbsp;·&nbsp; "))) +
         "</p>";
}

QString TslePage::detail() {
  const TsleSolver &s = solver(slot_);
  const QString muted = mutedColour();
  QString html;
  if (!item().card.empty() || steps_.empty()) {
    if (!steps_.empty() && !item().gaveUp) {
      const Step &step = steps_.front();
      html += "<p>" +
              tr("You: %1 (%2), leaving %3")
                  .arg("<b>" + algText(simplify(item().moves)) + "</b>",
                       movesText(step.moves), caseText(step.after)) +
              "</p>";
    }
    html += "<p>" +
            tr("%1, %2 slot. Every good trigger from this AUF:")
                .arg(caseText(start_), slotText(slot_)) +
            "</p>" + optionsHtml(start_) + pathHtml(start_);
    return html;
  }
  html += "<p>" + tr("%1, %2 slot.").arg(caseText(start_), slotText(slot_)) +
          "</p><table align='center' cellspacing='0' cellpadding='3'>"
          "<tr style='color:" +
          muted + "'><th align='left'>" + tr("From") +
          "</th><th align='left'>" + tr("You") + "</th><th align='left'>" +
          tr("Verdict") + "</th><th align='left'>" + tr("Best then") +
          "</th></tr>";
  for (const Step &step : steps_) {
    Alg moves(item().moves.begin() + static_cast<std::ptrdiff_t>(step.begin),
              item().moves.begin() + static_cast<std::ptrdiff_t>(step.end));
    QString bestThen;
    if (step.before.ocll()) {
      if (auto f = s.finish(step.before)) {
        bestThen = algText(f->alg);
      }
    } else {
      bestThen = algText(s.options(step.before).front().alg);
    }
    html +=
        QString("<tr><td>%1</td><td><b>%2</b></td><td>%3</td><td>%4</td></tr>")
            .arg(caseText(step.before), algText(simplify(moves)),
                 stepVerdict(step) +
                     (step.hint ? " · " + tr("hint") : QString()),
                 bestThen);
  }
  return html + "</table>" + pathHtml(start_);
}

// --- the lists ---------------------------------------------------------------

std::vector<PracticePage::ListRow> TslePage::caseRows(Rows which) {
  CubeLook look = cubeView()->look();
  look.map = kIdentityColourMap;
  const int size = settings().thumbnailSize;
  std::vector<ListRow> rows;
  for (TsleState c : tsleCases()) {
    if (c.ocll() && which == Rows::TsleOnly) {
      continue;
    }
    const int level = sectionOf(c);
    ListRow r;
    r.key = tsleKey(c);
    r.name = qs(tsleName(c));
    r.section = level == 0   ? tr("OCLL")
                : level == 1 ? tr("1 step left")
                             : tr("%1 steps left").arg(level);
    r.icon = cube_painter::caseIcon(cube_painter::cachedDiagram(
        tsleDiagram(c), DiagramView::Iso3D, size, look, devicePixelRatioF()));
    rows.push_back(std::move(r));
  }
  return rows;
}

std::vector<PracticePage::ListRow> TslePage::cardRows() {
  return caseRows(Rows::TsleOnly);
}

std::vector<PracticePage::Split> TslePage::splits() {
  std::vector<Split> out;
  for (std::size_t i = 0; i < steps_.size(); ++i) {
    out.push_back({steps_[i].before.ocll() ? tr("%1. OCLL").arg(i + 1)
                                           : tr("%1. trigger").arg(i + 1),
                   steps_[i].end});
  }
  return out;
}

std::vector<PracticePage::ListRow> TslePage::untimedGroups() {
  return {{kWhole, tr("Whole TSLE"), {}, {}}};
}

std::vector<PracticePage::ListRow> TslePage::timedGroups() {
  return caseRows(Rows::WithOclls);
}

QString TslePage::listHelp() const {
  if (tab() == Tab::Untimed) {
    return tr("Your untimed solves (F1 explains the columns).");
  }
  return tab() == Tab::Learn
             ? tr("The cases by steps left (F1 explains the names). "
                  "Double-click one to practise it, ungraded.")
             : tr("Timed solves by the case they started from.");
}

// --- the OCLL algs -----------------------------------------------------------

void TslePage::editOcllAlgs() {
  if (isBusy()) {
    return;
  }
  QDialog dialog(this);
  dialog.setWindowTitle(tr("OCLL algs"));
  auto *layout = new QVBoxLayout(&dialog);
  layout->addWidget(helpLabel(
      tr("The alg a chain ends with when an OCLL is left. They are shown, "
         "and their lengths counted, as written here for every slot; when "
         "you solve, any alg that works counts."),
      &dialog));
  auto *form = new QFormLayout;
  std::vector<OcllAlg> algs = ocllAlgs();
  std::vector<QLineEdit *> edits;
  for (const OcllAlg &a : algs) {
    auto *edit = new QLineEdit(qs(a.alg), &dialog);
    edit->setObjectName(qs(a.id));
    edit->setMinimumWidth(320);
    form->addRow(qs(a.name) + ":", edit);
    edits.push_back(edit);
  }
  layout->addLayout(form);
  auto *problem = new QLabel(&dialog);
  problem->setWordWrap(true);
  layout->addWidget(problem);
  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                               QDialogButtonBox::RestoreDefaults,
                           &dialog);
  layout->addWidget(buttons);
  connect(buttons->button(QDialogButtonBox::RestoreDefaults),
          &QPushButton::clicked, &dialog, [&edits] {
            const std::vector<OcllAlg> &defaults = defaultOcllAlgs();
            for (std::size_t i = 0; i < edits.size(); ++i) {
              edits[i]->setText(qs(defaults[i].alg));
            }
          });
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    for (std::size_t i = 0; i < edits.size(); ++i) {
      std::string text = cleanAlgText(edits[i]->text().toStdString());
      if (!ocllAlgWorks(algs[i].id, parseAlgOrEmpty(text))) {
        problem->setText(coloured(
            kFailColour, tr("The %1 alg doesn't solve that OCLL (from any U "
                            "turn, keeping the F2L).")
                             .arg(qs(algs[i].name))));
        edits[i]->setFocus();
        return;
      }
    }
    dialog.accept();
  });
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  const std::vector<OcllAlg> &defaults = defaultOcllAlgs();
  for (std::size_t i = 0; i < edits.size(); ++i) {
    std::string text = cleanAlgText(edits[i]->text().toStdString());
    const std::string key = "ocll/" + defaults[i].id;
    if (text != defaults[i].alg) {
      editData().notes[key] = text;
    } else {
      editData().notes.erase(key);
    }
  }
  save();
  rebuildSolvers();
  optionChanged();
}

} // namespace vcube
