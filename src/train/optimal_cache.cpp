#include "train/optimal_cache.hpp"

#include "train/step_solver.hpp"

#include <algorithm>

namespace vcube {

std::optional<int> OptimalCache::lookup(SolveStep step,
                                        const std::string &scramble,
                                        bool anyHold) const {
  auto it = known_.find({step, scramble, anyHold});
  if (it == known_.end()) {
    return std::nullopt;
  }
  return it->second;
}

void OptimalCache::invalidate() {
  ++round_;
  known_.clear();
}

std::optional<OptimalCache::Job>
OptimalCache::missing(SolveStep step, bool anyHold, std::vector<Hold> holds,
                      std::span<const MoveCountAttempt> attempts) const {
  Job job;
  job.step = step;
  job.anyHold = anyHold;
  job.holds = std::move(holds);
  job.round = round_;
  for (const MoveCountAttempt &a : attempts) {
    if (!a.scramble.empty() && !known_.contains({step, a.scramble, anyHold}) &&
        std::find(job.scrambles.begin(), job.scrambles.end(), a.scramble) ==
            job.scrambles.end()) {
      job.scrambles.push_back(a.scramble);
    }
  }
  if (job.scrambles.empty()) {
    return std::nullopt;
  }
  return job;
}

OptimalCache::Results OptimalCache::run(const Job &job) {
  Results out;
  for (const std::string &scramble : job.scrambles) {
    ParseResult parsed = parseAlg(scramble);
    std::optional<int> best =
        parsed ? heldDistance(parsed.alg, job.step, job.holds) : std::nullopt;
    out.emplace_back(scramble, best.value_or(-1));
  }
  return out;
}

bool OptimalCache::store(const Job &job, const Results &results) {
  if (job.round != round_) {
    return false;
  }
  for (const auto &[scramble, length] : results) {
    known_[{job.step, scramble, job.anyHold}] = length;
  }
  return true;
}

OverOptimal overOptimal(
    std::span<const MoveCountAttempt> attempts,
    const std::function<std::optional<int>(const MoveCountAttempt &)> &best) {
  OverOptimal out;
  int over = 0;
  for (const MoveCountAttempt &a : attempts) {
    std::optional<int> b = best(a);
    if (a.counted && !a.dnf && b && *b >= 0) {
      over += a.moves - *b;
      out.optimal += a.moves <= *b;
      ++out.known;
    }
  }
  if (out.known > 0) {
    out.mean = static_cast<double>(over) / out.known;
  }
  return out;
}

std::vector<std::size_t> worstAttempts(
    std::span<const MoveCountAttempt> attempts, std::size_t n,
    const std::function<std::optional<int>(const MoveCountAttempt &)> &best) {
  struct Ranked {
    std::size_t index;
    bool dnf;
    int over;
    int moves;
  };
  std::vector<Ranked> ranked;
  for (std::size_t i = 0; i < attempts.size(); ++i) {
    const MoveCountAttempt &a = attempts[i];
    if (!a.counted || a.scramble.empty()) {
      continue;
    }
    std::optional<int> b = best ? best(a) : std::nullopt;
    int over = b && *b >= 0 ? a.moves - *b : 0;
    ranked.push_back({i, a.dnf, over, a.moves});
  }
  std::stable_sort(ranked.begin(), ranked.end(),
                   [](const Ranked &a, const Ranked &b) {
                     if (a.dnf != b.dnf) {
                       return a.dnf;
                     }
                     if (a.over != b.over) {
                       return a.over > b.over;
                     }
                     return a.moves > b.moves;
                   });
  std::vector<std::size_t> out;
  for (std::size_t i = 0; i < std::min(n, ranked.size()); ++i) {
    out.push_back(ranked[i].index);
  }
  return out;
}

} // namespace vcube
