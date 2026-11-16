#include "train/f2l_stats.hpp"

#include <algorithm>
#include <numeric>

namespace vcube {

namespace {

template <typename T>
void addCapped(std::vector<T> &to, const T &a, std::size_t cap) {
  to.push_back(a);
  if (to.size() > cap) {
    to.erase(to.begin(),
             to.begin() + static_cast<std::ptrdiff_t>(to.size() - cap));
  }
}

} // namespace

void F2LStats::add(const std::string &caseKey, const PairAttempt &a) {
  addCapped(cases[caseKey], a, kPerCase);
}

void F2LStats::add(const ChoiceAttempt &a) { addCapped(choices, a, kOthers); }

void F2LStats::add(const FullAttempt &a) { addCapped(fulls, a, kOthers); }

double meanPairMs(const F2LStats &stats) {
  double sum = 0.0;
  int n = 0;
  for (const auto &[key, attempts] : stats.cases) {
    for (const PairAttempt &a : attempts) {
      if (!a.gaveUp) {
        sum += static_cast<double>(a.ms);
        ++n;
      }
    }
  }
  return n ? sum / n : 0.0;
}

CaseSummary summarizeCase(const std::vector<PairAttempt> &attempts,
                          double overallMs) {
  CaseSummary s;
  s.attempts = static_cast<int>(attempts.size());
  if (attempts.empty()) {
    s.weight = kUnseenWeight;
    return s;
  }
  std::size_t from =
      attempts.size() > kRecentAttempts ? attempts.size() - kRecentAttempts : 0;
  double excess = 0.0, ms = 0.0;
  int n = 0, timed = 0, helped = 0;
  for (std::size_t i = from; i < attempts.size(); ++i) {
    const PairAttempt &a = attempts[i];
    excess +=
        a.gaveUp ? kGaveUpExcess : a.excess + (a.hint ? kHintExcess : 0.0);
    helped += a.gaveUp || a.hint ? 1 : 0;
    if (!a.gaveUp) {
      ms += static_cast<double>(a.ms);
      ++timed;
    }
    ++n;
  }
  s.excess = excess / n;
  s.hints = static_cast<double>(helped) / n;
  s.meanMs = timed ? ms / timed : 0.0;
  double slow = overallMs > 0.0 && timed
                    ? std::max(0.0, s.meanMs / overallMs - 1.0)
                    : 0.0;
  s.weight = std::clamp(0.5 + 1.5 * s.excess + 2.0 * slow, 0.5, 12.0);
  s.weak = s.weight >= kWeakWeight;
  return s;
}

std::size_t pickWeighted(const std::vector<double> &weights, Rng &rng) {
  double total = std::accumulate(weights.begin(), weights.end(), 0.0);
  if (weights.empty() || total <= 0.0) {
    return 0;
  }
  double at = std::uniform_real_distribution<double>(0.0, total)(rng);
  for (std::size_t i = 0; i < weights.size(); ++i) {
    at -= weights[i];
    if (at < 0.0) {
      return i;
    }
  }
  return weights.size() - 1;
}

} // namespace vcube
