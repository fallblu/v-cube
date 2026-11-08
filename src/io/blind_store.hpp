#pragma once

#include "train/bld.hpp"
#include "train/stats.hpp"

#include <QJsonObject>
#include <QString>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vcube {

// How one sticker has gone in a Blind drill (the letter quiz or setup
// moves): attempts, right answers and the time the right ones took.
struct LetterStat {
  int attempts = 0;
  int correct = 0;
  std::int64_t correctMs = 0; // total time of the right answers

  void record(bool right, std::int64_t ms);
  // Share of right answers (0..1); nullopt before the first attempt.
  std::optional<double> accuracy() const;
  // Mean time of the right answers.
  std::optional<double> meanMs() const;
  friend bool operator==(const LetterStat &, const LetterStat &) = default;
};

// A drill's statistics for every sticker of both piece types.
struct LetterStats {
  std::array<LetterStat, kBldStickers> edges{};
  std::array<LetterStat, kBldStickers> corners{};

  LetterStat &of(BldPiece kind, int sticker);
  const LetterStat &of(BldPiece kind, int sticker) const;
  friend bool operator==(const LetterStats &, const LetterStats &) = default;
};

// One memo traced in the memo tracing drill.
struct TraceAttempt {
  std::int64_t timeMs = 0;
  bool edgesRight = false;
  bool cornersRight = false;
  std::int64_t timestamp = 0; // Unix seconds
  friend bool operator==(const TraceAttempt &, const TraceAttempt &) = default;
};

// One blindfolded solve: the time from the start of memo to the end, the
// memo part of it, and DNF when the cube wasn't solved. A virtual solve
// keeps its solution, starting with the rotation that turned the cube from
// the WCA hold (white top, green front) to the blind hold, so that the
// scramble and solution can be replayed.
struct BlindAttempt {
  std::int64_t timeMs = 0;
  std::int64_t memoMs = 0;
  bool dnf = false;
  std::string scramble;
  std::string solution;
  std::string comment;
  std::int64_t timestamp = 0; // Unix seconds when it was recorded
  friend bool operator==(const BlindAttempt &, const BlindAttempt &) = default;
};

// A session of blind solves on the virtual cube or a physical one.
struct BlindSession {
  std::string name = "Session 1";
  CubeKind cube = CubeKind::Virtual;
  std::vector<BlindAttempt> attempts;
  friend bool operator==(const BlindSession &, const BlindSession &) = default;
};

// The timer's view of a session: each attempt as a solve (DNF kept), so
// that the usual statistics and the trend chart work on it.
std::vector<Solve> blindSolves(const BlindSession &session);

// Everything Blind mode keeps in its file.
struct BlindData {
  std::vector<BlindSession> sessions{BlindSession{}};
  std::size_t current = 0;
  LetterStats quiz;                 // the letter quiz
  LetterStats setups;               // setup moves
  std::vector<TraceAttempt> traces; // oldest first, at most kMaxTraces

  static constexpr std::size_t kMaxTraces = 1000;
  friend bool operator==(const BlindData &, const BlindData &) = default;
};

// Blind mode's sessions and drill statistics, saved as one JSON file.
class BlindStore {
public:
  explicit BlindStore(QString path);

  // A missing file gives one empty virtual-cube session and no
  // statistics. A file that can't be read is set aside (renamed to
  // <name>.unreadable-<date>-<time>) and also starts afresh; if it can't
  // be moved, nothing is saved over it.
  bool load(QString *error = nullptr);
  const QString &setAsidePath() const { return setAside_; }
  bool save(QString *error = nullptr) const;

  BlindData &data() { return data_; }
  const BlindData &data() const { return data_; }

  std::vector<BlindSession> &sessions() { return data_.sessions; }
  std::size_t currentIndex() const { return data_.current; }
  void setCurrentIndex(std::size_t i);
  BlindSession &current() { return data_.sessions[data_.current]; }
  BlindSession &addSession(const std::string &name);
  // Removing the last session leaves one empty session.
  void removeSession(std::size_t i);
  void addTrace(const TraceAttempt &t);

  QString path() const { return path_; }

private:
  QString path_;
  BlindData data_;
  QString setAside_;
  bool keepOff_ = false; // the unreadable file is still there

  bool unreadable(QString *error, const QString &why);
};

// {"format": "v-cube.blind", "version": 1, "current": 0,
//  "sessions": [{"name": ..., "cube": "virtual", "attempts": [{"time":
//    83210, "memo": 31500, "dnf": true, "scramble": ..., "solution": ...,
//    "comment": ..., "timestamp": ...}]}],
//  "quiz": {"edges": {"A": {"attempts": 4, "correct": 3, "ms": 2810}},
//           "corners": {...}},
//  "setups": {same as "quiz"},
//  "tracing": [{"time": 61200, "edges": true, "corners": false,
//               "timestamp": ...}]}
// Stickers are named by their Speffz letter (A-X), whatever the user's
// lettering, and only stickers with attempts are written. "dnf",
// "solution" and "comment" are left out when false or empty, and
// "edges"/"corners" of a trace when false. A session without "cube" is for
// the virtual cube.
QJsonObject blindToJson(const BlindData &data);
BlindData blindFromJson(const QJsonObject &o);

} // namespace vcube
