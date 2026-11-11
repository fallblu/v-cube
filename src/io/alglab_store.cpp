#include "io/alglab_store.hpp"

#include <QJsonArray>
#include <QRandomGenerator>

#include <algorithm>

namespace vcube {

namespace {

constexpr FileFormat kAlgLabFile{"v-cube.alglab", 1, false, "Alg Lab file"};

QString q(const std::string &s) { return QString::fromStdString(s); }
QString q(std::string_view s) {
  return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}
std::string str(const QJsonValue &v) { return v.toString().toStdString(); }

QJsonObject sessionToJson(const LabSession &s) {
  QJsonObject o;
  o["id"] = q(s.id);
  o["name"] = q(s.name);
  o["cube"] = q(cubeKindId(s.cube));
  o["order"] = q(labOrderId(s.order));
  o["reps"] = s.reps;
  o["warmup"] = s.warmup;
  o["trim"] = s.trimPercent;
  o["randomAuf"] = s.randomAuf;
  QJsonArray entries;
  for (const auto &e : s.entries) {
    entries.append(QJsonObject{{"case", q(e.key.text())}, {"alg", q(e.alg)}});
  }
  o["entries"] = entries;
  return o;
}

LabSession sessionFromJson(const QJsonObject &o) {
  LabSession s;
  s.id = str(o["id"]);
  if (s.id.empty()) {
    s.id = newLabSessionId();
  }
  s.name = str(o["name"]);
  s.cube = cubeKindFromId(str(o["cube"])).value_or(CubeKind::Physical);
  s.order = labOrderFromId(str(o["order"])).value_or(LabOrder::Interleaved);
  s.reps = std::clamp(o["reps"].toInt(12), 1, 1000);
  s.warmup = std::clamp(o["warmup"].toInt(1), 0, 100);
  s.trimPercent = std::clamp(o["trim"].toInt(0), 0, 40);
  s.randomAuf = o["randomAuf"].toBool(false);
  for (const auto &v : o["entries"].toArray()) {
    QJsonObject e = v.toObject();
    auto key = LabCaseKey::parse(str(e["case"]));
    std::string alg = str(e["alg"]);
    if (key && !alg.empty()) {
      s.entries.push_back({*key, alg});
    }
  }
  return s;
}

QJsonObject profileToJson(const ErgoProfile &p) {
  QJsonObject o;
  o["name"] = q(p.name);
  QJsonObject moves;
  for (const auto &[k, v] : p.moves) {
    moves[q(k)] = v;
  }
  o["moves"] = moves;
  o["other"] = p.otherMove;
  o["axis"] = p.axisChange;
  QJsonArray rules;
  for (const auto &r : p.rules) {
    rules.append(QJsonObject{
        {"first", q(r.first)}, {"second", q(r.second)}, {"cost", r.cost}});
  }
  o["rules"] = rules;
  return o;
}

ErgoProfile profileFromJson(const QJsonObject &o) {
  ErgoProfile p;
  p.name = str(o["name"]);
  QJsonObject moves = o["moves"].toObject();
  for (auto it = moves.begin(); it != moves.end(); ++it) {
    p.moves[it.key().toStdString()] = it.value().toDouble();
  }
  p.otherMove = o["other"].toDouble(2.0);
  p.axisChange = o["axis"].toDouble(0.0);
  for (const auto &v : o["rules"].toArray()) {
    QJsonObject r = v.toObject();
    p.rules.push_back(
        {str(r["first"]), str(r["second"]), r["cost"].toDouble()});
  }
  return p;
}

} // namespace

std::string newLabSessionId() {
  quint64 n = QRandomGenerator::global()->generate64() & 0xffffffffffffULL;
  return QString::number(n, 16).rightJustified(12, '0').toStdString();
}

QJsonObject algLabToJson(const AlgLabData &d) {
  QJsonObject o;
  o["format"] = kAlgLabFile.format;
  o["version"] = kAlgLabFile.version;
  o["current"] = static_cast<int>(d.current);
  QJsonArray sessions;
  for (const auto &s : d.sessions) {
    sessions.append(sessionToJson(s));
  }
  o["sessions"] = sessions;
  QJsonArray cases;
  for (const auto &c : d.cases) {
    cases.append(QJsonObject{{"id", q(c.id)},
                             {"name", q(c.name)},
                             {"setup", q(c.setup)},
                             {"free", q(c.free)},
                             {"aufBefore", c.aufBefore},
                             {"aufAfter", c.aufAfter}});
  }
  o["cases"] = cases;
  QJsonArray candidates;
  for (const auto &cc : d.candidates) {
    QJsonArray algs;
    for (const auto &c : cc.candidates) {
      algs.append(QJsonObject{{"alg", q(c.alg)},
                              {"source", q(candidateSourceId(c.source))},
                              {"added", static_cast<qint64>(c.added)}});
    }
    candidates.append(QJsonObject{{"case", q(cc.key.text())}, {"algs", algs}});
  }
  o["candidates"] = candidates;
  QJsonArray attempts;
  for (const auto &a : d.attempts) {
    QJsonObject j;
    j["case"] = q(a.key.text());
    j["alg"] = q(a.alg);
    j["cube"] = q(cubeKindId(a.cube));
    j["time"] = static_cast<qint64>(a.timeMs);
    if (a.dnf) {
      j["dnf"] = true;
    }
    if (a.deviated) {
      j["deviated"] = true;
    }
    if (a.include) {
      j["include"] = true;
    }
    j["session"] = q(a.session);
    j["timestamp"] = static_cast<qint64>(a.timestamp);
    if (!a.scramble.empty()) {
      j["scramble"] = q(a.scramble);
    }
    if (!a.moves.empty()) {
      j["moves"] = q(a.moves);
    }
    attempts.append(j);
  }
  o["attempts"] = attempts;
  QJsonArray profiles;
  for (const auto &p : d.profiles) {
    profiles.append(profileToJson(p));
  }
  o["profiles"] = profiles;
  QJsonArray moveSets;
  for (const auto &m : d.moveSets) {
    moveSets.append(
        QJsonObject{{"name", q(m.name)}, {"moves", q(m.set.toString())}});
  }
  o["moveSets"] = moveSets;
  return o;
}

AlgLabData algLabFromJson(const QJsonObject &o) {
  AlgLabData d;
  for (const auto &v : o["sessions"].toArray()) {
    d.sessions.push_back(sessionFromJson(v.toObject()));
  }
  d.current = static_cast<std::size_t>(std::max(0, o["current"].toInt()));
  for (const auto &v : o["cases"].toArray()) {
    QJsonObject c = v.toObject();
    LabCase lc;
    lc.id = str(c["id"]);
    lc.name = str(c["name"]);
    lc.setup = str(c["setup"]);
    lc.free = str(c["free"]);
    lc.aufBefore = c["aufBefore"].toBool(true);
    lc.aufAfter = c["aufAfter"].toBool(true);
    if (!lc.id.empty()) {
      d.cases.push_back(std::move(lc));
    }
  }
  for (const auto &v : o["candidates"].toArray()) {
    QJsonObject c = v.toObject();
    auto key = LabCaseKey::parse(str(c["case"]));
    if (!key) {
      continue;
    }
    CaseCandidates cc{*key, {}};
    for (const auto &a : c["algs"].toArray()) {
      QJsonObject j = a.toObject();
      Candidate cand;
      cand.alg = str(j["alg"]);
      cand.source = candidateSourceFromId(str(j["source"]))
                        .value_or(CandidateSource::Typed);
      cand.added = j["added"].toInteger();
      if (!cand.alg.empty()) {
        cc.candidates.push_back(std::move(cand));
      }
    }
    d.candidates.push_back(std::move(cc));
  }
  for (const auto &v : o["attempts"].toArray()) {
    QJsonObject j = v.toObject();
    auto key = LabCaseKey::parse(str(j["case"]));
    if (!key) {
      continue;
    }
    LabAttempt a;
    a.key = *key;
    a.alg = str(j["alg"]);
    a.cube = cubeKindFromId(str(j["cube"])).value_or(CubeKind::Physical);
    a.timeMs = std::max<qint64>(0, j["time"].toInteger());
    a.dnf = j["dnf"].toBool(false);
    a.deviated = j["deviated"].toBool(false);
    a.include = j["include"].toBool(false);
    a.session = str(j["session"]);
    a.timestamp = j["timestamp"].toInteger();
    a.scramble = str(j["scramble"]);
    a.moves = str(j["moves"]);
    d.attempts.push_back(std::move(a));
  }
  if (o.contains("profiles")) {
    for (const auto &v : o["profiles"].toArray()) {
      d.profiles.push_back(profileFromJson(v.toObject()));
    }
  } else {
    d.profiles = builtinErgoProfiles();
  }
  for (const auto &v : o["moveSets"].toArray()) {
    QJsonObject m = v.toObject();
    auto set = MoveSet::parse(str(m["moves"]));
    if (set) {
      d.moveSets.push_back({str(m["name"]), *set});
    }
  }
  return d;
}

namespace {

std::vector<LabSession> freshSessions() {
  LabSession s;
  s.id = newLabSessionId();
  return {s};
}

} // namespace

AlgLabStore::AlgLabStore(QString path)
    : SessionFileStore(std::move(path), kAlgLabFile, freshSessions()) {
  profiles_ = builtinErgoProfiles();
}

std::vector<LabSession> AlgLabStore::defaults() const {
  return freshSessions();
}

void AlgLabStore::reset() {
  assign(defaults(), 0);
  cases_.clear();
  candidates_.clear();
  attempts_.clear();
  profiles_ = builtinErgoProfiles();
  moveSets_.clear();
}

AlgLabData AlgLabStore::data() const {
  return {sessions_, current_,  cases_,   candidates_,
          attempts_, profiles_, moveSets_};
}

QJsonObject AlgLabStore::toJson() const { return algLabToJson(data()); }

void AlgLabStore::fromJson(const QJsonObject &o) {
  AlgLabData d = algLabFromJson(o);
  assign(std::move(d.sessions), d.current);
  cases_ = std::move(d.cases);
  candidates_ = std::move(d.candidates);
  attempts_ = std::move(d.attempts);
  profiles_ = std::move(d.profiles);
  if (profiles_.empty()) {
    profiles_ = builtinErgoProfiles();
  }
  moveSets_ = std::move(d.moveSets);
}

LabSession &AlgLabStore::addLabSession(const std::string &name) {
  LabSession &s = addSession(name);
  s.id = newLabSessionId();
  return s;
}

const LabSession *AlgLabStore::sessionById(std::string_view id) const {
  for (const auto &s : sessions_) {
    if (s.id == id) {
      return &s;
    }
  }
  return nullptr;
}

int AlgLabStore::warmupOf(std::string_view id) const {
  const LabSession *s = sessionById(id);
  return s ? s->warmup : 0;
}

const LabCase *AlgLabStore::findCase(std::string_view id) const {
  for (const auto &c : cases_) {
    if (c.id == id) {
      return &c;
    }
  }
  return nullptr;
}

std::string AlgLabStore::newCaseId() const {
  for (int n = 1;; ++n) {
    std::string id = std::to_string(n);
    if (!findCase(id)) {
      return id;
    }
  }
}

const std::vector<Candidate> &
AlgLabStore::candidates(const LabCaseKey &key) const {
  static const std::vector<Candidate> none;
  for (const auto &cc : candidates_) {
    if (cc.key == key) {
      return cc.candidates;
    }
  }
  return none;
}

bool AlgLabStore::addCandidate(const LabCaseKey &key, const Candidate &c) {
  for (auto &cc : candidates_) {
    if (cc.key == key) {
      for (const auto &have : cc.candidates) {
        if (have.alg == c.alg) {
          return false;
        }
      }
      cc.candidates.push_back(c);
      return true;
    }
  }
  candidates_.push_back({key, {c}});
  return true;
}

bool AlgLabStore::removeCandidate(const LabCaseKey &key, std::string_view alg) {
  for (auto &cc : candidates_) {
    if (cc.key == key) {
      auto it = std::find_if(cc.candidates.begin(), cc.candidates.end(),
                             [&](const Candidate &c) { return c.alg == alg; });
      if (it != cc.candidates.end()) {
        cc.candidates.erase(it);
        return true;
      }
    }
  }
  return false;
}

void AlgLabStore::removeCandidates(const LabCaseKey &key) {
  std::erase_if(candidates_,
                [&](const CaseCandidates &cc) { return cc.key == key; });
}

void AlgLabStore::addAttempt(LabAttempt a) {
  attempts_.push_back(std::move(a));
}

void AlgLabStore::removeAttempt(std::size_t i) {
  if (i < attempts_.size()) {
    attempts_.erase(attempts_.begin() + static_cast<std::ptrdiff_t>(i));
  }
}

LabAttempt *AlgLabStore::attempt(std::size_t i) {
  return i < attempts_.size() ? &attempts_[i] : nullptr;
}

int AlgLabStore::clearAttempts(const LabCaseKey &key, std::string_view alg) {
  auto n = std::erase_if(attempts_, [&](const LabAttempt &a) {
    return a.key == key && a.alg == alg;
  });
  return static_cast<int>(n);
}

const ErgoProfile &AlgLabStore::profile(std::string_view name) const {
  for (const auto &p : profiles_) {
    if (p.name == name) {
      return p;
    }
  }
  return profiles_.empty() ? builtinErgoProfiles().front() : profiles_.front();
}

} // namespace vcube
