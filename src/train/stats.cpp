#include "train/stats.hpp"

#include <algorithm>
#include <cstdio>

namespace vcube {

std::string_view cubeKindId(CubeKind k) {
  return k == CubeKind::Virtual ? "virtual" : "physical";
}

std::optional<CubeKind> cubeKindFromId(std::string_view id) {
  if (id == "virtual") {
    return CubeKind::Virtual;
  }
  if (id == "physical") {
    return CubeKind::Physical;
  }
  return std::nullopt;
}

std::int64_t effectiveTime(const Solve &s) {
  switch (s.penalty) {
  case Penalty::Dnf:
    return kDnfTime;
  case Penalty::Plus2:
    return s.timeMs + 2000;
  case Penalty::None:
    break;
  }
  return s.timeMs;
}

std::int64_t statUnit(int decimals) {
  return decimals >= 3 ? 1 : decimals == 2 ? 10 : decimals == 1 ? 100 : 1000;
}

namespace {

// A time truncated to the unit, as the WCA truncates singles.
std::int64_t truncated(std::int64_t t, std::int64_t unit) {
  return t == kDnfTime ? t : t / unit * unit;
}

// The mean of times already truncated to the unit, rounded to the nearest
// unit (halves up).
std::int64_t roundedMean(std::int64_t sum, std::int64_t count,
                         std::int64_t unit) {
  std::int64_t units = sum / unit;
  return (2 * units + count) / (2 * count) * unit;
}

std::size_t trimOf(std::size_t n) { return n == 3 ? 0 : (n + 19) / 20; }

} // namespace

StatValue averageOf(std::span<const Solve> solves, std::int64_t unit) {
  std::size_t n = solves.size();
  if (n < 3) {
    return std::nullopt;
  }
  unit = std::max<std::int64_t>(unit, 1);
  std::vector<std::int64_t> t;
  t.reserve(n);
  for (const auto &s : solves) {
    t.push_back(truncated(effectiveTime(s), unit));
  }
  std::sort(t.begin(), t.end());
  std::size_t trim = trimOf(n);
  // Anything DNF beyond the trimmed worst makes the average DNF.
  if (t[n - trim - 1] == kDnfTime) {
    return kDnfTime;
  }
  std::int64_t sum = 0;
  for (std::size_t i = trim; i < n - trim; ++i) {
    sum += t[i];
  }
  return roundedMean(sum, static_cast<std::int64_t>(n - 2 * trim), unit);
}

std::vector<std::size_t> trimmedIndices(std::span<const Solve> solves) {
  std::size_t n = solves.size();
  std::vector<std::size_t> order(n);
  for (std::size_t i = 0; i < n; ++i) {
    order[i] = i;
  }
  if (n < 3) {
    return {};
  }
  // Ties keep solve order, as cstimer brackets the first best and the last
  // worst.
  std::stable_sort(order.begin(), order.end(), [&](std::size_t a,
                                                   std::size_t b) {
    return effectiveTime(solves[a]) < effectiveTime(solves[b]);
  });
  std::size_t trim = trimOf(n);
  std::vector<std::size_t> out(order.begin(),
                               order.begin() + static_cast<std::ptrdiff_t>(trim));
  out.insert(out.end(), order.end() - static_cast<std::ptrdiff_t>(trim),
             order.end());
  std::sort(out.begin(), out.end());
  return out;
}

StatValue meanOf(std::span<const Solve> solves, std::int64_t unit) {
  if (solves.empty()) {
    return std::nullopt;
  }
  unit = std::max<std::int64_t>(unit, 1);
  std::int64_t sum = 0;
  for (const auto &s : solves) {
    std::int64_t t = effectiveTime(s);
    if (t == kDnfTime) {
      return kDnfTime;
    }
    sum += truncated(t, unit);
  }
  return roundedMean(sum, static_cast<std::int64_t>(solves.size()), unit);
}

StatValue currentAverage(std::span<const Solve> all, std::size_t n,
                         std::int64_t unit) {
  if (all.size() < n) {
    return std::nullopt;
  }
  return averageOf(all.last(n), unit);
}

StatValue currentMean(std::span<const Solve> all, std::size_t n,
                      std::int64_t unit) {
  if (all.size() < n) {
    return std::nullopt;
  }
  return meanOf(all.last(n), unit);
}

namespace {

template <typename F>
std::optional<std::size_t> bestRollingStart(std::span<const Solve> all,
                                            std::size_t n, F stat) {
  StatValue best;
  std::optional<std::size_t> at;
  for (std::size_t end = n; end <= all.size() && n > 0; ++end) {
    StatValue v = stat(all.subspan(end - n, n));
    if (v && (!best || *v < *best)) {
      best = v;
      at = end - n;
    }
  }
  return at;
}

} // namespace

std::optional<std::size_t> bestAverageStart(std::span<const Solve> all,
                                            std::size_t n, bool mean,
                                            std::int64_t unit) {
  return mean ? bestRollingStart(all, n,
                                 [unit](std::span<const Solve> s) {
                                   return meanOf(s, unit);
                                 })
              : bestRollingStart(all, n, [unit](std::span<const Solve> s) {
                  return averageOf(s, unit);
                });
}

StatValue bestAverage(std::span<const Solve> all, std::size_t n,
                      std::int64_t unit) {
  auto at = bestAverageStart(all, n, false, unit);
  return at ? averageOf(all.subspan(*at, n), unit) : StatValue{};
}

StatValue bestMean(std::span<const Solve> all, std::size_t n,
                   std::int64_t unit) {
  auto at = bestAverageStart(all, n, true, unit);
  return at ? meanOf(all.subspan(*at, n), unit) : StatValue{};
}

std::vector<StatValue> rollingAverages(std::span<const Solve> all,
                                       std::size_t n, std::int64_t unit) {
  std::vector<StatValue> out(all.size());
  for (std::size_t end = n; end <= all.size(); ++end) {
    out[end - 1] = averageOf(all.subspan(end - n, n), unit);
  }
  return out;
}

std::vector<NewBest> newBests(std::span<const Solve> all, std::int64_t unit) {
  std::vector<NewBest> out;
  if (all.size() < 2) {
    return out;
  }
  std::span<const Solve> before = all.first(all.size() - 1);
  auto check = [&](const std::string &name, StatValue now, StatValue was) {
    if (now && *now != kDnfTime && was && *now < *was) {
      out.push_back({name, *now});
    }
  };
  // A single: better than every earlier single (DNFs don't count).
  StatValue bestSingle;
  for (const Solve &s : before) {
    std::int64_t t = effectiveTime(s);
    if (t != kDnfTime && (!bestSingle || t < *bestSingle)) {
      bestSingle = t;
    }
  }
  std::int64_t last = effectiveTime(all.back());
  check("single", last == kDnfTime ? StatValue{} : StatValue{last},
        bestSingle);
  check("mo3", currentMean(all, 3, unit), bestMean(before, 3, unit));
  for (std::size_t n : {5u, 12u, 100u}) {
    check("ao" + std::to_string(n), currentAverage(all, n, unit),
          bestAverage(before, n, unit));
  }
  return out;
}

SessionSummary summarize(std::span<const Solve> all, std::int64_t unit) {
  unit = std::max<std::int64_t>(unit, 1);
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
    sum += truncated(t, unit);
    if (!s.best || t < *s.best) {
      s.best = t;
    }
    if (!s.worst || t > *s.worst) {
      s.worst = t;
    }
  }
  if (valid > 0) {
    s.mean = roundedMean(sum, valid, unit);
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
  std::int64_t unit = decimals >= 3   ? 1
                      : decimals == 2 ? 10
                      : decimals == 1 ? 100
                                      : 1000;
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
    fracText =
        "." +
        std::string(static_cast<std::size_t>(d) -
                        std::min(digits.size(), static_cast<std::size_t>(d)),
                    '0') +
        digits;
  }
  if (hours > 0) {
    std::snprintf(buf, sizeof buf, "%lld:%02lld:%02lld%s",
                  static_cast<long long>(hours),
                  static_cast<long long>(minutes),
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
  case Penalty::Dnf:
    return "DNF(" + formatTime(s.timeMs, decimals) + ")";
  case Penalty::Plus2:
    return formatTime(s.timeMs + 2000, decimals) + "+";
  case Penalty::None:
    break;
  }
  return formatTime(s.timeMs, decimals);
}

} // namespace vcube
