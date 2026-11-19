#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vcube {

// One drill attempt of a session, as the session panel counts it.
struct SessionAttempt {
  std::string caseId;
  std::int64_t timeMs = 0;
  bool failed = false;
  bool newBest = false; // faster than the case's best before it
};

struct DrillSummary {
  int count = 0;
  int dnfs = 0;
  int newBests = 0;
  std::optional<double> meanMs; // of the attempts that didn't fail
  // The slowest cases by their slowest good attempt, slowest first.
  std::vector<SessionAttempt> slowest;
};

// The drill attempts made since the session started, in order.
class DrillSession {
public:
  // Returns the attempt's index.
  std::size_t add(const SessionAttempt &a);
  // The attempt was marked or unmarked as failed afterwards; one that
  // isn't failed may then be a new best.
  enum class Mark : std::uint8_t { Failed, Solved, NewBest };
  void setFailed(std::size_t index, Mark mark);
  void removeLast();
  void clear() { attempts_.clear(); }
  bool empty() const { return attempts_.empty(); }
  std::size_t size() const { return attempts_.size(); }
  const std::vector<SessionAttempt> &attempts() const { return attempts_; }
  // Of the attempts from `from` on, with the `slowest` slowest cases.
  DrillSummary summary(std::size_t from = 0, std::size_t slowest = 5) const;

private:
  std::vector<SessionAttempt> attempts_;
};

} // namespace vcube
