#pragma once

#include "io/data_file.hpp"
#include "train/tsle.hpp"

#include <QJsonObject>
#include <QString>

namespace vcube {

// TSLE mode's progress, saved as one JSON file.
//
// {"format": "v-cube.tsle", "version": 1,
//  "cases": {"UF-N-0120": [{"timestamp": ..., "ms": 1800, "moves": 4,
//    "best": 3, "excess": 1, "hint": true, "gaveUp": true,
//    "drill": "full"}]},
//  "introduced": ["UF-N-0120", ...],
//  "fulls": [{"timestamp": ..., "ms": 6100, "moves": 14, "pairs": 3,
//    "excess": 2}],
//  "ocll": {"pi": "R U2 R2 U' R2 U' R2 U2 R"}}
// A try is written as in F2L mode's file (its "excess" is in moves; a
// whole solve's "pairs" are its steps). "ocll" has only the OCLL algs
// changed from the default ones.
QJsonObject tsleToJson(const TsleProgress &progress);
TsleProgress tsleFromJson(const QJsonObject &o);

inline constexpr FileFormat kTsleFile{"v-cube.tsle", 1, false, "TSLE file"};

class TsleStore {
public:
  explicit TsleStore(QString path);

  // A missing file gives empty progress. A file that can't be read is
  // set aside (see DataFile) and also gives empty progress.
  bool load(QString *error = nullptr);
  bool save(QString *error = nullptr) const;
  const QString &setAsidePath() const { return file_.setAsidePath(); }
  QString path() const { return file_.path(); }

  TsleProgress progress;

private:
  DataFile file_;
};

} // namespace vcube
