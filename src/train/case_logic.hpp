#pragma once

#include "cube/scramble.hpp"
#include "train/algset.hpp"

#include <optional>
#include <string>

namespace vcube {

// U turns (in quarters, 0..3) to do before and after an alg so that it
// solves a given state.
struct AufFit {
  int before = 0;
  int after = 0;
};

// Whether `alg` (any notation, rotations allowed) takes `state` to a state
// that is solved except for the pieces `free` allows, allowing a U turn
// before and after. Prefers no AUF, then U, U', U2.
std::optional<AufFit> fitAlg(const CubieCube &state, const Alg &alg,
                             const PieceMask &free);

// Whether a cube (in standard orientation) has the step done: solved apart
// from `free` pieces, up to U turns before and after.
bool stepSolved(const CubieCube &cube, const PieceMask &free);

// The alg with its AUFs written in, e.g. "(U') R U R' U' (U2)".
std::string withAufs(const std::string &alg, AufFit fit);

// Text of a U turn by `quarters` ("", "U", "U2", "U'").
std::string aufText(int quarters);

// Indices of the case's algs that can be read and solve it, in list order.
// The first is the case's main alg; a case without any is not offered in
// Learn or Drill.
std::vector<std::size_t> passingAlgs(const AlgSet &set, const CaseGroup &group,
                                     const AlgCase &c);

struct CaseRef {
  std::size_t group = 0;
  std::size_t index = 0;
};

// The case of `set` that `alg` solves, if any.
std::optional<CaseRef> matchCase(const AlgSet &set, const Alg &alg);

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

// Imports algs, one case per line. A line may start with "name:" and may
// hold several algs separated by "/" (as Alg-Trainer does). Each alg is
// matched to the case it solves; unmatched algs make new cases (named from
// the line or numbered) when `createCases` is set.
ImportReport importAlgs(AlgSet &set, const std::string &text, bool createCases);

} // namespace vcube
