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

// A step with the U turn before it written in, joined with the U turns
// the step starts with whether or not one is needed: "U (R U' R')" a
// quarter turn on reads "U2 (R U' R')". The U turn is written bare before
// a step in brackets, as trigger steps are, and as "(U2) ..." otherwise.
std::string withAufBefore(const std::string &alg, int before);

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
  // In a reduction set (see reduction.hpp): the alg passes by stopping at
  // this other case of the set instead of finishing the step.
  std::string reducesTo;     // its id, or empty
  std::string reducesToName; // and its name

  bool passes() const { return fit.has_value(); }
};
// Checks an alg against a case under a step definition, allowing a U turn
// before and/or after it as the set does.
AlgCheck checkAlg(const CubieCube &state, std::string_view alg,
                  const PieceMask &free, bool aufBefore, bool aufAfter);
// Short visible result: "(U') … (U2), 14 moves" for a pass ("→ Sune, 3
// moves" for a step that stops at another case), otherwise e.g.
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
// is shown. In a reduction set an alg also passes when, done as written
// from the case as stored, it stops at another case of the set.
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

// One alg of a case checked as analyzeCase() checks it, whether or not the
// case has it yet.
AlgCheck checkCaseAlg(const AlgSet &set, const CaseGroup &group,
                      const AlgCase &c, std::string_view alg);

struct CaseRef {
  std::size_t group = 0;
  std::size_t index = 0;
};

// The case of `set` that `alg` solves, if any, with the U turns before and
// after it that the set allows.
std::optional<CaseRef> matchCase(const AlgSet &set, const Alg &alg);
// Every case of `set` that `alg` solves, in set order.
std::vector<CaseRef> matchCases(const AlgSet &set, const Alg &alg);

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

// One alg of pasted text, as read from its line.
struct PasteItem {
  int line = 0;       // 1-based line of the text
  std::string name;   // the line's case name, or empty
  std::string alg;    // as cleanAlgText() gives it
  std::string caseId; // a case chosen for it, which skips the matching
};

// Reads pasted text, one case per line. A line may start with "name:" (or
// be tab-separated "name<TAB>alg<TAB>alg...", as copied from a
// spreadsheet), may hold several algs separated by "/" (as Alg-Trainer
// does), may be numbered ("12." or "3)") and may end with a "//" comment.
std::vector<PasteItem> readPaste(const std::string &text);

enum class PasteOutcome : std::uint8_t {
  Added,     // goes into `caseId`
  Duplicate, // `caseId` already has it
  NewCase,   // makes the new case `caseId`
  Unmatched, // solves no case (and no case is made)
  Mismatch,  // the line names a case the alg doesn't solve, but it solves
             // `candidates`
  Ambiguous, // solves several cases (`candidates`) and the line names none
  Invalid,   // can't be read
};

struct PasteResult {
  PasteOutcome outcome = PasteOutcome::Unmatched;
  std::string caseId;                  // where it goes
  std::vector<std::string> candidates; // the cases it solves
  std::string reason;                  // e.g. "solves Sune and Antisune"
  bool filed() const {
    return outcome == PasteOutcome::Added || outcome == PasteOutcome::NewCase;
  }
};

// Result of importing pasted text into a set.
struct ImportReport {
  int added = 0;                      // algs added to existing cases
  int duplicates = 0;                 // algs the case already had
  int created = 0;                    // new cases created (when allowed)
  std::vector<std::string> unmatched; // algs that matched no case
  std::vector<std::string> ambiguous; // algs that matched several cases, or
                                      // another case than the one named
  std::vector<std::string> invalid;   // algs that failed to parse
};

// What pasting `items` into `set` does: one result per item, and the set
// with the filed algs added.
struct PastePlan {
  AlgSet set;
  std::vector<PasteResult> results;
  ImportReport report;
};

// Files each alg under a case. An alg goes to its chosen case if it has
// one; otherwise to the case the line names (by name or id, ignoring case)
// when it solves it, as analyzeCase() checks; otherwise to the one case it
// solves (any AUF the set allows, any rotations). An alg that solves
// another case than the one named, or several cases when none is named,
// isn't filed but reported. An alg that solves no case makes a new case in
// group `newCaseGroup` (named from the line, or numbered) when
// `createCases` is set, except in a set solved a step at a time, where a
// step alg doesn't tell its case. Later lines see the cases earlier lines
// made.
PastePlan planPaste(const AlgSet &set, const std::vector<PasteItem> &items,
                    bool createCases, std::size_t newCaseGroup);

// planPaste() of readPaste(text) applied to `set`, with new cases going to
// the last group.
ImportReport importAlgs(AlgSet &set, const std::string &text, bool createCases);

} // namespace vcube
