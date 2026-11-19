#pragma once

#include "train/algset.hpp"
#include "train/srs.hpp"

#include "io/data_file.hpp"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>
#include <string>

namespace vcube {

inline QString qs(const std::string &s) { return QString::fromStdString(s); }
inline std::string ss(const QString &s) { return s.toStdString(); }

// Alg-set files ("format": "v-cube.algset", version 1). Case states are
// 54-character facelet strings (URFDLB order). A set is refused if it
// has no id, its "free" can't be read, a case has no id, the id of another
// case or a state no cube has, an alg isn't text, or the target time isn't
// a positive number.
std::optional<AlgSet> algSetFromJson(const QJsonObject &o, QString *error);
QJsonObject algSetToJson(const AlgSet &set);

// `status` tells a file that couldn't be read or isn't an alg set
// (Unreadable), or couldn't be opened (CannotOpen), from one with a
// problem in its contents (Read).
// `unknownKeys` gets the keys of the file that v-cube doesn't read (a
// misspelt "targetSecond", say), which are ignored.
std::optional<AlgSet> readAlgSetFile(const QString &path, QString *error,
                                     FileStatus *status = nullptr,
                                     QStringList *unknownKeys = nullptr);
// The keys of an alg set that aren't read, set, group and case keys, as
// "\"key\"", "in a group \"key\"" and "in a case \"key\"" (each once).
QStringList unknownAlgSetKeys(const QJsonObject &o);
bool writeAlgSetFile(const AlgSet &set, const QString &path, QString *error);

// Per-set drill progress: SRS card and recent attempts per case
// ("format": "v-cube.progress", version 1; files from before the format was
// written have none). readProgressFile() reads and checks a file.
QJsonObject progressToJson(const SetProgress &p);
SetProgress progressFromJson(const QJsonObject &o);
FileRead readProgressFile(const QString &path);

// Writes `data` to `path` atomically (via a temporary file).
bool writeFileAtomically(const QString &path, const QByteArray &data,
                         QString *error);

// Renames a file that couldn't be read to "<name>.unreadable-<date>-<time>"
// next to it, so that saving afresh doesn't destroy it. Returns the new
// path, or an empty string if the file couldn't be renamed.
QString setAside(const QString &path);

} // namespace vcube
