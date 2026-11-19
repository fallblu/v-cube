#pragma once

#include "train/alg_lab.hpp"
#include "train/ergonomics.hpp"
#include "train/move_set.hpp"

#include "io/session_store.hpp"

#include <QJsonObject>
#include <QString>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vcube {

// Everything Alg Lab keeps, as written to its file.
struct AlgLabData {
  std::vector<LabSession> sessions;
  std::size_t current = 0;
  std::vector<LabCase> cases;
  std::vector<CaseCandidates> candidates;
  std::vector<LabAttempt> attempts; // oldest first
  std::vector<ErgoProfile> profiles;
  std::vector<MoveSetPreset> moveSets; // the user's own presets
  friend bool operator==(const AlgLabData &, const AlgLabData &) = default;
};

// Alg Lab's test sessions, lab cases, candidates, attempts, ergonomic
// profiles and move-set presets, saved as one JSON file. A new file has
// one empty physical-cube session, no lab cases and the built-in profiles.
class AlgLabStore : public SessionFileStore<LabSession> {
public:
  explicit AlgLabStore(QString path);

  AlgLabData data() const;

  // A new empty session (with a fresh id), made current.
  LabSession &addLabSession(const std::string &name);
  // The session with this id, if there is one.
  const LabSession *sessionById(std::string_view id) const;
  // The warm-up reps of a session by its id (0 for one that's gone).
  int warmupOf(std::string_view id) const;

  std::vector<LabCase> &cases() { return cases_; }
  const std::vector<LabCase> &cases() const { return cases_; }
  const LabCase *findCase(std::string_view id) const;
  // A lab case id not used yet.
  std::string newCaseId() const;

  // The kept candidates of a case (none if it has none).
  const std::vector<Candidate> &candidates(const LabCaseKey &key) const;
  // Adds a candidate unless the case has it already; true if added.
  bool addCandidate(const LabCaseKey &key, const Candidate &c);
  bool removeCandidate(const LabCaseKey &key, std::string_view alg);
  // Forgets a case's candidates (when the case is deleted, say).
  void removeCandidates(const LabCaseKey &key);

  const std::vector<LabAttempt> &attempts() const { return attempts_; }
  void addAttempt(LabAttempt a);
  // Removes the attempt at `i`; nothing for one that isn't there.
  void removeAttempt(std::size_t i);
  LabAttempt *attempt(std::size_t i);
  // Forgets every attempt of one alg of a case (on any cube).
  int clearAttempts(const LabCaseKey &key, std::string_view alg);

  std::vector<ErgoProfile> &profiles() { return profiles_; }
  const std::vector<ErgoProfile> &profiles() const { return profiles_; }
  // `name`, or `name` with a number after it, such that no profile but
  // the one at `except` has it: profiles are chosen by name.
  std::string uniqueProfileName(std::string name,
                                std::optional<std::size_t> except = {}) const;
  // The profile with this name, else the first.
  const ErgoProfile &profile(std::string_view name) const;

  std::vector<MoveSetPreset> &moveSets() { return moveSets_; }
  const std::vector<MoveSetPreset> &moveSets() const { return moveSets_; }

protected:
  QJsonObject toJson() const override;
  void fromJson(const QJsonObject &o) override;
  void reset() override;
  std::vector<LabSession> defaults() const override;

private:
  std::vector<LabCase> cases_;
  std::vector<CaseCandidates> candidates_;
  std::vector<LabAttempt> attempts_;
  std::vector<ErgoProfile> profiles_;
  std::vector<MoveSetPreset> moveSets_;
};

// A session id: 12 hex digits, random.
std::string newLabSessionId();

// {"format": "v-cube.alglab", "version": 2, "current": 0,
//  "sessions": [{"id": "3fa2...", "name": ..., "cube": "physical",
//    "order": "interleaved", "reps": 12, "warmup": 1, "trim": 0,
//    "randomAuf": false, "entries": [{"case": "set:pll/T", "alg": ...}]}],
//  "cases": [{"id": ..., "name": ..., "setup": ..., "free": "LL:p",
//    "aufBefore": true, "aufAfter": true}],
//  "candidates": [{"case": "lab:1", "algs": [{"alg": ..., "source":
//    "generated", "added": ...}]}],
//  "attempts": [{"case": ..., "alg": ..., "cube": "virtual", "time": 1830,
//    "dnf": true, "deviated": true, "include": true, "session": ...,
//    "timestamp": ..., "scramble": ..., "moves": ...}],
//  "profiles": [{"name": ..., "moves": {"R": 1.0, ...}, "other": 2.5,
//    "axis": 0.15, "rules": [{"first": "R2", "second": "F*", "cost": 0.6}],
//    "grips": {"other": 2.5, "regrip": 1.2, "hiddenRegrip": 0.4,
//      "regripStep": 0.3, "strain": 0.5, "start": 0.25, "sameFinger": 0.4,
//      "tricks": [{"move": "U", "hand": "right", "finger": "index",
//        "how": "flick", "from": "home, up", "turn": 0, "cost": 1.0,
//        "reset": true, "off": true}]}}],
//  "moveSets": [{"name": ..., "moves": "U, R, F2"}]}
// "dnf", "deviated", "include", "scramble" and "moves" are left out when
// false or empty. Entries, candidates and attempts whose case key can't be
// read are dropped; a file without "profiles" gets the built-in ones.
// "grips" makes a grip profile (left out for a move profile); "reset" and
// "off" are left out when false, and a trick that can't be read is
// dropped. A version 1 file (before grip profiles) with no grip profile
// gets the built-in one first.
QJsonObject algLabToJson(const AlgLabData &d);
AlgLabData algLabFromJson(const QJsonObject &o);

} // namespace vcube
