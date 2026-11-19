#include "app/learn_page.hpp"
#include "app/ui_text.hpp"

#include "app/case_history_dialog.hpp"
#include "app/train_keys.hpp"
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

// Every reason a case went as it did, e.g. both a hint and leaving the
// algs.
QString reasonsText(const GradeWhy &w) {
  QStringList reasons;
  if (w.gaveUp) {
    reasons << LearnPage::tr("gave up");
  } else if (w.miss != GradeWhy::Miss::None) {
    reasons << (w.miss == GradeWhy::Miss::NotCloser
                    ? LearnPage::tr("the step didn't get closer")
                : w.miss == GradeWhy::Miss::StartedOver
                    ? LearnPage::tr("started over")
                    : LearnPage::tr("left the algs"));
  }
  if (w.hint) {
    reasons << LearnPage::tr("used a hint");
  } else if (w.nextHint) {
    reasons << LearnPage::tr("used the next-move hint");
  }
  if (reasons.isEmpty()) {
    switch (w.kept) {
    case GradeWhy::Kept::WalkedThrough:
      reasons << LearnPage::tr("walked through");
      break;
    case GradeWhy::Kept::FromMemory:
      reasons << LearnPage::tr("solved from memory");
      break;
    case GradeWhy::Kept::SetStep:
      reasons << LearnPage::tr("the set's step");
      break;
    case GradeWhy::Kept::AnotherStep:
      reasons << LearnPage::tr("another step that gets closer");
      break;
    }
  }
  return reasons.join(LearnPage::tr(" and "));
}

} // namespace

CubieCube LearnPage::setUp(const std::string &caseId, Mirror world,
                           FreePieces pieces) {
  const CaseInfo *info = infoOf(caseId);
  const AlgSet *s = set();
  const CaseGroup *g = s ? s->groupOf(caseId) : nullptr;
  if (!info || !g) {
    return {};
  }
  // From a random side, as in Drill, when the set asks for random U turns,
  // and with the pieces the step leaves free anywhere when they are scrambled.
  return withRandomAufs(
      *s, setUpCase(info->state, s->specFor(*g).free, world, pieces, rng_),
      rng_);
}

void LearnPage::holdBack(std::function<void()> start) {
  pending_ = std::move(start);
  idleTimer_.stop();
  current_.reset();
  attempt_.clear();
  // No walkthrough is up. (Said to the stage directly: showGuide() right
  // after the reset makes GCC warn about the guide it has just dropped.)
  guide_.reset();
  stage_->showGuide(nullptr);
  reveal_->clear();
  gradeRow_->setVisible(false);
  stage_->hideFamily();
  stage_->hideQuiz();
  stage_->setActions({});
  cube_ = FaceletCube();
  cubeView_->setColourMap(colourMapOf(settings_.neutrality.main));
  cubeView_->clearMarks();
  cubeView_->setState(cube_);
  clearCaseSelection();
  say(tr("Ready when you are: a case is timed from the moment it is shown."),
      {{tr("Space"), tr("start")}});
  setPhase(Phase::NoCase);
}

void LearnPage::begin(const std::string &caseId, LearnAttempt::Kind kind) {
  const bool isNew = kind == LearnAttempt::Kind::New;
  const bool practice = kind == LearnAttempt::Kind::Practice;
  const CaseInfo *info = infoOf(caseId);
  const AlgSet *s = set();
  const AlgCase *c = s ? s->findCase(caseId) : nullptr;
  const CaseGroup *g = s ? s->groupOf(caseId) : nullptr;
  if (!info || !c || !g) {
    return;
  }
  if (!ready_) {
    holdBack([this, caseId, kind] { begin(caseId, kind); });
    return;
  }
  if (context_) {
    context_.reset();
    contextButton_->setText(tr("Practise in context"));
  }
  idleTimer_.stop();
  stage_->hideQuiz();
  Hold hold = pickHold();
  Mirror world = pickWorld(caseId);
  // A reduction set's cases come up with the step's free pieces anywhere,
  // as they do after a step, and so do learned cases (so that e.g. an OLL
  // isn't known by its permutation); a new case keeps the picture it was
  // learned from.
  const CaseProgress *cp = cardOf(caseId);
  bool reviewed = cp && (cp->card.state == CardState::Review ||
                         cp->card.state == CardState::Relearning);
  CubieCube start = setUp(caseId, world,
                          s->reduction || reviewed ? FreePieces::Scrambled
                                                   : FreePieces::AsDrawn);
  current_ = Current{caseId, start, hold, isNew, practice, world};
  last_ = cardId(caseId);
  undo_.close();
  attempt_.begin(caseId, kind);
  linkNote_.clear();
  reveal_->clear();
  gradeRow_->setVisible(false);
  stage_->hideFamily();
  cubeView_->setColourMap(colourMapOf(hold));
  cube_ = start.toFacelets();
  cubeView_->setState(cube_);
  startLink(viewOfCase(*s, *g, *c, start, world)
                .value_or(CaseView{caseId, false, 0}));
  // The list doesn't name the case before it is revealed.
  clearCaseSelection();
  // A guided set walks through a case the first time it comes up.
  Phase p =
      guide_ && !practice && firstSight(caseId) ? Phase::Demo : Phase::Solving;
  attempt_.startTiming(monotonicMs(), p == Phase::Demo);
  setPhase(p);
  if (p == Phase::Demo) {
    selectInTree(caseId);
  }
}

// --- undo --------------------------------------------------------------------

void LearnPage::undoLast() {
  std::optional<LearnUndo> popped = undo_.pop();
  if (!popped) {
    emit message(tr("Nothing to take back"));
    return;
  }
  LearnUndo u = std::move(*popped);
  if (!library_.find(u.setId)) {
    emit message(tr("The set of that grade is gone"));
    return;
  }
  takeBack(library_.progress(u.setId), u);
  if (!library_.saveProgress(u.setId)) {
    emit message(tr("Could not save progress for %1").arg(qs(u.setId)));
  }
  if (u.setId != setId_) {
    if (!acrossSets()) {
      emit message(tr("Took back a grade in another set"));
      return;
    }
    showSet(u.setId);
    adoptSet();
  }
  updateAllRows();
  if (u.fromList) {
    // Nothing came up for it: whatever is on the cube stays.
    int n = static_cast<int>(u.cards.size());
    emit message(plural(n, tr("Took back the change to %n case"),
                        tr("Took back the change to %n cases")));
    if (phase() == Phase::NoCase) {
      nextCase();
    }
    return;
  }
  const AlgCase *c = set() ? set()->findCase(u.caseId) : nullptr;
  emit message(
      tr("Took back the grade of %1").arg(c ? qs(c->name) : QString()));
  if (context_ || !eligible(u.caseId)) {
    if (phase() == Phase::NoCase) {
      nextCase();
    }
    return;
  }
  // The case comes up again to be done over.
  if (u.recognition) {
    beginRecognition(u.caseId);
  } else {
    begin(u.caseId,
          u.isNew ? LearnAttempt::Kind::New : LearnAttempt::Kind::Known);
  }
}

bool LearnPage::chained() const {
  const AlgSet *s = set();
  return s && s->reduction;
}

std::string LearnPage::linkCase() const {
  return attempt_.hasLinks() ? attempt_.lastLink()->view.caseId
         : current_          ? current_->caseId
                             : std::string();
}

LearnGrading LearnPage::grading() {
  LearnGrading g;
  g.progress = &progress();
  g.undo = &undo_;
  g.srs = &settings_.srs;
  g.rng = &rng_;
  g.setId = setId_;
  g.chained = chained();
  g.dayEnd = localDayEnd();
  g.cardId = [this](const std::string &id) { return cardId(id); };
  g.stepsFrom = [this](const std::string &id) { return stepsFrom(id); };
  return g;
}

PieceMask LearnPage::linkFree() const {
  const AlgSet *s = set();
  const CaseGroup *g = s ? s->groupOf(linkCase()) : nullptr;
  if (!g || !current_) {
    return {};
  }
  return mirrorMask(s->specFor(*g).free, current_->world);
}

void LearnPage::startLink(const CaseView &view) {
  attempt_.startLink(
      view, cube_,
      CubieCube::fromFacelets(oriented(cube_)).value_or(current_->start));
  const std::size_t links = attempt_.links().size();
  if (links > 1) {
    reveal_->clear(); // a hint for the last case isn't this one's
  }
  // In 3D, a mirrored case is looked at from its slot's side (from the
  // front, as a back slot is done). A case whose algs are done as
  // written, such as an OCLL a mirrored step stops at, is looked at from
  // the group's own corner (the front right), as its algs are.
  CubeLook look = cubeView_->look();
  double yaw = yawFor(view.caseId, view.mirrored ? current_->world : Mirror{});
  viewMoved_ = links > 1 && look.yaw != yaw && !view.mirrored;
  if (look.yaw != yaw) {
    look.yaw = yaw;
    cubeView_->finishAnimations();
    cubeView_->setLook(look);
  }
  guide_.reset();
  refreshLink();
  // A case passed on the way that was never learned has its step shown.
  if (links > 1 && guide_ && firstSight(view.caseId)) {
    guide_->follow = true;
  }
}

void LearnPage::refreshLink() {
  const AlgSet *s = set();
  const LearnLink *link = attempt_.lastLink();
  if (!link || !s || !current_) {
    return;
  }
  const AlgCase *c = s->findCase(link->view.caseId);
  const CaseGroup *g = s->groupOf(link->view.caseId);
  const CaseInfo *info = infoOf(link->view.caseId);
  std::vector<ViewAlg> algs;
  if (c && g && info) {
    algs = algsFor(*s, *g, *c, info->analysis, link->view, link->state,
                   current_->world);
  }
  attempt_.setLinkAlgs(std::move(algs));
  bool follow = guide_ && guide_->follow;
  buildGuide();
  if (guide_ && follow) {
    guide_->follow = true;
    trackGuide();
  }
}

bool LearnPage::guided() const {
  const AlgSet *s = set();
  return s && s->guided;
}

bool LearnPage::firstSight(const std::string &caseId) const {
  const CaseProgress *cp = cardOf(caseId);
  return !cp || cp->card.introduced == 0;
}

bool LearnPage::inLearningSteps(const std::string &caseId) const {
  const CaseProgress *cp = cardOf(caseId);
  return cp && cp->card.introduced != 0 &&
         (cp->card.state == CardState::Learning ||
          cp->card.state == CardState::Relearning);
}

void LearnPage::buildGuide() {
  guide_.reset();
  const LearnLink *link = attempt_.lastLink();
  if (!current_ || !guided() || !link || link->algs.empty()) {
    return;
  }
  AlgGuide guide(parseAlgOrEmpty(link->algs.front().text), link->start);
  if (!guide.empty()) {
    guide_ = std::move(guide);
  }
}

void LearnPage::stepDemo(int delta) {
  if (!guide_ || phase() != Phase::Demo) {
    return;
  }
  for (Move m : guide_->step(delta)) {
    cube_.apply(m);
    cubeView_->animateMove(m);
  }
  showGuide();
}

void LearnPage::startFollowing() {
  if (!guide_) {
    setPhase(Phase::Solving);
    return;
  }
  resetCube();
  attempt_.restartCase();
  guide_->restart();
  guide_->follow = true;
  setPhase(Phase::Solving);
}

void LearnPage::trackGuide() {
  if (guide_) {
    guide_->track(cube_);
  }
}

void LearnPage::showGuide() {
  bool shown =
      guide_ && (phase() == Phase::Demo || (phase() == Phase::Solving &&
                                            (guide_->follow || guide_->next)));
  const bool solving = phase() == Phase::Solving;
  stage_->showGuide(shown ? &*guide_ : nullptr,
                    {.nextOnly = shown && solving && !guide_->follow,
                     .solving = solving,
                     .deviated = attempt_.follower().deviated()});
}

void LearnPage::resetCube() {
  if (!current_) {
    return;
  }
  // While a case is being solved: back to where it came up. Afterwards:
  // back to the start of the attempt, seen as the first case was.
  bool solving = phase() == Phase::Solving || phase() == Phase::Demo;
  const std::vector<LearnLink> &links = attempt_.links();
  cube_ = solving && !links.empty() ? links.back().start
                                    : current_->start.toFacelets();
  if (!solving && !links.empty()) {
    const CaseView &first = links.front().view;
    CubeLook look = cubeView_->look();
    double yaw =
        yawFor(first.caseId, first.mirrored ? current_->world : Mirror{});
    if (look.yaw != yaw) {
      look.yaw = yaw;
      cubeView_->finishAnimations();
      cubeView_->setLook(look);
    }
  }
  cubeView_->setState(cube_);
}

void LearnPage::setPhase(Phase p) {
  session_.setPhase(p);
  statusLabel_->setStyleSheet({});
  if (p != Phase::NoCase) {
    stage_->setActions({}); // the idle screen's offers
  }
  switch (p) {
  case Phase::NoCase:
    break;
  case Phase::Context:
  case Phase::ContextDone:
    showGuide();
    gradeRow_->setVisible(false);
    stage_->hideFamily();
    showContextStatus();
    break;
  case Phase::Demo:
    showGuide();
    say(tr("New case: step through its main alg, then try it yourself with "
           "each next move shown."),
        {{tr("→ ←"), tr("step")},
         {tr("Home"), tr("back to the start")},
         {tr("Space"), tr("try it")}});
    break;
  case Phase::Solving:
    showGuide();
    showSolvingStatus();
    break;
  case Phase::Done:
    showGuide();
    if (current_ && current_->practice) {
      say({}, {{tr("Space"), tr("next case")}, {tr("Esc"), tr("practise it")}});
    } else {
      say({}, {{tr("Space"), tr("next case")},
               {tr("1-4"), tr("change the grade")},
               {tr("Esc"), tr("practise it")},
               {tr("Ctrl+Z"), tr("take the grade back")}});
    }
    break;
  case Phase::Recognise:
    showGuide();
    say(tr("Recognise the case. Rotations and U and D turns look at it."),
        {{tr("Space"), tr("I recall its alg")},
         {tr("Shift+Space"), tr("I can't")}});
    break;
  case Phase::Recalled:
    showGuide();
    say(tr("Grade yourself."), {{tr("1-4"), tr("grade and go on")},
                                {tr("Space"), tr("the grade shown")},
                                {tr("Ctrl+Z"), tr("take a grade back")}});
    break;
  case Phase::Quiz:
    showGuide();
    say(tr("Which case is this? Rotations and U and D turns look at it."),
        {{tr("1-%1").arg(quiz_ ? quiz_->choices() : 0), tr("answer")}});
    break;
  case Phase::QuizDone:
    break;
  }
  updateHeader();
}

void LearnPage::showSolvingStatus() {
  std::string id = linkCase();
  bool later = attempt_.links().size() > 1;
  const AlgFollower &follower = attempt_.follower();
  bool nextMoveHint = guide_ && !guide_->follow && !guide_->next &&
                      !attempt_.hinted() && current_ && inLearningSteps(id);
  const QList<KeyHint> keys = {
      {tr("Tab"), nextMoveHint ? tr("next move") : tr("show the algs")},
      {tr("Shift+Space"), tr("give up")},
      {tr("Esc"), tr("reset")}};
  QString note = later && !linkNote_.isEmpty() ? linkNote_ + " " : QString();
  if (follower.deviated() && !chained()) {
    bool practice = current_ && current_->practice;
    QString what =
        follower.startedOver()
            ? (practice ? tr("Started over.")
                        : tr("Started over, so the case will be graded "
                             "Again."))
            : (practice ? tr("That isn't one of the algs.")
                        : tr("That isn't one of the algs, so the case will "
                             "be graded Again."));
    say(what, keys, true);
    return;
  }
  QString what;
  if (later) {
    what = guide_ && guide_->follow
               ? tr("You haven't learned this case yet: its step is shown.")
               : tr("Carry on from this case.");
    if (viewMoved_) {
      what += " " + tr("Its alg is done as written, so the cube is seen "
                       "from the front right again.");
    }
  } else if (current_ && current_->practice) {
    what = tr("Practice: the case isn't due, so it won't be graded.");
  } else if (guide_ && guide_->follow) {
    what = tr("Do the alg, one move at a time (not graded: the case comes "
              "back soon to do from memory).");
  } else if (current_ && current_->isNew) {
    what = tr("New case.");
  } else {
    what = chained() ? tr("Do the next step from memory.")
                     : tr("Solve the case from memory.");
  }
  say(note + what, keys);
}

void LearnPage::say(const QString &message, const QList<KeyHint> &keys,
                    bool warn) {
  statusLabel_->setText(message);
  statusLabel_->setStyleSheet(
      warn ? "color: " + statusColour(Status::Warning).name() : QString());
  stage_->setKeys(keys);
}

bool LearnPage::handleKey(QKeyEvent *e, bool press) {
  LearnSession::Step step = session_.key(sessionKey(e, press));
  perform(step);
  return step.handled;
}

LearnSession::KeyEvent LearnPage::sessionKey(const QKeyEvent *e,
                                             bool press) const {
  LearnSession::KeyEvent k = trainKeyEvent(e, press, KeyChords::Undo);
  k.move = settings_.keymap.lookup(e);
  return k;
}

void LearnPage::perform(const LearnSession::Step &step) {
  using A = LearnSession::Action;
  switch (step.action) {
  case A::None:
    break;
  case A::Undo:
    undoLast();
    break;
  case A::ToggleList:
    toggleList();
    break;
  case A::NextEarly:
  case A::NextCase:
    // Space: whatever was waiting for it starts now.
    ready_ = true;
    if (pending_) {
      std::function<void()> start = std::move(pending_);
      pending_ = nullptr;
      start();
      break;
    }
    // Checks again, and (early) takes a learning case if nothing is due.
    nextCase(step.action == A::NextEarly ? LearnAhead::Early
                                         : LearnAhead::WhenDue);
    break;
  case A::Turn:
    turn(*step.move);
    break;
  case A::Look:
    if (isRotation(step.move->type) || udTurn(cube_, *step.move)) {
      cube_.apply(*step.move);
      cubeView_->animateMove(*step.move);
    }
    break;
  case A::ResetCube:
    resetCube();
    break;
  case A::DemoStep:
    stepDemo(step.step);
    break;
  case A::DemoRestart:
    resetCube();
    if (guide_) {
      guide_->restart();
    }
    showGuide();
    break;
  case A::StartFollowing:
    startFollowing();
    break;
  case A::Hint:
    solvingHint();
    break;
  case A::GiveUp:
    attempt_.giveUp();
    endLink(LearnAttempt::LinkEnd::Unsolved, std::nullopt);
    finish();
    resetCube(); // so the alg can be tried
    break;
  case A::ResetCase:
    resetCube();
    attempt_.restartCase();
    if (guide_) {
      guide_->restart();
      showGuide();
    }
    showSolvingStatus();
    break;
  case A::Regrade:
    regrade(*step.grade);
    break;
  case A::Recalled:
    recalled(step.knew);
    break;
  case A::GradeRecognition:
    gradeRecognition(step.grade.value_or(attempt_.grade()));
    break;
  case A::Answer:
    answerQuiz(step.choice);
    break;
  case A::NextQuiz:
    nextQuiz();
    break;
  case A::ContextHint:
    // The set's cheapest order as a hint; nothing is graded anyway.
    showContextResult();
    break;
  case A::ContextGiveUp:
    finishContext(ContextPractice::End::GaveUp);
    break;
  case A::ContextReset:
    resetContext();
    break;
  case A::ContextLeave:
    leaveContext();
    break;
  case A::ContextNext:
    startContext();
    break;
  }
}

void LearnPage::solvingHint() {
  if (guide_ && !guide_->follow && !guide_->next && !attempt_.hinted() &&
      current_ && inLearningSteps(linkCase())) {
    // In the learning steps the first hint is the next move only.
    attempt_.nextMoveHint();
    guide_->next = true;
    trackGuide();
    showGuide();
    showSolvingStatus();
    return;
  }
  attempt_.hint();
  showReveal();
  if (attempt_.links().size() == 1 && current_) {
    selectInTree(current_->caseId);
  }
}

void LearnPage::turn(Move m) {
  cube_.apply(m);
  cubeView_->animateMove(m);
  if (phase() == Phase::Context && context_) {
    if (context_->turned(m, cube_)) {
      finishContext(ContextPractice::End::Solved);
    }
    return;
  }
  if (phase() == Phase::ContextDone) {
    return; // turning freely after the pairs
  }
  if (phase() != Phase::Solving || !current_) {
    return; // untimed practice after the case
  }
  bool wasOff = attempt_.follower().deviated();
  attempt_.turned(m, monotonicMs());
  if (guide_) {
    trackGuide();
    showGuide();
  }
  auto c = CubieCube::fromFacelets(oriented(cube_));
  const AlgSet *s = set();
  if (!c || !s) {
    return;
  }
  // The cube itself must reach the step's end state: a case that leaves
  // the last layer to be lined up (PLL, say) needs its last U turn too,
  // while one that leaves the last-layer permutation free (OLL) doesn't.
  if (withinMask(*c, linkFree())) {
    endLink(LearnAttempt::LinkEnd::Solved, std::nullopt);
    finish();
    turnUpright();
    return;
  }
  // A step of a reduction set is over when the cube shows a case again:
  // at the end of one of the case's algs, or wherever other moves get to
  // (an alg may pass through other cases on its way).
  const AlgFollower &follower = attempt_.follower();
  if (chained() && follower.begun() &&
      (follower.finished() || follower.leftAt())) {
    if (auto next = seenCase(*s, *c, current_->world);
        next && eligible(next->caseId)) {
      endLink(LearnAttempt::LinkEnd::Unsolved, next);
      startLink(*next);
      showGuide();
      showSolvingStatus();
      return;
    }
  }
  if (follower.deviated() != wasOff) {
    showSolvingStatus();
  }
}

void LearnPage::turnUpright() {
  FaceletCube upright = cube_;
  for (Move m : upright.normalizeOrientation()) {
    cube_.apply(m);
    cubeView_->animateMove(m);
  }
}

void LearnPage::endLink(LearnAttempt::LinkEnd end,
                        const std::optional<CaseView> &next) {
  if (!attempt_.hasLinks() || !current_ || !set()) {
    return;
  }
  const std::string id = attempt_.lastLink()->view.caseId;
  bool changed = attempt_.endLink(
      end, next ? std::optional<std::string>(next->caseId) : std::nullopt,
      guide_ && guide_->follow ? LearnAttempt::Guide::EveryMove
                               : LearnAttempt::Guide::None,
      unixNow(), monotonicMs(), grading());
  if (chained()) {
    const LearnLink &link = *attempt_.lastLink();
    const int left = link.stepsLeft;
    linkNote_ =
        link.gaveUp ? QString()
        : link.closer
            ? (left == 1 ? tr("Closer: 1 step left.")
                         : tr("Closer: %1 steps left.").arg(left))
            : (left == 1
                   ? tr("That didn't get closer: 1 step left.")
                   : tr("That didn't get closer: %1 steps left.").arg(left));
  }
  if (changed) {
    saveProgress();
    updateCardRows(id);
  }
}

void LearnPage::finish() {
  if (!current_ || !set()) {
    return;
  }
  setPhase(Phase::Done);
  showReveal();
  showGrade();
  selectInTree(current_->caseId);
}

void LearnPage::regrade(Grade grade) {
  if (phase() != Phase::Done || !current_ ||
      !attempt_.regrade(grade, unixNow(), grading())) {
    showGrade();
    return;
  }
  saveProgress();
  showGrade();
  if (chained()) {
    showReveal(); // the list's marks follow the grade
  }
  updateCardRows(current_->caseId);
}

void LearnPage::showFamily() {
  const std::vector<std::string> *members =
      current_ ? families_.familyFor(current_->caseId) : nullptr;
  bool shown = byFamily() &&
               (phase() == Phase::Done || phase() == Phase::Recalled) &&
               members && members->size() > 1;
  if (!shown) {
    stage_->hideFamily();
    return;
  }
  const AlgSet *s = set();
  std::vector<CaseStage::FamilyCase> cases;
  for (const auto &id : *members) {
    const CaseInfo *info = infoOf(id);
    const AlgCase *c = s ? s->findCase(id) : nullptr;
    if (!info || !c) {
      continue;
    }
    cases.push_back({caseDiagram(id, info->state, 84,
                                 colourMapOf(settings_.neutrality.main)),
                     qs(c->name),
                     info->passing.empty() ? tr("no alg")
                                           : qs(info->mainAlg).toHtmlEscaped(),
                     id == current_->caseId});
  }
  stage_->showFamily(cases);
}

QString LearnPage::chainHtml() const {
  const AlgSet *s = set();
  if (!s) {
    return {};
  }
  return linksHtml(*s, attempt_.links(), [this](const LearnLink &link) {
    switch (link.note) {
    case LearnLink::Note::None:
      break;
    case LearnLink::Note::AlreadyGraded:
      return tr("already graded in this attempt");
    case LearnLink::Note::Practice:
      return tr("practice, so not graded");
    case LearnLink::Note::NotStarted:
      return tr("not started yet, so not graded");
    case LearnLink::Note::NotDue:
      return tr("not due yet, so unchanged");
    case LearnLink::Note::Graded:
      return tr("%1 (%2) → next %3")
          .arg(tr(gradeName(link.grade)), reasonsText(link.why),
               dueText(cardOf(link.view.caseId), link.notedAt, localDayEnd()));
    }
    return QString();
  });
}

void LearnPage::showReveal() {
  showFamily();
  const AlgSet *s = set();
  if (!current_ || !s) {
    return;
  }
  if (phase() == Phase::Recalled) {
    // Every step in a set solved a step at a time.
    reveal_->setText(
        algsHtml(current_->caseId, current_->start, current_->world));
    return;
  }
  if (!attempt_.hasLinks()) {
    return;
  }
  if (phase() == Phase::Done && chained()) {
    reveal_->setText(chainHtml());
    return;
  }
  // The case being solved (a hint), or the case that was solved.
  const LearnLink &link = *attempt_.lastLink();
  QString html = viewAlgsHtml(*s, link.view, link.state, current_->world);
  const AlgFollower &follower = attempt_.follower();
  if (phase() == Phase::Done && attempt_.leftAlgs()) {
    QString moves = movesHtml(follower.moves(), follower.leftAt());
    html += "<p style='color:" + roleColour(Role::Muted).name() + "'>" +
            (follower.startedOver()
                 ? tr("Your moves after starting over: %1").arg(moves)
                 : tr("Your moves: %1").arg(moves));
    if (follower.leftAt()) {
      html += "<br>" + tr("The marked move is where you left the algs.");
    }
    html += "</p>";
  }
  reveal_->setText(html);
}

QString LearnPage::whyText() const {
  if (attempt_.byHand() || attempt_.grade() != attempt_.autoGrade()) {
    return tr("graded by hand");
  }
  if (phase() == Phase::Recalled) {
    std::int64_t ms = attempt_.recognitionMs();
    return ms > 0 ? tr("recalled in %1 s")
                        .arg(secondsText(static_cast<double>(ms)))
                  : tr("not recalled");
  }
  return reasonsText(attempt_.why());
}

void LearnPage::showGrade() {
  bool shown =
      (phase() == Phase::Done || phase() == Phase::Recalled) && current_;
  if (!shown) {
    gradeRow_->setVisible(false);
    return;
  }
  gradeRow_->setVisible(true);
  const Grade chosen = attempt_.grade();
  bool recalled = phase() == Phase::Recalled;
  bool buttons = recalled || (!current_->practice &&
                              !attempt_.gradeReplaced() && attempt_.graded());
  std::int64_t when = recalled ? unixNow() : attempt_.gradeTime();
  for (int g = 0; g < 4; ++g) {
    auto grade = static_cast<Grade>(g + 1);
    QPushButton *b = stage_->gradeButtons[static_cast<std::size_t>(g)];
    b->setVisible(buttons);
    // What the grade would give, from the card as it was before.
    b->setText(QString("%1 %2 · %3")
                   .arg(g + 1)
                   .arg(tr(gradeName(grade)))
                   .arg(intervalText(nextInterval(attempt_.cardBefore(), grade,
                                                  when, settings_.srs))));
    b->setChecked(!attempt_.walkedThrough() && chosen == grade);
  }
  const QString grey = roleColour(Role::GradeNone).name();
  auto outcome = [this](const QString &text, const QString &colour,
                        const QString &detail) {
    stage_->showOutcome(text, colour, detail);
  };
  QString why = whyText();
  const CaseProgress *card =
      progressOf(attempt_.gradedCard().empty() ? cardId(current_->caseId)
                                               : attempt_.gradedCard());
  QString next = dueText(card, unixNow(), localDayEnd());
  if (recalled) {
    outcome(tr(gradeName(chosen)), gradeColour(chosen),
            tr("%1. Grade yourself:").arg(why));
    return;
  }
  if (attempt_.gradeReplaced()) {
    outcome(tr("Changed"), grey, tr("from the case list → next %1").arg(next));
    return;
  }
  if (current_->practice) {
    outcome(chosen == Grade::Again ? tr("Missed") : tr("Well done"),
            chosen == Grade::Again ? gradeColour(Grade::Again) : grey,
            tr("Practice (%1), not graded; next review %2.").arg(why, next));
    return;
  }
  if (attempt_.walkedThrough()) {
    outcome(tr("Walked through"), roleColour(Role::GradeWalked).name(),
            tr("Not graded: it comes back in %1 to do from "
               "memory. 1-4 grade it now instead.")
                .arg(next));
    return;
  }
  if (!attempt_.graded()) {
    outcome(tr(gradeName(chosen)), grey, tr("(%1), not graded").arg(why));
    return;
  }
  outcome(tr(gradeName(chosen)), gradeColour(chosen),
          tr("%1 → next %2").arg(why, next));
}

} // namespace vcube
