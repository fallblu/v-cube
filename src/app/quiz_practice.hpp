#pragma once

#include "train/learn_quiz.hpp"

#include <QString>
#include <QStringList>

namespace vcube {

struct AlgSet;

// What Learn's quiz (see LearnQuiz) says.

// The choices as they are offered.
QStringList quizChoiceNames(const LearnQuiz &quiz, const AlgSet &set);
// What is said of an answer.
QString quizResultText(const LearnQuiz &quiz, const AlgSet *set,
                       const LearnQuiz::Result &result);

} // namespace vcube
