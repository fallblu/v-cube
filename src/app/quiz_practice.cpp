#include "app/quiz_practice.hpp"

#include "app/ui_text.hpp"
#include "train/algset.hpp"

namespace vcube {

namespace {

constexpr char kTrContext[] = "vcube::LearnPage";
constexpr auto tr = trIn<kTrContext>;

} // namespace

QStringList quizChoiceNames(const LearnQuiz &quiz, const AlgSet &set) {
  QStringList names;
  for (const auto &choice : quiz.choiceIds()) {
    const AlgCase *c = set.findCase(choice);
    names << (c ? qs(c->name) : QString());
  }
  return names;
}

QString quizResultText(const LearnQuiz &quiz, const AlgSet *set,
                       const LearnQuiz::Result &result) {
  const AlgCase *c = set ? set->findCase(quiz.caseId()) : nullptr;
  return result.right ? tr("Right, in %1 s.")
                            .arg(secondsText(static_cast<double>(result.ms)))
                      : tr("It was %1.").arg(c ? qs(c->name) : QString());
}

} // namespace vcube
