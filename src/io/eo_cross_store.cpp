#include "io/eo_cross_store.hpp"

#include "io/f2l_store.hpp"

#include <QJsonArray>

namespace vcube {

QJsonObject eoCrossToJson(const EoCrossStats &stats) {
  QJsonObject kinds;
  for (const auto &[key, attempts] : stats.kinds) {
    QJsonArray list;
    for (const PairAttempt &a : attempts) {
      list.append(pairAttemptToJson(a));
    }
    kinds[QString::fromStdString(key)] = list;
  }
  return {{"format", kEoCrossFile.format},
          {"version", kEoCrossFile.version},
          {"kinds", kinds}};
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

EoCrossStore::EoCrossStore(QString path)
    : file_(std::move(path), kEoCrossFile) {}

bool EoCrossStore::load(QString *error) {
  stats = {};
  QJsonObject o;
  FileStatus status = file_.load(o, error);
  if (status != FileStatus::Read) {
    return status == FileStatus::Missing;
  }
  stats = eoCrossFromJson(o);
  return true;
}

bool EoCrossStore::save(QString *error) const {
  return file_.save(eoCrossToJson(stats), error);
}

} // namespace vcube
