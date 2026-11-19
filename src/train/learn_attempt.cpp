#include "train/learn_attempt.hpp"

#include <algorithm>

namespace vcube {

namespace {

const CaseProgress *find(const SetProgress &progress, const std::string &id) {
  auto it = progress.find(id);
  return it == progress.end() ? nullptr : &it->second;
}

} // namespace

void LearnAttempt::begin(const std::string &caseId, Kind kind) {
  clear();
  caseId_ = caseId;
  isNew_ = kind == Kind::New;
  practice_ = kind == Kind::Practice;
}

void LearnAttempt::startTiming(std::int64_t shownAtMs, bool spoiled) {
  shownAtMs_ = shownAtMs;
  recognitionMs_ = 0;
  recognitionSpoiled_ = spoiled;
}

void LearnAttempt::spoilRecognition() {
  if (recognitionMs_ == 0) {
    recognitionSpoiled_ = true;
  }
}

void LearnAttempt::clear() {
  // The grade itself stays until the next case is graded.
  Grade grade = grade_, autoGrade = autoGrade_;
  *this = LearnAttempt();
  grade_ = grade;
  autoGrade_ = autoGrade;
}

std::string LearnAttempt::linkCase() const {
  return !links_.empty() ? links_.back().view.caseId : caseId_;
}

void LearnAttempt::startLink(const CaseView &view, const FaceletCube &start,
                             const CubieCube &state) {
  LearnLink link;
  link.view = view;
  link.start = start;
  link.state = state;
  links_.push_back(std::move(link));
  hinted_ = false;
  nextHinted_ = false;
  follower_ = AlgFollower(start, {});
}

void LearnAttempt::setLinkAlgs(std::vector<ViewAlg> algs) {
  if (links_.empty()) {
    return;
  }
  std::vector<Alg> parsed;
  for (const ViewAlg &alg : algs) {
    parsed.push_back(parseAlgOrEmpty(alg.text));
  }
  links_.back().algs = std::move(algs);
  follower_.setAlgs(std::move(parsed));
}

void LearnAttempt::turned(Move m, std::int64_t nowMs) {
  bool wasBegun = follower_.begun();
  follower_.move(m);
  // Recognition ends at the first turn of a layer off the U-D axis.
  if (!wasBegun && follower_.begun() && links_.size() == 1 &&
      recognitionMs_ == 0) {
    recognitionMs_ = std::max<std::int64_t>(1, nowMs - shownAtMs_);
  }
}

void LearnAttempt::restartCase() { follower_.restart(); }

void LearnAttempt::hint() {
  if (!follower_.begun()) {
    recognitionSpoiled_ = true; // the algs were seen first
  }
  hinted_ = true;
}

void LearnAttempt::nextMoveHint() {
  if (!follower_.begun()) {
    recognitionSpoiled_ = true;
  }
  nextHinted_ = true;
}

bool LearnAttempt::endLink(LinkEnd end, const std::optional<std::string> &next,
                           Guide guide, std::int64_t now, std::int64_t nowMs,
                           const LearnGrading &g) {
  const bool solved = end == LinkEnd::Solved;
  const bool guided = guide == Guide::EveryMove;
  if (links_.empty()) {
    return false;
  }
  SetProgress &progress = *g.progress;
  LearnLink &link = links_.back();
  const std::string id = link.view.caseId;
  const bool first = links_.size() == 1;
  link.over = true;
  link.solved = solved;
  link.reached = next.value_or(std::string());
  link.gaveUp = gaveUp_;
  link.moves = follower_.moves();
  link.setStep = !follower_.leftAt();
  bool wrong = false;
  if (g.chained) {
    // Any step counts that leaves fewer steps to go than there were.
    int before = g.stepsFrom(id);
    int left = solved ? 0 : next ? g.stepsFrom(*next) : before;
    link.closer = !gaveUp_ && left < before;
    link.stepsLeft = left;
    wrong = !link.closer;
  } else {
    wrong = !gaveUp_ && !follower_.onAlg();
  }
  link.grade =
      learnGrade({.hinted = hinted_, .gaveUp = gaveUp_, .leftAlgs = wrong});
  if (link.grade == Grade::Good && nextHinted_) {
    link.grade = Grade::Hard;
  }
  GradeWhy why;
  why.gaveUp = gaveUp_;
  if (!gaveUp_ && wrong) {
    why.miss = g.chained                 ? GradeWhy::Miss::NotCloser
               : follower_.startedOver() ? GradeWhy::Miss::StartedOver
                                         : GradeWhy::Miss::LeftAlgs;
  }
  why.hint = hinted_;
  why.nextHint = !hinted_ && nextHinted_;
  why.kept = guided         ? GradeWhy::Kept::WalkedThrough
             : !g.chained   ? GradeWhy::Kept::FromMemory
             : link.setStep ? GradeWhy::Kept::SetStep
                            : GradeWhy::Kept::AnotherStep;
  link.why = why;

  const std::string card = g.cardId(id);
  const CaseProgress *cardNow = find(progress, card);
  bool record = false;
  if (first) {
    leftAlgs_ = wrong && !g.chained;
    grade_ = link.grade;
    autoGrade_ = link.grade;
    why_ = why;
    gradeTime_ = now;
    gradedCard_ = card;
    if (!practice_ && guided && (!cardNow || cardNow->card.introduced == 0)) {
      // A walkthrough isn't recall: the case is introduced and comes back
      // at its first learning step to be done from memory.
      CaseProgress &cp = progress[gradedCard_];
      cardBefore_ = cp.card;
      g.undo->push({g.setId,
                    caseId_,
                    isNew_,
                    false,
                    {{gradedCard_, cp.card, 0, {}}},
                    false});
      gradedCards_.push_back(gradedCard_);
      introduce(cp.card, now, *g.srs);
      walkedThrough_ = true;
      graded_ = true;
      return true;
    }
    record = !practice_;
  } else if (std::find(gradedCards_.begin(), gradedCards_.end(), card) !=
             gradedCards_.end()) {
    link.note = LearnLink::Note::AlreadyGraded;
  } else if (practice_) {
    link.note = LearnLink::Note::Practice;
  } else if (!cardNow || cardNow->card.introduced == 0) {
    link.note = LearnLink::Note::NotStarted;
  } else if (isDue(cardNow->card, now, *g.srs, g.dayEnd) ||
             link.grade == Grade::Again) {
    // A case passed on the way is graded when it is due, and counts as
    // forgotten whenever its step is missed.
    record = true;
  } else {
    link.note = LearnLink::Note::NotDue;
  }
  if (!record) {
    return false;
  }
  CaseProgress &cp = progress[card];
  if (first) {
    cardBefore_ = cp.card;
    attemptTime_ = now;
    g.undo->push(
        {g.setId, caseId_, isNew_, false, {{card, cp.card, now, {}}}, false});
    graded_ = true;
  } else if (LearnUndo *open = g.undo->open()) {
    open->cards.push_back({card, cp.card, now, {}});
  }
  gradedCards_.push_back(card);
  Attempt attempt{now, 0, link.grade == Grade::Again, AttemptMode::Learn};
  // Only a case recognised rightly (its alg kept to) has a recognition
  // time worth keeping.
  if (first && recognitionMs_ > 0 && !recognitionSpoiled_ &&
      link.grade != Grade::Again) {
    attempt.recognitionMs = recognitionMs_;
    attempt.executionMs =
        std::max<std::int64_t>(0, nowMs - shownAtMs_ - recognitionMs_);
  }
  cp.record(attempt);
  review(cp.card, link.grade, now, *g.srs, g.rng);
  if (!first) {
    link.note = LearnLink::Note::Graded;
    link.notedAt = now;
  }
  return true;
}

bool LearnAttempt::regrade(Grade grade, std::int64_t now,
                           const LearnGrading &g) {
  if (practice_ || gradeReplaced_ || !graded_ || gradedCard_.empty()) {
    return false;
  }
  grade_ = grade;
  if (!links_.empty()) {
    links_.front().grade = grade;
  }
  // The card the grade went to, even if learning by family was turned on
  // or off since.
  CaseProgress &cp = (*g.progress)[gradedCard_];
  cp.card = cardBefore_;
  if (attemptTime_ == 0) {
    // A walkthrough graded by hand: now it is an attempt.
    attemptTime_ = now;
    cp.record({now, 0, grade == Grade::Again, AttemptMode::Learn});
    if (LearnUndo *open = g.undo->open()) {
      for (auto &c : open->cards) {
        if (c.cardId == gradedCard_ && c.attempt == 0) {
          c.attempt = now;
        }
      }
    }
  } else {
    for (auto a = cp.attempts.rbegin(); a != cp.attempts.rend(); ++a) {
      if (a->mode == AttemptMode::Learn && a->timestamp == attemptTime_) {
        a->failed = grade == Grade::Again;
        break;
      }
    }
  }
  if (walkedThrough_) {
    walkedThrough_ = false;
    byHand_ = true;
    autoGrade_ = grade;
  }
  review(cp.card, grade_, now, *g.srs, g.rng);
  return true;
}

void LearnAttempt::recalled(bool knew, std::int64_t nowMs, std::int64_t now,
                            const LearnGrading &g) {
  recognitionMs_ = knew && !recognitionSpoiled_
                       ? std::max<std::int64_t>(1, nowMs - shownAtMs_)
                       : std::int64_t{0};
  gradedCard_ = g.cardId(caseId_);
  const CaseProgress *cp = find(*g.progress, gradedCard_);
  cardBefore_ = cp ? cp->card : SrsCard{};
  gradeTime_ = now;
  grade_ = autoGrade_ = knew ? Grade::Good : Grade::Again;
}

void LearnAttempt::gradeRecognition(Grade grade, std::int64_t now,
                                    const LearnGrading &g) {
  CaseProgress &cp = (*g.progress)[gradedCard_];
  LearnUndo u{g.setId, caseId_, false, true, {{gradedCard_, cp.card, now, {}}},
              false};
  Attempt a{now, 0, grade == Grade::Again, AttemptMode::Learn};
  a.recognitionMs = recognitionMs_;
  a.recognitionOnly = true;
  cp.record(a);
  review(cp.card, grade, now, *g.srs, g.rng);
  g.undo->push(std::move(u), false);
}

} // namespace vcube
