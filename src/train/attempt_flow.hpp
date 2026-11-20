#pragma once

#include "train/keys.hpp"
#include "train/move_count.hpp"
#include "train/typed_count.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace vcube {

// Untimed and Move Count mode's attempts as a state machine: the keys and
// the page's own events come in, and what the page should do comes out
// (Effects), in the style of TimedFlow. Times are milliseconds on a
// monotonic clock supplied by the caller.
//
// On the virtual cube a scramble is looked at first (Inspect: the cube
// turns as a whole only), Space is held for the hold time (Holding, then
// Ready) and released to start the solve (Solving), and Space ends it, or
// in Untimed with auto stop the cube being solved. Move Count then goes
// straight on to the next scramble; Untimed leaves the cube to turn freely
// (Done) until Space or Enter asks for the next one. A recorded attempt can
// be solved again, not recorded (a retry), alone or as one of a queue of
// them. On a physical cube Move Count has the count typed in for each
// scramble, and Untimed steps through scrambles: forward keeps the one
// shown as done, back goes to one done before.
class AttemptFlow {
public:
  enum class Phase : std::uint8_t { Inspect, Solving, Done };
  using Hold = HoldCue;

  struct Config {
    bool moveCount = false; // Move Count, not Untimed
    bool virtualCube = true;
    std::int64_t holdMs = 300;
    bool autoStop = false; // Untimed: the solve ends when the cube is solved
  };

  // Of the shared keys the flow reads the turn a key makes (on the virtual
  // cube, any key but the chords turns it), the chords for the last attempt
  // and the scramble, and a Delete with nothing held.
  using Key = TrainKey;
  using KeyEvent = TrainKeyEvent;

  // A recorded attempt solved again from its review: not recorded, and
  // compared with the attempt it repeats.
  struct Retry {
    std::size_t index = 0; // of the attempt
    MoveCountAttempt original;
    bool done = false;       // solved; Space goes back to the scrambles
    bool feedSolved = false; // the scramble shown before was solved
    MoveCountAttempt result; // the retry's own, once done
  };

  // What the page tells the user.
  enum class Notice : std::uint8_t {
    None,
    WaitToStart,  // no scramble to start on yet
    WaitToRecord, // no scramble to record yet
    TypeFirst,    // Enter with no count typed
  };
  // The keys for the last attempt that the page acts on.
  enum class Action : std::uint8_t { None, Comment, Review, Undo, Delete };

  // An attempt for the page to record for the scramble shown.
  struct Kept {
    std::optional<int> count; // nullopt: not counted (scrambles only)
    bool dnf = false;
    std::string solution; // the moves done on the virtual cube
  };

  // What the page does after an event, in this order.
  struct Effects {
    bool used = false; // the key was for the page
    Notice notice = Notice::None;
    // The hold started, became ready or was let go of: show it (or, let
    // go of, what it covered). holdStarted: the hold timers start.
    bool showHold = false;
    bool holdStarted = false;
    Action action = Action::None;
    bool turn = false;        // turn the cube (and then call turned())
    bool toggleAlgs = false;  // Tab: the algs shown or hidden, if offered
    bool restartCube = false; // the scramble as it was dealt, again
    bool solvedCube = false;  // the cube solved, to turn freely
    bool startSolve = false;  // the solve's moves start counting
    // The solve ended: the page judges the cube and calls solveEnded().
    bool endSolve = false;
    std::optional<Kept> record;
    bool retryResult = false; // a retry is done: show how it went
    bool phase = false;       // the phase was set (again): show it
    bool entryChanged = false;
    bool historyToEnd = false; // scrambles only: on to the new scramble
    // Scrambles only: forward through the scrambles gone back to, or keep
    // the one shown as done and go on to the next; back one scramble.
    bool forward = false;
    bool back = false;
    bool nextScramble = false; // on to the feed's next scramble
    bool showScramble = false; // the scramble shown, again
    // Retry this attempt next: the page checks it is there and calls
    // beginRetry() with it.
    std::optional<std::size_t> retryNext;
    bool retryStarted = false; // show the retry's scramble
    bool retryEnded = false;   // back to the scrambles after the retries
    bool analyse = false;      // Untimed: the solve just recorded, looked at
    bool notReached = false;   // the solve recorded was a DNF
  };

  void setConfig(const Config &c) { config_ = c; }
  const Config &config() const { return config_; }

  Phase phase() const { return phase_; }
  Hold hold() const { return hold_; }
  // A solve on the virtual cube is under way.
  bool isBusy() const { return phase_ == Phase::Solving; }
  bool isHolding() const { return hold_ != Hold::None; }
  // Untimed on a physical cube: only scrambles are kept.
  bool scramblesOnly() const {
    return !config_.moveCount && !config_.virtualCube;
  }
  const std::optional<Retry> &retry() const { return retry_; }
  // The retries in the queue at its start, and which of them this one is.
  std::size_t retryCount() const { return retryCount_; }
  std::size_t retryNumber() const { return retryCount_ - queue_.size(); }
  bool retriesQueued() const { return !queue_.empty(); }
  // Move Count, physical: the count typed so far.
  const TypedCount &entry() const { return entry_; }

  // A key down or up.
  Effects key(const KeyEvent &k, std::int64_t now, Scramble scramble);
  // Advances a hold. With `popupOpen` (a popup or dialog took the keys)
  // a hold is let go of, as its release won't come.
  Effects tick(std::int64_t now, bool popupOpen);
  // How long the hold has still to go before it is Ready.
  std::int64_t holdLeft(std::int64_t now) const;
  // The window or page lost the keyboard: a hold is let go of.
  Effects dropHold();
  // A turn was done during or after the solve; `solved`: the cube is.
  Effects turned(bool solved);
  // The page judged the cube after endSolve: `reached` the step (or
  // solved), with the solution's count.
  Effects solveEnded(bool reached, int count, const std::string &solution);
  // Move Count, physical: records the count typed (or DNF).
  enum class Typed : std::uint8_t { Count, Dnf };
  Effects recordTyped(Typed typed, Scramble scramble);
  // Skip scramble: on to the next one, or from a retry back to the
  // scrambles.
  Effects skip();
  // A new scramble is shown (on the virtual cube: to inspect).
  Effects scrambleShown();
  // The scramble shown, or the next one if the one shown was solved.
  Effects freshScramble();

  // Retries attempt `index` alone, or the attempts in `indices` one after
  // another.
  Effects retryOne(std::size_t index);
  Effects retryQueue(const std::vector<std::size_t> &indices);
  // Starts retrying `original`, attempt `index` (from retryNext).
  Effects beginRetry(std::size_t index, const MoveCountAttempt &original);
  // An attempt was deleted: those queued after it have moved.
  void attemptDeleted();
  // Another session or cube: no retries.
  void resetRetry();
  void clearEntry() { entry_.clear(); }

private:
  Config config_;
  Phase phase_ = Phase::Inspect;
  Hold hold_ = Hold::None;
  std::int64_t holdStart_ = 0;
  std::optional<Retry> retry_;
  // The attempts still to retry after this one.
  std::deque<std::size_t> queue_;
  std::size_t retryCount_ = 0;
  TypedCount entry_;

  void setPhase(Phase p, Effects &e);
  void cancelHold(Effects &e);
  void pressStart(std::int64_t now, Scramble scramble, Effects &e);
  void releaseStart(Scramble scramble, Effects &e);
  void next(Effects &e);
  void endRetry(Effects &e);
  void recordTyped(Typed typed, Scramble scramble, Effects &e);
  void skip(Effects &e);
};

} // namespace vcube
