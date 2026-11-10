#pragma once

#include "cube/facelet.hpp"
#include "cube/move.hpp"
#include "cube/scramble.hpp"
#include "train/bld.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vcube {

// The Blind drills' state machines (the letter quiz, setup moves, memo
// tracing and blind solves) in the style of SolveTimer and TimedFlow: the
// keys and other events come in, and what the page should do comes out
// (Effects). All times are milliseconds on a monotonic clock supplied by
// the caller.

// --- letters ---------------------------------------------------------------

// A sticker asked about in the letter quiz or the setup drill.
struct LetterChoice {
  BldPiece kind = BldPiece::Edge;
  int sticker = 0;
  friend bool operator==(const LetterChoice &, const LetterChoice &) = default;
};

// The stickers a drill asks about: every sticker of the piece types picked
// (edges first), or for the setup drill only those that need a setup (not
// the buffer piece's, nor the swap spot).
std::vector<LetterChoice> letterPool(bool edges, bool corners, bool setups);

// How a letter has gone so far (see LetterStat).
struct LetterRecord {
  int attempts = 0;
  std::optional<double> accuracy; // 0..1
  std::optional<double> meanMs;   // of the right answers
};

// Picks a letter of `pool` (`records` holds each one's record), weighted
// towards the slow and missed ones and the ones never asked, and never
// `last` twice in a row when there is a choice. The index into `pool`.
std::size_t pickLetter(std::span<const LetterChoice> pool,
                       std::span<const LetterRecord> records,
                       const std::optional<LetterChoice> &last, Rng &rng);

// Every facelet holding its own index, to follow stickers through moves.
FaceletCube numberedFacelets();
// Where the sticker that started at `facelet` is now.
int whereIs(const FaceletCube &ids, int facelet);

// --- clocks that stop while away --------------------------------------------

// When the page or the window went away while a clock was running, so
// that the clock can be moved on by the time away.
class AwayClock {
public:
  bool away() const { return at_.has_value(); }
  // Away from `now` (a second call keeps the first time).
  void leave(std::int64_t now) {
    if (!at_) {
      at_ = now;
    }
  }
  // Back at `now`: how long it was away, nullopt if it wasn't.
  std::optional<std::int64_t> back(std::int64_t now);
  void reset() { at_.reset(); }

private:
  std::optional<std::int64_t> at_;
};

// --- the letter quiz and setup moves -----------------------------------------

// A letter asked about, and how it was answered.
struct LetterItem {
  BldPiece kind = BldPiece::Edge;
  int sticker = 0;
  bool toLetter = true; // quiz: type the letter (else click the sticker)
  std::int64_t shownAt = 0;
  bool answered = false;
  bool right = false;
};

// An answer: right or not, and the time it took.
struct LetterAnswer {
  bool right = false;
  std::int64_t ms = 0;
};

// One letter at a time, its clock stopped while away.
class LetterFlow {
public:
  const std::optional<LetterItem> &item() const { return item_; }
  // A letter is shown and not answered yet.
  bool open() const { return item_ && !item_->answered; }

  // A new letter shown at `now`. `away`: the page or the window is away,
  // so its clock starts when they are back.
  void show(LetterChoice c, bool toLetter, std::int64_t now, bool away);
  // No letter, and no clock.
  void reset();
  // The open letter answered at `now`: its time; nullopt with none open.
  std::optional<LetterAnswer> finish(bool right, std::int64_t now);

  // The page or the window went away, or came back: an open letter's
  // clock stops, and a letter shown while away starts on return.
  void pause(std::int64_t now);
  void resume(std::int64_t now);

protected:
  std::optional<LetterItem> item_;
  AwayClock away_;
};

// The letter quiz: the ringed sticker's letter is typed (a letter that
// only one sticker's letter starts with, and is all of it, answers at
// once; Enter answers a longer one), or the letter's sticker clicked. A
// right answer moves on by itself after kAutoNextMs, and a Space just
// after that (kSpaceGraceMs) was meant for the letter before, so it
// doesn't give up the new one.
class QuizFlow : public LetterFlow {
public:
  static constexpr std::int64_t kAutoNextMs = 700;
  static constexpr std::int64_t kSpaceGraceMs = 500;

  enum class Key { Letter, Space, Enter, Backspace, Other };
  struct Effects {
    bool used = false;
    bool next = false;                  // show a new letter
    std::optional<LetterAnswer> answer; // answered: record and show it
    bool givenUp = false;               // the answer was a Space
    bool showTyped = false;             // show typed() so far
    bool autoNext = false; // a new letter after kAutoNextMs, unless sooner
  };

  // How typed and scheme letters are compared (foldLetter() by default).
  void setFold(std::function<std::string(std::string_view)> fold);

  void show(LetterChoice c, bool toLetter, std::int64_t now, bool away);
  void reset();

  // A key pressed (or released, or repeated) at `now`: a letter key with
  // its `text`, or another key. `letters` gives the scheme's letters.
  Effects key(Key k, std::string_view text, bool press, bool repeat,
              std::int64_t now, const LetterScheme &letters);
  // A sticker of the open letter's piece type clicked at `now` (nullopt:
  // a sticker of the other type, or a centre).
  Effects click(std::optional<int> sticker, std::int64_t now);
  // The quiz moved on to a new letter by itself at `now`.
  void movedOn(std::int64_t now) { graceUntil_ = now + kSpaceGraceMs; }

  // The letters typed for the open letter.
  const std::string &typed() const { return typed_; }

private:
  std::function<std::string(std::string_view)> fold_;
  std::string typed_;
  std::int64_t graceUntil_ = 0;

  Effects answer(bool right, std::int64_t now);
};

// Setup moves: the ringed sticker is brought to the swap spot on the
// virtual cube (turned from solved, layers only), or the setup typed.
class SetupFlow : public LetterFlow {
public:
  enum class Key { Space, Enter, Escape, Move, Other };
  struct Effects {
    bool used = false;
    bool next = false;                  // show a new letter
    bool again = false;                 // the same letter again: show it afresh
    bool focusField = false;            // typed: the keys go back to the field
    bool resetCube = false;             // back to the solved cube
    std::optional<LetterAnswer> answer; // answered: record and show it
    bool skipped = false;               // the answer was giving up
    bool onlyLayers = false;            // a rotation was refused
    std::optional<Move> turned;         // the cube was turned
    // Turned to the swap spot, but by a setup that moves what it mustn't.
    bool notValid = false;
  };

  // The setup is typed rather than done on the cube.
  void setTyped(bool on) { typed_ = on; }
  bool typed() const { return typed_; }

  void show(LetterChoice c, std::int64_t now, bool away);
  void reset();

  Effects key(Key k, std::optional<Move> move, bool press, bool repeat,
              std::int64_t now);
  // A typed setup, read as `alg`, at `now`: the cube shows where it takes
  // the sticker, and it is right if it is a valid setup.
  std::optional<LetterAnswer> typedSetup(const Alg &alg, std::int64_t now);

  // The cube turned from solved, the moves, and where the open letter's
  // sticker is now.
  const FaceletCube &cube() const { return cube_; }
  const Alg &moves() const { return moves_; }
  int target() const;
  void resetCube();

private:
  bool typed_ = false;
  FaceletCube cube_;
  FaceletCube ids_ = numberedFacelets();
  Alg moves_;

  Effects turn(Move m, std::int64_t now);
};

// --- memo tracing ------------------------------------------------------------

// A scrambled cube's memo traced and typed: Ready (or Done) until Space
// starts one, which waits for a scramble when there is none yet; then
// Memo, its clock stopped while away, until the letters are checked.
class TraceFlow {
public:
  enum class Phase { Ready, Memo, Done };
  enum class Key { Space, Enter, Tab, Escape, Move, Other };
  struct Effects {
    bool used = false;
    bool start = false;   // trace the feed's scramble, from now
    bool waiting = false; // no scramble yet: say it is coming
    bool giveUp = false;  // the scramble given up
    bool focusFields = false;
    bool rotate = false;        // Memo: turn the traced cube as a whole
    bool onlyRotations = false; // Memo: a layer turn was refused
    bool look = false;          // Done: turn the cube shown (rotations)
  };

  Phase phase() const { return phase_; }
  bool waiting() const { return waiting_; }

  // A key at `now`; `rotation` for a turning key that turns the whole
  // cube, `scrambleReady` when the feed has one to trace.
  Effects key(Key k, bool rotation, bool press, bool repeat, bool scrambleReady,
              std::int64_t now);
  // The feed has a scramble now: a trace waiting for it starts.
  Effects scrambleReady(std::int64_t now);
  // The letters are checked at `now`: the trace's time; nullopt unless
  // tracing.
  std::optional<std::int64_t> finish(std::int64_t now);
  // The time traced so far.
  std::int64_t elapsed(std::int64_t now) const { return now - startMs_; }
  void reset();

  void pause(std::int64_t now);
  void resume(std::int64_t now);

private:
  Phase phase_ = Phase::Ready;
  bool waiting_ = false;
  std::int64_t startMs_ = 0;
  AwayClock away_;

  Effects start(bool scrambleReady, std::int64_t now);
};

// --- blind solves
// -------------------------------------------------------------

// A blind solve: Ready, then Memo (Space, once there is a scramble), then
// Executing (Space), then Done (Space, or on a physical cube any key);
// Esc cancels memo or execution. On the virtual cube the cube is turned
// during both (only rotations during memo, which the cube itself
// refuses), and as a whole when done. On a physical cube Backspace
// toggles DNF on the solve just recorded while it is still the session's
// last.
class BlindSolveFlow {
public:
  enum class Phase { Ready, Memo, Executing, Done };
  enum class Key { Space, Escape, Backspace, Move, Other };

  // The session, for Backspace: the session shown and its last solve.
  struct Context {
    bool scrambleReady = false;
    std::size_t session = 0;
    std::optional<std::int64_t> lastTimestamp;
  };
  struct Stop {
    std::int64_t timeMs = 0;
    std::int64_t memoMs = 0;
  };
  struct Effects {
    bool used = false;
    bool waitForScramble = false; // Space before there is a scramble
    bool startMemo = false;
    bool startExecution = false;
    std::optional<Stop> stop; // record the solve (then recorded())
    bool cancel = false;
    bool next = false;      // on to the next scramble
    bool toggleDnf = false; // of the session's last solve
    bool turn = false;      // virtual: turn the cube in the solve
    bool look = false;      // virtual, done: turn the cube as a whole
  };

  void setVirtual(bool on) { virtual_ = on; }
  bool isVirtual() const { return virtual_; }
  Phase phase() const { return phase_; }
  bool isBusy() const {
    return phase_ == Phase::Memo || phase_ == Phase::Executing;
  }
  std::int64_t memoMs() const { return memoMs_; }
  std::int64_t elapsed(std::int64_t now) const { return now - startMs_; }

  Effects key(Key k, bool press, bool repeat, std::int64_t now,
              const Context &c);
  // Esc, or the page left: the solve under way is cancelled.
  Effects cancel();
  // The solve stopped was recorded in `session` with this timestamp.
  void recorded(std::size_t session, std::int64_t timestamp) {
    justRecorded_ = {session, timestamp};
  }
  void reset();

private:
  struct Recorded {
    std::size_t session = 0;
    std::int64_t timestamp = 0;
  };

  bool virtual_ = true;
  Phase phase_ = Phase::Ready;
  std::int64_t startMs_ = 0; // when memo began
  std::int64_t memoMs_ = 0;
  std::optional<Recorded> justRecorded_;

  Effects space(std::int64_t now, const Context &c);
};

} // namespace vcube
