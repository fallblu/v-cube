#pragma once

#include "train/stats.hpp"

#include "io/session_store.hpp"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <optional>
#include <string>
#include <vector>

namespace vcube {

struct Session {
  std::string name = "Session 1";
  std::string scrambleType = "333";
  std::vector<Solve> solves;
  CubeKind cube = CubeKind::Virtual;
};

// The timer's sessions, saved as one JSON file.
class SessionStore : public SessionFileStore<Session> {
public:
  explicit SessionStore(QString path);

protected:
  QJsonObject toJson() const override;
  void fromJson(const QJsonObject &o) override;
};

// {"format": "v-cube.sessions", "version": 1, "current": 0, "sessions":
// [{"name": ..., "scrambleType": ..., "cube": "virtual", "solves": [[ms,
// penalty, scramble, comment, unixSeconds, moves?, [ms of each move]?],
// ...]}]}. Files from before "format" was written have none. A session
// without "cube" was saved before sessions had one, when the timer was for
// physical cubes only; a solve without move times was saved before they
// were kept.
QJsonObject sessionsToJson(const std::vector<Session> &sessions,
                           std::size_t current);
std::vector<Session> sessionsFromJson(const QJsonObject &o,
                                      std::size_t *current = nullptr);

// cstimer export files: {"session1": [[[penalty, ms], scramble, comment,
// unixSeconds, [solution, "333"]?], ...], ..., "properties":
// {"sessionData": "<json>"}}, where penalty is 0, 2000 (+2) or -1 (DNF)
// and the solution, from a virtual or smart cube, is moves with the
// milliseconds they were turned at ("R@120 U'@250"). sessionData has each
// session's name, rank (its place in the list) and options, among them
// "scrType", cstimer's scramble type.
struct CstimerImport {
  // The 3x3 sessions with solves, in cstimer's order, for a physical cube.
  std::vector<Session> sessions;
  int emptySkipped = 0;            // sessions without solves
  std::vector<std::string> others; // names of sessions for other puzzles
};
// nullopt (with `error` set) when the object has no cstimer sessions, or
// when a session or sessionData is stored as a string that doesn't read.
std::optional<CstimerImport> sessionsFromCstimer(const QJsonObject &o,
                                                 QString *error);
// The same from a file's contents; a file that isn't whole JSON (cut off,
// say) is reported as such.
std::optional<CstimerImport> sessionsFromCstimerFile(const QByteArray &bytes,
                                                     QString *error);
// The sessions as a cstimer export. The text starts with "session1", as
// cstimer's "Import session(s) from other timers" expects to recognise its
// own format; that adds the sessions to the ones there. (Its "Import from
// file" restores a backup instead, replacing everything.) Scramble types
// are cstimer's where it has the same type, else 3x3 (333).
QByteArray sessionsToCstimer(const std::vector<Session> &sessions);
// cstimer's scramble type for a v-cube one and back; scrambleTypeFromCstimer
// gives nullopt for a type that isn't for a 3x3.
std::string cstimerScrambleType(const std::string &type);
std::optional<std::string> scrambleTypeFromCstimer(const std::string &type);

} // namespace vcube
