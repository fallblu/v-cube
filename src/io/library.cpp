#include "io/library.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

namespace vcube {

namespace {

constexpr auto kInstalledFile = "installed-builtins.json";

QString sanitizeId(const std::string &id) {
  QString s = qs(id).toLower();
  QString out;
  for (QChar c : s) {
    out += (c.isLetterOrNumber() || c == '-' || c == '_') ? c : QChar('-');
  }
  return out.isEmpty() ? QString("set") : out;
}

} // namespace

AlgLibrary::AlgLibrary(QString dir, QString builtinDir)
    : dir_(std::move(dir)), builtinDir_(std::move(builtinDir)) {}

QString AlgLibrary::pathFor(const std::string &id) const {
  return QDir(dir_).filePath(sanitizeId(id) + ".json");
}

QString AlgLibrary::progressPathFor(const std::string &id) const {
  return QDir(dir_).filePath("progress/" + sanitizeId(id) + ".json");
}

QStringList AlgLibrary::builtinIds() const {
  QStringList ids;
  for (const QString &file :
       QDir(builtinDir_).entryList({"*.json"}, QDir::Files, QDir::Name)) {
    QString error;
    if (auto set = readAlgSetFile(QDir(builtinDir_).filePath(file), &error)) {
      ids << qs(set->id);
    }
  }
  return ids;
}

void AlgLibrary::installBuiltins() {
  QDir dir(dir_);
  QStringList installed;
  QFile marker(dir.filePath(kInstalledFile));
  if (marker.open(QIODevice::ReadOnly)) {
    for (const auto &v : QJsonDocument::fromJson(marker.readAll()).array()) {
      installed << v.toString();
    }
    marker.close();
  }
  bool changed = false;
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
    writeFileAtomically(dir.filePath(kInstalledFile),
                        QJsonDocument(arr).toJson(), &error);
  }
}

void AlgLibrary::load() {
  errors_.clear();
  sets_.clear();
  progress_.clear();
  progressKeptOff_.clear();
  QDir().mkpath(dir_);
  QDir().mkpath(QDir(dir_).filePath("progress"));
  installBuiltins();
  QDir dir(dir_);
  for (const QString &file :
       dir.entryList({"*.json"}, QDir::Files, QDir::Name)) {
    if (file == kInstalledFile) {
      continue;
    }
    QString error;
    auto set = readAlgSetFile(dir.filePath(file), &error);
    if (!set) {
      errors_ << QString("%1: %2").arg(file, error);
      continue;
    }
    sets_.push_back(std::move(*set));
  }
  for (const AlgSet &s : sets_) {
    progress(s.id); // to report unreadable progress files now
  }
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

bool AlgLibrary::save(const AlgSet &set, QString *error) {
  QDir().mkpath(dir_);
  return writeAlgSetFile(set, pathFor(set.id), error);
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
  std::string b = ss(sanitizeId(base));
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

AlgSet &AlgLibrary::add(AlgSet set) {
  if (set.id.empty() || idTaken(set.id)) {
    set.id = uniqueId(set.id.empty() ? set.name : set.id);
  }
  sets_.push_back(std::move(set));
  save(sets_.back());
  return sets_.back();
}

bool AlgLibrary::remove(const std::string &id) {
  auto it = std::find_if(sets_.begin(), sets_.end(),
                         [&](const AlgSet &s) { return s.id == id; });
  if (it == sets_.end()) {
    return false;
  }
  QFile::remove(pathFor(id));
  QFile::remove(progressPathFor(id));
  progress_.erase(id);
  sets_.erase(it);
  return true;
}

bool AlgLibrary::restoreBuiltin(const std::string &id, QString *error) {
  for (const QString &file :
       QDir(builtinDir_).entryList({"*.json"}, QDir::Files)) {
    auto shipped = readAlgSetFile(QDir(builtinDir_).filePath(file), error);
    if (!shipped || shipped->id != id) {
      continue;
    }
    AlgSet *current = find(id);
    if (!current) {
      sets_.push_back(*shipped);
      return save(*shipped, error);
    }
    // Shipped cases go back into the group of that name (made again if the
    // user deleted it); everything else of the user's stays.
    for (const CaseGroup &sg : shipped->groups) {
      auto group =
          std::find_if(current->groups.begin(), current->groups.end(),
                       [&sg](const CaseGroup &g) { return g.name == sg.name; });
      if (group == current->groups.end()) {
        current->groups.push_back({sg.name, sg.spec, {}});
        group = current->groups.end() - 1;
      }
      for (const AlgCase &sc : sg.cases) {
        if (AlgCase *c = current->findCase(sc.id)) {
          c->name = sc.name;
          c->state = sc.state;
        } else {
          group->cases.push_back(sc);
        }
      }
    }
    current->builtin = true;
    return save(*current, error);
  }
  if (error) {
    *error = "no built-in set with that id";
  }
  return false;
}

SetProgress &AlgLibrary::progress(const std::string &setId) {
  auto it = progress_.find(setId);
  if (it != progress_.end()) {
    return it->second;
  }
  SetProgress p;
  QString path = progressPathFor(setId);
  QFile f(path);
  if (f.open(QIODevice::ReadOnly)) {
    QJsonParseError pe;
    auto doc = QJsonDocument::fromJson(f.readAll(), &pe);
    f.close();
    if (pe.error == QJsonParseError::NoError && doc.isObject()) {
      p = progressFromJson(doc.object());
    } else {
      QString kept = setAside(path);
      if (kept.isEmpty()) {
        progressKeptOff_.insert(setId);
      }
      errors_ << (kept.isEmpty()
                      ? QString("progress/%1 couldn't be read or moved aside")
                            .arg(QFileInfo(path).fileName())
                      : QString("progress/%1 couldn't be read, so it was "
                                "kept as %2 and the set's progress starts "
                                "afresh")
                            .arg(QFileInfo(path).fileName(),
                                 QFileInfo(kept).fileName()));
    }
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

bool AlgLibrary::saveProgress(const std::string &setId) {
  if (progressKeptOff_.contains(setId)) {
    return false; // never save over a file that couldn't be read
  }
  QDir().mkpath(QDir(dir_).filePath("progress"));
  QString error;
  return writeFileAtomically(progressPathFor(setId),
                             QJsonDocument(progressToJson(progress(setId)))
                                 .toJson(QJsonDocument::Compact),
                             &error);
}

} // namespace vcube
