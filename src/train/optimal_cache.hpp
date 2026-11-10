#pragma once

#include "cube/hold.hpp"
#include "train/move_count.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace vcube {

// The optimal lengths of Move Count attempts' scrambles for their step, by
// step, scramble and which holds count (any hold, or those of the colour
// neutrality). They take a while, so they are worked out in jobs off the
// GUI thread: missing() makes a job for the scrambles not known yet, run()
// works it out (on any thread) and store() keeps its results, unless the
// holds changed in the meantime (invalidate()), which makes them stale.
class OptimalCache {
public:
  using Results = std::vector<std::pair<std::string, int>>;
  struct Job {
    SolveStep step = SolveStep::EOCross;
    bool anyHold = false;
    std::vector<Hold> holds;            // the holds the lengths are for
    std::vector<std::string> scrambles; // each once
    int round = 0;                      // the cache's, when it was made
  };

  // nullopt while it isn't known, -1 for a scramble that can't be read.
  std::optional<int> lookup(SolveStep step, const std::string &scramble,
                            bool anyHold) const;
  // The holds changed: what is known, and the jobs under way, are stale.
  void invalidate();
  // A job for the attempts' scrambles that aren't known yet (each once, in
  // their order, empty ones left out); nullopt when none are missing.
  std::optional<Job> missing(SolveStep step, bool anyHold,
                             std::vector<Hold> holds,
                             std::span<const MoveCountAttempt> attempts) const;
  // Works a job out: each scramble's optimal length for the step in the
  // job's holds, -1 for one that can't be read (so it isn't tried again).
  static Results run(const Job &job);
  // Keeps a job's results; false (nothing kept) for a stale job.
  bool store(const Job &job, const Results &results);

private:
  using Key = std::tuple<SolveStep, std::string, bool>;
  std::map<Key, int> known_;
  int round_ = 0;
};

// How far over the optimal the counted, finished attempts with a known
// optimal length were.
struct OverOptimal {
  int known = 0;   // attempts with an optimal length
  int optimal = 0; // ...that took no more moves than it
  double mean = 0; // moves over it, on average
};
OverOptimal overOptimal(
    std::span<const MoveCountAttempt> attempts,
    const std::function<std::optional<int>(const MoveCountAttempt &)> &best);

// The worst `n` attempts to retry, by index: counted attempts with a
// scramble, DNFs first, then the most moves over the optimal (`best`,
// when it gives a length), then the most moves; ties in their order.
std::vector<std::size_t> worstAttempts(
    std::span<const MoveCountAttempt> attempts, std::size_t n,
    const std::function<std::optional<int>(const MoveCountAttempt &)> &best);

} // namespace vcube
