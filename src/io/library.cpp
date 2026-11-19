#include "io/library.hpp"

#include "io/backup.hpp"
#include "train/case_logic.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

namespace vcube {

namespace {

constexpr auto kInstalledFile = "installed-builtins.json";

// Ids made for new sets are cut to this many characters, so that their
// file names stay well within what file systems allow.
constexpr qsizetype kMaxNewId = 80;

// Why an alg-set file wasn't loaded; one that can't be read is set aside.
QString unreadableSetMessage(const QString &path, const QString &error,
                             FileStatus status) {
  QString file = QFileInfo(path).fileName();
  if (status != FileStatus::Unreadable) {
    return QString("%1: %2").arg(file, error);
  }
  QString kept = setAside(path);
  return kept.isEmpty()
             ? QString("%1 couldn't be read (%2) or moved aside")
                   .arg(file, error)
             : QString("%1 couldn't be read (%2), so it was kept as %3")
                   .arg(file, error, QFileInfo(kept).fileName());
}

QString sanitizeId(const std::string &id) {
  QString s = qs(id).toLower();
  QString out;
  for (QChar c : s) {
    out += (c.isLetterOrNumber() || c == u'-' || c == u'_') ? c : u'-';
  }
  return out.isEmpty() ? QString("set") : out;
}

} // namespace

AlgLibrary::AlgLibrary(QString dir, QString builtinDir)
    : dir_(std::move(dir)), builtinDir_(std::move(builtinDir)) {}

QString AlgLibrary::pathFor(const std::string &id) const {
  if (auto it = paths_.find(id); it != paths_.end()) {
    return it->second;
  }
  return QDir(dir_).filePath(sanitizeId(id) + ".json");
}

QString AlgLibrary::progressPathFor(const std::string &id) const {
  return QDir(dir_).filePath("progress/" + sanitizeId(id) + ".json");
}

const std::vector<AlgSet> &AlgLibrary::shipped() const {
  if (!shipped_) {
    shipped_.emplace();
    for (const QString &file :
         QDir(builtinDir_).entryList({"*.json"}, QDir::Files, QDir::Name)) {
      QString error;
      if (auto set = readAlgSetFile(QDir(builtinDir_).filePath(file), &error)) {
        set->builtin = true;
        shipped_->push_back(std::move(*set));
      }
    }
  }
  return *shipped_;
}

QStringList AlgLibrary::builtinIds() const {
  QStringList ids;
  for (const AlgSet &set : shipped()) {
    ids << qs(set.id);
  }
  return ids;
}

std::vector<AlgSet> AlgLibrary::missingBuiltins() const {
  std::vector<AlgSet> out;
  for (const AlgSet &set : shipped()) {
    if (std::none_of(sets_.begin(), sets_.end(),
                     [&set](const AlgSet &s) { return s.id == set.id; })) {
      out.push_back(set);
    }
  }
  return out;
}

void AlgLibrary::installBuiltins() {
  QDir dir(dir_);
  QStringList installed;
  QFile marker(dir.filePath(kInstalledFile));
  bool known = false; // the marker could be read
  if (marker.open(QIODevice::ReadOnly)) {
    QJsonParseError pe;
    auto doc = QJsonDocument::fromJson(marker.readAll(), &pe);
    known = pe.error == QJsonParseError::NoError && doc.isArray();
    for (const auto &v : doc.array()) {
      installed << v.toString();
    }
    marker.close();
  }
  bool changed = false;
  if (!known) {
    // Without the marker, a library that already has sets can't tell
    // deleted built-ins from new ones: install none, and say how to get
    // them back, rather than bring back every set the user deleted.
    QStringList existing = dir.entryList({"*.json"}, QDir::Files);
    existing.removeAll(kInstalledFile);
    if (!existing.isEmpty()) {
      QStringList missing;
      for (const AlgSet &set : shipped()) {
        QString id = qs(set.id);
        if (!installed.contains(id)) {
          if (!QFile::exists(pathFor(set.id))) {
            missing << qs(set.name);
          }
          installed << id;
          changed = true;
        }
      }
      if (!missing.isEmpty()) {
        errors_ << QString("%1 was missing or unreadable, so built-in sets "
                           "not in your library (%2) were not added; Set > "
                           "Restore a built-in set adds one")
                       .arg(kInstalledFile, missing.join(", "));
      }
    }
  }
  for (const QString &file :
       QDir(builtinDir_).entryList({"*.json"}, QDir::Files, QDir::Name)) {
    QString error;
    auto set = readAlgSetFile(QDir(builtinDir_).filePath(file), &error);
    if (!set) {
      errors_ << QString("built-in %1: %2").arg(file, error);
      continue;
    }
    QString id = qs(set->id);
    if (installed.contains(id)) {
      continue;
    }
    if (!QFile::exists(pathFor(set->id)) && !save(*set, &error)) {
      errors_ << QString("installing %1: %2").arg(file, error);
      continue;
    }
    installed << id;
    changed = true;
  }
  if (changed) {
    QJsonArray arr;
    for (const auto &id : installed) {
      arr.append(id);
    }
    QString error;
    if (!writeFileAtomically(dir.filePath(kInstalledFile),
                             QJsonDocument(arr).toJson(), &error)) {
      errors_ << QString("%1 couldn't be written, so new built-in sets may "
                         "not be installed next time: %2")
                     .arg(kInstalledFile, error);
    }
  }
}

void AlgLibrary::load() {
  errors_.clear();
  sets_.clear();
  progress_.clear();
  progressKeptOff_.clear();
  paths_.clear();
  QDir().mkpath(dir_);
  QDir().mkpath(QDir(dir_).filePath("progress"));
  installBuiltins();
  paths_.clear();
  QDir dir(dir_);
  for (const QString &file :
       dir.entryList({"*.json"}, QDir::Files, QDir::Name)) {
    if (file == kInstalledFile) {
      continue;
    }
    QString error;
    FileStatus status = FileStatus::Read;
    QString path = dir.filePath(file);
    auto set = readAlgSetFile(path, &error, &status);
    if (!set) {
      errors_ << unreadableSetMessage(dir.filePath(file), error, status);
      continue;
    }
    auto same = std::find_if(sets_.begin(), sets_.end(),
                             [&](const AlgSet &s) { return s.id == set->id; });
    if (same != sets_.end()) {
      // Two files of one set, such as a copy made in a file manager: keep
      // the one named for the id, so that edits never go over it from the
      // other, and leave the other alone.
      QString canonical = sanitizeId(set->id) + ".json";
      QString kept = QFileInfo(paths_[set->id]).fileName();
      QString skipped = file;
      if (file == canonical && kept != canonical) {
        std::swap(kept, skipped);
        *same = std::move(*set);
        paths_[same->id] = path;
      }
      errors_ << QString("%1 has the same set id (\"%2\") as %3, so it was "
                         "not loaded; give it another id or remove it")
                     .arg(skipped, qs(same->id), kept);
      continue;
    }
    paths_[set->id] = path;
    sets_.push_back(std::move(*set));
  }
  for (const AlgSet &s : sets_) {
    progress(s.id); // to report unreadable progress files now
  }
  sortSets();
}

void AlgLibrary::sortSets() {
  // Built-ins first in a fixed order, then the user's sets by name.
  QStringList order = {"pll", "oll", "2look-oll", "2look-pll"};
  std::stable_sort(sets_.begin(), sets_.end(),
                   [&order](const AlgSet &a, const AlgSet &b) {
                     auto rank = [&order](const AlgSet &s) {
                       auto i = order.indexOf(qs(s.id));
                       return i < 0 ? 1000 : static_cast<int>(i);
                     };
                     if (rank(a) != rank(b)) {
                       return rank(a) < rank(b);
                     }
                     return qs(a.name).localeAwareCompare(qs(b.name)) < 0;
                   });
}

AlgSet *AlgLibrary::find(const std::string &id) {
  for (auto &s : sets_) {
    if (s.id == id) {
      return &s;
    }
  }
  return nullptr;
}

const AlgSet *AlgLibrary::find(const std::string &id) const {
  for (const auto &s : sets_) {
    if (s.id == id) {
      return &s;
    }
  }
  return nullptr;
}

bool AlgLibrary::save(const AlgSet &set, QString *error) {
  QDir().mkpath(dir_);
  QString path = pathFor(set.id);
  QString why;
  bool ok = writeAlgSetFile(set, path, &why);
  reportWrite(path, ok, why);
  if (error) {
    *error = why;
  }
  if (!ok) {
    return false;
  }
  paths_[set.id] = path;
  return true;
}

bool AlgLibrary::idTaken(const std::string &id) const {
  QString file = sanitizeId(id);
  return std::any_of(sets_.begin(), sets_.end(),
                     [&](const AlgSet &s) {
                       return s.id == id || sanitizeId(s.id) == file;
                     }) ||
         QFile::exists(pathFor(id));
}

std::string AlgLibrary::uniqueId(const std::string &base) const {
  std::string b = ss(sanitizeId(base).left(kMaxNewId));
  if (!idTaken(b)) {
    return b;
  }
  for (int i = 2;; ++i) {
    std::string id = b + "-" + std::to_string(i);
    if (!idTaken(id)) {
      return id;
    }
  }
}

AlgSet *AlgLibrary::add(AlgSet set, QString *error) {
  if (set.id.empty() || idTaken(set.id) ||
      sanitizeId(set.id).size() > kMaxNewId) {
    set.id = uniqueId(set.id.empty() ? set.name : set.id);
  }
  if (!save(set, error)) {
    return nullptr;
  }
  std::string id = set.id;
  sets_.push_back(std::move(set));
  sortSets();
  return find(id);
}

QString AlgLibrary::deletedDirectory() const {
  return QDir(dir_).filePath(kDeletedFolder);
}

bool AlgLibrary::remove(const std::string &id, QString *error) {
  auto it = std::find_if(sets_.begin(), sets_.end(),
                         [&](const AlgSet &s) { return s.id == id; });
  if (it == sets_.end()) {
    if (error) {
      *error = "no set with that id";
    }
    return false;
  }
  finishBackgroundWrites(progressPathFor(id));
  QDateTime now = QDateTime::currentDateTime();
  QString why;
  if (QFile::exists(pathFor(id)) &&
      moveToDeleted(pathFor(id), deletedDirectory(), now, &why).isEmpty()) {
    reportWrite(pathFor(id), false, why);
    if (error) {
      *error = why;
    }
    return false;
  }
  if (QFile::exists(progressPathFor(id)) &&
      moveToDeleted(progressPathFor(id),
                    QDir(deletedDirectory()).filePath("progress"), now, &why)
          .isEmpty()) {
    // The set is gone; its progress stays where it was, and would come
    // back with a set of the same id.
    reportWrite(progressPathFor(id), false, why);
    if (error) {
      *error = why;
    }
  }
  progress_.erase(id);
  progressKeptOff_.erase(id);
  paths_.erase(id);
  sets_.erase(it);
  return true;
}

bool AlgLibrary::restoreBuiltin(const std::string &id, QString *error) {
  const std::vector<AlgSet> &all = shipped();
  auto ship = std::find_if(all.begin(), all.end(),
                           [&id](const AlgSet &s) { return s.id == id; });
  if (ship == all.end()) {
    if (error) {
      *error = "no built-in set with that id";
    }
    return false;
  }
  AlgSet *current = find(id);
  if (!current) {
    // Progress left behind (its set's file was set aside, or removed by
    // hand) goes where a deleted set's does, so the restored set starts
    // afresh without losing it.
    progress_.erase(id);
    if (QFile::exists(progressPathFor(id))) {
      finishBackgroundWrites(progressPathFor(id));
      QString why;
      if (moveToDeleted(progressPathFor(id),
                        QDir(deletedDirectory()).filePath("progress"),
                        QDateTime::currentDateTime(), &why)
              .isEmpty()) {
        if (error) {
          *error = why;
        }
        return false;
      }
    }
    return add(*ship, error) != nullptr;
  }
  AlgSet restored = *current;
  std::vector<std::string> newIds;
  // Shipped cases go back into the group of that name (made again if the
  // user deleted it); everything else of the user's stays.
  for (const CaseGroup &sg : ship->groups) {
    auto group =
        std::find_if(restored.groups.begin(), restored.groups.end(),
                     [&sg](const CaseGroup &g) { return g.name == sg.name; });
    if (group == restored.groups.end()) {
      restored.groups.push_back({sg.name, sg.spec, {}});
      group = restored.groups.end() - 1;
    }
    for (const AlgCase &sc : sg.cases) {
      AlgCase *c = restored.findCase(sc.id);
      if (c && c->state != sc.state) {
        // The same case if one of its algs solves the shipped one, as a
        // corrected shipped state still would.
        const CaseGroup *g = restored.groupOf(c->id);
        bool same = c->algs.empty() && c->name == sc.name;
        for (const std::string &alg : c->algs) {
          same = same || checkCaseAlg(restored, *g, sc, alg).passes();
        }
        if (!same) {
          c = nullptr; // the user's own case under that id
          AlgCase back = sc;
          back.id = restored.uniqueCaseId(sc.id);
          newIds.push_back(back.id);
          group->cases.push_back(std::move(back));
          continue;
        }
      }
      if (c) {
        c->name = sc.name;
        c->state = sc.state;
      } else {
        group->cases.push_back(sc);
        newIds.push_back(sc.id);
      }
    }
  }
  restored.builtin = true;
  if (!save(restored, error)) {
    return false;
  }
  *current = std::move(restored);
  forgetProgress(id, newIds);
  return true;
}

SetProgress &AlgLibrary::progress(const std::string &setId) {
  return loadedProgress(setId);
}

const SetProgress &AlgLibrary::progress(const std::string &setId) const {
  return loadedProgress(setId);
}

SetProgress &AlgLibrary::loadedProgress(const std::string &setId) const {
  auto it = progress_.find(setId);
  if (it != progress_.end()) {
    return it->second;
  }
  SetProgress p;
  QString path = progressPathFor(setId);
  QString file = QFileInfo(path).fileName();
  FileRead r = readProgressFile(path);
  if (r.status == FileStatus::Read) {
    p = progressFromJson(r.object);
  } else if (r.status == FileStatus::Newer) {
    progressKeptOff_.insert(setId);
    errors_ << QString("progress/%1 can't be used (%2), so it is left as it "
                       "is and the set's progress isn't saved")
                   .arg(file, r.error);
  } else if (r.status == FileStatus::Unreadable) {
    QString kept = setAside(path);
    if (kept.isEmpty()) {
      progressKeptOff_.insert(setId);
    }
    errors_ << (kept.isEmpty()
                    ? QString("progress/%1 couldn't be read (%2) or moved "
                              "aside, so the set's progress isn't saved")
                          .arg(file, r.error)
                    : QString("progress/%1 couldn't be read (%2), so it was "
                              "kept as %3 and the set's progress starts "
                              "afresh")
                          .arg(file, r.error, QFileInfo(kept).fileName()));
  }
  return progress_[setId] = std::move(p);
}

void AlgLibrary::forgetProgress(const std::string &setId,
                                const std::vector<std::string> &caseIds) {
  SetProgress &p = progress(setId);
  std::size_t erased = 0;
  for (const auto &id : caseIds) {
    erased += p.erase(id);
  }
  if (erased > 0) {
    saveProgress(setId);
  }
}

std::function<bool(QString *)>
AlgLibrary::progressWriter(const std::string &setId) {
  QString path = progressPathFor(setId);
  if (progressKeptOff_.contains(setId)) {
    // Never save over a file that couldn't be used.
    return [path](QString *error) {
      if (error) {
        *error = QString("%1 couldn't be used, so it isn't saved over")
                     .arg(QDir::toNativeSeparators(path));
      }
      return false;
    };
  }
  return [path, progress = progress(setId),
          dir = QDir(dir_).filePath("progress")](QString *error) {
    QDir().mkpath(dir);
    return writeFileAtomically(
        path,
        QJsonDocument(progressToJson(progress)).toJson(QJsonDocument::Compact),
        error);
  };
}

bool AlgLibrary::saveProgress(const std::string &setId, QString *error) {
  QString why;
  bool ok = progressWriter(setId)(&why);
  reportWrite(progressPathFor(setId), ok, why);
  if (error) {
    *error = why;
  }
  return ok;
}

QString AlgLibrary::progressPath(const std::string &setId) const {
  return progressPathFor(setId);
}

} // namespace vcube
