#pragma once

#include "app/practice_page.hpp"
#include "io/alglab_store.hpp"
#include "io/library.hpp"
#include "train/f2l.hpp"

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

class QComboBox;
class QToolButton;

namespace vcube {

// F2L mode: the F2L of a method (ZZ F2L - 1 as ZZ-CT leaves it by default;
// the whole ZZ F2L; CFOP's, without EO) in the two tabs every practice
// mode has (see PracticePage).
//
//   Learn, one pair    a pair case's card, set up at some point of a solve
//                      with the pair ringed. Good when it is solved unaided
//                      within kGoodOver of the best solution's cost.
//   Learn, whole F2L   a random F2L to the end, compared pair by pair in
//                      the details.
//   Timed              the whole F2L, kept by method.
//
// Solutions are searched for the state on the cube (see f2l.hpp) and
// ranked by the Alg Lab's score profile; the matching alg of the user's
// set of pair cases is listed with them.
class F2LPage : public PracticePage {
  Q_OBJECT
public:
  // A card's try is Good up to this much cost over the best solution.
  static constexpr double kGoodOver = 1.0;

  F2LPage(AppSettings &settings, AlgLibrary &library, AlgLabStore &lab,
          const QString &dataDir, QWidget *parent = nullptr);
  ~F2LPage() override;

  void activated() override;
  // The alg sets changed: the reference set is read again.
  void reloadSets();

  // What the page shows, for tests and scripts.
  const F2LConfig &config() const { return config_; }
  void setConfig(const F2LConfig &config);
  const CubieCube &startState() const { return start_; }
  std::optional<Slot> target() const { return target_; }
  bool analysisReady() const override;
  // The pair's solutions (best first); empty until worked out.
  std::vector<RankedSolution> shownSolutions() const;

protected:
  QString modeId() const override { return "f2l"; }
  QString stepName() const override { return "F2L"; }
  QString unitName() const override { return tr("One pair"); }
  void addOptions(OptionBar *bar) override;
  void showOptions() override;
  std::vector<std::string> cardPool() const override;
  std::optional<Setup> setUp(bool whole, const std::string &card) override;
  void restarted() override;
  bool turned() override;
  void cubeChanged() override;
  Outcome judge() override;
  QString detail() override;
  bool waitsForAnalysis() const override;
  QString hint() override;
  bool hasHints() const override { return target_.has_value(); }
  Alg demoAlg() override;
  QString demoText() override;
  QString task() const override;
  QString taskNote() const override;
  std::vector<ListRow> cardRows() override;
  std::vector<ListRow> learnGroups() override;
  std::vector<ListRow> timedGroups() override;
  QString listHelp() const override;
  void groupActivated(const std::string &key) override;
  QString cardName(const std::string &card) const override;

private:
  // What a background job worked out, for the request that asked.
  struct Analysis {
    int request = 0;
    int item = 0;
    std::optional<PairAnalysis> pair;
    std::vector<std::vector<PairRank>> segments; // whole: per segment
  };

  AlgLibrary &library_;
  AlgLabStore &lab_;
  F2LConfig config_;
  std::shared_ptr<const AlgSet> reference_;
  std::shared_ptr<std::atomic<bool>> stop_;
  int requests_ = 0;

  // The state on the cube.
  CubieCube start_;
  std::optional<Slot> target_; // one pair
  PairCase pcase_;
  int hintMoves_ = 0;
  // The pair's solutions, once worked out.
  std::optional<PairAnalysis> pair_;
  // A whole solve's pairs, and once worked out how each compares.
  std::vector<F2LSegment> segments_;
  std::vector<std::vector<PairRank>> ranks_;
  bool wholeAsked_ = false;
  bool wholeReady_ = false;

  QComboBox *methodBox_ = nullptr;
  QToolButton *leaveButton_ = nullptr;
  QWidget *leaveItem_ = nullptr;
  QToolButton *movesButton_ = nullptr;

  void loadChoices();
  void saveChoices() const;
  void configChanged();
  AnalysisOptions analysisOptions() const;
  double costOf(const Alg &moves) const;
  void requestPair();
  void requestWhole();
  void arrived(const Analysis &a);
  bool initiallySolvedKept(const CubieCube &c) const;
  QString solutionsHtml(const PairAnalysis &pair) const;
  QString wholeHtml() const;
};

} // namespace vcube
