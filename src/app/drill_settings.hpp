#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QSettings;
class QSpinBox;

namespace vcube {

// Drill mode's own preferences, kept in QSettings under "drill/".
struct DrillSettings {
  // Virtual cube: the attempt ends as soon as the cube shows the step
  // done, without Space.
  bool autoStop = true;
  // Virtual cube: a tap of Space arms the timer and the first face turn
  // starts it; off: hold Space and let go, as on a physical cube.
  bool tapToArm = true;
  // How long Space is held before the timer can start (ms).
  int holdMs = 300;
  static constexpr int kMaxHoldMs = 2000;
  // Shift+Space gives a case up; off: Backspace does, as in earlier
  // versions (when it also toggles the DNF afterwards).
  bool shiftSpaceGivesUp = true;
  // After a DNF, Space practises the case (with the algs, then without)
  // rather than going on to the next case.
  bool practiseAfterDnf = false;
  // Practice goes on by itself once each stage is solved.
  bool autoAdvance = true;
  // A failed case comes back after this many other cases (0: never).
  int requeueAfter = 3;
  // The cases the "Weakest" choice drills.
  int weakestCount = 5;

  void load(const QSettings &q);
  void save(QSettings &q) const;

  friend bool operator==(const DrillSettings &,
                         const DrillSettings &) = default;
};

// The Drill tab of the Settings dialog.
class DrillSettingsTab : public QWidget {
  Q_OBJECT
public:
  DrillSettingsTab(const DrillSettings &settings, QWidget *parent = nullptr);
  DrillSettings result() const;

private:
  DrillSettings settings_;
  QCheckBox *autoStopBox_;
  QComboBox *startBox_;
  QSpinBox *holdSpin_;
  QComboBox *giveUpBox_;
  QComboBox *afterDnfBox_;
  QCheckBox *autoAdvanceBox_;
  QSpinBox *requeueSpin_;
  QSpinBox *weakestSpin_;
};

} // namespace vcube
