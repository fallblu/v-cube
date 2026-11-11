#pragma once

#include "io/alglab_store.hpp"
#include "io/library.hpp"
#include "train/algset.hpp"
#include "train/case_logic.hpp"

#include <QColor>
#include <QPixmap>
#include <QString>
#include <QTableWidgetItem>

#include <optional>
#include <string>
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

// One candidate as the page lists it.
struct CandidateRow {
  std::string alg; // labAlgKey() form
  CandidateSource source = CandidateSource::Typed;
  bool inSet = false;        // the case's set has it
  std::optional<AufFit> fit; // when it solves the case
  QString shown;             // the alg with its AUFs, or why it fails
};

// The set's algs, then the kept candidates the set doesn't have.
std::vector<CandidateRow> candidateRows(const ResolvedCase &c,
                                        const AlgLabStore &store);

// A table cell that sorts by a number (missing values last) rather than
// its text.
class SortItem : public QTableWidgetItem {
public:
  SortItem(const QString &text, std::optional<double> key);
  bool operator<(const QTableWidgetItem &other) const override;
};

// Colours for the algs compared in a chart, in a fixed order (eight).
QColor seriesColour(std::size_t i, bool dark);

// Seconds with the app's decimals, "DNF" or "–".
QString labTime(std::optional<std::int64_t> ms, int decimals);

} // namespace vcube
