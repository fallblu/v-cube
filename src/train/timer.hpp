#pragma once

#include "train/stats.hpp"

#include <cstdint>
#include <optional>

namespace vcube {

// cstimer-style solve timer as a pure state machine; all times are
// milliseconds on a monotonic clock supplied by the caller.
//
// Physical cube (space bar):
//   Idle --press--> Holding --(held >= holdMs)--> Ready --release--> Running
//   Running --any key--> Stopped
// With inspection, a press-and-release in Idle first starts Inspecting;
// holding from there arms the timer as above. Starting after 15 s of
// inspection is +2, after 17 s DNF.
//
// Virtual cube: arm() starts inspection (or waits), startNow() starts the
// solve on the first turn, stopNow() ends it when the cube is solved.
class SolveTimer {
public:
  enum class State { Idle, Inspecting, Holding, Ready, Running, Stopped };

  struct Config {
    std::int64_t holdMs = 300;
    bool inspection = false;
    std::int64_t inspectionMs = 15000;
  };

  struct Result {
    std::int64_t timeMs = 0;
    Penalty penalty = Penalty::None;
  };

  void setConfig(const Config &c) { config_ = c; }
  const Config &config() const { return config_; }
  State state() const { return state_; }

  void pressStart(std::int64_t now);   // start key (space) down
  void releaseStart(std::int64_t now); // start key up
  void pressStop(std::int64_t now);    // any key down while running
  void tick(std::int64_t now);         // advances Holding -> Ready
  void cancel();                       // abandon inspection or the solve

  // Virtual-cube control.
  void arm(std::int64_t now);      // scrambled: inspect or wait for a move
  void startNow(std::int64_t now); // first move
  void stopNow(std::int64_t now);  // solved

  // Running time, or the final time once stopped.
  std::int64_t elapsed(std::int64_t now) const;
  // Milliseconds of inspection so far (nullopt when not inspecting).
  std::optional<std::int64_t> inspectionElapsed(std::int64_t now) const;
  // Penalty the inspection would give if the solve started now.
  Penalty inspectionPenalty(std::int64_t now) const;

  // The result of the last stopped solve, once.
  std::optional<Result> takeResult();

private:
  Config config_;
  State state_ = State::Idle;
  std::optional<std::int64_t> inspectStart_;
  std::int64_t holdStart_ = 0;
  std::int64_t start_ = 0;
  std::int64_t final_ = 0;
  Penalty penalty_ = Penalty::None;
  bool armedForInspection_ = false;
  std::optional<Result> result_;

  void begin(std::int64_t now);
};

} // namespace vcube
