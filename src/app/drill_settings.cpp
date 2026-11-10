#include "app/drill_settings.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QSettings>
#include <QSpinBox>

#include <algorithm>

namespace vcube {

void DrillSettings::load(const QSettings &q) {
  DrillSettings d;
  autoStop = q.value("drill/autoStop", d.autoStop).toBool();
  tapToArm = q.value("drill/tapToArm", d.tapToArm).toBool();
  holdMs = std::clamp(q.value("drill/holdMs", d.holdMs).toInt(), 0, 2000);
  shiftSpaceGivesUp =
      q.value("drill/shiftSpaceGivesUp", d.shiftSpaceGivesUp).toBool();
  practiseAfterDnf =
      q.value("drill/practiseAfterDnf", d.practiseAfterDnf).toBool();
  autoAdvance = q.value("drill/autoAdvance", d.autoAdvance).toBool();
  requeueAfter =
      std::clamp(q.value("drill/requeueAfter", d.requeueAfter).toInt(), 0, 50);
  weakestCount =
      std::clamp(q.value("drill/weakestCount", d.weakestCount).toInt(), 1, 500);
}

void DrillSettings::save(QSettings &q) const {
  q.setValue("drill/autoStop", autoStop);
  q.setValue("drill/tapToArm", tapToArm);
  q.setValue("drill/holdMs", holdMs);
  q.setValue("drill/shiftSpaceGivesUp", shiftSpaceGivesUp);
  q.setValue("drill/practiseAfterDnf", practiseAfterDnf);
  q.setValue("drill/autoAdvance", autoAdvance);
  q.setValue("drill/requeueAfter", requeueAfter);
  q.setValue("drill/weakestCount", weakestCount);
}

DrillSettingsTab::DrillSettingsTab(const DrillSettings &settings,
                                   QWidget *parent)
    : QWidget(parent), settings_(settings) {
  auto *form = new QFormLayout(this);
  startBox_ = new QComboBox(this);
  startBox_->addItem(tr("Tap Space, then the first face turn starts it"), true);
  startBox_->addItem(tr("Hold Space and let go"), false);
  startBox_->setCurrentIndex(settings_.tapToArm ? 0 : 1);
  startBox_->setToolTip(
      tr("On the virtual cube. A physical cube always starts on letting go "
         "of Space."));
  form->addRow(tr("Virtual cube timer start:"), startBox_);
  autoStopBox_ = new QCheckBox(
      tr("Stop as soon as the step is done (virtual cube)"), this);
  autoStopBox_->setToolTip(
      tr("Off: Space stops the timer, and the attempt is a DNF unless the "
         "cube then shows the step done"));
  autoStopBox_->setChecked(settings_.autoStop);
  form->addRow(tr("Timer stop:"), autoStopBox_);
  holdBox_ = new QComboBox(this);
  for (int ms : {0, 150, 300, 550, 1000}) {
    holdBox_->addItem(QString::number(ms / 1000.0, 'g', 3) + " s", ms);
  }
  int hold = holdBox_->findData(settings_.holdMs);
  if (hold < 0) {
    holdBox_->addItem(QString::number(settings_.holdMs / 1000.0, 'g', 3) + " s",
                      settings_.holdMs);
    hold = holdBox_->count() - 1;
  }
  holdBox_->setCurrentIndex(hold);
  holdBox_->setToolTip(tr("Drill's own hold time, for a physical cube and "
                          "for holding Space on the virtual cube"));
  form->addRow(tr("Hold Space before starting:"), holdBox_);
  giveUpBox_ = new QComboBox(this);
  giveUpBox_->addItem(tr("Shift+Space (Backspace then only toggles the DNF)"),
                      true);
  giveUpBox_->addItem(tr("Backspace"), false);
  giveUpBox_->setCurrentIndex(settings_.shiftSpaceGivesUp ? 0 : 1);
  form->addRow(tr("Give a case up with:"), giveUpBox_);
  afterDnfBox_ = new QComboBox(this);
  afterDnfBox_->addItem(tr("Space goes on to the next case (Tab practises)"),
                        false);
  afterDnfBox_->addItem(
      tr("Space practises the case, with the algs and then without"), true);
  afterDnfBox_->setCurrentIndex(settings_.practiseAfterDnf ? 1 : 0);
  form->addRow(tr("After a DNF:"), afterDnfBox_);
  autoAdvanceBox_ = new QCheckBox(
      tr("Go on by itself once each stage is solved (virtual cube)"), this);
  autoAdvanceBox_->setToolTip(
      tr("With the algs, then without them, then the next case"));
  autoAdvanceBox_->setChecked(settings_.autoAdvance);
  form->addRow(tr("Practice:"), autoAdvanceBox_);
  requeueSpin_ = new QSpinBox(this);
  requeueSpin_->setRange(0, 50);
  requeueSpin_->setSpecialValueText(tr("Never"));
  requeueSpin_->setSuffix(tr(" cases later"));
  requeueSpin_->setValue(settings_.requeueAfter);
  requeueSpin_->setToolTip(tr("A case failed comes back this many cases "
                              "later, whatever the order"));
  form->addRow(tr("Bring a failed case back:"), requeueSpin_);
  weakestSpin_ = new QSpinBox(this);
  weakestSpin_->setRange(1, 500);
  weakestSpin_->setSuffix(tr(" cases"));
  weakestSpin_->setValue(settings_.weakestCount);
  weakestSpin_->setToolTip(
      tr("How many cases the \"Weakest\" choice of cases drills"));
  form->addRow(tr("Weakest:"), weakestSpin_);
  auto *note = new QLabel(
      tr("For about 0.3 s after the timer stops, keys are ignored, so that a "
         "second key press doesn't skip the result. A virtual attempt "
         "stopped before any face turn is cancelled rather than recorded."),
      this);
  note->setWordWrap(true);
  form->addRow(note);
}

DrillSettings DrillSettingsTab::result() const {
  DrillSettings d = settings_;
  d.autoStop = autoStopBox_->isChecked();
  d.tapToArm = startBox_->currentData().toBool();
  d.holdMs = holdBox_->currentData().toInt();
  d.shiftSpaceGivesUp = giveUpBox_->currentData().toBool();
  d.practiseAfterDnf = afterDnfBox_->currentData().toBool();
  d.autoAdvance = autoAdvanceBox_->isChecked();
  d.requeueAfter = requeueSpin_->value();
  d.weakestCount = weakestSpin_->value();
  return d;
}

} // namespace vcube
