#include "train/stats.hpp"

#include "train/enum_ids.hpp"

#include <algorithm>
#include <cstdio>

namespace vcube {

constexpr std::array<std::string_view, 2> kCubeKindIds = {"virtual",
                                                          "physical"};

std::string_view cubeKindId(CubeKind k) {
  return idOf(kCubeKindIds, k, "physical");
}

std::optional<CubeKind> cubeKindFromId(std::string_view id) {
  return fromId<CubeKind>(kCubeKindIds, id);
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

// The average of n times in ascending order, already truncated to the
// unit (n at least 3).
StatValue averageOfSorted(const std::vector<std::int64_t> &t,
                          std::int64_t unit) {
  std::size_t n = t.size();
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

// Calls visit(start, average) with averageOf() of every n solves in a
// row, in order. The window's times are kept sorted from one window to
// the next (one time leaves, one comes in), where sorting each window
// afresh is what makes a long session slow. Nothing is visited for n
// under 3, which has no average.
template <typename F>
void rollingAverageOf(std::span<const Solve> all, std::size_t n,
                      std::int64_t unit, F visit) {
  if (n < 3 || all.size() < n) {
    return;
  }
  unit = std::max<std::int64_t>(unit, 1);
  auto time = [&](std::size_t i) {
    return truncated(effectiveTime(all[i]), unit);
  };
  std::vector<std::int64_t> window;
  window.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    window.push_back(time(i));
  }
  std::sort(window.begin(), window.end());
  visit(std::size_t{0}, averageOfSorted(window, unit));
  for (std::size_t end = n + 1; end <= all.size(); ++end) {
    window.erase(
        std::lower_bound(window.begin(), window.end(), time(end - n - 1)));
    std::int64_t in = time(end - 1);
    window.insert(std::upper_bound(window.begin(), window.end(), in), in);
    visit(end - n, averageOfSorted(window, unit));
  }
}

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
  return averageOfSorted(t, unit);
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
  std::stable_sort(order.begin(), order.end(),
                   [&](std::size_t a, std::size_t b) {
                     return effectiveTime(solves[a]) < effectiveTime(solves[b]);
                   });
  std::size_t trim = trimOf(n);
  std::vector<std::size_t> out(
      order.begin(), order.begin() + static_cast<std::ptrdiff_t>(trim));
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
                                            std::size_t n, Rolling rolling,
                                            std::int64_t unit) {
  if (rolling == Rolling::Mean) {
    return bestRollingStart(
        all, n, [unit](std::span<const Solve> s) { return meanOf(s, unit); });
  }
  // The first of the best, as bestRollingStart() picks.
  StatValue best;
  std::optional<std::size_t> at;
  rollingAverageOf(all, n, unit, [&](std::size_t start, StatValue v) {
    if (v && (!best || *v < *best)) {
      best = v;
      at = start;
    }
  });
  return at;
}

StatValue bestAverage(std::span<const Solve> all, std::size_t n,
                      std::int64_t unit) {
  auto at = bestAverageStart(all, n, Rolling::Average, unit);
  return at ? averageOf(all.subspan(*at, n), unit) : StatValue{};
}

StatValue bestMean(std::span<const Solve> all, std::size_t n,
                   std::int64_t unit) {
  auto at = bestAverageStart(all, n, Rolling::Mean, unit);
  return at ? meanOf(all.subspan(*at, n), unit) : StatValue{};
}

std::vector<StatValue> rollingAverages(std::span<const Solve> all,
                                       std::size_t n, std::int64_t unit) {
  std::vector<StatValue> out(all.size());
  rollingAverageOf(all, n, unit, [&](std::size_t start, StatValue v) {
    out[start + n - 1] = v;
  });
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
  check("single", last == kDnfTime ? StatValue{} : StatValue{last}, bestSingle);
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

std::optional<StatId> parseStatId(std::string_view id) {
  if (id == "single") {
    return StatId{};
  }
  if (id.size() < 3 || (!id.starts_with("mo") && !id.starts_with("ao"))) {
    return std::nullopt;
  }
  std::size_t n = 0;
  for (char c : id.substr(2)) {
    if (c < '0' || c > '9' || n > 100000) {
      return std::nullopt;
    }
    n = n * 10 + static_cast<std::size_t>(c - '0');
  }
  if (n == 0) {
    return std::nullopt;
  }
  return StatId{id[0] == 'm' ? StatId::Kind::Mean : StatId::Kind::Average, n};
}

std::optional<StatSpan> statSpan(std::span<const Solve> all, StatId id,
                                 StatPick pick, std::int64_t unit) {
  const bool best = pick == StatPick::Best;
  if (all.empty()) {
    return std::nullopt;
  }
  if (id.kind == StatId::Kind::Single) {
    std::size_t at = all.size() - 1;
    if (best) {
      for (std::size_t i = 0; i < all.size(); ++i) {
        if (effectiveTime(all[i]) < effectiveTime(all[at])) {
          at = i;
        }
      }
    }
    return StatSpan{at, 1};
  }
  if (id.n == 0 || all.size() < id.n) {
    return std::nullopt;
  }
  if (!best) {
    return StatSpan{all.size() - id.n, id.n};
  }
  std::optional<std::size_t> at = bestAverageStart(
      all, id.n,
      id.kind == StatId::Kind::Mean ? Rolling::Mean : Rolling::Average, unit);
  if (!at) {
    return std::nullopt;
  }
  return StatSpan{*at, id.n};
}

SessionStats sessionStats(std::span<const Solve> all,
                          std::span<const std::string_view> ids,
                          std::int64_t unit) {
  SessionStats out;
  out.summary = summarize(all, unit);
  for (std::string_view name : ids) {
    StatLine line;
    line.id = std::string(name);
    if (std::optional<StatId> id = parseStatId(name)) {
      switch (id->kind) {
      case StatId::Kind::Single:
        if (!all.empty()) {
          line.current = effectiveTime(all.back());
        }
        line.best = out.summary.best;
        break;
      case StatId::Kind::Mean:
        line.current = currentMean(all, id->n, unit);
        line.best = bestMean(all, id->n, unit);
        break;
      case StatId::Kind::Average:
        line.current = currentAverage(all, id->n, unit);
        line.best = bestAverage(all, id->n, unit);
        break;
      }
    }
    out.lines.push_back(std::move(line));
  }
  return out;
}

} // namespace vcube
