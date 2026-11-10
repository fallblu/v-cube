#pragma once

#include "train/srs.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace vcube {

// How Drill mode picks the next case.
enum class PickMode : std::uint8_t {
  Random,   // uniform, never the same case twice in a row
  Weighted, // slow and often-failed cases come up more
  InOrder,  // in list order
  Recap,    // every case once, in a random order, then nothing
};

// The time Weighted compares a pool's cases with: `targetMs` when there is
// one, else the mean of the cases' recent means in `mode`.
double drillReference(const std::vector<std::string> &pool,
                      const SetProgress &progress, AttemptMode mode,
                      double targetMs);
// How weak a case is in `mode`, as Weighted weighs it: the square of its
// recent median over `referenceMs` (kept between 0.5 and 3, so that one
// walked-away time can't take over), times 1 + 3 x its fail rate. 1 is a
// case at the reference time that never fails. nullopt: never drilled.
std::optional<double> drillWeakness(const CaseProgress *p, AttemptMode mode,
                                    double referenceMs);
// The `n` weakest cases of the pool that were drilled in `mode`, weakest
// first (ties in pool order).
std::vector<std::string> weakestCases(const std::vector<std::string> &pool,
                                      const SetProgress &progress,
                                      AttemptMode mode, double targetMs,
                                      std::size_t n);

class CasePicker {
public:
  void setMode(PickMode m);
  PickMode mode() const { return mode_; }
  // Cases to choose from, in list order. In order goes on from where it
  // was in the old pool.
  void setPool(std::vector<std::string> caseIds);
  const std::vector<std::string> &pool() const { return pool_; }
  // Reference time for Weighted: cases slower than this come up more.
  // 0 compares cases with the pool's overall mean instead.
  void setTargetMs(double ms) { targetMs_ = ms; }
  // The drill mode whose statistics Weighted goes by.
  void setAttemptMode(AttemptMode mode) { attemptMode_ = mode; }
  // A case started some other way (from the list): the next pick avoids
  // it as it does the last one picked.
  void setLast(const std::string &caseId) { last_ = caseId; }
  // Brings a case back once `after` other cases have come (e.g. one just
  // failed), whatever the order; `after` 0 forgets it.
  void requeue(const std::string &caseId, int after);
  bool isRequeued(const std::string &caseId) const;

  // Recap: the cases still to come this round, and whether the round is
  // over (next() then gives nothing until restartRecap()).
  std::size_t recapLeft() const { return recapLeft_.size(); }
  bool recapDone() const {
    return recapStarted_ && recapLeft_.empty() && requeued_.empty();
  }
  void restartRecap();

  std::optional<std::string> next(const SetProgress &progress,
                                  std::mt19937_64 &rng);

private:
  PickMode mode_ = PickMode::Random;
  std::vector<std::string> pool_;
  std::size_t orderPos_ = 0;
  double targetMs_ = 0;
  AttemptMode attemptMode_ = AttemptMode::PhysicalDrill;
  std::string last_;
  std::vector<std::pair<std::string, int>> requeued_; // case, cases to wait
  bool lastRequeued_ = false; // the last pick was a requeued case
  std::vector<std::string> recapLeft_;
  bool recapStarted_ = false;
};

} // namespace vcube
