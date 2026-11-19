#include "app/move_count_page.hpp"
#include "app/eo_cross_check.hpp"
#include "app/page_parts.hpp"

#include "app/flow_layout.hpp"
#include "app/review_dialog.hpp"
#include "app/text_style.hpp"
#include "train/step_solver.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace vcube {

namespace {

QStringList offeredTypes() {
  QStringList out;
  for (const auto &id : moveCountScrambleTypes()) {
    out << qs(id);
  }
  return out;
}

QString stepText(SolveStep step) { return qs(std::string(stepName(step))); }

} // namespace

MoveCountPage::MoveCountPage(AppSettings &settings, MoveCountStore &store,
                             AlgLibrary &library, QWidget *parent)
    : AttemptPage(settings, store, library, "movecount", true, parent) {
  QWidget *row = stage()->options;
  stepBox_ = new QComboBox(row);
  stepBox_->setFocusPolicy(Qt::NoFocus);
  for (SolveStep step : solveSteps()) {
    stepBox_->addItem(stepText(step), static_cast<int>(step));
  }
  stepBox_->setToolTip(
      tr("The step this session is for (fixed once the session has "
         "solves). On the virtual cube a scramble counts only if the step "
         "is done (on any colour) when you press Space."));
  stage()->options->addPicker(tr("Step:"), stepBox_);
  stage()->options->addSpacing();
  auto *types = new ScrambleTypeBox(library, row, offeredTypes());
  types->setToolTip(
      tr("What the scramble leaves solved: a full WCA scramble for EO, "
         "EOLine, EO Cross, XEOCross or the cross, EOLine solved, EO Cross "
         "solved for ZZ F2L, or the cross solved for CFOP F2L"));
  setTypeBox(types);

  QWidget *right = cubeView_->parentWidget();
  stepLabel_ = new QLabel(right);
  stepLabel_->setAlignment(Qt::AlignCenter);
  setTextRole(stepLabel_, TextRole::Title, true);
  stage()->addAboveCube(stepLabel_);

  // A typed count is recorded, or the scramble recorded as DNF, with
  // Enter and Alt+3 or by clicking their chips (see physicalHint()).

  connect(stepBox_, &QComboBox::activated, this, [this](int index) {
    auto step = static_cast<SolveStep>(stepBox_->itemData(index).toInt());
    if (isBusy() || session().step == step) {
      return;
    }
    if (!session().attempts.empty()) {
      // Its attempts were checked and counted for the step they had.
      stepBox_->setCurrentIndex(
          stepBox_->findData(static_cast<int>(session().step)));
      emit message(tr("This session has solves: start a new session for "
                      "another step"));
      return;
    }
    session().step = step;
    save();
    typeBox_->setCurrentType(qs(defaultScrambleType(step)));
    selectSession(store_.currentIndex());
  });
  connect(&optimalWatcher_, &QFutureWatcher<OptimalCache::Results>::finished,
          this, [this] {
            // Results worked out for holds that have changed since are
            // dropped.
            optimal_.store(optimalJob_, optimalWatcher_.result());
            refreshAttempts();
          });

  start();
}

MoveCountPage::~MoveCountPage() { optimalWatcher_.waitForFinished(); }

void MoveCountPage::showModeWidgets(bool virtualCube) {
  bool typed = !virtualCube; // the count is typed in
  display_->setVisible(true);
  display_->setToolTip(typed ? tr("The move count you are typing")
                             : tr("Moves so far (HTM)"));
}

std::vector<AttemptColumn> MoveCountPage::columns() const {
  auto col = [](const QString &title, bool fit = false) {
    AttemptColumn c;
    c.id = title;
    c.title = title;
    c.fit = fit;
    return c;
  };
  AttemptColumn opt = col(tr("Opt"));
  opt.available = hasOptimal();
  return {col("#", true), col(tr("Moves")), opt, col("ao5"), col("ao12")};
}

void MoveCountPage::addCells(std::vector<AttemptCell> &cells,
                             const MoveCountAttempt &a, const QString &ao5,
                             const QString &ao12) const {
  std::optional<int> best =
      hasOptimal() ? optimalOf(a) : std::optional<int>(-1);
  QString opt = !best       ? QString("…")
                : *best < 0 ? QString("-")
                            : QString::number(*best);
  for (const QString &text : {qs(formatAttempt(a)), opt, ao5, ao12}) {
    AttemptCell cell;
    cell.text = text;
    cells.push_back(cell);
  }
}

void MoveCountPage::addStats(QString &html) {
  if (!hasOptimal()) {
    return;
  }
  // How far over the optimal the counted, finished attempts were.
  OverOptimal over =
      overOptimal(session().attempts,
                  [this](const MoveCountAttempt &a) { return optimalOf(a); });
  if (over.known > 0) {
    html += tr("<p>Over optimal: %1 on average; optimal %2 of %3</p>")
                .arg(QString("%1%2")
                         .arg(over.mean >= 0 ? "+" : "")
                         .arg(over.mean, 0, 'f', 2))
                .arg(over.optimal)
                .arg(over.known);
  }
  requestOptimal();
}

AttemptPage::Hint MoveCountPage::physicalHint() const {
  // The chips of Enter and Alt+3 record, as the keys do.
  auto *self = const_cast<MoveCountPage *>(this);
  return {tr("Type the number of moves your solution took."),
          {{tr("Enter"), tr("record"), [self] { self->recordTyped(false); }},
           {tr("Backspace"), tr("correct")},
           {tr("Esc"), tr("clear")},
           {tr("Alt+3"), tr("DNF"), [self] { self->recordTyped(true); }}}};
}

AttemptPage::Hint MoveCountPage::solvingHint() const {
  return {
      {},
      {{tr("Space"), tr("the step is done")}, {tr("Esc"), tr("start again")}}};
}

bool MoveCountPage::reached(const FaceletCube &cube) const {
  return stepReached(cube, session().step);
}

QString MoveCountPage::notReachedText() const {
  return tr("DNF: %1 wasn't done when you pressed Space")
      .arg(stepText(session().step));
}

QString MoveCountPage::retryNote(const MoveCountAttempt &was) const {
  if (hasOptimal()) {
    if (std::optional<int> best =
            heldDistance(parseAlgOrEmpty(was.scramble), session().step,
                         optimalHolds(settings_))) {
      return tr(", optimal %1").arg(*best);
    }
  }
  return {};
}

bool MoveCountPage::reviewable(const MoveCountAttempt &a) const {
  return !a.solution.empty() || !a.scramble.empty();
}

void MoveCountPage::describeReview(ReviewedSolve &solve, int index) const {
  solve.title = tr("%1 #%2").arg(stepText(session().step)).arg(index + 1);
  solve.step = session().step;
}

void MoveCountPage::addDetails(QList<QPair<QString, QString>> &rows,
                               const MoveCountAttempt &a) const {
  rows.append({tr("Step"), stepText(session().step)});
  if (std::optional<int> best = optimalOf(a); best && *best >= 0) {
    rows.append({tr("Optimal"), QString::number(*best)});
  }
}

std::function<std::optional<int>(const MoveCountAttempt &)>
MoveCountPage::optimalLookup() const {
  return [this](const MoveCountAttempt &a) { return optimalOf(a); };
}

QString MoveCountPage::suggestedName() const {
  return stepText(session().step);
}

QString MoveCountPage::newSessionLabel() const {
  return tr("Name (e.g. the step, such as EO Cross):");
}

void MoveCountPage::setUpSession(MoveCountSession &s, const QString &name,
                                 const MoveCountSession &was) {
  // A session named after a step is for that step.
  SolveStep step = stepFromName(ss(name)).value_or(was.step);
  s.scrambleType =
      step != s.step ? defaultScrambleType(step) : ss(typeBox_->currentType());
  s.step = step;
  s.cube = was.cube;
}

void MoveCountPage::sessionShown() {
  stepBox_->setCurrentIndex(
      stepBox_->findData(static_cast<int>(session().step)));
  QString step = stepText(session().step);
  QString name = qs(session().name);
  // "Virtual ZZ F2L" names its step already.
  stepLabel_->setText(name.contains(step, Qt::CaseInsensitive)
                          ? name
                          : tr("%1 (%2)").arg(name, step));
}

void MoveCountPage::settingsApplied() {
  // The holds may have changed: results on the way are stale.
  optimal_.invalidate();
}

void MoveCountPage::holdShown(const QColor &colour) {
  display_->showText("0", colour);
}

void MoveCountPage::controlsEnabled(bool on) { stepBox_->setEnabled(on); }

bool MoveCountPage::hasOptimal() const {
  return hasOptimalSolver(session().step);
}

std::optional<int> MoveCountPage::optimalOf(const MoveCountAttempt &a) const {
  return optimal_.lookup(session().step, a.scramble, settings_.optimalAnyHold);
}

void MoveCountPage::requestOptimal() {
  if (optimalWatcher_.isRunning() || !hasOptimal()) {
    return;
  }
  std::optional<OptimalCache::Job> job =
      optimal_.missing(session().step, settings_.optimalAnyHold,
                       optimalHolds(settings_), session().attempts);
  if (!job) {
    return;
  }
  optimalJob_ = *job;
  optimalWatcher_.setFuture(
      QtConcurrent::run([job = *job] { return OptimalCache::run(job); }));
}

void MoveCountPage::recordTyped(bool dnf) {
  apply(flow_.recordTyped(dnf, feed_.current().has_value()));
}

} // namespace vcube
