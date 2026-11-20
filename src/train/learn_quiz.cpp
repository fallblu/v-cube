#include "train/learn_quiz.hpp"

#include "train/algset.hpp"

#include <algorithm>
#include <random>

namespace vcube {

namespace {

// Choices in "which case is this?".
constexpr std::size_t kQuizChoices = 6;

} // namespace

std::optional<LearnQuiz>
LearnQuiz::pick(const AlgSet &set, const std::vector<std::string> &ticked,
                const std::function<bool(const std::string &)> &started,
                const std::string &last, Rng &rng) {
  // Cases already started are asked first.
  std::vector<std::string> known;
  for (const auto &id : ticked) {
    if (started(id)) {
      known.push_back(id);
    }
  }
  const std::vector<std::string> &from = known.size() >= 2 ? known : ticked;
  if (from.size() < 2) {
    return std::nullopt;
  }
  std::vector<std::string> candidates;
  for (const auto &id : from) {
    if (id != last) {
      candidates.push_back(id);
    }
  }
  std::uniform_int_distribution<std::size_t> pickOne(0, candidates.size() - 1);
  std::string id = candidates[pickOne(rng)];
  // The others from its group, the look-alikes, else from the rest.
  const CaseGroup *g = set.groupOf(id);
  std::vector<std::string> others;
  for (const auto &t : ticked) {
    if (t != id && set.groupOf(t) == g) {
      others.push_back(t);
    }
  }
  if (others.empty()) {
    for (const auto &t : from) {
      if (t != id) {
        others.push_back(t);
      }
    }
  }
  std::shuffle(others.begin(), others.end(), rng);
  if (others.size() > kQuizChoices - 1) {
    others.resize(kQuizChoices - 1);
  }
  others.push_back(id);
  std::shuffle(others.begin(), others.end(), rng);
  LearnQuiz quiz;
  quiz.answer_ = static_cast<std::size_t>(
      std::find(others.begin(), others.end(), id) - others.begin());
  quiz.choices_ = std::move(others);
  return quiz;
}

LearnQuiz::Result LearnQuiz::answered(std::size_t choice,
                                      std::int64_t nowMs) const {
  return {choice == answer_, nowMs - shownAtMs_};
}

} // namespace vcube
