#pragma once

#include "io/data_file.hpp"
#include "train/practice.hpp"

#include <QJsonObject>
#include <QString>

#include <map>
#include <string>

namespace vcube {

// What EO Cross, F2L and TSLE mode keep (see practice.hpp), one JSON file
// per mode: eocross.json, f2l.json and tsle.json in the data folder.
//
// {"format": "v-cube.practice", "version": 1,
//  "cards": {"cases": {"URF1-UB0": {"card": {...}, "attempts": [...]}}},
//  "learn": {"whole": [{"timestamp": ..., "moves": 31, "best": 24,
//    "extra": 4.5, "dnf": true, "hint": true}]},
//  "timed": {"virtual/zz-1": [{"timestamp": ..., "ms": 14200, "moves": 31}]},
//  "inspection": {"eocross": [{"timestamp": ..., "inspect": 9300,
//    "moves": 8, "best": 7, "extra": 1, "over": true}]},
//  "choices": {"front/URF1-UB0": ["U R U' R'", "y' U' L' U L"]},
//  "notes": {"ocll/sune": "R U R' U R U2 R'"}}
// "cards" is written as an alg set's progress is; "learn" holds the
// Untimed tab's solves. Of a try, "ms", "moves", "best" and "inspect" are
// left out when 0, "extra" when it isn't known, and "dnf", "hint" and
// "over" when false. "inspection" and "choices" are left out when empty.
QJsonObject practiceToJson(const PracticeData &data);
PracticeData practiceFromJson(const QJsonObject &o);

inline constexpr FileFormat kPracticeFile{"v-cube.practice", 1, false,
                                          "practice file"};

using PracticeStore =
    JsonStore<PracticeData, kPracticeFile, practiceToJson, practiceFromJson>;

// The three modes kept other files under the same names before they had
// Learn and Timed tabs ("v-cube.eocross", "v-cube.f2l", "v-cube.tsle").
// Such a file at `path` is deleted, so that the mode starts afresh; what
// it held is returned (an empty object when there was none), for the OCLL
// algs TSLE carries over.
QJsonObject removeOldPracticeFile(const QString &path);
// The OCLL algs changed from the default ones in an old TSLE file, as
// notes ("ocll/<id>").
std::map<std::string, std::string> oldOcllNotes(const QJsonObject &oldFile);

} // namespace vcube
