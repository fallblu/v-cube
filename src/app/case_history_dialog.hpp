#pragma once

#include "train/srs.hpp"

#include <QString>

class QWidget;

namespace vcube {

// Shows a case's history: its spaced-repetition card (`card`, or null when
// it has none), flagged when it is a leech, and its attempts in every mode
// with their results and recognition and execution times, newest first.
// `familyCard`: the card is shared by the case's mirror family.
void showCaseHistory(QWidget *parent, const QString &caseName,
                     const CaseProgress *card,
                     const std::vector<Attempt> &attempts, bool familyCard,
                     const SrsConfig &config);

} // namespace vcube
