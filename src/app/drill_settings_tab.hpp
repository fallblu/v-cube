#pragma once

#include "app/drill_settings.hpp"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QSpinBox;

namespace vcube {

// The Drill tab of the Settings dialog.
class DrillSettingsTab : public QWidget {
  Q_OBJECT
public:
  // The rows about starting and stopping the timer go in `timing` from
  // `row` on, where the same choices of the other modes are.
  DrillSettingsTab(const DrillSettings &settings, QFormLayout *timing, int row,
                   QWidget *parent = nullptr);
  DrillSettings result() const;
  QFormLayout *form() const { return form_; }

private:
  DrillSettings settings_;
  QFormLayout *form_ = nullptr;
  QCheckBox *autoStopBox_ = nullptr;
  QComboBox *startBox_ = nullptr;
  QSpinBox *holdSpin_ = nullptr;
  QComboBox *giveUpBox_ = nullptr;
  QComboBox *afterDnfBox_ = nullptr;
  QCheckBox *autoAdvanceBox_ = nullptr;
  QSpinBox *requeueSpin_ = nullptr;
  QSpinBox *weakestSpin_ = nullptr;
};

} // namespace vcube
