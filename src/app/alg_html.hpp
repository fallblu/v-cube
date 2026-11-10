#pragma once

#include "cube/cubie.hpp"
#include "cube/move.hpp"
#include "train/learn_attempt.hpp"
#include "train/reduction.hpp"
#include "train/srs.hpp"

#include <QString>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace vcube {

struct AlgSet;

// Algs and moves as rich text, for the reveal under the cube in Learn and
// Drill and the alg panels of Timed and Untimed.

// The algs of a case that solve `state` (the case set up in its display
// orientation, possibly with AUFs and free pieces scrambled), with the
// AUFs they need, main alg first, as rich text under the case's name. For
// Timed and Untimed, which have no case list.
QString caseAlgsHtml(const AlgSet &set, const std::string &caseId,
                     const CubieCube &state);

// The algs of the case a cube shows in `world` as they are done from the
// cube (mirrored for a mirrored view), main alg first, under the case's
// name. A step that stops at another case names it.
QString viewAlgsHtml(const AlgSet &set, const CaseView &view,
                     const CubieCube &state, Mirror world);
// Every step from `state` to the end of a reduction set's step by the
// set's main algs, under the first case's name.
QString chainHtml(const AlgSet &set, const CubieCube &state, Mirror world);

// The moves as done, two quarter turns of a layer the same way written as
// a half turn. The move at `mark` (an index into `moves`) is highlighted
// and kept on its own.
QString movesHtml(const Alg &moves, std::optional<std::size_t> mark);

// A grade's name ("Good") and its colour.
const char *gradeName(Grade g);
QString gradeColour(Grade g);

// Each case of a Learn attempt in a reduction set, with its step and how
// it went (✓ or ✗); `note` says what became of a case's card.
QString linksHtml(const AlgSet &set, const std::vector<LearnLink> &links,
                  const std::function<QString(const LearnLink &)> &note);

} // namespace vcube
