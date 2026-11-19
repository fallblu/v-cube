#include "app/quiz_practice.hpp"

#include "app/ui_text.hpp"
#include "train/algset.hpp"

#include <QCoreApplication>

#include <algorithm>
#include <random>

namespace vcube {

namespace {

QString tr(const char *text) {
  return QCoreApplication::translate("vcube::LearnPage", text);
}

// Choices in "which case is this?".
constexpr std::size_t kQuizChoices = 6;

} // namespace

std::optional<QuizPractice>
QuizPractice::pick(const AlgSet &set, const std::vector<std::string> &ticked,
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
  QuizPractice quiz;
  quiz.answer_ = static_cast<std::size_t>(
      std::find(others.begin(), others.end(), id) - others.begin());
  quiz.choices_ = std::move(others);
  return quiz;
}

QStringList QuizPractice::names(const AlgSet &set) const {
  QStringList names;
  for (const auto &choice : choices_) {
    const AlgCase *c = set.findCase(choice);
    names << (c ? qs(c->name) : QString());
  }
  return names;
}

QuizPractice::Result QuizPractice::answered(std::size_t choice,
                                            std::int64_t nowMs) const {
  return {choice == answer_, nowMs - shownAtMs_};
}

QString QuizPractice::resultText(const AlgSet *set,
                                 const Result &result) const {
  const AlgCase *c = set ? set->findCase(caseId()) : nullptr;
  return result.right ? tr("Right, in %1 s.")
                            .arg(secondsText(static_cast<double>(result.ms)))
                      : tr("It was %1.").arg(c ? qs(c->name) : QString());
}

} // namespace vcube
