#pragma once

#include "cube/scramble.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace vcube {

struct AlgSet;

// Learn's "Which case?": a case of the set is shown, to be named among
// others of its group, the look-alikes. Not graded.
class LearnQuiz {
public:
  // A new question from the `ticked` cases of `set`: about one already
  // started (`started` says whether a case is) when two or more are, and
  // not about `last` again. nullopt with fewer than two cases to ask.
  static std::optional<LearnQuiz>
  pick(const AlgSet &set, const std::vector<std::string> &ticked,
       const std::function<bool(const std::string &)> &started,
       const std::string &last, Rng &rng);

  // The case shown.
  const std::string &caseId() const { return choices_[answer_]; }
  // The cases to choose from, in the order they are offered, and which of
  // them it is.
  const std::vector<std::string> &choiceIds() const { return choices_; }
  std::size_t choices() const { return choices_.size(); }
  std::size_t answer() const { return answer_; }

  // The case is on the cube: the answer is timed from now.
  void shown(std::int64_t nowMs) { shownAtMs_ = nowMs; }
  struct Result {
    bool right = false;
    std::int64_t ms = 0; // from the case being shown
  };
  Result answered(std::size_t choice, std::int64_t nowMs) const;

private:
  std::vector<std::string> choices_;
  std::size_t answer_ = 0;
  std::int64_t shownAtMs_ = 0;
};

} // namespace vcube
