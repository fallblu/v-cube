#pragma once

#include "io/data_file.hpp"
#include "train/f2l_stats.hpp"

#include <QJsonObject>
#include <QString>

#include <map>
#include <string>
#include <vector>

namespace vcube {

// F2L mode's statistics, saved as one JSON file.
//
// {"format": "v-cube.f2l", "version": 1,
//  "cases": {"URF1-UB0": [{"timestamp": ..., "ms": 2310, "moves": 8,
//    "best": 7, "excess": 0.45, "hint": true, "gaveUp": true,
//    "drill": "pair"}]},
//  "choices": [{"timestamp": ..., "rank": 2, "of": 3, "regret": 1.2}],
//  "fulls": [{"timestamp": ..., "ms": 14200, "moves": 31, "pairs": 3,
//    "excess": 4.1}]}
// "hint" and "gaveUp" are left out when false, and "drill" ("pair",
// "choice" or "full") when it is "pair".
QJsonObject f2lToJson(const F2LStats &stats);
F2LStats f2lFromJson(const QJsonObject &o);

// One try and one whole solve, as they are written here and in TSLE
// mode's file.
QJsonObject pairAttemptToJson(const PairAttempt &a);
// The tries of each case or kind, by its key.
QJsonObject
attemptsToJson(const std::map<std::string, std::vector<PairAttempt>> &byKey);
PairAttempt pairAttemptFromJson(const QJsonObject &o);
QJsonObject fullAttemptToJson(const FullAttempt &a);
FullAttempt fullAttemptFromJson(const QJsonObject &o);

inline constexpr FileFormat kF2LFile{"v-cube.f2l", 1, false, "F2L file"};

using F2LStore = JsonStore<F2LStats, kF2LFile, f2lToJson, f2lFromJson>;

} // namespace vcube
