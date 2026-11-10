#pragma once

#include "train/srs.hpp"

#include <QString>

class QWidget;

namespace vcube {

// Shows a case's drill history in one drill mode: its attempts kept (the
// last CaseProgress::kMaxAttempts) as a chart, and every attempt ever made
// summed up by day, as a table and as a chart of the daily means.
void showDrillHistory(QWidget *parent, const QString &caseName,
                      const CaseProgress *progress, AttemptMode mode,
                      int decimals);

} // namespace vcube
