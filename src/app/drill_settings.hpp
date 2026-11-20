#pragma once

namespace vcube {

// Drill mode's own preferences, kept in QSettings under "drill/" (see
// AppSettings).
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
  // Shift+Space gives a case up; off: Backspace does (and also toggles
  // the DNF afterwards).
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

  friend bool operator==(const DrillSettings &,
                         const DrillSettings &) = default;
};

} // namespace vcube
