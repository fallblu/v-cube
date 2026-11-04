#pragma once

#include "io/algset_json.hpp"

#include <QString>
#include <QStringList>

#include <map>
#include <vector>

namespace vcube {

// The user's alg sets, one JSON file each in `dir`, plus per-set progress in
// `dir/progress`. Built-in sets are copied from `builtinDir` the first time
// they are seen, so the user can then add and swap algs freely; deleting a
// built-in set does not bring it back.
class AlgLibrary {
public:
  AlgLibrary(QString dir, QString builtinDir);

  // Loads every set (installing new built-ins first). Problems with
  // individual files are collected in errors().
  void load();
  const QStringList &errors() const { return errors_; }

  std::vector<AlgSet> &sets() { return sets_; }
  const std::vector<AlgSet> &sets() const { return sets_; }
  AlgSet *find(const std::string &id);

  bool save(const AlgSet &set, QString *error = nullptr);
  // Adds a new set (giving it a unique id if taken) and saves it.
  AlgSet &add(AlgSet set);
  bool remove(const std::string &id);
  std::string uniqueId(const std::string &base) const;

  // Replaces a built-in set's cases and settings with the shipped ones,
  // keeping the user's algs for cases that still exist.
  bool restoreBuiltin(const std::string &id, QString *error = nullptr);
  QStringList builtinIds() const;

  SetProgress &progress(const std::string &setId);
  bool saveProgress(const std::string &setId);

  QString directory() const { return dir_; }

private:
  QString dir_;
  QString builtinDir_;
  std::vector<AlgSet> sets_;
  std::map<std::string, SetProgress> progress_;
  QStringList errors_;

  QString pathFor(const std::string &id) const;
  QString progressPathFor(const std::string &id) const;
  void installBuiltins();
};

} // namespace vcube
