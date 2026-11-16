#include "io/eo_cross_store.hpp"

#include "io/f2l_store.hpp"

#include <QJsonArray>

namespace vcube {

QJsonObject eoCrossToJson(const EoCrossStats &stats) {
  return {{"format", kEoCrossFile.format},
          {"version", kEoCrossFile.version},
          {"kinds", attemptsToJson(stats.kinds)}};
}

EoCrossStats eoCrossFromJson(const QJsonObject &o) {
  EoCrossStats stats;
  const QJsonObject kinds = o.value("kinds").toObject();
  for (auto it = kinds.begin(); it != kinds.end(); ++it) {
    // Only the kinds there are, and the newest tries of each.
    std::optional<EoKind> kind = eoKindFromKey(it.key().toStdString());
    if (!kind) {
      continue;
    }
    for (const QJsonValue &v : it.value().toArray()) {
      stats.add(*kind, pairAttemptFromJson(v.toObject()));
    }
  }
  return stats;
}

} // namespace vcube
