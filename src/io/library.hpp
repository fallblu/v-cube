#pragma once

#include "io/algset_json.hpp"

#include <QString>
#include <QStringList>

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace vcube {

// The user's alg sets, one JSON file each in `dir`, plus per-set progress in
// `dir/progress`. Built-in sets are copied from `builtinDir` the first time
// they are seen, so the user can then add and swap algs freely; deleting a
// built-in set does not bring it back (restoreBuiltin() does). Each set is
// saved back to the file it was loaded from; two files with the same set id
// load only one of them (the one named for the id, else the first) and the
// other is reported in errors().
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

  // Writes go through reportWrite(), so that a failure is seen whoever
  // asked for it.
  bool save(const AlgSet &set, QString *error = nullptr);
  // Adds a new set and saves it, giving it a unique id if its id is taken.
  // nullptr (and nothing added) if it can't be saved.
  AlgSet *add(AlgSet set, QString *error = nullptr);
  // Removes a set. Its file and progress are moved to dir/deleted (named
  // with the date and time) rather than deleted, so they can be brought
  // back by hand. false (and the set kept) if its file can't be moved.
  bool remove(const std::string &id, QString *error = nullptr);
  QString deletedDirectory() const;
  // The file a set is saved to.
  QString pathFor(const std::string &id) const;
  // Whether a set uses `id` or its file name (ids are stored by file
  // name, so "OLL" and "oll" share one), or a file that failed to load
  // has that name.
  bool idTaken(const std::string &id) const;
  std::string uniqueId(const std::string &base) const;

  // Brings back a built-in set's shipped cases: their names and states
  // replace the user's and deleted ones return, while the user's algs, the
  // cases and groups they added and the set's settings are kept. A case of
  // the user's that only shares a shipped case's id (a different case, none
  // of whose algs solve the shipped one) is kept, and the shipped case comes
  // back beside it under a new id. A deleted built-in set is added again
  // as shipped.
  bool restoreBuiltin(const std::string &id, QString *error = nullptr);
  // Ids of the shipped built-in sets.
  QStringList builtinIds() const;
  // Shipped built-in sets that aren't in the library (deleted ones).
  std::vector<AlgSet> missingBuiltins() const;

  // A set's progress, read the first time it is asked for. A progress file
  // that can't be read is set aside (see setAside()) and noted in errors();
  // one that can't be moved aside, or was written by a newer version of
  // v-cube, is noted and never saved over.
  SetProgress &progress(const std::string &setId);
  bool saveProgress(const std::string &setId, QString *error = nullptr);
  // A function that writes the set's progress as it is now (a copy), from
  // any thread, e.g. for writeInBackground() to progressPath().
  std::function<bool(QString *)> progressWriter(const std::string &setId);
  QString progressPath(const std::string &setId) const;
  // Drops the progress of cases that were deleted or are new, so that a
  // new case never inherits a deleted one's, and saves it if it changed.
  void forgetProgress(const std::string &setId,
                      const std::vector<std::string> &caseIds);

private:
  QString dir_;
  QString builtinDir_;
  std::vector<AlgSet> sets_;
  std::map<std::string, SetProgress> progress_;
  std::set<std::string> progressKeptOff_; // unreadable and not moved aside
  QStringList errors_;
  std::map<std::string, QString> paths_; // file each set was loaded from
  mutable std::optional<std::vector<AlgSet>> shipped_;

  const std::vector<AlgSet> &shipped() const;
  void sortSets(); // built-ins first in a fixed order, then by name
  QString progressPathFor(const std::string &id) const;
  void installBuiltins();
};

} // namespace vcube
