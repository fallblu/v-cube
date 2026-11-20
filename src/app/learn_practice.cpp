#include "app/learn_page.hpp"
#include "app/ui_text.hpp"

#include "app/case_history_dialog.hpp"
#include "app/ui_state.hpp"
#include "app/widgets.hpp"

#include <QPushButton>
#include <QSpinBox>

namespace vcube {

// --- recognition reviews and the quiz ----------------------------------------

void LearnPage::beginRecognition(const std::string &caseId) {
  const AlgSet *s = set();
  const AlgCase *c = s ? s->findCase(caseId) : nullptr;
  const CaseGroup *g = s ? s->groupOf(caseId) : nullptr;
  if (!c || !g || !infoOf(caseId)) {
    return;
  }
  if (!ready_) {
    holdBack([this, caseId] { beginRecognition(caseId); });
    return;
  }
  idleTimer_.stop();
  Hold hold = pickHold();
  Mirror world = pickWorld(caseId);
  CubieCube start = setUp(caseId, world, FreePieces::Scrambled);
  current_ = Current{caseId, start, hold, false, false, world};
  last_ = cardId(caseId);
  undo_.close();
  attempt_.begin(caseId, LearnAttempt::Kind::Known);
  stage_->clearCase();
  cubeView_->setColourMap(colourMapOf(hold));
  cube_ = start.toFacelets();
  cubeView_->setState(cube_);
  startLink(viewOfCase(*s, *g, *c, start, world)
                .value_or(CaseView{caseId, false, 0}));
  guide_.reset();
  clearCaseSelection();
  std::int64_t shown = monotonicMs();
  attempt_.startTiming(shown);
  session_.startRecognising(shown);
  setPhase(Phase::Recognise);
}

void LearnPage::recalled(bool knew) {
  if (phase() != Phase::Recognise || !current_) {
    return;
  }
  attempt_.recalled(knew, monotonicMs(), unixNow(), grading());
  setPhase(Phase::Recalled);
  showReveal();
  selectInTree(current_->caseId);
  showGrade();
}

void LearnPage::gradeRecognition(Grade grade) {
  if (phase() != Phase::Recalled || !current_ ||
      attempt_.gradedCard().empty()) {
    return;
  }
  attempt_.gradeRecognition(grade, unixNow(), grading());
  saveProgress();
  updateCardRows(current_->caseId);
  nextCase();
}

void LearnPage::nextQuiz() {
  if (!ready_) {
    holdBack([this] { nextQuiz(); });
    return;
  }
  current_.reset();
  attempt_.clear();
  guide_.reset();
  showGuide();
  reveal_->clear();
  gradeRow_->setVisible(false);
  stage_->hideFamily();
  const AlgSet *s = set();
  quiz_ = s ? LearnQuiz::pick(
                  *s, tickedEligible(),
                  [this](const std::string &id) {
                    return statusOf(cardOf(id)) != CaseStatus::Unstarted;
                  },
                  last_, rng_)
            : std::nullopt;
  if (!quiz_) {
    stage_->hideQuiz();
    cube_ = FaceletCube();
    cubeView_->setState(cube_);
    say(tr("The quiz needs two or more ticked cases with algs (cases you "
           "have started come first)."));
    setPhase(Phase::NoCase);
    return;
  }
  const std::string id = quiz_->caseId();

  Hold hold = pickHold();
  CubieCube start = setUp(id, Mirror{}, FreePieces::Scrambled);
  current_ = Current{id, start, hold, false, true, Mirror{}};
  last_ = id;
  cubeView_->setColourMap(colourMapOf(hold));
  CubeLook look = cubeView_->look();
  look.yaw = yawFor(id, Mirror{});
  cubeView_->finishAnimations();
  cubeView_->setLook(look);
  cube_ = start.toFacelets();
  cubeView_->setState(cube_);

  stage_->setQuizChoices(quizChoiceNames(*quiz_, *s));
  clearCaseSelection();
  quiz_->shown(monotonicMs());
  session_.setQuizChoices(quiz_->choices());
  setPhase(Phase::Quiz);
}

void LearnPage::answerQuiz(std::size_t choice) {
  if (phase() != Phase::Quiz || !current_ || !quiz_ ||
      choice >= quiz_->choices()) {
    return;
  }
  const LearnQuiz::Result result = quiz_->answered(choice, monotonicMs());
  session_.answered(result.right, result.ms);
  stage_->showQuizAnswer(quiz_->answer(), choice);
  reveal_->setText(algsHtml(current_->caseId, current_->start));
  selectInTree(current_->caseId);
  setPhase(Phase::QuizDone);
  say(quizResultText(*quiz_, set(), result),
      {{tr("Space"), tr("another one")}});
  statusLabel_->setStyleSheet(result.right ? QString()
                                           : "color: " + kWarnColour.name());
  updateHeader();
}

void LearnPage::startContext() {
  AlgSet *s = set();
  if (!s || !pairSet_) {
    return;
  }
  ContextPractice practice(
      *s, slotsSpin_->value(), rng_, [this](const std::string &id) {
        return statusOf(cardOf(id)) != CaseStatus::Unstarted;
      });
  idleTimer_.stop();
  current_.reset();
  guide_.reset();
  attempt_.clear();
  stage_->hideQuiz();
  gradeRow_->setVisible(false);
  practice.setHold(pickHold());
  context_ = std::move(practice);
  contextButton_->setText(tr("Back to learning"));
  learnNextButton_->setEnabled(false);
  cubeView_->setColourMap(colourMapOf(context_->hold()));
  CubeLook look = cubeView_->look();
  look.yaw = CubeLook{}.yaw;
  cubeView_->setLook(look);
  reveal_->clear();
  clearCaseSelection();
  resetContext();
}

void LearnPage::resetContext() {
  if (!context_) {
    return;
  }
  context_->reset();
  cube_ = context_->start().toFacelets();
  cubeView_->setState(cube_);
  reveal_->clear();
  setPhase(Phase::Context);
}

void LearnPage::finishContext(ContextPractice::End end) {
  if (!context_) {
    return;
  }
  context_->finish(end);
  setPhase(Phase::ContextDone);
  showContextResult();
}

void LearnPage::leaveContext() {
  context_.reset();
  contextButton_->setText(tr("Practise in context"));
  reveal_->clear();
  updateQueue();
  nextCase();
}

void LearnPage::showContextStatus() {
  if (!context_) {
    return;
  }
  statusLabel_->setStyleSheet({});
  bool done = phase() != Phase::Context;
  QList<KeyHint> keys;
  if (done) {
    keys = {{tr("Space"), tr("another one")},
            {tr("Esc"), tr("this one again")},
            {tr("Backspace"), tr("back to learning")}};
  } else {
    keys = {{tr("Tab"), tr("cheapest order")},
            {tr("Esc"), tr("start again")},
            {tr("Shift+Space"), tr("give up")},
            {tr("Backspace"), tr("back to learning")}};
  }
  say(contextStatusText(*context_, done), keys);
}

void LearnPage::showContextResult() {
  const AlgSet *s = set();
  if (!context_ || !s) {
    return;
  }
  reveal_->setText(
      contextResultHtml(*context_, *s, phase() == Phase::ContextDone,
                        palette().color(QPalette::PlaceholderText).name()));
}

} // namespace vcube
