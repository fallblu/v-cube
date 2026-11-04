#include "train/stats.hpp"

#include <algorithm>
#include <cstdio>

namespace vcube {

std::int64_t effectiveTime(const Solve &s) {
  switch (s.penalty) {
  case Penalty::Dnf: return kDnfTime;
  case Penalty::Plus2: return s.timeMs + 2000;
  case Penalty::None: break;
  }
  return s.timeMs;
}

StatValue averageOf(std::span<const Solve> solves) {
  std::size_t n = solves.size();
  if (n < 3) {
    return std::nullopt;
  }
  std::vector<std::int64_t> t;
  t.reserve(n);
  for (const auto &s : solves) {
    t.push_back(effectiveTime(s));
  }
  std::sort(t.begin(), t.end());
  std::size_t trim = (n + 19) / 20;
  if (n == 3) {
    trim = 0;
  }
  // Anything DNF beyond the trimmed worst makes the average DNF.
  if (t[n - trim - 1] == kDnfTime) {
    return kDnfTime;
  }
  std::int64_t sum = 0;
  for (std::size_t i = trim; i < n - trim; ++i) {
    sum += t[i];
  }
  auto count = static_cast<std::int64_t>(n - 2 * trim);
  return (sum + count / 2) / count;
}

StatValue meanOf(std::span<const Solve> solves) {
  if (solves.empty()) {
    return std::nullopt;
  }
  std::int64_t sum = 0;
  for (const auto &s : solves) {
    std::int64_t t = effectiveTime(s);
    if (t == kDnfTime) {
      return kDnfTime;
    }
    sum += t;
  }
  auto count = static_cast<std::int64_t>(solves.size());
  return (sum + count / 2) / count;
}

StatValue currentAverage(std::span<const Solve> all, std::size_t n) {
  if (all.size() < n) {
    return std::nullopt;
  }
  return averageOf(all.last(n));
}

StatValue currentMean(std::span<const Solve> all, std::size_t n) {
  if (all.size() < n) {
    return std::nullopt;
  }
  return meanOf(all.last(n));
}

namespace {

template <typename F>
StatValue bestRolling(std::span<const Solve> all, std::size_t n, F stat) {
  StatValue best;
  for (std::size_t end = n; end <= all.size(); ++end) {
    StatValue v = stat(all.subspan(end - n, n));
    if (v && (!best || *v < *best)) {
      best = v;
    }
  }
  return best;
}

} // namespace

StatValue bestAverage(std::span<const Solve> all, std::size_t n) {
  return bestRolling(all, n, averageOf);
}

StatValue bestMean(std::span<const Solve> all, std::size_t n) {
  return bestRolling(all, n, meanOf);
}

std::vector<StatValue> rollingAverages(std::span<const Solve> all,
                                       std::size_t n) {
  std::vector<StatValue> out(all.size());
  for (std::size_t end = n; end <= all.size(); ++end) {
    out[end - 1] = averageOf(all.subspan(end - n, n));
  }
  return out;
}

SessionSummary summarize(std::span<const Solve> all) {
  SessionSummary s;
  std::int64_t sum = 0;
  int valid = 0;
  for (const auto &solve : all) {
    ++s.count;
    std::int64_t t = effectiveTime(solve);
    if (t == kDnfTime) {
      ++s.dnfs;
      continue;
    }
    ++valid;
    sum += t;
    if (!s.best || t < *s.best) {
      s.best = t;
    }
    if (!s.worst || t > *s.worst) {
      s.worst = t;
    }
  }
  if (valid > 0) {
    s.mean = (sum + valid / 2) / valid;
  }
  return s;
}

std::string formatTime(StatValue ms, int decimals) {
  if (!ms) {
    return "-";
  }
  if (*ms == kDnfTime) {
    return "DNF";
  }
  std::int64_t v = *ms;
  // Truncate (as WCA displays) to the requested precision.
  std::int64_t unit = decimals >= 3 ? 1 : decimals == 2 ? 10 : decimals == 1 ? 100 : 1000;
  v = v / unit;
  std::int64_t perSecond = 1000 / unit;
  std::int64_t frac = v % perSecond;
  std::int64_t totalSeconds = v / perSecond;
  std::int64_t hours = totalSeconds / 3600;
  std::int64_t minutes = (totalSeconds / 60) % 60;
  std::int64_t seconds = totalSeconds % 60;
  char buf[64];
  int d = std::clamp(decimals, 0, 3);
  std::string fracText;
  if (d > 0) {
    std::string digits = std::to_string(frac);
    fracText = "." + std::string(static_cast<std::size_t>(d) - std::min(digits.size(), static_cast<std::size_t>(d)), '0') + digits;
  }
  if (hours > 0) {
    std::snprintf(buf, sizeof buf, "%lld:%02lld:%02lld%s",
                  static_cast<long long>(hours), static_cast<long long>(minutes),
                  static_cast<long long>(seconds), fracText.c_str());
  } else if (minutes > 0) {
    std::snprintf(buf, sizeof buf, "%lld:%02lld%s",
                  static_cast<long long>(minutes),
                  static_cast<long long>(seconds), fracText.c_str());
  } else {
    std::snprintf(buf, sizeof buf, "%lld%s", static_cast<long long>(seconds),
                  fracText.c_str());
  }
  return buf;
}

std::string formatSolve(const Solve &s, int decimals) {
  switch (s.penalty) {
  case Penalty::Dnf: return "DNF(" + formatTime(s.timeMs, decimals) + ")";
  case Penalty::Plus2: return formatTime(s.timeMs + 2000, decimals) + "+";
  case Penalty::None: break;
  }
  return formatTime(s.timeMs, decimals);
}

} // namespace vcube
