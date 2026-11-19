#include "train/alg_lab.hpp"

#include "cube/move.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace vcube {

CubieCube labCaseState(const LabCase &c) {
  auto parsed = parseAlg(c.setup);
  if (!parsed) {
    return {};
  }
  return cubieOf(parsed.alg);
}

PieceMask labCaseFree(const LabCase &c) {
  auto m = parseFree(c.free);
  return m ? *m : PieceMask{};
}

std::string LabCaseKey::text() const {
  return isLab() ? "lab:" + caseId : "set:" + setId + "/" + caseId;
}

std::optional<LabCaseKey> LabCaseKey::parse(std::string_view text) {
  if (text.starts_with("lab:") && text.size() > 4) {
    return LabCaseKey{"", std::string(text.substr(4))};
  }
  if (text.starts_with("set:")) {
    std::string_view rest = text.substr(4);
    auto slash = rest.find('/');
    if (slash == std::string_view::npos || slash == 0 ||
        slash + 1 == rest.size()) {
      return std::nullopt;
    }
    return LabCaseKey{std::string(rest.substr(0, slash)),
                      std::string(rest.substr(slash + 1))};
  }
  return std::nullopt;
}

std::string labAlgKey(std::string_view alg) {
  auto parsed = parseAlg(alg);
  if (!parsed || parsed.alg.empty()) {
    return cleanAlgText(alg);
  }
  return toString(parsed.alg);
}

std::string_view candidateSourceId(CandidateSource s) {
  switch (s) {
  case CandidateSource::Set:
    return "set";
  case CandidateSource::Generated:
    return "generated";
  case CandidateSource::Pasted:
    return "pasted";
  case CandidateSource::Typed:
    return "typed";
  }
  return "typed";
}

std::optional<CandidateSource> candidateSourceFromId(std::string_view id) {
  for (auto s : {CandidateSource::Set, CandidateSource::Generated,
                 CandidateSource::Pasted, CandidateSource::Typed}) {
    if (candidateSourceId(s) == id) {
      return s;
    }
  }
  return std::nullopt;
}

std::string_view labOrderId(LabOrder o) {
  return o == LabOrder::Blocked ? "blocked" : "interleaved";
}

std::optional<LabOrder> labOrderFromId(std::string_view id) {
  if (id == "blocked") {
    return LabOrder::Blocked;
  }
  if (id == "interleaved") {
    return LabOrder::Interleaved;
  }
  return std::nullopt;
}

std::vector<int> repsDone(const LabSession &s,
                          std::span<const LabAttempt> attempts) {
  std::vector<int> done(s.entries.size(), 0);
  for (const auto &a : attempts) {
    if (a.session != s.id) {
      continue;
    }
    for (std::size_t i = 0; i < s.entries.size(); ++i) {
      if (s.entries[i].key == a.key && s.entries[i].alg == a.alg) {
        ++done[i];
        break;
      }
    }
  }
  return done;
}

std::optional<std::size_t> nextEntry(const LabSession &s,
                                     std::span<const LabAttempt> attempts,
                                     std::optional<std::size_t> last,
                                     Rng &rng) {
  std::vector<int> done = repsDone(s, attempts);
  if (s.order == LabOrder::Blocked) {
    for (std::size_t i = 0; i < done.size(); ++i) {
      if (done[i] < s.reps) {
        return i;
      }
    }
    return std::nullopt;
  }
  int fewest = s.reps;
  for (int d : done) {
    fewest = std::min(fewest, d);
  }
  if (fewest >= s.reps) {
    return std::nullopt;
  }
  std::vector<std::size_t> pick;
  for (std::size_t i = 0; i < done.size(); ++i) {
    if (done[i] == fewest && (!last || *last != i)) {
      pick.push_back(i);
    }
  }
  if (pick.empty()) {
    // Only the entry just timed is behind: the next round starts with it.
    return last;
  }
  std::uniform_int_distribution<std::size_t> d(0, pick.size() - 1);
  return pick[d(rng)];
}

double LabStats::dnfRate() const {
  return timed == 0 ? 0.0 : static_cast<double>(dnfs) / timed;
}

namespace {

bool inWindowKind(const LabAttempt &a, const LabWindow &w) {
  return w.kind != LabWindow::Kind::Session || a.session == w.session;
}

// The attempts of the alg in the window, oldest first.
std::vector<const LabAttempt *> windowed(std::span<const LabAttempt> attempts,
                                         const LabCaseKey &key,
                                         std::string_view alg, CubeKind cube,
                                         const LabWindow &window) {
  std::vector<const LabAttempt *> out;
  for (const auto &a : attempts) {
    if (a.key == key && a.alg == alg && a.cube == cube &&
        inWindowKind(a, window)) {
      out.push_back(&a);
    }
  }
  if (window.kind == LabWindow::Kind::Recent && window.recent >= 0 &&
      out.size() > static_cast<std::size_t>(window.recent)) {
    out.erase(out.begin(), out.end() - window.recent);
  }
  return out;
}

std::vector<LabAttempt> afterWarmup(std::span<const LabAttempt> attempts,
                                    const LabCaseKey &key, std::string_view alg,
                                    CubeKind cube, const LabWindow &window,
                                    const WarmupOf &warmupOf, int *inWindow,
                                    int *deviated) {
  // Warm-up reps are counted over the whole session, so they are found
  // before the window cuts it.
  std::map<std::string, int> seen;
  std::vector<const LabAttempt *> warm;
  for (const auto &a : attempts) {
    if (a.key == key && a.alg == alg && a.cube == cube) {
      int skip = warmupOf ? warmupOf(a.session) : 0;
      if (seen[a.session]++ < skip) {
        warm.push_back(&a);
      }
    }
  }
  auto in = windowed(attempts, key, alg, cube, window);
  if (inWindow) {
    *inWindow = static_cast<int>(in.size());
  }
  std::vector<LabAttempt> out;
  int dev = 0;
  for (const LabAttempt *a : in) {
    if (std::find(warm.begin(), warm.end(), a) != warm.end()) {
      continue;
    }
    if (a->deviated && !a->include) {
      ++dev;
      continue;
    }
    out.push_back(*a);
  }
  if (deviated) {
    *deviated = dev;
  }
  return out;
}

} // namespace

std::vector<LabAttempt> labTimed(std::span<const LabAttempt> attempts,
                                 const LabCaseKey &key, std::string_view alg,
                                 CubeKind cube, const LabWindow &window,
                                 const WarmupOf &warmupOf) {
  return afterWarmup(attempts, key, alg, cube, window, warmupOf, nullptr,
                     nullptr);
}

std::vector<Solve> labSolves(std::span<const LabAttempt> timed) {
  std::vector<Solve> out;
  out.reserve(timed.size());
  for (const auto &a : timed) {
    Solve s;
    s.timeMs = a.timeMs;
    s.penalty = a.dnf ? Penalty::Dnf : Penalty::None;
    s.timestamp = a.timestamp;
    out.push_back(s);
  }
  return out;
}

LabStats statsOf(std::span<const LabAttempt> timed, int trimPercent,
                 int windowAttempts, int deviated) {
  LabStats st;
  st.attempts = windowAttempts;
  st.deviated = deviated;
  std::vector<std::int64_t> times;
  for (const auto &a : timed) {
    if (a.dnf) {
      ++st.dnfs;
    } else {
      times.push_back(a.timeMs);
    }
  }
  st.timed = static_cast<int>(timed.size());
  std::sort(times.begin(), times.end());
  if (!times.empty()) {
    st.best = times.front();
    st.worst = times.back();
  }
  std::size_t cut = times.size() *
                    static_cast<std::size_t>(std::clamp(trimPercent, 0, 49)) /
                    100;
  std::vector<std::int64_t> kept(times.begin() + static_cast<long>(cut),
                                 times.end() - static_cast<long>(cut));
  st.used = static_cast<int>(kept.size());
  if (!kept.empty()) {
    double sum = 0;
    for (auto t : kept) {
      sum += static_cast<double>(t);
    }
    double mean = sum / static_cast<double>(kept.size());
    st.mean = std::llround(mean);
    std::size_t mid = kept.size() / 2;
    st.median = kept.size() % 2 ? kept[mid] : (kept[mid - 1] + kept[mid]) / 2;
    if (kept.size() >= 2) {
      double v = 0;
      for (auto t : kept) {
        v += (static_cast<double>(t) - mean) * (static_cast<double>(t) - mean);
      }
      st.sd = std::sqrt(v / static_cast<double>(kept.size() - 1));
    }
  }
  std::vector<Solve> solves = labSolves(timed);
  st.ao5 = currentAverage(solves, 5);
  st.ao12 = currentAverage(solves, 12);
  return st;
}

LabStats labStats(std::span<const LabAttempt> attempts, const LabCaseKey &key,
                  std::string_view alg, CubeKind cube, const LabWindow &window,
                  int trimPercent, const WarmupOf &warmupOf) {
  int inWindow = 0, deviated = 0;
  auto timed = afterWarmup(attempts, key, alg, cube, window, warmupOf,
                           &inWindow, &deviated);
  return statsOf(timed, trimPercent, inWindow, deviated);
}

LabAttemptGroups::LabAttemptGroups(std::span<const LabAttempt> attempts) {
  for (const auto &a : attempts) {
    groups_[{a.key, a.alg}].push_back(a);
  }
}

LabAttemptGroups::LabAttemptGroups(std::span<const LabAttempt> attempts,
                                   const LabCaseKey &key) {
  for (const auto &a : attempts) {
    if (a.key == key) {
      groups_[{a.key, a.alg}].push_back(a);
    }
  }
}

std::span<const LabAttempt> LabAttemptGroups::of(const LabCaseKey &key,
                                                 const std::string &alg) const {
  auto it = groups_.find({key, alg});
  if (it == groups_.end()) {
    return {};
  }
  return it->second;
}

CubieCube labSetup(const CubieCube &state, const std::string &alg,
                   const PieceMask &free, bool aufBefore, bool aufAfter,
                   bool randomAuf, Rng &rng) {
  CubieCube s = state;
  Alg rest = parseAlgOrEmpty(alg);
  int after = 0;
  if (auto d = displayOrientation(state, alg, free, aufBefore, aufAfter)) {
    s = d->state;
    rest = parseAlgOrEmpty(d->alg);
    if (auto fit = fitAlg(s, rest, free, aufBefore, aufAfter)) {
      s = s * uTurn(fit->before);
      after = fit->after;
    }
  }
  // The pieces the step leaves free are mixed up too, as Drill does: the
  // alg (and its AUF) then end at another state the step allows.
  if (!free.isEmpty() && !rest.empty()) {
    CubieCube a = cubieOf(rest);
    CubieCube end = s * a * uTurn(after);
    if (withinMask(end, free)) {
      s = end * randomState(free, rng) * uTurn((4 - after) % 4) * a.inverse();
    }
  }
  if (randomAuf && aufBefore) {
    std::uniform_int_distribution<int> d(0, 3);
    s = s * uTurn(d(rng));
  }
  return s;
}

} // namespace vcube
