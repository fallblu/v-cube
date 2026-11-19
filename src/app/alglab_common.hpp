#pragma once

#include "io/alglab_store.hpp"
#include "io/library.hpp"
#include "train/algset.hpp"
#include "train/case_logic.hpp"
#include "train/move_set.hpp"

#include <QColor>
#include <QPixmap>
#include <QString>
#include <QTableWidgetItem>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace vcube {

struct AppSettings;

// A case of Alg Lab with everything the page needs to know about it,
// whether it is a case of an alg set or a lab case.
struct ResolvedCase {
  LabCaseKey key;
  QString name;  // "T" or the lab case's name
  QString where; // "PLL / Edges and corners" or "Lab cases"
  CubieCube state;
  PieceMask free;
  bool aufBefore = true;
  bool aufAfter = true;
  std::vector<std::string> setAlgs; // a set case's own algs
  StepSpec spec;                    // how its diagram is drawn
};

// nullopt for a case that is no longer there.
std::optional<ResolvedCase> resolveLabCase(const AlgLibrary &library,
                                           const AlgLabStore &store,
                                           const LabCaseKey &key);

// The case's diagram, as its set draws it (a lab case from above when only
// the last layer is unsolved, else in 3D).
QPixmap labDiagram(const ResolvedCase &c, const AppSettings &settings, int size,
                   qreal dpr);

// What a candidate's row shows that only its moves and its case decide.
struct CandidateFacts {
  std::string key; // labAlgKey() form
  Alg moves;       // empty when it can't be read
  MoveCounts counts;
  bool rotations = false;
  std::optional<AufFit> fit; // when it solves the case
  QString shown;             // the alg with its AUFs, or why it fails
};

// The facts of the algs of one case, kept from one fill of the table to
// the next: fitting an alg to its case is most of the work of a row. They
// are of the case last asked about, and forgotten once another case, or
// that case changed, is asked about.
class CandidateCache {
public:
  // `alg` as it is written in the set or kept as a candidate.
  const CandidateFacts &of(const ResolvedCase &c, const std::string &alg);

private:
  CubieCube state_;
  PieceMask free_;
  bool aufBefore_ = true;
  bool aufAfter_ = true;
  std::unordered_map<std::string, CandidateFacts> facts_;
};

// One candidate as the page lists it.
struct CandidateRow {
  std::string alg; // labAlgKey() form
  CandidateSource source = CandidateSource::Typed;
  bool inSet = false; // the case's set has it
  Alg moves;
  MoveCounts counts;
  bool rotations = false;
  std::optional<AufFit> fit; // when it solves the case
  QString shown;             // the alg with its AUFs, or why it fails
};

// The set's algs, then the kept candidates the set doesn't have.
std::vector<CandidateRow> candidateRows(const ResolvedCase &c,
                                        const AlgLabStore &store,
                                        CandidateCache &cache);

// A table cell that sorts by a number (missing values last) rather than
// its text.
class SortItem : public QTableWidgetItem {
public:
  SortItem(const QString &text, std::optional<double> key);
  bool operator<(const QTableWidgetItem &other) const override;
};

// The grip profile that shows fingertricks: `chosen` (a profile's name)
// when it is a grip profile, else the first grip profile; null if there
// is none.
const ErgoProfile *labGuideProfile(const AlgLabStore &store,
                                   const QString &chosen);
// The same with the profile chosen in Alg Lab.
const ErgoProfile *labGuideProfile(const AlgLabStore &store);

// Seconds with the app's decimals, "DNF" or "–".
QString labTime(std::optional<std::int64_t> ms, int decimals);

} // namespace vcube
