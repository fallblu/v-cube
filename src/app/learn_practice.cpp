#include "app/learn_page.hpp"
#include "app/ui_text.hpp"

#include "app/case_history_dialog.hpp"
#include "app/ui_state.hpp"
#include "app/widgets.hpp"

#include <QActionGroup>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace vcube {

namespace {

// Choices in "which case is this?".
constexpr std::size_t kQuizChoices = 6;

} // namespace

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
  reveal_->clear();
  gradeRow_->setVisible(false);
  stage_->hideFamily();
  stage_->hideQuiz();
  cubeView_->setColourMap(colourMapOf(hold));
  cube_ = start.toFacelets();
  cubeView_->setState(cube_);
  startLink(viewOfCase(*s, *g, *c, start, world)
                .value_or(CaseView{caseId, false, 0}));
  guide_.reset();
  clearCaseSelection();
  attempt_.startTiming(monotonicMs());
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
  std::vector<std::string> ticked =
      s ? tickedEligible() : std::vector<std::string>{};
  // Cases already started are asked first.
  std::vector<std::string> started;
  for (const auto &id : ticked) {
    if (statusOf(cardOf(id)) != CaseStatus::Unstarted) {
      started.push_back(id);
    }
  }
  const std::vector<std::string> &from = started.size() >= 2 ? started : ticked;
  if (from.size() < 2) {
    stage_->hideQuiz();
    cube_ = FaceletCube();
    cubeView_->setState(cube_);
    say(tr("The quiz needs two or more ticked cases with algs (cases you "
           "have started come first)."));
    setPhase(Phase::NoCase);
    return;
  }
  std::vector<std::string> candidates;
  for (const auto &id : from) {
    if (id != last_) {
      candidates.push_back(id);
    }
  }
  std::uniform_int_distribution<std::size_t> pickOne(0, candidates.size() - 1);
  std::string id = candidates[pickOne(rng_)];
  // The others from its group, the look-alikes, else from the rest.
  const CaseGroup *g = s->groupOf(id);
  std::vector<std::string> others;
  for (const auto &t : ticked) {
    if (t != id && s->groupOf(t) == g) {
      others.push_back(t);
    }
  }
  if (others.empty()) {
    for (const auto &t : from) {
      if (t != id) {
        others.push_back(t);
      }
    }
  }
  std::shuffle(others.begin(), others.end(), rng_);
  if (others.size() > kQuizChoices - 1) {
    others.resize(kQuizChoices - 1);
  }
  others.push_back(id);
  std::shuffle(others.begin(), others.end(), rng_);
  quizChoices_ = others;
  quizAnswer_ = static_cast<std::size_t>(
      std::find(others.begin(), others.end(), id) - others.begin());

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

  QStringList names;
  for (const auto &choice : quizChoices_) {
    const AlgCase *c = s->findCase(choice);
    names << (c ? qs(c->name) : QString());
  }
  stage_->setQuizChoices(names);
  clearCaseSelection();
  quizShownAtMs_ = monotonicMs();
  session_.setQuizChoices(quizChoices_.size());
  setPhase(Phase::Quiz);
}

void LearnPage::answerQuiz(std::size_t choice) {
  if (phase() != Phase::Quiz || !current_ || choice >= quizChoices_.size()) {
    return;
  }
  std::int64_t ms = monotonicMs() - quizShownAtMs_;
  bool right = choice == quizAnswer_;
  session_.answered(right, ms);
  stage_->showQuizAnswer(quizAnswer_, choice);
  reveal_->setText(algsHtml(current_->caseId, current_->start));
  selectInTree(current_->caseId);
  setPhase(Phase::QuizDone);
  const AlgCase *c = set() ? set()->findCase(current_->caseId) : nullptr;
  say(right ? tr("Right, in %1 s.").arg(secondsText(static_cast<double>(ms)))
            : tr("It was %1.").arg(c ? qs(c->name) : QString()),
      {{tr("Space"), tr("another one")}});
  statusLabel_->setStyleSheet(right ? QString()
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
  say(context_->statusText(done), keys);
}

void LearnPage::showContextResult() {
  const AlgSet *s = set();
  if (!context_ || !s) {
    return;
  }
  reveal_->setText(
      context_->resultHtml(*s, phase() == Phase::ContextDone,
                           palette().color(QPalette::PlaceholderText).name()));
}

} // namespace vcube
