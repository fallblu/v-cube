#pragma once

#include "cube/scramble.hpp"
#include "train/algset.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace vcube {

// U turns (in quarters, 0..3) to do before and after an alg so that it
// solves a given state.
struct AufFit {
  int before = 0;
  int after = 0;
};

// Whether `alg` (any notation, rotations allowed) takes `state` to a state
// that is solved except for the pieces `free` allows, allowing a U turn
// before and/or after it. Prefers no AUF, then U, U', U2.
std::optional<AufFit> fitAlg(const CubieCube &state, const Alg &alg,
                             const PieceMask &free, bool aufBefore = true,
                             bool aufAfter = true);

// Whether a cube (in standard orientation) has the step done: solved apart
// from `free` pieces, up to U turns before and after.
bool stepSolved(const CubieCube &cube, const PieceMask &free);

// The alg with its AUFs written in, e.g. "(U') R U R' U' (U2)". U turns
// written at the start (or end) of the alg are joined with the AUF on that
// side, so "(U2) R U R'" with a U before it reads "(U') R U R'".
std::string withAufs(const std::string &alg, AufFit fit);

// Text of a U turn by `quarters` ("", "U", "U2", "U'").
std::string aufText(int quarters);

// What a cube leaves unsolved beyond the pieces a step may leave: pieces
// in the wrong place, and pieces in place (or free to move) but twisted or
// flipped. Counts are by layer: [0] U, [1] middle (edges only), [2] D.
struct Unsolved {
  std::array<int, 3> cornersMisplaced{};
  std::array<int, 3> cornersTwisted{};
  std::array<int, 3> edgesMisplaced{};
  std::array<int, 3> edgesFlipped{};

  int corners() const;
  int edges() const;
  bool none() const { return corners() == 0 && edges() == 0; }
  friend bool operator==(const Unsolved &, const Unsolved &) = default;
};
Unsolved unsolvedPieces(const CubieCube &cube, const PieceMask &free);
// e.g. "leaves 3 corners unsolved (U layer)", "leaves 2 edges flipped
// (U layer)" or "leaves 1 corner and 2 edges unsolved (U and D layers)".
std::string describeUnsolved(const Unsolved &u);

// The result of checking one alg against its case.
struct AlgCheck {
  std::optional<ParseError> parseError; // the alg can't be read
  bool empty = false;                   // nothing to check
  std::optional<AufFit> fit;            // set when the alg passes
  int moves = 0;                        // HTM, when the alg can be read
  Unsolved unsolved; // for a failing alg: the closest it gets

  bool passes() const { return fit.has_value(); }
};
// Checks an alg against a case under a step definition, allowing a U turn
// before and/or after it as the set does.
AlgCheck checkAlg(const CubieCube &state, std::string_view alg,
                  const PieceMask &free, bool aufBefore, bool aufAfter);
// Short visible result: "(U') … (U2), 14 moves" for a pass, otherwise e.g.
// "leaves 3 corners unsolved (U layer)" or "can't read at character 7".
std::string describeCheck(const AlgCheck &check);

// The case as it is best shown and set up for its main alg: the alg's
// leading y rotations (and leading U turns when the set allows a U turn
// before the case) are done to the case, so that the rest of the alg solves
// it straight away. Leading x and z rotations stay in the alg: they tilt
// the cube to do the alg, not to look at the case.
struct DisplayOrientation {
  CubieCube state; // the case in that orientation
  std::string alg; // the main alg without its leading prefix
};
// nullopt when `mainAlg` doesn't solve `state`.
std::optional<DisplayOrientation>
displayOrientation(const CubieCube &state, const std::string &mainAlg,
                   const PieceMask &free, bool aufBefore, bool aufAfter);

// Every alg of a case checked under the step definition in force (the
// group's own or the set's) and the set's AUF settings, plus how the case
// is shown.
struct CaseAnalysis {
  std::vector<AlgCheck> checks;     // one per alg, in list order
  std::vector<std::size_t> passing; // indices of passing algs
  std::optional<std::size_t> main;  // the first passing alg
  CubieCube display;                // display orientation, or canonical
  std::string mainAlg;              // the main alg as done from `display`

  int failing() const {
    return static_cast<int>(checks.size() - passing.size());
  }
};
CaseAnalysis analyzeCase(const AlgSet &set, const CaseGroup &group,
                         const AlgCase &c);

struct CaseRef {
  std::size_t group = 0;
  std::size_t index = 0;
};

// The case of `set` that `alg` solves, if any, with the U turns before and
// after it that the set allows.
std::optional<CaseRef> matchCase(const AlgSet &set, const Alg &alg);

// The case with the random U turns before and after it that the set asks
// for, so that it is seen from any side, as Learn and Drill set it up.
CubieCube withRandomAufs(const AlgSet &set, const CubieCube &state, Rng &rng);

// A drill state for a case: random U turns around it (as the set asks) and
// the step's free pieces scrambled, so that the case can't be told from
// the scramble.
CubieCube drillState(const AlgSet &set, const CaseGroup &group,
                     const AlgCase &c, Rng &rng);

// The cube state an alg solves (its inverse applied to a solved cube).
CubieCube stateSolvedBy(const Alg &alg);

// Result of importing pasted text into a set.
struct ImportReport {
  int added = 0;                      // algs added to existing cases
  int duplicates = 0;                 // algs the case already had
  int created = 0;                    // new cases created (when allowed)
  std::vector<std::string> unmatched; // lines that matched no case
  std::vector<std::string> invalid;   // lines that failed to parse
};

// Imports algs, one case per line. A line may start with "name:", may
// hold several algs separated by "/" (as Alg-Trainer does) and may end with
// a "//" comment. Algs are stored as cleanAlgText() gives them. Each alg is
// matched to the case it solves; unmatched algs make new cases (named from
// the line or numbered) when `createCases` is set.
ImportReport importAlgs(AlgSet &set, const std::string &text, bool createCases);

} // namespace vcube
