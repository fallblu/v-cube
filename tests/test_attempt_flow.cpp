#include "train/attempt_flow.hpp"
#include "train/optimal_cache.hpp"
#include "train/scramble_history.hpp"
#include "train/typed_count.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace vcube;

namespace {

using K = AttemptFlow::Key;
using Phase = AttemptFlow::Phase;
using HoldState = AttemptFlow::Hold;

AttemptFlow flow(bool moveCount, bool virt = true, bool autoStop = false) {
  AttemptFlow f;
  f.setConfig({moveCount, virt, 300, autoStop});
  return f;
}

AttemptFlow::KeyEvent down(K k, int digit = 0) {
  AttemptFlow::KeyEvent e;
  e.key = k;
  e.digit = digit;
  return e;
}

AttemptFlow::KeyEvent up(K k) {
  AttemptFlow::KeyEvent e;
  e.key = k;
  e.press = false;
  return e;
}

// Holds Space long enough and lets go: the solve starts.
void start(AttemptFlow &f, std::int64_t at = 0) {
  f.key(down(K::Space), at, true);
  f.tick(at + 300, false);
  f.key(up(K::Space), at + 310, true);
}

MoveCountAttempt attempt(int moves, bool dnf = false,
                         const std::string &scramble = "R U F") {
  MoveCountAttempt a;
  a.moves = moves;
  a.dnf = dnf;
  a.scramble = scramble;
  return a;
}

} // namespace

TEST_CASE("TypedCount takes three digits with no leading zeros", "[attempt]") {
  TypedCount t;
  CHECK(t.empty());
  CHECK(t.value() == 0);
  CHECK(t.add(0));
  CHECK(t.text() == "0");
  CHECK(t.add(7)); // "0" then "7" is 7
  CHECK(t.text() == "7");
  CHECK(t.add(2));
  CHECK(t.add(5));
  CHECK_FALSE(t.add(1));
  CHECK(t.value() == 725);
  t.backspace();
  CHECK(t.text() == "72");
  t.clear();
  CHECK(t.empty());
  t.backspace(); // nothing to take away
  CHECK(t.empty());
}

TEST_CASE("ScrambleHistory goes back and forward through done scrambles",
          "[attempt]") {
  ScrambleHistory<std::string> h;
  CHECK_FALSE(h.shown());
  CHECK_FALSE(h.back());
  h.push("a");
  h.push("b");
  CHECK_FALSE(h.shown()); // the new scramble
  REQUIRE(h.back());
  CHECK(*h.shown() == "b");
  CHECK(h.stepsBack() == 1);
  REQUIRE(h.back());
  CHECK(*h.shown() == "a");
  CHECK_FALSE(h.back());
  REQUIRE(h.forward());
  CHECK(*h.shown() == "b");
  REQUIRE(h.forward());
  CHECK_FALSE(h.shown());
  CHECK_FALSE(h.forward());
  h.back();
  h.toEnd();
  CHECK_FALSE(h.shown());
  for (int i = 0; i < 150; ++i) {
    h.push(std::to_string(i));
  }
  CHECK(h.size() == ScrambleHistory<std::string>::kCapacity);
  h.clear();
  CHECK(h.size() == 0);
  CHECK_FALSE(h.back());
}

TEST_CASE("OptimalCache keeps results unless they are stale", "[attempt]") {
  OptimalCache c;
  std::vector<MoveCountAttempt> list = {
      attempt(9, false, "R U"), attempt(8, false, "R U"), attempt(7, false, ""),
      attempt(6, false, "F2 D")};
  CHECK_FALSE(c.lookup(SolveStep::EOCross, "R U", false));
  auto job = c.missing(SolveStep::EOCross, false, {}, list);
  REQUIRE(job);
  CHECK(job->scrambles == std::vector<std::string>{"R U", "F2 D"});
  CHECK(c.store(*job, {{"R U", 2}, {"F2 D", -1}}));
  CHECK(c.lookup(SolveStep::EOCross, "R U", false) == 2);
  CHECK(c.lookup(SolveStep::EOCross, "F2 D", false) == -1);
  CHECK_FALSE(c.lookup(SolveStep::EOCross, "R U", true)); // other holds
  CHECK_FALSE(c.lookup(SolveStep::Cross, "R U", false));
  CHECK_FALSE(c.missing(SolveStep::EOCross, false, {}, list));
  // The holds changed while a job ran: its results are dropped.
  auto other = c.missing(SolveStep::Cross, false, {}, list);
  REQUIRE(other);
  c.invalidate();
  CHECK_FALSE(c.lookup(SolveStep::EOCross, "R U", false));
  CHECK_FALSE(c.store(*other, {{"R U", 3}}));
  CHECK_FALSE(c.lookup(SolveStep::Cross, "R U", false));
}

TEST_CASE("OptimalCache works a job out", "[attempt]") {
  OptimalCache::Job job;
  job.step = SolveStep::Cross;
  job.holds = {kWcaHold};
  job.scrambles = {"R", "not an alg"};
  auto results = OptimalCache::run(job);
  REQUIRE(results.size() == 2);
  CHECK(results[0].second == 1);
  CHECK(results[1].second == -1);
}

TEST_CASE("overOptimal and worstAttempts rank against the optimal",
          "[attempt]") {
  std::vector<MoveCountAttempt> list = {
      attempt(10, false, "a"), attempt(12, true, "b"), attempt(9, false, "c"),
      attempt(14, false, "d"), attempt(8, false, "")};
  list[4].counted = true;
  auto best = [](const MoveCountAttempt &a) -> std::optional<int> {
    if (a.scramble == "a") {
      return 6; // 4 over
    }
    if (a.scramble == "c") {
      return 9; // optimal
    }
    if (a.scramble == "d") {
      return -1; // unreadable
    }
    return std::nullopt;
  };
  OverOptimal over = overOptimal(list, best);
  CHECK(over.known == 2);
  CHECK(over.optimal == 1);
  CHECK(over.mean == 2.0);
  // DNF first, then 4 over, then by moves; the one with no scramble is
  // left out.
  CHECK(worstAttempts(list, 5, best) == std::vector<std::size_t>{1, 0, 3, 2});
  CHECK(worstAttempts(list, 2, best) == std::vector<std::size_t>{1, 0});
  // No optimal lengths (Untimed): by moves.
  CHECK(worstAttempts(list, 5, {}) == std::vector<std::size_t>{1, 3, 0, 2});
}

TEST_CASE("AttemptFlow starts a solve when Space is held and released",
          "[attempt]") {
  AttemptFlow f = flow(false);
  auto e = f.key(down(K::Space), 0, true);
  CHECK(e.used);
  CHECK(e.holdStarted);
  CHECK(e.showHold);
  CHECK(f.hold() == HoldState::Holding);
  CHECK(f.holdLeft(100) == 200);
  e = f.tick(200, false);
  CHECK_FALSE(e.showHold);
  e = f.tick(300, false);
  CHECK(e.showHold);
  CHECK(f.hold() == HoldState::Ready);
  // Other keys wait while Space is held.
  e = f.key(down(K::Turn), 305, true);
  CHECK(e.used);
  CHECK_FALSE(e.turn);
  e = f.key(up(K::Space), 310, true);
  CHECK(e.showHold);
  CHECK(e.startSolve);
  CHECK(e.phase);
  CHECK(f.phase() == Phase::Solving);
  CHECK(f.isBusy());
  CHECK_FALSE(f.isHolding());
}

TEST_CASE("AttemptFlow: a short hold doesn't start, nor one without a "
          "scramble",
          "[attempt]") {
  AttemptFlow f = flow(true);
  f.key(down(K::Space), 0, true);
  auto e = f.key(up(K::Space), 100, true);
  CHECK(e.showHold);
  CHECK_FALSE(e.startSolve);
  CHECK(f.phase() == Phase::Inspect);
  CHECK_FALSE(f.isHolding());
  e = f.key(down(K::Space), 200, false);
  CHECK(e.used);
  CHECK(e.notice == AttemptFlow::Notice::WaitToStart);
  CHECK_FALSE(f.isHolding());
  // The scramble went away during the hold.
  f.key(down(K::Space), 300, true);
  f.tick(600, false);
  e = f.key(up(K::Space), 610, false);
  CHECK(e.notice == AttemptFlow::Notice::WaitToStart);
  CHECK(f.phase() == Phase::Inspect);
  // A popup takes the release, or the window loses the keyboard.
  f.key(down(K::Space), 700, true);
  e = f.tick(750, true);
  CHECK(e.showHold);
  CHECK_FALSE(f.isHolding());
  f.key(down(K::Space), 800, true);
  CHECK(f.dropHold().showHold);
  CHECK_FALSE(f.dropHold().showHold);
}

TEST_CASE("AttemptFlow: Esc in each phase", "[attempt]") {
  AttemptFlow f = flow(false);
  // Holding: lets go.
  f.key(down(K::Space), 0, true);
  auto e = f.key(down(K::Escape), 10, true);
  CHECK(e.showHold);
  CHECK_FALSE(e.restartCube);
  CHECK_FALSE(f.isHolding());
  // Inspecting: the scramble again.
  e = f.key(down(K::Escape), 20, true);
  CHECK(e.restartCube);
  CHECK(e.phase);
  CHECK(f.phase() == Phase::Inspect);
  // Solving: the scramble again, to inspect.
  start(f, 100);
  REQUIRE(f.isBusy());
  e = f.key(down(K::Escape), 1000, true);
  CHECK(e.restartCube);
  CHECK(e.entryChanged);
  CHECK(f.phase() == Phase::Inspect);
  // Done: the cube solved, to turn freely.
  start(f, 2000);
  e = f.key(down(K::Space), 3000, true);
  REQUIRE(e.endSolve);
  f.solveEnded(true, 20, "R");
  REQUIRE(f.phase() == Phase::Done);
  e = f.key(down(K::Escape), 3100, true);
  CHECK(e.solvedCube);
  CHECK_FALSE(e.restartCube);
  CHECK(f.phase() == Phase::Done);
}

TEST_CASE("AttemptFlow: Untimed ends in Done, Space or Enter goes on",
          "[attempt]") {
  AttemptFlow f = flow(false);
  // Enter isn't for the page before the solve is done.
  CHECK_FALSE(f.key(down(K::Enter), 0, true).used);
  start(f);
  CHECK_FALSE(f.key(down(K::Enter), 400, true).used);
  auto e = f.key(down(K::Space), 500, true);
  CHECK(e.endSolve);
  e = f.solveEnded(false, 31, "R U");
  REQUIRE(e.record);
  CHECK(e.record->count == 31);
  CHECK(e.record->dnf);
  CHECK(e.record->solution == "R U");
  CHECK(e.analyse);
  CHECK(e.notReached);
  CHECK_FALSE(e.nextScramble);
  CHECK(f.phase() == Phase::Done);
  e = f.key(down(K::Enter), 600, true);
  CHECK(e.used);
  CHECK(e.nextScramble);
  CHECK(f.phase() == Phase::Inspect);
  // The next scramble is shown.
  CHECK(f.scrambleShown().phase);
  start(f, 700);
  f.key(down(K::Space), 1500, true);
  f.solveEnded(true, 30, "R");
  e = f.key(down(K::Space), 1600, true);
  CHECK(e.nextScramble);
  // The release that follows does nothing.
  e = f.key(up(K::Space), 1650, true);
  CHECK(e.used);
  CHECK_FALSE(e.startSolve);
  CHECK_FALSE(e.showHold);
}

TEST_CASE("AttemptFlow: Move Count goes straight on to the next scramble",
          "[attempt]") {
  AttemptFlow f = flow(true);
  CHECK_FALSE(f.key(down(K::Tab), 0, true).used); // no algs in Move Count
  start(f);
  auto e = f.key(down(K::Space), 1000, true);
  REQUIRE(e.endSolve);
  e = f.solveEnded(true, 7, "R U");
  REQUIRE(e.record);
  CHECK_FALSE(e.record->dnf);
  CHECK(e.nextScramble);
  CHECK_FALSE(e.notReached);
  CHECK_FALSE(e.analyse);
  CHECK(f.phase() == Phase::Inspect);
  // The cube being solved doesn't end a Move Count solve.
  f = flow(true, true, true);
  start(f);
  CHECK_FALSE(f.turned(true).endSolve);
}

TEST_CASE("AttemptFlow: auto stop ends an Untimed solve once solved",
          "[attempt]") {
  AttemptFlow f = flow(false, true, true);
  CHECK_FALSE(f.turned(true).endSolve); // not solving yet
  start(f);
  CHECK_FALSE(f.turned(false).endSolve);
  CHECK(f.turned(true).endSolve);
  f = flow(false, true, false);
  start(f);
  CHECK_FALSE(f.turned(true).endSolve);
}

TEST_CASE("AttemptFlow: turning keys and Tab", "[attempt]") {
  AttemptFlow f = flow(false);
  auto e = f.key(down(K::Turn), 0, true);
  CHECK(e.used);
  CHECK(e.turn);
  e = f.key(up(K::Turn), 0, true);
  CHECK(e.used);
  CHECK_FALSE(e.turn);
  AttemptFlow::KeyEvent repeat = down(K::Turn);
  repeat.autoRepeat = true;
  CHECK_FALSE(f.key(repeat, 0, true).turn);
  CHECK(f.key(down(K::Tab), 0, true).toggleAlgs);
  CHECK_FALSE(f.key(down(K::Other), 0, true).used);
}

TEST_CASE("AttemptFlow: the keys for the last attempt", "[attempt]") {
  AttemptFlow f = flow(false);
  CHECK(f.key(down(K::Comment), 0, true).action ==
        AttemptFlow::Action::Comment);
  CHECK(f.key(down(K::Review), 0, true).action == AttemptFlow::Action::Review);
  CHECK(f.key(down(K::Undo), 0, true).action == AttemptFlow::Action::Undo);
  CHECK(f.key(down(K::Delete), 0, true).action == AttemptFlow::Action::Delete);
  auto e = f.key(down(K::Skip), 0, true);
  CHECK(e.nextScramble);
  CHECK(e.historyToEnd);
  // Used but not acted on when released, during a hold or a solve.
  e = f.key(up(K::Delete), 0, true);
  CHECK(e.used);
  CHECK(e.action == AttemptFlow::Action::None);
  f.key(down(K::Space), 0, true);
  e = f.key(down(K::Delete), 10, true);
  CHECK(e.used);
  CHECK(e.action == AttemptFlow::Action::None);
  f.key(down(K::Escape), 20, true);
  start(f, 100);
  e = f.key(down(K::Skip), 500, true);
  CHECK(e.used);
  CHECK_FALSE(e.nextScramble);
  // Alt+3 records a DNF only in Move Count on a physical cube.
  CHECK_FALSE(f.key(down(K::Dnf), 600, true).record);
}

TEST_CASE("AttemptFlow: Move Count on a physical cube has counts typed in",
          "[attempt]") {
  AttemptFlow f = flow(true, false);
  CHECK_FALSE(f.key(down(K::Space), 0, true).used);
  auto e = f.key(down(K::Digit, 0), 0, true);
  CHECK(e.entryChanged);
  f.key(down(K::Digit, 2), 0, true);
  f.key(down(K::Digit, 3), 0, true);
  f.key(down(K::Digit, 4), 0, true);
  CHECK(f.entry().text() == "234");
  e = f.key(down(K::Digit, 5), 0, true); // three digits at most
  CHECK(e.used);
  CHECK_FALSE(e.entryChanged);
  f.key(down(K::Backspace), 0, true);
  CHECK(f.entry().text() == "23");
  AttemptFlow::KeyEvent ctrl = down(K::Digit, 1);
  ctrl.modified = true;
  CHECK_FALSE(f.key(ctrl, 0, true).used);
  e = f.key(down(K::Enter), 0, false);
  CHECK(e.notice == AttemptFlow::Notice::WaitToRecord);
  e = f.key(down(K::Enter), 0, true);
  REQUIRE(e.record);
  CHECK(e.record->count == 23);
  CHECK_FALSE(e.record->dnf);
  CHECK(e.nextScramble);
  CHECK(f.entry().empty());
  e = f.key(down(K::Enter), 0, true);
  CHECK(e.notice == AttemptFlow::Notice::TypeFirst);
  CHECK_FALSE(e.record);
  f.key(down(K::Digit, 9), 0, true);
  f.key(down(K::Escape), 0, true);
  CHECK(f.entry().empty());
  // Alt+3 records a DNF, with or without a count.
  e = f.key(down(K::Dnf), 0, true);
  REQUIRE(e.record);
  CHECK(e.record->dnf);
  CHECK(e.nextScramble);
  e = f.recordTyped(true, true);
  CHECK(e.record);
}

TEST_CASE("AttemptFlow: Untimed on a physical cube steps through scrambles",
          "[attempt]") {
  AttemptFlow f = flow(false, false);
  REQUIRE(f.scramblesOnly());
  auto e = f.key(down(K::Space), 0, true);
  CHECK(e.forward);
  CHECK_FALSE(e.nextScramble);
  CHECK(f.key(down(K::Right), 0, true).forward);
  CHECK(f.key(down(K::Backspace), 0, true).back);
  CHECK(f.key(down(K::Left), 0, true).back);
  e = f.key(up(K::Left), 0, true);
  CHECK(e.used);
  CHECK_FALSE(e.back);
  CHECK(f.key(down(K::Tab), 0, true).toggleAlgs);
  CHECK_FALSE(f.key(up(K::Tab), 0, true).used);
  CHECK_FALSE(f.key(down(K::Escape), 0, true).used);
  CHECK_FALSE(f.key(down(K::Digit, 3), 0, true).used);
  e = f.skip();
  CHECK(e.historyToEnd);
  CHECK(e.nextScramble);
}

TEST_CASE("AttemptFlow retries an attempt and goes back to the scrambles",
          "[attempt]") {
  AttemptFlow f = flow(true);
  auto e = f.retryOne(3);
  REQUIRE(e.retryNext);
  CHECK(*e.retryNext == 3);
  e = f.beginRetry(3, attempt(25));
  CHECK(e.retryStarted);
  REQUIRE(f.retry());
  CHECK(f.retry()->index == 3);
  CHECK_FALSE(f.retry()->feedSolved);
  CHECK(f.retryCount() == 1);
  CHECK(f.retryNumber() == 1);
  // No scramble from the feed is needed to start a retry.
  f.key(down(K::Space), 0, false);
  f.tick(300, false);
  e = f.key(up(K::Space), 310, false);
  CHECK(e.startSolve);
  f.key(down(K::Space), 1000, true);
  e = f.solveEnded(false, 30, "R U");
  CHECK(e.retryResult);
  CHECK_FALSE(e.record);
  CHECK(f.retry()->done);
  CHECK(f.retry()->result.dnf);
  CHECK(f.retry()->result.moves == 30);
  CHECK(f.phase() == Phase::Done);
  // Space: back to the scramble shown before.
  e = f.key(down(K::Space), 1100, true);
  CHECK(e.retryEnded);
  CHECK(e.showScramble);
  CHECK_FALSE(e.nextScramble);
  CHECK_FALSE(f.retry());
  CHECK(f.phase() == Phase::Inspect);
}

TEST_CASE("AttemptFlow: a retry after a solved scramble moves on after it",
          "[attempt]") {
  AttemptFlow f = flow(false);
  start(f);
  f.key(down(K::Space), 500, true);
  f.solveEnded(true, 20, "R");
  REQUIRE(f.phase() == Phase::Done);
  f.retryOne(0);
  f.beginRetry(0, attempt(20));
  CHECK(f.retry()->feedSolved);
  // Skip scramble goes back to new scrambles: the solved one is done.
  auto e = f.skip();
  CHECK(e.retryEnded);
  CHECK(e.nextScramble);
}

TEST_CASE("AttemptFlow works through a queue of retries", "[attempt]") {
  AttemptFlow f = flow(true);
  CHECK_FALSE(f.retryQueue({}).retryNext);
  auto e = f.retryQueue({4, 1, 2});
  REQUIRE(e.retryNext);
  CHECK(*e.retryNext == 4);
  f.beginRetry(4, attempt(30));
  CHECK(f.retryCount() == 3);
  CHECK(f.retryNumber() == 1);
  CHECK(f.retriesQueued());
  start(f);
  f.key(down(K::Space), 1000, true);
  f.solveEnded(true, 20, "R");
  e = f.key(down(K::Space), 1100, true);
  REQUIRE(e.retryNext);
  CHECK(*e.retryNext == 1);
  CHECK_FALSE(e.retryEnded);
  f.beginRetry(1, attempt(28));
  CHECK(f.retryNumber() == 2);
  CHECK_FALSE(f.retry()->done);
  // Deleting an attempt drops the rest of the queue.
  f.attemptDeleted();
  CHECK_FALSE(f.retriesQueued());
  e = f.skip();
  CHECK(e.retryEnded);
  CHECK_FALSE(f.retry());
  // Skip ends the queue too.
  f.retryQueue({0, 1});
  f.beginRetry(0, attempt(20));
  e = f.skip();
  CHECK(e.retryEnded);
  CHECK_FALSE(f.retriesQueued());
  // Another session: no retry.
  f.retryOne(2);
  f.beginRetry(2, attempt(20));
  f.resetRetry();
  CHECK_FALSE(f.retry());
}

TEST_CASE("AttemptFlow shows a fresh scramble after a solved one",
          "[attempt]") {
  AttemptFlow f = flow(false);
  auto e = f.freshScramble();
  CHECK(e.showScramble);
  CHECK_FALSE(e.nextScramble);
  start(f);
  f.key(down(K::Space), 500, true);
  f.solveEnded(true, 20, "R");
  e = f.freshScramble();
  CHECK(e.nextScramble);
  CHECK(f.phase() == Phase::Inspect);
  // A physical cube has no phases to show.
  f = flow(true, false);
  CHECK_FALSE(f.scrambleShown().phase);
}
