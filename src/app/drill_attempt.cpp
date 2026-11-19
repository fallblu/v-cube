#include "app/drill_page.hpp"
#include "app/drill_private.hpp"
#include "app/page_parts.hpp"
#include "app/train_keys.hpp"
#include "app/ui_text.hpp"

#include "app/drill_history_dialog.hpp"
#include "app/ui_state.hpp"
#include "train/alg_follow.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>
#include <utility>

namespace vcube {

namespace {

// A practice stage solved is shown this long before the next one.
constexpr int kAdvanceMs = 700;

} // namespace

bool DrillPage::handleKey(QKeyEvent *e, bool press) {
  struct Update {
    DrillPage *page;
    ~Update() { page->updateControls(); }
  } update{this};
  bool scrambleShown =
      current_ && (onVirtualCube() || !current_->scramble.isEmpty());
  return driver_.key(flowKey(e, press), monotonicMs(),
                     scrambleShown ? Scramble::Shown : Scramble::Awaited);
}

DrillFlow::KeyEvent DrillPage::flowKey(const QKeyEvent *e, bool press) const {
  DrillFlow::KeyEvent k = trainKeyEvent(e, press, KeyChords::Undo);
  if (onVirtualCube()) {
    k.move = settings_.keymap.lookup(e);
  }
  return k;
}

void DrillPage::timedNext(bool fromIdle) {
  if (fromIdle && picker_.recapDone()) {
    picker_.restartRecap();
    recapFrom_ = session_.size();
  }
  nextCase();
}

void DrillPage::timedRotate(Move m) {
  if (attempt_.turn(m)) {
    cubeView_->animateMove(m);
  }
}

void DrillPage::timedStarted(std::int64_t now) {
  (void)now;
  if (onVirtualCube()) {
    attempt_.start();
  }
}

std::optional<DrillFlow::Turn> DrillPage::timedTurn(Move m, std::int64_t now) {
  (void)now;
  const DrillFlow::Turn turn = isRotation(m.type) ? DrillFlow::Turn::Rotation
                               : udTurn(attempt_.cube(), m)
                                   ? DrillFlow::Turn::UdFace
                                   : DrillFlow::Turn::OffUdFace;
  if (!attempt_.turn(m)) {
    return std::nullopt;
  }
  cubeView_->animateMove(m);
  followView();
  return turn;
}

void DrillPage::timedFinishAnimations() { cubeView_->finishAnimations(); }

void DrillPage::timedSay(const QString &text) {
  statusLabel_->setText(text);
  statusLabel_->show();
}

void DrillPage::timedCancelled(const QString &why) {
  showStatus();
  statusLabel_->setText((why + " " + statusLabel_->text()).trimmed());
  statusLabel_->show();
}

std::optional<CubieCube> DrillPage::cubeState() const {
  FaceletCube f = attempt_.cube();
  f.normalizeOrientation();
  return CubieCube::fromFacelets(f);
}

bool DrillPage::stepDone() const {
  const AlgSet *s = set();
  const CaseGroup *g = s && current_ ? s->groupOf(current_->caseId) : nullptr;
  if (!g) {
    return false;
  }
  auto c = cubeState();
  // As in Learn: the cube itself must reach the step's end state, so a
  // case that leaves the last layer to be lined up (PLL, say) needs its
  // last U turn too, while one that leaves the last-layer permutation free
  // (OLL) doesn't.
  return c && withinMask(*c, mirrorMask(s->specFor(*g).free, current_->world));
}

void DrillPage::startPractice(bool aided) {
  if (!current_) {
    return;
  }
  flow_.startPractice(aided);
  if (onVirtualCube()) {
    cubeView_->finishAnimations();
    resetCube();
    attempt_.start(); // every layer turns
  }
  if (aided) {
    showReveal();
  } else {
    reveal_->clear();
    setDiagram({});
  }
  showStatus();
  showResult();
}

void DrillPage::practiceTurn(Move m) {
  if (!attempt_.turn(m)) {
    return;
  }
  cubeView_->animateMove(m);
  followView();
  if (!flow_.practiceTurned(!isRotation(m.type),
                            [this] { return stepDone(); })) {
    return;
  }
  showStatus();
  if (settings_.drill.autoAdvance) {
    const bool aided = flow_.phase() == Phase::Aided;
    QTimer::singleShot(
        kAdvanceMs, this, [this, token = flow_.practiceToken(), aided] {
          if (token != flow_.practiceToken() || !flow_.practiceDone()) {
            return;
          }
          if (aided && flow_.phase() == Phase::Aided) {
            startPractice(false);
          } else if (!aided && flow_.phase() == Phase::Unaided) {
            nextCase();
          }
        });
  }
}

void DrillPage::finish(std::int64_t timeMs, DrillFlow::Result result) {
  const bool failed = result != DrillFlow::Result::Solved;
  if (!current_ || !set()) {
    return;
  }
  const AttemptMode mode = attemptMode();
  const CaseProgress *before = progressOf(current_->caseId);
  flow_.finished(timeMs, result, before ? before->best(mode) : std::nullopt,
                 monotonicMs());
  Attempt a{unixNow(), timeMs, failed, mode};
  // On the virtual cube: recognition from the case being set up to the
  // first turn off U and D, execution from there to the stop; only for a
  // case solved, which was recognised rightly.
  if (auto t = flow_.timing(timeMs, failed)) {
    a.recognitionMs = t->recognitionMs;
    a.executionMs = t->executionMs;
  }
  progress()[current_->caseId].record(a);
  sessionIndex_ =
      session_.add({current_->caseId, timeMs, failed, flow_.newBest()});
  picker_.requeue(current_->caseId, failed ? settings_.drill.requeueAfter : 0);
  missText_.clear();
  if (flow_.unfinished() && onVirtualCube()) {
    const AlgSet *s = set();
    const CaseGroup *g = s->groupOf(current_->caseId);
    auto c = cubeState();
    if (g && c) {
      StepMiss miss =
          stepMiss(*c, mirrorMask(s->specFor(*g).free, current_->world));
      missText_ =
          miss.auf != 0 ? tr("Off by an AUF: %1 would have finished it.")
                              .arg(qs(aufText(miss.auf)))
          : miss.pieces == 1
              ? tr("1 piece wasn't where the step leaves it when you stopped "
                   "the timer.")
              : tr("%1 pieces weren't where the step leaves them when you "
                   "stopped the timer.")
                    .arg(miss.pieces);
    }
  }
  saveProgress();
  selectInTree(current_->caseId);
  showStatus();
  showReveal();
  showResult();
  updateRow(current_->caseId);
  refreshSession();
}

void DrillPage::setWrong(bool wrong) {
  if (!current_ || !flow_.setWrong(wrong)) {
    showResult();
    return;
  }
  const bool failed = flow_.failed();
  missText_.clear();
  auto &attempts = progress()[current_->caseId].attempts;
  // The attempt just recorded is the last one in this drill mode.
  for (auto it = attempts.rbegin(); it != attempts.rend(); ++it) {
    if (it->mode == attemptMode()) {
      it->failed = failed;
      if (failed) {
        it->recognitionMs = 0; // a case failed wasn't recognised rightly
        it->executionMs = 0;
      }
      break;
    }
  }
  saveProgress();
  if (sessionIndex_) {
    session_.setFailed(*sessionIndex_, failed ? DrillSession::Mark::Failed
                                       : flow_.newBest()
                                           ? DrillSession::Mark::NewBest
                                           : DrillSession::Mark::Solved);
  }
  picker_.requeue(current_->caseId, failed ? settings_.drill.requeueAfter : 0);
  showStatus(); // what Space does next depends on the DNF
  showResult();
  updateRow(current_->caseId);
  refreshSession();
}

void DrillPage::deleteAttempt() {
  if (!current_ || !flow_.recorded()) {
    return;
  }
  deleted_.reset();
  std::optional<Attempt> attempt;
  auto &p = progress();
  if (auto it = p.find(current_->caseId); it != p.end()) {
    const std::vector<Attempt> &all = it->second.attempts;
    for (auto a = all.rbegin(); a != all.rend(); ++a) {
      if (a->mode == attemptMode()) {
        attempt = *a;
        break;
      }
    }
    it->second.removeLast(attemptMode());
  }
  saveProgress();
  std::optional<SessionAttempt> row;
  if (sessionIndex_ && *sessionIndex_ + 1 == session_.size()) {
    row = session_.attempts().back();
    session_.removeLast();
  }
  sessionIndex_.reset();
  picker_.requeue(current_->caseId, 0);
  const bool takenBack = flow_.deleted();
  if (!takenBack && attempt) {
    deleted_ = Deleted{current_->caseId, *attempt, row};
  }
  updateRow(current_->caseId);
  refreshSession();
  if (takenBack) {
    // Given up by mistake: the case is set up as it was, to try.
    missText_.clear();
    reveal_->clear();
    setDiagram({});
    resultRow_->setVisible(false);
    display_->showTime(0);
    clearCaseSelection();
    if (onVirtualCube()) {
      resetCube();
    }
    showStatus();
    statusLabel_->setText((tr("Give-up taken back; nothing was recorded.") +
                           " " + statusLabel_->text())
                              .trimmed());
    statusLabel_->show();
    return;
  }
  showStatus();
  showResult();
}

void DrillPage::restoreAttempt() {
  if (!current_ || !deleted_ || deleted_->caseId != current_->caseId ||
      deleted_->attempt.mode != attemptMode() || !flow_.restored()) {
    return;
  }
  const Deleted d = *std::exchange(deleted_, std::nullopt);
  progress()[d.caseId].record(d.attempt);
  saveProgress();
  if (d.row) {
    sessionIndex_ = session_.add(*d.row);
  }
  picker_.requeue(d.caseId,
                  d.attempt.failed ? settings_.drill.requeueAfter : 0);
  updateRow(d.caseId);
  refreshSession();
  showStatus();
  showResult();
}

void DrillPage::showResult() {
  if (!flow_.resultShown() || !current_) {
    resultRow_->setVisible(false);
    return;
  }
  const bool failed = flow_.failed();
  const bool gaveUp = flow_.gaveUp();
  const bool recorded = flow_.recorded();
  resultRow_->setVisible(true);
  const AlgSet *s = set();
  double target = s ? s->targetSeconds : 0.0;
  Solve shown;
  shown.timeMs = flow_.timeMs();
  shown.penalty = failed ? Penalty::Dnf : Penalty::None;
  QColor colour;
  if (!failed && target > 0) {
    colour = static_cast<double>(flow_.timeMs()) <= target * 1000.0
                 ? kPassColour
                 : kWarnColour;
  }
  display_->showText(
      gaveUp ? tr("DNF") : qs(formatSolve(shown, settings_.decimals)), colour);
  QString text;
  if (!recorded) {
    text = tr("Deleted");
  } else if (!failed) {
    text = flow_.newBest() ? tr("New best! Target %1 s").arg(target, 0, 'f', 2)
                           : tr("Target %1 s").arg(target, 0, 'f', 2);
  } else if (gaveUp) {
    text = tr("Given up");
  } else if (!onVirtualCube()) {
    text = tr("Wrong alg");
  } else if (flow_.unfinished()) {
    text = missText_.isEmpty()
               ? tr("The case wasn't solved when you stopped the timer.")
               : missText_;
  } else {
    text = tr("Marked DNF");
  }
  resultLabel_->setText(text);
}

void DrillPage::showReveal() {
  if (!current_) {
    return;
  }
  QString html = algsHtml(current_->caseId, current_->state, current_->world);
  // A virtual DNF shows what was done.
  if (onVirtualCube() && flow_.failed() && !attempt_.moves().empty() &&
      flow_.phase() == Phase::Revealed) {
    html += "<p style='color:" + roleColour(Role::Muted).name() + "'>" +
            tr("Your moves: %1")
                .arg(qs(toString(attempt_.solution())))
                .toHtmlEscaped() +
            "</p>";
  }
  reveal_->setText(html);
  setDiagram(caseDiagram(current_->caseId, current_->state, kDiagramSize,
                         colourMapOf(current_->hold), current_->world));
}

void DrillPage::setDiagram(const QPixmap &pixmap) {
  diagram_->setPixmap(pixmap);
  diagram_->setVisible(!pixmap.isNull());
}

} // namespace vcube
