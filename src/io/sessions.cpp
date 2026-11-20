#include "io/sessions.hpp"

#include "cube/scramble.hpp"
#include "io/algset_json.hpp"
#include "io/json_values.hpp"

#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

namespace vcube {

namespace {

// Sessions files were written without a "format" at first.
constexpr FileField kSessionsFields[] = {{"sessions", true}};
constexpr FileFormat kSessionsFile{"v-cube.sessions", 1, true, "sessions file",
                                   kSessionsFields};

int penaltyCode(Penalty p) {
  return p == Penalty::Plus2 ? 1 : p == Penalty::Dnf ? 2 : 0;
}

Penalty penaltyFromCode(int c) {
  return c == 1 ? Penalty::Plus2 : c == 2 ? Penalty::Dnf : Penalty::None;
}

QJsonArray solveToJson(const Solve &s) {
  QJsonArray a{static_cast<double>(s.timeMs), penaltyCode(s.penalty),
               qs(s.scramble), qs(s.comment), static_cast<double>(s.timestamp)};
  if (!s.moves.empty()) {
    a.append(qs(s.moves));
    if (!s.moveTimes.empty()) {
      a.append(jsonArray(
          s.moveTimes, [](std::int64_t t) { return static_cast<double>(t); }));
    }
  }
  return a;
}

Solve solveFromJson(const QJsonValue &v) {
  const QJsonArray a = v.toArray();
  Solve s;
  s.timeMs = jsonInt64(a.at(0));
  s.penalty = penaltyFromCode(a.at(1).toInt());
  s.scramble = ss(a.at(2).toString());
  s.comment = ss(a.at(3).toString());
  s.timestamp = jsonInt64(a.at(4));
  s.moves = ss(a.at(5).toString());
  s.moveTimes = vectorOf(a.at(6), jsonInt64);
  return s;
}

} // namespace

SessionStore::SessionStore(QString path)
    : SessionFileStore(std::move(path), kSessionsFile) {}

QJsonObject SessionStore::toJson() const {
  return sessionsToJson(sessions_, current_, &cache_);
}

void SessionStore::fromJson(const QJsonObject &o) {
  std::size_t current = 0;
  std::vector<Session> sessions = sessionsFromJson(o, &current);
  assign(std::move(sessions), current);
}

QJsonObject sessionsToJson(const std::vector<Session> &sessions,
                           std::size_t current, ItemJsonCache<Solve> *cache) {
  ItemJsonCache<Solve> fresh;
  ItemJsonCache<Solve> &solves = cache ? *cache : fresh;
  solves.trim(sessions.size());
  std::size_t i = 0;
  return sessionListJson(kSessionsFile.format, sessions, current,
                         [&](const Session &s, QJsonObject &so) {
                           so["solves"] =
                               solves.array(i++, s.solves, solveToJson);
                         });
}

std::vector<Session> sessionsFromJson(const QJsonObject &o,
                                      std::size_t *current) {
  return sessionListFromJson<Session>(
      o, current, [](const QJsonObject &so, Session &s) {
        s.solves = vectorOf(so.value("solves"), solveFromJson);
      });
}

namespace {

// v-cube's types that cstimer has with the same id, and cstimer's own 3x3
// types (the rest of its types are for other puzzles).
const std::vector<std::string> &sharedTypes() {
  static const std::vector<std::string> ids = {
      "333", "f2l", "eoline", "eocross", "ll",   "zbll",
      "cll", "ell", "pll",    "corners", "edges"};
  return ids;
}

bool isCstimer3x3(const std::string &type) {
  static const std::vector<std::string> ids = {
      "2gen",  "2genl",  "2gll",   "333drud", "cmll",   "coll",
      "easyc", "easyxc", "eols",   "half",    "lse",    "lsemu",
      "lsll2", "mt3qb",  "mt6cp",  "mtcdrll", "mteole", "mtl5ep",
      "mttdr", "oll",    "roux",   "sbrx",    "ttll",   "vls",
      "wvls",  "zbls",   "zbllx2", "zzll",    "input"};
  return type.empty() || type.starts_with("333") ||
         std::find(ids.begin(), ids.end(), type) != ids.end() ||
         std::find(sharedTypes().begin(), sharedTypes().end(), type) !=
             sharedTypes().end();
}

// "Rw" for v-cube's "r", and the rest as they are.
std::string cstimerMove(Move m) {
  std::string text = toString(m);
  if (!text.empty() && text[0] >= 'a' && text[0] <= 'z' && text[0] != 'x' &&
      text[0] != 'y' && text[0] != 'z') {
    text[0] = static_cast<char>(text[0] - 'a' + 'A');
    text.insert(1, "w");
  }
  return text;
}

// "R@120 U'@250" into moves and times, if every token reads. The times
// must not go back, and are kept within kMaxFileNumber, so that the gaps
// between them can be worked out.
bool readCstimerSolution(const QString &text, std::string *moves,
                         std::vector<std::int64_t> *times) {
  Alg alg;
  std::vector<std::int64_t> at;
  for (const QString &token : text.split(' ', Qt::SkipEmptyParts)) {
    qsizetype sign = token.lastIndexOf('@');
    if (sign <= 0) {
      return false;
    }
    bool ok = false;
    std::int64_t ms = token.mid(sign + 1).toLongLong(&ok);
    ParseResult parsed = parseAlg(ss(token.left(sign)));
    if (!ok || ms < 0 || !parsed || parsed.alg.size() != 1) {
      return false;
    }
    ms = std::min(ms, kMaxFileNumber);
    if (!at.empty() && ms < at.back()) {
      return false;
    }
    alg.push_back(parsed.alg.front());
    at.push_back(ms);
  }
  if (alg.empty()) {
    return false;
  }
  *moves = toString(alg);
  *times = std::move(at);
  return true;
}

} // namespace

std::string cstimerScrambleType(const std::string &type) {
  if (type == "lsll") {
    return "lsll2";
  }
  const auto &shared = sharedTypes();
  return std::find(shared.begin(), shared.end(), type) != shared.end()
             ? type
             : std::string("333");
}

std::optional<std::string> scrambleTypeFromCstimer(const std::string &type) {
  if (!isCstimer3x3(type)) {
    return std::nullopt;
  }
  if (type == "lsll2") {
    return "lsll";
  }
  const auto &shared = sharedTypes();
  return std::find(shared.begin(), shared.end(), type) != shared.end() &&
                 findScrambleType(type)
             ? type
             : std::string("333");
}

std::optional<CstimerImport> sessionsFromCstimerFile(const QByteArray &bytes,
                                                     QString *error) {
  QJsonParseError pe;
  QJsonDocument doc = QJsonDocument::fromJson(bytes, &pe);
  if (pe.error != QJsonParseError::NoError) {
    if (error) {
      *error = QString("the file isn't complete JSON (%1)")
                   .arg(parseErrorText(bytes, pe));
    }
    return std::nullopt;
  }
  if (!doc.isObject()) {
    if (error) {
      *error = "not a cstimer export";
    }
    return std::nullopt;
  }
  return sessionsFromCstimer(doc.object(), error);
}

std::optional<CstimerImport> sessionsFromCstimer(const QJsonObject &o,
                                                 QString *error) {
  // Session names and order live in properties.sessionData, which cstimer
  // stores as a JSON string (older exports may have an object).
  QJsonObject sessionData;
  QJsonValue sd = o.value("properties").toObject().value("sessionData");
  if (sd.isString()) {
    // Read as nothing it would give every session a default name and the
    // 3x3 type, other puzzles' sessions among them.
    QJsonParseError pe;
    QJsonDocument names = QJsonDocument::fromJson(sd.toString().toUtf8(), &pe);
    if (!names.isObject()) {
      if (error) {
        *error = QString("the session names and types couldn't be read (%1)")
                     .arg(pe.error != QJsonParseError::NoError
                              ? pe.errorString()
                              : QString("not an object"));
      }
      return std::nullopt;
    }
    sessionData = names.object();
  } else {
    sessionData = sd.toObject();
  }
  struct Entry {
    int number;
    int rank;
    Session session;
  };
  std::vector<Entry> entries;
  CstimerImport out;
  bool any = false;
  for (auto it = o.begin(); it != o.end(); ++it) {
    if (!it.key().startsWith("session")) {
      continue;
    }
    bool ok = false;
    int number = it.key().mid(7).toInt(&ok);
    if (!ok) {
      continue;
    }
    any = true;
    QJsonValue value = it.value();
    // Some exports store each session as a JSON string. One that doesn't
    // read is not an empty session: its solves would be left out unsaid.
    QJsonArray solves = value.toArray();
    if (value.isString()) {
      QJsonParseError pe;
      QJsonDocument stored =
          QJsonDocument::fromJson(value.toString().toUtf8(), &pe);
      if (!stored.isArray()) {
        if (error) {
          *error = QString("session %1 couldn't be read (%2)")
                       .arg(number)
                       .arg(pe.error != QJsonParseError::NoError
                                ? pe.errorString()
                                : QString("not a list of solves"));
        }
        return std::nullopt;
      }
      solves = stored.array();
    }
    QJsonObject meta = sessionData.value(QString::number(number)).toObject();
    Entry e{number, meta.value("rank").toInt(number), {}};
    e.session.cube = CubeKind::Physical;
    e.session.name = ss(meta.value("name").toVariant().toString());
    if (e.session.name.empty()) {
      e.session.name = "Session " + std::to_string(number);
    }
    // Older exports have "scr" for the type.
    QJsonObject opt = meta.value("opt").toObject();
    std::string cstimerType =
        ss(opt.value("scrType").toString(meta.value("scr").toString("333")));
    std::optional<std::string> type = scrambleTypeFromCstimer(cstimerType);
    if (!type) {
      out.others.push_back(e.session.name);
      continue;
    }
    e.session.scrambleType = *type;
    for (const auto &sv : solves) {
      QJsonArray a = sv.toArray();
      QJsonArray time = a.at(0).toArray();
      if (time.size() < 2) {
        continue;
      }
      Solve s;
      int pen = time.at(0).toInt();
      s.timeMs = jsonInt64(time.at(1));
      s.penalty = pen == -1 ? Penalty::Dnf
                  : pen > 0 ? Penalty::Plus2
                            : Penalty::None;
      s.scramble = ss(a.at(1).toString());
      s.comment = ss(a.at(2).toString());
      s.timestamp = jsonInt64(a.at(3));
      // A virtual or smart cube solve's moves, with their times.
      QJsonArray solution = a.at(4).toArray();
      if (solution.size() >= 1 &&
          solution.at(1).toString("333") == QLatin1String("333")) {
        readCstimerSolution(solution.at(0).toString(), &s.moves, &s.moveTimes);
      }
      e.session.solves.push_back(std::move(s));
    }
    if (e.session.solves.empty()) {
      ++out.emptySkipped;
      continue;
    }
    entries.push_back(std::move(e));
  }
  if (!any) {
    if (error) {
      *error = "no cstimer sessions found";
    }
    return std::nullopt;
  }
  std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
    return a.rank != b.rank ? a.rank < b.rank : a.number < b.number;
  });
  for (auto &e : entries) {
    out.sessions.push_back(std::move(e.session));
  }
  return out;
}

QByteArray sessionsToCstimer(const std::vector<Session> &sessions) {
  // Written by hand so that "session1" comes first (QJsonObject sorts its
  // keys), which is how cstimer recognises its own format among other
  // timers' files.
  QByteArray out = "{";
  QJsonObject sessionData;
  for (std::size_t i = 0; i < sessions.size(); ++i) {
    const Session &s = sessions[i];
    int number = static_cast<int>(i) + 1;
    QJsonArray solves;
    for (const auto &solve : s.solves) {
      int pen = solve.penalty == Penalty::Dnf     ? -1
                : solve.penalty == Penalty::Plus2 ? 2000
                                                  : 0;
      QJsonArray a{QJsonArray{pen, static_cast<double>(solve.timeMs)},
                   qs(solve.scramble), qs(solve.comment),
                   static_cast<double>(solve.timestamp)};
      if (ParseResult moves = parseAlg(solve.moves);
          moves && !moves.alg.empty()) {
        bool timed = solve.moveTimes.size() == moves.alg.size();
        QStringList tokens;
        for (std::size_t m = 0; m < moves.alg.size(); ++m) {
          tokens << QString("%1@%2")
                        .arg(qs(cstimerMove(moves.alg[m])))
                        .arg(timed ? solve.moveTimes[m] : 0);
        }
        a.append(QJsonArray{tokens.join(' '), "333"});
      }
      solves.append(a);
    }
    out += "\"session" + QByteArray::number(number) +
           "\":" + QJsonDocument(solves).toJson(QJsonDocument::Compact) + ",";
    sessionData[QString::number(number)] = QJsonObject{
        {"name", qs(s.name)},
        {"opt",
         QJsonObject{{"scrType", qs(cstimerScrambleType(s.scrambleType))}}},
        {"rank", number}};
  }
  QJsonObject properties{
      {"sessionData",
       QString::fromUtf8(
           QJsonDocument(sessionData).toJson(QJsonDocument::Compact))}};
  out += "\"properties\":" +
         QJsonDocument(properties).toJson(QJsonDocument::Compact) + "}";
  return out;
}

} // namespace vcube
