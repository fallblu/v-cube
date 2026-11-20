#include "io/alglab_store.hpp"

#include "io/json_values.hpp"
#include "io/qt_text.hpp"

#include <QJsonArray>
#include <QRandomGenerator>

#include <algorithm>

namespace vcube {

namespace {

// Version 2 added grip profiles; a version 1 file gets the built-in one.
// Version 1 files lack some of these, so none has to be there.
constexpr FileField kAlgLabFields[] = {
    {"sessions", true, false},   {"cases", true, false},
    {"candidates", true, false}, {"attempts", true, false},
    {"profiles", true, false},   {"moveSets", true, false}};
constexpr FileFormat kAlgLabFile{"v-cube.alglab", 2, false, "Alg Lab file",
                                 kAlgLabFields};

QJsonObject sessionToJson(const LabSession &s) {
  QJsonObject o;
  o["id"] = qs(s.id);
  o["name"] = qs(s.name);
  o["cube"] = qs(cubeKindId(s.cube));
  o["order"] = qs(labOrderId(s.order));
  o["reps"] = s.reps;
  o["warmup"] = s.warmup;
  o["trim"] = s.trimPercent;
  o["randomAuf"] = s.randomAuf;
  QJsonArray entries;
  for (const auto &e : s.entries) {
    entries.append(QJsonObject{{"case", qs(e.key.text())}, {"alg", qs(e.alg)}});
  }
  o["entries"] = entries;
  return o;
}

LabSession sessionFromJson(const QJsonObject &o) {
  LabSession s;
  s.id = ss(o["id"].toString());
  if (s.id.empty()) {
    s.id = newLabSessionId();
  }
  s.name = ss(o["name"].toString());
  s.cube =
      cubeKindFromId(ss(o["cube"].toString())).value_or(CubeKind::Physical);
  s.order =
      labOrderFromId(ss(o["order"].toString())).value_or(LabOrder::Interleaved);
  s.reps = std::clamp(o["reps"].toInt(12), 1, 1000);
  s.warmup = std::clamp(o["warmup"].toInt(1), 0, 100);
  s.trimPercent = std::clamp(o["trim"].toInt(0), 0, 40);
  s.randomAuf = o["randomAuf"].toBool(false);
  for (const auto &v : o["entries"].toArray()) {
    QJsonObject e = v.toObject();
    auto key = LabCaseKey::parse(ss(e["case"].toString()));
    std::string alg = ss(e["alg"].toString());
    if (key && !alg.empty()) {
      s.entries.push_back({*key, alg});
    }
  }
  return s;
}

QJsonObject profileToJson(const ErgoProfile &p) {
  QJsonObject o;
  o["name"] = qs(p.name);
  QJsonObject moves;
  for (const auto &[k, v] : p.moves) {
    moves[qs(k)] = v;
  }
  o["moves"] = moves;
  o["other"] = p.otherMove;
  o["axis"] = p.axisChange;
  QJsonArray rules;
  for (const auto &r : p.rules) {
    rules.append(QJsonObject{
        {"first", qs(r.first)}, {"second", qs(r.second)}, {"cost", r.cost}});
  }
  o["rules"] = rules;
  if (p.grips) {
    const GripModel &g = *p.grips;
    QJsonArray tricks;
    for (const auto &t : g.tricks) {
      QJsonObject j{{"move", qs(t.move)},
                    {"hand", qs(handId(t.hand))},
                    {"finger", qs(fingerId(t.finger))},
                    {"how", qs(t.how)},
                    {"from", qs(gripMaskText(t.from))},
                    {"turn", t.turn},
                    {"cost", t.cost}};
      if (t.reset) {
        j["reset"] = true;
      }
      if (!t.on) {
        j["off"] = true;
      }
      tricks.append(j);
    }
    o["grips"] = QJsonObject{{"other", g.otherMove},
                             {"regrip", g.regrip},
                             {"hiddenRegrip", g.hiddenRegrip},
                             {"regripStep", g.regripStep},
                             {"strain", g.strain},
                             {"start", g.start},
                             {"sameFinger", g.sameFinger},
                             {"tricks", tricks}};
  }
  return o;
}

// A cost from a file, or `fallback` when it is missing or not one a
// profile may hold.
double costOr(const QJsonValue &v, double fallback) {
  return v.isDouble() && validCost(v.toDouble()) ? v.toDouble() : fallback;
}

GripModel gripsFromJson(const QJsonObject &o) {
  GripModel g;
  g.otherMove = costOr(o["other"], g.otherMove);
  g.regrip = costOr(o["regrip"], g.regrip);
  g.hiddenRegrip = costOr(o["hiddenRegrip"], g.hiddenRegrip);
  g.regripStep = costOr(o["regripStep"], g.regripStep);
  g.strain = costOr(o["strain"], g.strain);
  g.start = costOr(o["start"], g.start);
  g.sameFinger = costOr(o["sameFinger"], g.sameFinger);
  for (const auto &v : o["tricks"].toArray()) {
    QJsonObject j = v.toObject();
    auto hand = handFromId(ss(j["hand"].toString()));
    auto finger = fingerFromId(ss(j["finger"].toString()));
    auto from = gripMaskFromText(ss(j["from"].toString()));
    std::string move = ss(j["move"].toString());
    if (!hand || !finger || !from || move.empty()) {
      continue;
    }
    GripTrick t;
    t.move = move;
    t.hand = *hand;
    t.finger = *finger;
    t.how = ss(j["how"].toString());
    t.from = *from;
    t.turn = std::clamp(j["turn"].toInt(0), -4, 4);
    t.cost = costOr(j["cost"], 1.0);
    t.reset = j["reset"].toBool(false);
    t.on = !j["off"].toBool(false);
    g.tricks.push_back(std::move(t));
  }
  return g;
}

ErgoProfile profileFromJson(const QJsonObject &o) {
  ErgoProfile p;
  p.name = ss(o["name"].toString());
  QJsonObject moves = o["moves"].toObject();
  for (auto it = moves.begin(); it != moves.end(); ++it) {
    // A cost that can't be used leaves the move at the "other" cost.
    if (it.value().isDouble() && validCost(it.value().toDouble())) {
      p.moves[it.key().toStdString()] = it.value().toDouble();
    }
  }
  p.otherMove = costOr(o["other"], 2.0);
  p.axisChange = costOr(o["axis"], 0.0);
  for (const auto &v : o["rules"].toArray()) {
    QJsonObject r = v.toObject();
    if (r["cost"].isDouble() && validCost(r["cost"].toDouble())) {
      p.rules.push_back({ss(r["first"].toString()), ss(r["second"].toString()),
                         r["cost"].toDouble()});
    }
  }
  if (o["grips"].isObject()) {
    p.grips = gripsFromJson(o["grips"].toObject());
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
    cases.append(QJsonObject{{"id", qs(c.id)},
                             {"name", qs(c.name)},
                             {"setup", qs(c.setup)},
                             {"free", qs(c.free)},
                             {"aufBefore", c.aufBefore},
                             {"aufAfter", c.aufAfter}});
  }
  o["cases"] = cases;
  QJsonArray candidates;
  for (const auto &cc : d.candidates) {
    QJsonArray algs;
    for (const auto &c : cc.candidates) {
      algs.append(QJsonObject{{"alg", qs(c.alg)},
                              {"source", qs(candidateSourceId(c.source))},
                              {"added", static_cast<qint64>(c.added)}});
    }
    candidates.append(QJsonObject{{"case", qs(cc.key.text())}, {"algs", algs}});
  }
  o["candidates"] = candidates;
  QJsonArray attempts;
  for (const auto &a : d.attempts) {
    QJsonObject j;
    j["case"] = qs(a.key.text());
    j["alg"] = qs(a.alg);
    j["cube"] = qs(cubeKindId(a.cube));
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
    j["session"] = qs(a.session);
    j["timestamp"] = static_cast<qint64>(a.timestamp);
    if (!a.scramble.empty()) {
      j["scramble"] = qs(a.scramble);
    }
    if (!a.moves.empty()) {
      j["moves"] = qs(a.moves);
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
        QJsonObject{{"name", qs(m.name)}, {"moves", qs(m.set.toString())}});
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
    lc.id = ss(c["id"].toString());
    lc.name = ss(c["name"].toString());
    lc.setup = ss(c["setup"].toString());
    lc.free = ss(c["free"].toString());
    lc.aufBefore = c["aufBefore"].toBool(true);
    lc.aufAfter = c["aufAfter"].toBool(true);
    if (!lc.id.empty()) {
      d.cases.push_back(std::move(lc));
    }
  }
  for (const auto &v : o["candidates"].toArray()) {
    QJsonObject c = v.toObject();
    auto key = LabCaseKey::parse(ss(c["case"].toString()));
    if (!key) {
      continue;
    }
    CaseCandidates cc{*key, {}};
    for (const auto &a : c["algs"].toArray()) {
      QJsonObject j = a.toObject();
      Candidate cand;
      cand.alg = ss(j["alg"].toString());
      cand.source = candidateSourceFromId(ss(j["source"].toString()))
                        .value_or(CandidateSource::Typed);
      cand.added = jsonInt64(j["added"]);
      if (!cand.alg.empty()) {
        cc.candidates.push_back(std::move(cand));
      }
    }
    d.candidates.push_back(std::move(cc));
  }
  for (const auto &v : o["attempts"].toArray()) {
    QJsonObject j = v.toObject();
    auto key = LabCaseKey::parse(ss(j["case"].toString()));
    if (!key) {
      continue;
    }
    LabAttempt a;
    a.key = *key;
    a.alg = ss(j["alg"].toString());
    a.cube =
        cubeKindFromId(ss(j["cube"].toString())).value_or(CubeKind::Physical);
    a.timeMs = jsonInt64(j["time"]);
    a.dnf = j["dnf"].toBool(false);
    a.deviated = j["deviated"].toBool(false);
    a.include = j["include"].toBool(false);
    a.session = ss(j["session"].toString());
    a.timestamp = jsonInt64(j["timestamp"]);
    a.scramble = ss(j["scramble"].toString());
    a.moves = ss(j["moves"].toString());
    d.attempts.push_back(std::move(a));
  }
  if (o.contains("profiles")) {
    for (const auto &v : o["profiles"].toArray()) {
      d.profiles.push_back(profileFromJson(v.toObject()));
    }
    // Written before grip profiles: the built-in one goes first, as the
    // new default.
    if (o["version"].toInt(1) < 2 &&
        std::none_of(
            d.profiles.begin(), d.profiles.end(),
            [](const ErgoProfile &p) { return p.grips.has_value(); })) {
      d.profiles.insert(d.profiles.begin(), builtinErgoProfiles().front());
    }
  } else {
    d.profiles = builtinErgoProfiles();
  }
  for (const auto &v : o["moveSets"].toArray()) {
    QJsonObject m = v.toObject();
    auto set = MoveSet::parse(ss(m["moves"].toString()));
    if (set) {
      d.moveSets.push_back({ss(m["name"].toString()), *set});
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
  // Two of one name (from an older version, or a file edited by hand)
  // could not be told apart.
  for (std::size_t i = 1; i < profiles_.size(); ++i) {
    profiles_[i].name = uniqueProfileName(profiles_[i].name, i);
  }
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

std::string
AlgLabStore::uniqueProfileName(std::string name,
                               std::optional<std::size_t> except) const {
  auto taken = [&](const std::string &n) {
    for (std::size_t i = 0; i < profiles_.size(); ++i) {
      if (i != except && profiles_[i].name == n) {
        return true;
      }
    }
    return false;
  };
  if (!taken(name)) {
    return name;
  }
  for (int n = 2;; ++n) {
    std::string numbered = name + " " + std::to_string(n);
    if (!taken(numbered)) {
      return numbered;
    }
  }
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
