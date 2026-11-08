#pragma once

#include "cube/facelet.hpp"
#include "cube/hold.hpp"
#include "cube/move.hpp"
#include "train/stats.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vcube {

// One scramble in Move Count or Untimed mode: how many moves the solution
// to the step (e.g. EO Cross, or the whole solve) took, or DNF when the
// step wasn't solved. On the virtual cube the solution itself is kept; on
// a physical cube Move Count has the count typed in, and Untimed keeps
// only the scramble (no count).
struct MoveCountAttempt {
  int moves = 0;
  bool dnf = false;
  std::string scramble;
  std::string comment;
  std::int64_t timestamp = 0; // Unix seconds when it was recorded
  std::string solution;       // the moves done on the virtual cube, if any
  bool counted = true; // false: no move count (an Untimed physical solve)
};

// The steps Move Count can check on the virtual cube. EO+1 and EO+2 are EO
// with any one or two cross edges solved.
enum class SolveStep : std::uint8_t { EO, EOPlus1, EOPlus2, EOCross, ZZF2L };
const std::vector<SolveStep> &solveSteps();
// "eo", "eo+1", "eo+2", "eocross", "zzf2l".
std::string_view stepId(SolveStep step);
// "EO", "EO+1", "EO+2", "EO Cross", "ZZ F2L".
std::string_view stepName(SolveStep step);
std::optional<SolveStep> stepFromId(std::string_view id);
// The step a session name such as "EO Cross" or "zz f2l" is named after.
std::optional<SolveStep> stepFromName(std::string_view name);
// The scramble type a step is practised from: EO Cross solved for ZZ F2L,
// a WCA scramble for the rest.
std::string defaultScrambleType(SolveStep step);

// Whether the cube shows `step` done, held in any of the 24 ways: the
// cross is on the bottom and the edges are oriented for the front-back
// axis (they can be solved without F or B quarter turns), as in ZZ. So
// EO counts on any axis and the cross on any colour.
bool stepReached(const FaceletCube &cube, SolveStep step);
// The holds in which the cube shows `step` done (cross on the bottom, EO
// for the front-back axis), sorted; empty if none.
std::vector<Hold> stepReachedHolds(const FaceletCube &cube, SolveStep step);

// Moves in HTM: every turn of an outer layer (a face or a wide turn) is
// one move, a slice turn is two, and rotations are free.
int htmCount(const Alg &alg);

// One attempt on the virtual cube: the scrambled cube can only be turned
// as a whole (to look at it) until the solve starts, and every move from
// the scramble on is kept. The solution is the moves as done, with
// consecutive turns of the same layer joined (R R is R2, R R' nothing),
// since the keyboard only turns quarter turns.
class VirtualAttempt {
public:
  // A new scramble: only rotations until start().
  void load(const Alg &scramble);
  // A cube set up some other way (a Drill case), likewise.
  void load(const FaceletCube &start);
  // Back to the scramble, forgetting the moves.
  void restart();
  void start() { started_ = true; }
  // Every move is allowed (after the solve, to turn the cube freely).
  void setFree(bool on) { free_ = on; }

  // Turns the cube; false (and nothing done) for a turn of a layer before
  // the solve started.
  bool turn(Move m);

  bool started() const { return started_; }
  const FaceletCube &cube() const { return cube_; }
  const Alg &moves() const { return moves_; }
  Alg solution() const;
  int count() const { return htmCount(solution()); }

private:
  FaceletCube start_;
  FaceletCube cube_;
  Alg moves_;
  bool started_ = false;
  bool free_ = false;
};

// The largest move count that can be entered.
inline constexpr int kMaxMoveCount = 999;

// The scramble types Move Count mode offers, by id: a full WCA scramble
// (for EO, EOLine, EO Cross or cross), EOLine solved, EO Cross solved (ZZ
// F2L) and cross solved (CFOP F2L).
const std::vector<std::string> &moveCountScrambleTypes();
bool isMoveCountScrambleType(const std::string &id);

// Move Count statistics are the timer's (WCA trimmed averages, means,
// best rolling averages), worked out on the attempts as solves of one
// "millisecond" per thousandth of a move. Their values are therefore in
// thousandths of a move, and kDnfTime stands for DNF.
inline constexpr std::int64_t kMoveUnit = 1000;
std::vector<Solve> attemptsAsSolves(std::span<const MoveCountAttempt> attempts);

// A statistic in thousandths of a move, rounded to `decimals` (0-2):
// "23", "23.33", "DNF", or "-" for nullopt.
std::string formatMoves(StatValue thousandths, int decimals = 2);
// An attempt as listed: "23", "DNF(23)", "DNF" with no count, or "-" for
// an attempt that isn't counted.
std::string formatAttempt(const MoveCountAttempt &a);

} // namespace vcube
