#include "train/case_picker.hpp"

#include <algorithm>

namespace vcube {

std::string_view pickModeId(PickMode m) {
  switch (m) {
  case PickMode::Random:
    return "random";
  case PickMode::Weighted:
    return "weighted";
  case PickMode::InOrder:
    return "inorder";
  case PickMode::Recap:
    return "recap";
  }
  return "random";
}

std::optional<PickMode> pickModeFromId(std::string_view id) {
  for (PickMode m : {PickMode::Random, PickMode::Weighted, PickMode::InOrder,
                     PickMode::Recap}) {
    if (id == pickModeId(m)) {
      return m;
    }
  }
  return std::nullopt;
}

double drillReference(const std::vector<std::string> &pool,
                      const SetProgress &progress, AttemptMode mode,
                      double targetMs) {
  if (targetMs > 0) {
    return targetMs;
  }
  double total = 0;
  int counted = 0;
  for (const auto &id : pool) {
    auto it = progress.find(id);
    if (it == progress.end()) {
      continue;
    }
    if (auto m = it->second.recentMean(mode)) {
      total += *m;
      ++counted;
    }
  }
  return counted > 0 ? total / counted : 0.0;
}

std::optional<double> drillWeakness(const CaseProgress *p, AttemptMode mode,
                                    double referenceMs) {
  if (!p || p->drillCount(mode) == 0) {
    return std::nullopt;
  }
  // The median, so that one slow time among good ones counts for little.
  auto m = p->recentMedian(mode);
  double ratio = (m && referenceMs > 0) ? *m / referenceMs : 1.5;
  ratio = std::clamp(ratio, 0.5, 3.0);
  return ratio * ratio * (1.0 + 3.0 * p->failRate(mode));
}

std::vector<std::string> weakestCases(const std::vector<std::string> &pool,
                                      const SetProgress &progress,
                                      AttemptMode mode, double targetMs,
                                      std::size_t n) {
  double reference = drillReference(pool, progress, mode, targetMs);
  std::vector<std::pair<double, std::string>> weak;
  for (const auto &id : pool) {
    auto it = progress.find(id);
    if (auto w = drillWeakness(it == progress.end() ? nullptr : &it->second,
                               mode, reference)) {
      weak.emplace_back(*w, id);
    }
  }
  std::stable_sort(weak.begin(), weak.end(), [](const auto &a, const auto &b) {
    return a.first > b.first;
  });
  std::vector<std::string> out;
  for (std::size_t i = 0; i < weak.size() && i < n; ++i) {
    out.push_back(weak[i].second);
  }
  return out;
}

void CasePicker::setMode(PickMode m) {
  if (m != mode_) {
    mode_ = m;
    orderPos_ = 0;
    restartRecap();
  }
}

void CasePicker::setPool(std::vector<std::string> caseIds) {
  if (caseIds == pool_) {
    return;
  }
  // In order goes on with the first case still in the pool from where it
  // was, rather than from the top.
  std::size_t pos = 0;
  for (std::size_t k = 0; k < pool_.size(); ++k) {
    const std::string &id = pool_[(orderPos_ + k) % pool_.size()];
    auto it = std::find(caseIds.begin(), caseIds.end(), id);
    if (it != caseIds.end()) {
      pos = static_cast<std::size_t>(it - caseIds.begin());
      break;
    }
  }
  orderPos_ = pos;
  auto inPool = [&caseIds](const std::string &id) {
    return std::find(caseIds.begin(), caseIds.end(), id) != caseIds.end();
  };
  std::erase_if(recapLeft_,
                [&inPool](const std::string &id) { return !inPool(id); });
  std::erase_if(requeued_,
                [&inPool](const auto &r) { return !inPool(r.first); });
  pool_ = std::move(caseIds);
}

void CasePicker::requeue(const std::string &caseId, int after) {
  std::erase_if(requeued_,
                [&caseId](const auto &r) { return r.first == caseId; });
  if (after > 0) {
    requeued_.emplace_back(caseId, after);
  }
}

bool CasePicker::isRequeued(const std::string &caseId) const {
  return std::any_of(requeued_.begin(), requeued_.end(),
                     [&caseId](const auto &r) { return r.first == caseId; });
}

void CasePicker::restartRecap() {
  recapLeft_.clear();
  recapStarted_ = false;
}

std::optional<std::string> CasePicker::next(const SetProgress &progress,
                                            std::mt19937_64 &rng) {
  if (pool_.empty()) {
    return std::nullopt;
  }
  auto find = [&progress](const std::string &id) -> const CaseProgress * {
    auto it = progress.find(id);
    return it == progress.end() ? nullptr : &it->second;
  };
  const bool several = pool_.size() > 1;
  if (mode_ == PickMode::Recap && !recapStarted_) {
    recapLeft_ = pool_;
    recapStarted_ = true;
  }
  const bool recapOver = mode_ == PickMode::Recap && recapLeft_.empty();
  if (recapOver && requeued_.empty()) {
    return std::nullopt;
  }
  // A requeued case whose wait is over comes first (at the end of a recap,
  // whether it is over or not), but not two in a row, so that a run of
  // failures doesn't hold up the other cases.
  std::string pick;
  for (auto it = requeued_.begin(); it != requeued_.end(); ++it) {
    if ((it->second <= 0 || recapOver) && (!lastRequeued_ || recapOver) &&
        (it->first != last_ || !several)) {
      pick = it->first;
      requeued_.erase(it);
      break;
    }
  }
  if (pick.empty() && recapOver) {
    // Only the last case is waiting: it comes even so.
    pick = requeued_.front().first;
    requeued_.erase(requeued_.begin());
  }
  for (auto &r : requeued_) {
    r.second = std::max(0, r.second - 1);
  }
  lastRequeued_ = !pick.empty();
  if (!pick.empty()) {
    std::erase(recapLeft_, pick);
    last_ = pick;
    return pick;
  }
  // Uniform over `ids` without the last case.
  auto uniform = [this, several, &rng](const std::vector<std::string> &ids) {
    auto last = std::find(ids.begin(), ids.end(), last_);
    bool skip = several && ids.size() > 1 && last != ids.end();
    std::uniform_int_distribution<std::size_t> dist(0, ids.size() -
                                                           (skip ? 2 : 1));
    std::size_t i = dist(rng);
    if (skip && i >= static_cast<std::size_t>(last - ids.begin())) {
      ++i;
    }
    return ids[i];
  };
  switch (mode_) {
  case PickMode::Random:
    pick = uniform(pool_);
    break;
  case PickMode::Weighted: {
    double reference = drillReference(pool_, progress, attemptMode_, targetMs_);
    std::vector<double> weights;
    for (const auto &id : pool_) {
      // Never drilled: favour it.
      double w = drillWeakness(find(id), attemptMode_, reference).value_or(2.0);
      // Never the same case twice in a row.
      weights.push_back(id == last_ && several ? 0.0 : std::max(w, 0.05));
    }
    std::discrete_distribution<std::size_t> dist(weights.begin(),
                                                 weights.end());
    pick = pool_[dist(rng)];
    break;
  }
  case PickMode::InOrder:
    pick = pool_[orderPos_ % pool_.size()];
    if (pick == last_ && several) {
      // Just done (started from the list): on to the one after it.
      orderPos_ = (orderPos_ + 1) % pool_.size();
      pick = pool_[orderPos_];
    }
    orderPos_ = (orderPos_ + 1) % pool_.size();
    break;
  case PickMode::Recap:
    pick = uniform(recapLeft_);
    std::erase(recapLeft_, pick);
    break;
  }
  last_ = pick;
  return pick;
}

} // namespace vcube
