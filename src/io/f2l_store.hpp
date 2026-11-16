#pragma once

#include "io/data_file.hpp"
#include "train/f2l_stats.hpp"

#include <QJsonObject>
#include <QString>

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

inline constexpr FileFormat kF2LFile{"v-cube.f2l", 1, false, "F2L file"};

class F2LStore {
public:
  explicit F2LStore(QString path);

  // A missing file gives empty statistics. A file that can't be read is
  // set aside (see DataFile) and also gives empty ones.
  bool load(QString *error = nullptr);
  bool save(QString *error = nullptr) const;
  const QString &setAsidePath() const { return file_.setAsidePath(); }
  QString path() const { return file_.path(); }

  F2LStats stats;

private:
  DataFile file_;
};

} // namespace vcube
