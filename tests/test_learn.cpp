#include "train/alg_guide.hpp"
#include "train/learn_attempt.hpp"
#include "train/learn_session.hpp"

#include <catch2/catch_test_macros.hpp>

#include <map>

using namespace vcube;

namespace {

Alg alg(const char *text) { return parseAlgOrEmpty(text); }

// The cube a case's alg solves: the alg undone from solved.
FaceletCube caseFor(const char *text) {
  FaceletCube f;
  f.apply(invert(alg(text)));
  return f;
}

CubieCube cubie(const FaceletCube &f) {
  return CubieCube::fromFacelets(oriented(f)).value_or(CubieCube{});
}

// A set's progress with its grading context.
struct Setup {
  SetProgress progress;
  LearnUndoStack undo;
  SrsConfig srs;
  std::map<std::string, int> steps;
  std::map<std::string, std::string> cards; // case -> card (families)
  LearnGrading grading(bool chained = false) {
    LearnGrading g;
    g.progress = &progress;
    g.undo = &undo;
    g.srs = &srs;
    g.setId = "set";
    g.chained = chained;
    g.cardId = [this](const std::string &id) {
      auto it = cards.find(id);
      return it == cards.end() ? id : it->second;
    };
    g.stepsFrom = [this](const std::string &id) {
      auto it = steps.find(id);
      return it == steps.end() ? 0 : it->second;
    };
    return g;
  }
  // A card in review, due long ago.
  void reviewCard(const std::string &id) {
    CaseProgress &cp = progress[id];
    cp.card.state = CardState::Review;
    cp.card.introduced = 1000;
    cp.card.due = 1000;
    cp.card.intervalDays = 4;
  }
};

constexpr std::int64_t kNow = 1'800'000'000;

// Starts an attempt at `id` whose alg is `text`, the cube set up for it.
void start(LearnAttempt &a, const std::string &id, const char *text,
           bool practice = false) {
  a.begin(id, false, practice);
  FaceletCube f = caseFor(text);
  a.startLink({id, false, 0}, f, cubie(f));
  a.setLinkAlgs({{text, {}}});
  a.startTiming(10'000);
}

void doMoves(LearnAttempt &a, const char *text, std::int64_t atMs = 11'000) {
  for (Move m : alg(text)) {
    a.turned(m, atMs);
  }
}

using Key = LearnSession::Key;
using Action = LearnSession::Action;
using Phase = LearnSession::Phase;

LearnSession::KeyEvent press(Key k, bool shift = false) {
  LearnSession::KeyEvent e;
  e.key = k;
  e.shift = shift;
  return e;
}
LearnSession::KeyEvent digit(int d) {
  LearnSession::KeyEvent e;
  e.key = Key::Digit;
  e.digit = d;
  return e;
}
LearnSession::KeyEvent turnKey(Move m) {
  LearnSession::KeyEvent e;
  e.move = m;
  return e;
}

const Move kR{MoveType::R, 1};

} // namespace

// --- the guide ------------------------------------------------------------

TEST_CASE("guide: the walkthrough steps along the alg and back") {
  AlgGuide g(alg("R U R'"), caseFor("R U R'"));
  REQUIRE_FALSE(g.empty());
  CHECK(g.step(-1).empty()); // already at the start
  CHECK(g.step(1) == alg("R"));
  CHECK(g.step(1) == alg("U"));
  CHECK(g.at() == 2);
  CHECK(g.step(-1) == alg("U'"));
  CHECK(g.at() == 1);
  g.step(1);
  g.step(1);
  CHECK(g.done());
  CHECK(g.step(1).empty());
  g.restart();
  CHECK(g.at() == 0);
}

TEST_CASE("guide: the cube is tracked along the alg") {
  FaceletCube cube = caseFor("R U R' U'");
  AlgGuide g(alg("R U R' U'"), cube);
  cube.apply(alg("R U"));
  g.track(cube);
  CHECK(g.matched());
  CHECK(g.at() == 2);
  cube.apply(alg("F")); // off the alg
  g.track(cube);
  CHECK_FALSE(g.matched());
  CHECK(g.at() == 2);
  cube.apply(alg("F' R'"));
  g.track(cube);
  CHECK(g.matched());
  CHECK(g.at() == 3);
}

TEST_CASE("guide: a rotation counts once the cube is held as after it") {
  FaceletCube cube = caseFor("y R U R'");
  AlgGuide g(alg("y R U R'"), cube);
  g.track(cube);
  CHECK(g.at() == 0);
  cube.apply(alg("y"));
  g.track(cube);
  CHECK(g.at() == 1);
  cube.apply(alg("R"));
  g.track(cube);
  CHECK(g.at() == 2);
}

// --- the keys -------------------------------------------------------------

TEST_CASE("learn keys: walkthrough, solving and the grade") {
  LearnSession s;
  CHECK(s.key(press(Key::Space)).action == Action::NextEarly);
  CHECK_FALSE(s.key(press(Key::Tab)).handled);
  CHECK_FALSE(s.key(turnKey(kR)).handled); // nothing to turn

  s.setPhase(Phase::Demo);
  CHECK(s.key(press(Key::Right)).step == 1);
  CHECK(s.key(press(Key::Left)).step == -1);
  CHECK(s.key(press(Key::Home)).action == Action::DemoRestart);
  CHECK(s.key(press(Key::Escape)).action == Action::DemoRestart);
  // The turning keys wait for the try.
  auto turn = s.key(turnKey(kR));
  CHECK(turn.handled);
  CHECK(turn.action == Action::None);
  CHECK(s.key(press(Key::Enter)).action == Action::StartFollowing);

  s.setPhase(Phase::Solving);
  CHECK(s.key(press(Key::Tab)).action == Action::Hint);
  CHECK(s.key(press(Key::Space, true)).action == Action::GiveUp);
  // Space alone does nothing, so that a double tap doesn't give up the
  // next case too.
  auto space = s.key(press(Key::Space));
  CHECK(space.handled);
  CHECK(space.action == Action::None);
  CHECK(s.key(press(Key::Escape)).action == Action::ResetCase);
  auto t = s.key(turnKey(kR));
  CHECK(t.action == Action::Turn);
  CHECK(t.move == kR);

  s.setPhase(Phase::Done);
  CHECK(s.key(press(Key::Space, true)).action == Action::None);
  CHECK(s.key(press(Key::Space)).action == Action::NextCase);
  auto regrade = s.key(digit(4));
  CHECK(regrade.action == Action::Regrade);
  CHECK(regrade.grade == Grade::Easy);
  CHECK_FALSE(s.key(digit(5)).handled);
  CHECK(s.key(press(Key::Escape)).action == Action::ResetCube);
  CHECK(s.key(turnKey(kR)).action == Action::Turn); // free turning

  // Releases are only kept for Space; Ctrl+Z and F9 work everywhere.
  LearnSession::KeyEvent up = press(Key::Space);
  up.press = false;
  CHECK(s.key(up).handled);
  up.key = Key::Tab;
  CHECK_FALSE(s.key(up).handled);
  CHECK(s.key(press(Key::Undo)).action == Action::Undo);
  CHECK(s.key(press(Key::List)).action == Action::ToggleList);
}

TEST_CASE("learn keys: recognition, the quiz and context practice") {
  LearnSession s;
  s.setPhase(Phase::Recognise);
  auto knew = s.key(press(Key::Space));
  CHECK(knew.action == Action::Recalled);
  CHECK(knew.knew);
  CHECK_FALSE(s.key(press(Key::Space, true)).knew);
  CHECK(s.key(turnKey(kR)).action == Action::Look);
  CHECK_FALSE(s.key(press(Key::Tab)).handled);

  s.setPhase(Phase::Recalled);
  auto g = s.key(digit(2));
  CHECK(g.action == Action::GradeRecognition);
  CHECK(g.grade == Grade::Hard);
  auto shown = s.key(press(Key::Enter));
  CHECK(shown.action == Action::GradeRecognition);
  CHECK_FALSE(shown.grade); // the grade shown
  CHECK(s.key(press(Key::Space, true)).action == Action::None);

  s.setPhase(Phase::Quiz);
  s.setQuizChoices(4);
  auto answer = s.key(digit(4));
  CHECK(answer.action == Action::Answer);
  CHECK(answer.choice == 3);
  CHECK_FALSE(s.key(digit(5)).handled);
  s.setPhase(Phase::QuizDone);
  CHECK(s.key(press(Key::Space)).action == Action::NextQuiz);
  s.answered(true, 1200);
  s.answered(false, 800);
  CHECK(s.quizScore().right == 1);
  CHECK(s.quizScore().asked == 2);
  CHECK(s.quizScore().totalMs == 2000);

  s.setPhase(Phase::Context);
  CHECK(s.key(press(Key::Tab)).action == Action::ContextHint);
  CHECK(s.key(press(Key::Space)).action == Action::None);
  CHECK(s.key(press(Key::Space, true)).action == Action::ContextGiveUp);
  CHECK(s.key(press(Key::Escape)).action == Action::ContextReset);
  CHECK(s.key(press(Key::Backspace)).action == Action::ContextLeave);
  CHECK(s.key(turnKey(kR)).action == Action::Turn);
  s.setPhase(Phase::ContextDone);
  CHECK(s.key(press(Key::Enter)).action == Action::ContextNext);
}

// --- grading --------------------------------------------------------------

TEST_CASE("learn: a case solved from memory with its alg is Good") {
  Setup set;
  set.reviewCard("a");
  LearnAttempt a;
  start(a, "a", "R U R' U'");
  doMoves(a, "R U R' U'", 10'800);
  CHECK(a.recognitionMs() == 800);
  REQUIRE(a.endLink(true, std::nullopt, false, kNow, 12'000, set.grading()));
  CHECK(a.grade() == Grade::Good);
  CHECK(a.graded());
  CHECK(a.why().kept == GradeWhy::Kept::FromMemory);
  const CaseProgress &cp = set.progress["a"];
  CHECK(cp.card.intervalDays > 4);
  REQUIRE(cp.attempts.size() == 1);
  CHECK_FALSE(cp.attempts[0].failed);
  CHECK(cp.attempts[0].recognitionMs == 800);
  CHECK(cp.attempts[0].executionMs == 1200);
  CHECK(set.undo.size() == 1);
}

TEST_CASE("learn: hints, giving up and leaving the algs") {
  Setup set;
  set.reviewCard("a");
  SECTION("the algs shown: Again, no recognition time") {
    LearnAttempt a;
    start(a, "a", "R U R' U'");
    a.hint();
    doMoves(a, "R U R' U'");
    a.endLink(true, std::nullopt, false, kNow, 12'000, set.grading());
    CHECK(a.grade() == Grade::Again);
    CHECK(a.why().hint);
    CHECK(set.progress["a"].attempts.back().recognitionMs == 0);
  }
  SECTION("the next move shown: Hard") {
    LearnAttempt a;
    start(a, "a", "R U R' U'");
    a.nextMoveHint();
    doMoves(a, "R U R' U'");
    a.endLink(true, std::nullopt, false, kNow, 12'000, set.grading());
    CHECK(a.grade() == Grade::Hard);
    CHECK(a.why().nextHint);
  }
  SECTION("given up: Again") {
    LearnAttempt a;
    start(a, "a", "R U R' U'");
    a.giveUp();
    a.endLink(false, std::nullopt, false, kNow, 12'000, set.grading());
    CHECK(a.grade() == Grade::Again);
    CHECK(a.why().gaveUp);
    CHECK(set.progress["a"].attempts.back().failed);
  }
  SECTION("another way to the end: Again, the moves shown") {
    LearnAttempt a;
    start(a, "a", "R U R' U'");
    doMoves(a, "F F' R U R' U'");
    a.endLink(true, std::nullopt, false, kNow, 12'000, set.grading());
    CHECK(a.grade() == Grade::Again);
    CHECK(a.why().miss == GradeWhy::Miss::LeftAlgs);
    CHECK(a.leftAlgs());
  }
}

TEST_CASE("learn: the grade can be changed and taken back") {
  Setup set;
  set.reviewCard("a");
  const SrsCard before = set.progress["a"].card;
  LearnAttempt a;
  start(a, "a", "R U R' U'");
  a.hint();
  doMoves(a, "R U R' U'");
  a.endLink(true, std::nullopt, false, kNow, 12'000, set.grading());
  REQUIRE(a.grade() == Grade::Again);
  // Changed by hand: the card is graded again from where it was.
  REQUIRE(a.regrade(Grade::Easy, kNow + 5, set.grading()));
  CHECK(a.grade() == Grade::Easy);
  CHECK(a.grade() != a.autoGrade()); // "graded by hand"
  CHECK(set.progress["a"].card.intervalDays > before.intervalDays);
  CHECK(set.progress["a"].attempts.size() == 1);
  CHECK_FALSE(set.progress["a"].attempts.back().failed);
  // The case list changed the card: 1-4 can't any more.
  a.replaceGrade();
  CHECK_FALSE(a.regrade(Grade::Good, kNow + 6, set.grading()));
  // Ctrl+Z puts the card back and drops the attempt.
  auto u = set.undo.pop();
  REQUIRE(u);
  CHECK(u->caseId == "a");
  takeBack(set.progress, *u);
  CHECK(set.progress["a"].card.intervalDays == before.intervalDays);
  CHECK(set.progress["a"].attempts.empty());
  CHECK(set.undo.empty());
}

TEST_CASE("learn: practice of a case not due isn't graded") {
  Setup set;
  set.reviewCard("a");
  LearnAttempt a;
  start(a, "a", "R U R' U'", true);
  doMoves(a, "R U R' U'");
  CHECK_FALSE(
      a.endLink(true, std::nullopt, false, kNow, 12'000, set.grading()));
  CHECK_FALSE(a.graded());
  CHECK(set.progress["a"].attempts.empty());
  CHECK_FALSE(a.regrade(Grade::Good, kNow, set.grading()));
}

TEST_CASE("learn: a walkthrough introduces a new case without a grade") {
  Setup set;
  LearnAttempt a;
  start(a, "a", "R U R' U'");
  doMoves(a, "R U R' U'");
  REQUIRE(a.endLink(true, std::nullopt, true, kNow, 12'000, set.grading()));
  CHECK(a.walkedThrough());
  CHECK(a.graded());
  CHECK(a.why().kept == GradeWhy::Kept::WalkedThrough);
  CHECK(set.progress["a"].card.introduced == kNow);
  CHECK(set.progress["a"].attempts.empty());
  REQUIRE(set.undo.open());
  CHECK(set.undo.open()->cards.front().attempt == 0);
  // Graded by hand instead: now it is an attempt, which undo removes.
  REQUIRE(a.regrade(Grade::Good, kNow + 1, set.grading()));
  CHECK(a.byHand());
  CHECK_FALSE(a.walkedThrough());
  CHECK(set.progress["a"].attempts.size() == 1);
  CHECK(set.undo.open()->cards.front().attempt == kNow + 1);
  takeBack(set.progress, *set.undo.pop());
  CHECK(set.progress["a"].card.introduced == 0);
  CHECK(set.progress["a"].attempts.empty());
}

TEST_CASE("learn: a reduction set grades each case passed on the way") {
  Setup set;
  set.steps = {{"a", 2}, {"b", 1}, {"c", 2}, {"d", 1}};
  set.reviewCard("a");
  set.reviewCard("b");
  LearnAttempt a;
  start(a, "a", "R U R' U'");
  doMoves(a, "R U R' U'");
  // The step stopped at b, one step from the end: closer, Good.
  a.endLink(false, std::string("b"), false, kNow, 12'000, set.grading(true));
  CHECK(a.links().front().closer);
  CHECK(a.links().front().stepsLeft == 1);
  CHECK(a.grade() == Grade::Good);
  CHECK(a.why().kept == GradeWhy::Kept::SetStep);
  // b is due: graded too, in the same undo entry.
  FaceletCube f = caseFor("U R U' R'");
  a.startLink({"b", false, 0}, f, cubie(f));
  a.setLinkAlgs({{"U R U' R'", {}}});
  doMoves(a, "U R U' R'");
  REQUIRE(
      a.endLink(true, std::nullopt, false, kNow, 13'000, set.grading(true)));
  CHECK(a.links().back().note == LearnLink::Note::Graded);
  CHECK(a.links().back().notedAt == kNow);
  REQUIRE(set.undo.open());
  CHECK(set.undo.open()->cards.size() == 2);
  CHECK(set.progress["b"].attempts.size() == 1);
  // The first grade is still the first case's.
  CHECK(a.grade() == Grade::Good);
}

TEST_CASE("learn: chained cases not started, not due or seen twice") {
  Setup set;
  set.steps = {{"a", 2}, {"b", 1}, {"c", 2}};
  set.reviewCard("a");
  LearnAttempt a;
  start(a, "a", "R U R' U'");
  doMoves(a, "R U R' U'");
  a.endLink(false, std::string("b"), false, kNow, 12'000, set.grading(true));
  FaceletCube f = caseFor("U R U' R'");
  a.startLink({"b", false, 0}, f, cubie(f));
  SECTION("not started") {
    a.endLink(true, std::nullopt, false, kNow, 13'000, set.grading(true));
    CHECK(a.links().back().note == LearnLink::Note::NotStarted);
  }
  SECTION("not due, and the step done") {
    CaseProgress &b = set.progress["b"];
    b.card.state = CardState::Review;
    b.card.introduced = 1000;
    b.card.due = kNow + 10 * kSecondsPerDay;
    a.endLink(true, std::nullopt, false, kNow, 13'000, set.grading(true));
    CHECK(a.links().back().note == LearnLink::Note::NotDue);
  }
  SECTION("the same card as the first case") {
    set.cards["b"] = "a";
    a.endLink(true, std::nullopt, false, kNow, 13'000, set.grading(true));
    CHECK(a.links().back().note == LearnLink::Note::AlreadyGraded);
  }
}

TEST_CASE("learn: a step that doesn't get closer is Again") {
  Setup set;
  set.steps = {{"a", 2}, {"c", 2}};
  set.reviewCard("a");
  LearnAttempt a;
  start(a, "a", "R U R' U'");
  doMoves(a, "R U R' U'");
  a.endLink(false, std::string("c"), false, kNow, 12'000, set.grading(true));
  CHECK_FALSE(a.links().front().closer);
  CHECK(a.grade() == Grade::Again);
  CHECK(a.why().miss == GradeWhy::Miss::NotCloser);
  CHECK_FALSE(a.leftAlgs()); // the moves aren't shown in a reduction set
}

TEST_CASE("learn: a recognition review is graded by hand") {
  Setup set;
  set.reviewCard("a");
  LearnAttempt a;
  a.begin("a", false, false);
  a.startTiming(10'000);
  a.recalled(true, 11'500, kNow, set.grading());
  CHECK(a.recognitionMs() == 1500);
  CHECK(a.grade() == Grade::Good);
  CHECK(a.gradedCard() == "a");
  a.gradeRecognition(Grade::Hard, kNow, set.grading());
  const Attempt &rec = set.progress["a"].attempts.back();
  CHECK(rec.recognitionOnly);
  CHECK(rec.recognitionMs == 1500);
  CHECK_FALSE(rec.failed);
  // Its undo entry isn't open to later grades.
  CHECK_FALSE(set.undo.open());
  auto u = set.undo.pop();
  REQUIRE(u);
  CHECK(u->recognition);

  LearnAttempt b;
  b.begin("a", false, false);
  b.recalled(false, 11'500, kNow, set.grading());
  CHECK(b.recognitionMs() == 0);
  CHECK(b.grade() == Grade::Again);
}

TEST_CASE("learn: the undo stack keeps the last grades") {
  LearnUndoStack undo;
  for (int i = 0; i < 12; ++i) {
    undo.push({"set", std::to_string(i), false, false, {}});
  }
  CHECK(undo.size() == LearnUndoStack::kDepth);
  REQUIRE(undo.open());
  undo.close();
  CHECK_FALSE(undo.open());
  CHECK(undo.pop()->caseId == "11");
}
