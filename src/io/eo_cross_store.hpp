#pragma once

#include "io/data_file.hpp"
#include "train/eo_cross.hpp"

#include <QJsonObject>
#include <QString>

namespace vcube {

// EO Cross mode's statistics, saved as one JSON file.
//
// {"format": "v-cube.eocross", "version": 1,
//  "kinds": {"solve/eocross/6": [{"timestamp": ..., "ms": 9310, "moves": 13,
//    "best": 11, "excess": 2}], "edges/4": [...], "axis/eo+2": [...]}}
// A try is written as in F2L mode's file ("hint" and "gaveUp" left out
// when false); see EoCrossStats for what its numbers mean in each drill.
QJsonObject eoCrossToJson(const EoCrossStats &stats);
EoCrossStats eoCrossFromJson(const QJsonObject &o);

inline constexpr FileFormat kEoCrossFile{"v-cube.eocross", 1, false,
                                         "EO Cross file"};

using EoCrossStore =
    JsonStore<EoCrossStats, kEoCrossFile, eoCrossToJson, eoCrossFromJson>;

} // namespace vcube
