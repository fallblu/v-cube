#include "io/tsle_store.hpp"

#include "io/f2l_store.hpp"

#include <QJsonArray>

namespace vcube {

QJsonObject tsleToJson(const TsleProgress &progress) {
  QJsonObject cases;
  for (const auto &[key, attempts] : progress.cases) {
    QJsonArray list;
    for (const PairAttempt &a : attempts) {
      list.append(pairAttemptToJson(a));
    }
    cases[QString::fromStdString(key)] = list;
  }
  QJsonArray introduced;
  for (const std::string &key : progress.introduced) {
    introduced.append(QString::fromStdString(key));
  }
  QJsonArray fulls;
  for (const FullAttempt &a : progress.fulls) {
    fulls.append(fullAttemptToJson(a));
  }
  QJsonObject ocll;
  for (const auto &[id, alg] : progress.ocll) {
    ocll[QString::fromStdString(id)] = QString::fromStdString(alg);
  }
  return {{"format", kTsleFile.format}, {"version", kTsleFile.version},
          {"cases", cases},             {"introduced", introduced},
          {"fulls", fulls},             {"ocll", ocll}};
}

TsleProgress tsleFromJson(const QJsonObject &o) {
  TsleProgress progress;
  const QJsonObject cases = o.value("cases").toObject();
  for (auto it = cases.begin(); it != cases.end(); ++it) {
    // Only the cases there are, and the newest tries of each.
    if (!tsleFromKey(it.key().toStdString())) {
      continue;
    }
    for (const QJsonValue &v : it.value().toArray()) {
      progress.add(it.key().toStdString(), pairAttemptFromJson(v.toObject()));
    }
  }
  for (const QJsonValue &v : o.value("introduced").toArray()) {
    if (tsleFromKey(v.toString().toStdString())) {
      progress.introduced.insert(v.toString().toStdString());
    }
  }
  for (const QJsonValue &v : o.value("fulls").toArray()) {
    progress.add(fullAttemptFromJson(v.toObject()));
  }
  const QJsonObject ocll = o.value("ocll").toObject();
  for (const OcllAlg &a : defaultOcllAlgs()) {
    QString alg = ocll.value(QString::fromStdString(a.id)).toString();
    if (!alg.isEmpty() &&
        ocllAlgWorks(a.id, parseAlgOrEmpty(alg.toStdString()))) {
      progress.ocll[a.id] = alg.toStdString();
    }
  }
  return progress;
}

TsleStore::TsleStore(QString path) : file_(std::move(path), kTsleFile) {}

bool TsleStore::load(QString *error) {
  progress = {};
  QJsonObject o;
  FileStatus status = file_.load(o, error);
  if (status != FileStatus::Read) {
    return status == FileStatus::Missing;
  }
  progress = tsleFromJson(o);
  return true;
}

bool TsleStore::save(QString *error) const {
  return file_.save(tsleToJson(progress), error);
}

} // namespace vcube
