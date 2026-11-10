#include "app/untimed_page.hpp"

#include "app/review_dialog.hpp"
#include "train/solve_review.hpp"
#include "train/step_solver.hpp"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace vcube {

UntimedPage::UntimedPage(AppSettings &settings, MoveCountStore &store,
                         AlgLibrary &library, QWidget *parent)
    : AttemptPage(settings, store, library, "untimed", false, parent) {
  QWidget *right = cubeView_->parentWidget();
  algs_ = new ScrambleAlgs(library, "untimed/showAlgs", right);
  setTypeBox(new ScrambleTypeBox(library, typeRow()), algs_->button());
  centre()->insertWidget(centre()->count() - 1, algs_->label());

  algPanel_ = new QWidget(right);
  auto *algLayout = new QVBoxLayout(algPanel_);
  algLayout->setContentsMargins(0, 0, 0, 0);
  auto *algRow = new QHBoxLayout;
  algEdit_ = new QLineEdit(algPanel_);
  algEdit_->setFocusPolicy(Qt::ClickFocus);
  algEdit_->setPlaceholderText(
      tr("After a solve: type an alg and press Enter to apply it"));
  algEdit_->installEventFilter(this);
  auto *applyButton = flatButton(tr("Apply"), algPanel_);
  algRow->addWidget(new QLabel(tr("Alg:"), algPanel_));
  algRow->addWidget(algEdit_, 1);
  algRow->addWidget(applyButton);
  algLayout->addLayout(algRow);
  algError_ = new QLabel(algPanel_);
  algError_->setStyleSheet("color: #e66");
  algError_->setVisible(false);
  algLayout->addWidget(algError_);
  addBelowCentre(algPanel_);

  connect(
      &eoWatcher_, &QFutureWatcher<std::optional<int>>::finished, this, [this] {
        std::optional<int> best = eoWatcher_.result();
        if (!best || isBusy() || flow_.retry()) {
          return;
        }
        QString yours = eoYours_ ? QString::number(*eoYours_) : tr("not done");
        QString line =
            tr("EO Cross: yours %1, optimal %2").arg(yours).arg(*best);
        QString was = lastLabel_->text();
        lastLabel_->setText(was.isEmpty() ? line : was + " · " + line);
      });
  connect(algEdit_, &QLineEdit::returnPressed, this, &UntimedPage::applyAlg);
  connect(applyButton, &QPushButton::clicked, this, &UntimedPage::applyAlg);
  connect(algEdit_, &QLineEdit::textChanged, this,
          [this] { algError_->setVisible(false); });

  start();
}

UntimedPage::~UntimedPage() { eoWatcher_.waitForFinished(); }

bool UntimedPage::eventFilter(QObject *obj, QEvent *event) {
  if (obj == algEdit_ && event->type() == QEvent::KeyPress &&
      static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
    returnFocus();
    return true;
  }
  return AttemptPage::eventFilter(obj, event);
}

void UntimedPage::returnFocus() {
  if (algEdit_->hasFocus()) {
    algEdit_->clearFocus();
  }
}

void UntimedPage::showModeWidgets(bool virtualCube) {
  display_->setVisible(false);
  display_->setToolTip(tr("Moves so far (HTM)"));
  algPanel_->setVisible(virtualCube && typeRow()->isVisibleTo(this));
}

std::vector<AttemptColumn> UntimedPage::columns() const {
  auto col = [](const QString &title, bool fit = false) {
    AttemptColumn c;
    c.id = title;
    c.title = title;
    c.fit = fit;
    return c;
  };
  if (scramblesOnly()) {
    return {col("#", true), col(tr("Scramble")), col(tr("Date"), true)};
  }
  return {col("#", true), col(tr("Moves")), col("ao5"), col("ao12")};
}

void UntimedPage::addCells(std::vector<AttemptCell> &cells,
                           const MoveCountAttempt &a, const QString &ao5,
                           const QString &ao12) const {
  auto add = [&cells](const QString &text, bool right = true) {
    AttemptCell cell;
    cell.text = text;
    cell.alignRight = right;
    cells.push_back(cell);
  };
  if (scramblesOnly()) {
    add(qs(a.scramble), false);
    add(dateText(a.timestamp));
    return;
  }
  add(qs(formatAttempt(a)));
  add(ao5);
  add(ao12);
}

QString UntimedPage::physicalHint() const {
  return tr("Solve the scramble, then press Space (or →) for the next one. "
            "Backspace (or ←) goes back.");
}

QString UntimedPage::solvingHint() const {
  return settings_.autoStop
             ? tr("The solve ends when the cube is solved (or on Space). Esc "
                  "starts the scramble again.")
             : tr("Press Space when the cube is solved. Esc starts the "
                  "scramble again.");
}

bool UntimedPage::reached(const FaceletCube &cube) const {
  return cube.isSolved();
}

QString UntimedPage::notReachedText() const {
  return tr("DNF: the cube wasn't solved when you pressed Space");
}

QString UntimedPage::retryWorstTip() const {
  return tr("Retry the DNFs and the longest solves, one after another");
}

void UntimedPage::describeReview(ReviewedSolve &solve, int index) const {
  solve.title = tr("Solve #%1").arg(index + 1);
}

QString UntimedPage::suggestedName() const {
  return tr("Session %1").arg(store_.sessions().size() + 1);
}

QString UntimedPage::newSessionLabel() const { return tr("Name:"); }

void UntimedPage::setUpSession(MoveCountSession &s, const QString &name,
                               const MoveCountSession &was) {
  (void)name;
  s.scrambleType = ss(typeBox_->currentType());
  s.step = was.step;
  s.cube = was.cube;
}

const GeneratedScramble *UntimedPage::historyScramble() const {
  return onVirtualCube() ? nullptr : history_.shown();
}

QString UntimedPage::scrambleText(const GeneratedScramble &s) const {
  if (!history_.shown()) {
    return s.text;
  }
  return tr("%1\n(done %2 back)").arg(s.text).arg(history_.stepsBack());
}

void UntimedPage::settingsApplied() { algs_->refresh(); }

void UntimedPage::typesRefreshed() { algs_->refresh(); }

void UntimedPage::scrambleTypeChanged(const QString &type, bool fresh) {
  (void)fresh;
  algs_->setType(type);
}

void UntimedPage::scrambleShown(const GeneratedScramble *shown) {
  algs_->setScramble(shown);
}

void UntimedPage::phaseShown(bool done) {
  algEdit_->setEnabled(done);
  if (!done) {
    returnFocus();
    algError_->setVisible(false);
  }
}

void UntimedPage::focusModeChanged(bool on) {
  algPanel_->setVisible(!on && onVirtualCube());
}

bool UntimedPage::toggleAlgs() { return algs_->toggle(); }

void UntimedPage::solveRecorded(const MoveCountAttempt &a) {
  // The optimal EO Cross of the solve's scramble, and the solve's own.
  ParseResult scramble = parseAlg(a.scramble);
  ParseResult moves = parseAlg(a.solution);
  if (!scramble || !moves || eoWatcher_.isRunning()) {
    return;
  }
  FaceletCube start;
  start.apply(scramble.alg);
  eoYours_.reset();
  for (const StepMark &m : stepMarks(start, moves.alg, {SolveStep::EOCross},
                                     settings_.simplifiedCounts)) {
    eoYours_ = m.htm;
  }
  std::vector<Hold> holds =
      settings_.optimalAnyHold
          ? std::vector<Hold>(allHolds().begin(), allHolds().end())
          : withBothEoAxes(settings_.neutrality.holds);
  Alg alg = scramble.alg;
  eoWatcher_.setFuture(QtConcurrent::run(
      [alg, holds] { return heldDistance(alg, SolveStep::EOCross, holds); }));
}

void UntimedPage::forward() {
  // Forward through the scrambles gone back to, then the scramble shown is
  // done and kept.
  if (history_.forward()) {
    showScramble();
    return;
  }
  const auto &current = feed_.current();
  if (!current) {
    return;
  }
  record(std::nullopt, false);
  history_.push(*current);
  feed_.advance(); // shows the next scramble
}

void UntimedPage::back() {
  if (history_.back()) {
    showScramble();
  }
}

void UntimedPage::applyAlg() {
  QString text = algEdit_->text().trimmed();
  if (text.isEmpty() || flow_.phase() != AttemptFlow::Phase::Done) {
    returnFocus();
    return;
  }
  ParseResult parsed = parseAlg(ss(text));
  if (!parsed) {
    algError_->setText(tr("Can't read this alg at character %1: %2")
                           .arg(parsed.error->position + 1)
                           .arg(qs(parsed.error->message)));
    algError_->setVisible(true);
    return; // keep the field focused to fix it
  }
  Alg applied;
  for (Move m : parsed.alg) {
    if (attempt_.turn(m)) {
      applied.push_back(m);
    }
  }
  cubeView_->animateSequence(applied);
  algError_->setVisible(false);
  algEdit_->clear();
  returnFocus();
}

} // namespace vcube
