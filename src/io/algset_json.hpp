#pragma once

#include "train/algset.hpp"
#include "train/srs.hpp"

#include <QJsonObject>
#include <QString>

#include <optional>

namespace vcube {

inline QString qs(const std::string &s) { return QString::fromStdString(s); }
inline std::string ss(const QString &s) { return s.toStdString(); }

// Alg-set files ("format": "v-cube.algset", version 1). Case states are
// 54-character facelet strings (URFDLB order).
std::optional<AlgSet> algSetFromJson(const QJsonObject &o, QString *error);
QJsonObject algSetToJson(const AlgSet &set);

std::optional<AlgSet> readAlgSetFile(const QString &path, QString *error);
bool writeAlgSetFile(const AlgSet &set, const QString &path, QString *error);

// Per-set drill progress: SRS card and recent attempts per case.
QJsonObject progressToJson(const SetProgress &p);
SetProgress progressFromJson(const QJsonObject &o);

// Writes `data` to `path` atomically (via a temporary file).
bool writeFileAtomically(const QString &path, const QByteArray &data,
                         QString *error);

} // namespace vcube
