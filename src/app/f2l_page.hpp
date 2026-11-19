#pragma once

#include "app/practice_page.hpp"
#include "io/alglab_store.hpp"
#include "io/library.hpp"
#include "train/f2l.hpp"

#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QToolButton;

namespace vcube {

// F2L mode: the F2L of a method (ZZ F2L - 1 as ZZ-CT leaves it by default;
// the whole ZZ F2L; CFOP's, without EO) in three tabs (see PracticePage).
//
//   Learn    a pair case's card, for the front slots or the back ones, set
//            up with the other slots solved and the pair ringed. When a
//            case comes in, the solutions to use for it are chosen from
//            the best ones searched for it (and the user's sheet's alg,
//            ticked to start with); a try is Good when it is one of them,
//            or costs no more than the cheapest of them (which can then be
//            added). A card's solutions are written for its side's own
//            slot (FR or BR) and done mirrored in the other (FL or BL).
//   Untimed  a random F2L to the end, for the pairs picked and how well
//            they were solved: a line on each pair as it goes in, then
//            the pair-by-pair comparison.
//   Timed    the whole F2L, kept by method.
//
// Solutions are searched for with every move the method allows (see
// f2l.hpp) and ranked by the Alg Lab's score profile.
class F2LPage : public PracticePage {
  Q_OBJECT
public:
  // How many searched solutions a case's choice is made from.
  static constexpr std::size_t kCandidates = 8;

  F2LPage(AppSettings &settings, AlgLibrary &library, AlgLabStore &lab,
          const QString &dataDir, QWidget *parent = nullptr);
  ~F2LPage() override;

  void activated() override;
  // The alg sets changed: the sheet's algs may be read from them.
  void reloadSets();

  // What the page shows, for tests and scripts.
  const F2LConfig &config() const { return config_; }
  void setConfig(const F2LConfig &config);
  const CubieCube &startState() const { return start_; }
  std::optional<Slot> target() const { return target_; }
  bool analysisReady() const override;
  // The pair's solutions from the state on the cube (best first); empty
  // until worked out.
  std::vector<RankedSolution> shownSolutions() const;
  // The card's chosen solutions, written for its side's own slot.
  std::vector<Alg> chosenSolutions(const std::string &card) const;
  // The card on the cube's chosen solutions that can be done from its state
  // with the method's moves (one chosen with another method may not).
  std::vector<Alg> usableSolutions() const;
  // The solutions offered while choosing (written for the side's own
  // slot), whether each is ticked, and whether they are worked out.
  struct Candidate {
    RankedSolution solution;
    bool ticked = false;
    bool sheet = false;  // the user's sheet's alg for the case
    bool custom = false; // typed in
  };
  const std::vector<Candidate> &candidates() const { return candidates_; }
  bool candidatesReady() const { return candidatesReady_; }
  // Types `text` into the field for a solution of one's own (as done for
  // the slot shown) and adds it: an error when it doesn't solve the pair.
  QString addOwnSolution(const QString &text);
  // Whether the try just done may be added to the card's solutions.
  bool offersToAdd() const { return offerAdd_; }

protected:
  QString modeId() const override { return "f2l"; }
  QString stepName() const override { return "F2L"; }
  std::vector<Tab> tabs() const override {
    return {Tab::Learn, Tab::Untimed, Tab::Timed};
  }
  void addOptions(OptionBar *bar) override;
  void showOptions() override;
  std::vector<std::string> cardPool() const override;
  std::optional<Setup> setUp(bool whole, const std::string &card) override;
  void restarted() override;
  bool turned() override;
  void cubeChanged() override;
  Outcome judge() override;
  QString detail() override;
  QString hint() override;
  bool hasHints() const override { return target_.has_value(); }
  Alg demoAlg() override;
  QString demoText() override;
  QString task() const override;
  QString taskNote() const override;
  bool needsChoice() const override;
  QWidget *choicePanel() override;
  void startChoosing() override;
  void choiceKey(int n) override;
  bool confirmChoice() override;
  void cancelChoice() override {}
  QString choosingText() const override;
  bool doneKey(QKeyEvent *e) override;
  QList<KeyHint> doneKeys() const override;
  std::vector<Split> splits() override;
  std::vector<ListRow> cardRows() override;
  std::vector<ListRow> untimedGroups() override;
  std::vector<ListRow> timedGroups() override;
  QString listHelp() const override;
  void groupActivated(const std::string &key) override;
  QString cardName(const std::string &card) const override;

private:
  // What a background job worked out, for the request that asked.
  struct Analysis {
    int request = 0;
    int ask = 0; // whole: which asking for the segment
    std::optional<PairAnalysis> pair;
    std::optional<std::size_t> segment; // whole: the segment's index
    std::vector<PairRank> ranks;
  };

  AlgLibrary &library_;
  AlgLabStore &lab_;
  F2LConfig config_;
  std::shared_ptr<std::atomic<bool>> stop_;
  // Stops the rankings of segments after the start, which a restart drops.
  std::shared_ptr<std::atomic<bool>> laterStop_;
  int requests_ = 0;
  int asks_ = 0;

  // The state on the cube.
  CubieCube start_;
  std::optional<Slot> target_; // one pair
  std::optional<PairCard> card_;
  int hintMoves_ = 0;
  // The pair's solutions from the state, once worked out.
  std::optional<PairAnalysis> pair_;
  // A whole solve's pairs, and once worked out how each compares: the
  // ranking of the pairs from where each was begun (index: the segment;
  // with no pair solved, the start).
  std::vector<F2LSegment> segments_;
  std::map<std::size_t, std::vector<PairRank>> ranks_;
  std::map<std::size_t, int> asked_; // segment -> the asking still wanted
  // A card's try: whether it can be added to its solutions.
  bool offerAdd_ = false;
  Alg tryAsChosen_; // the try, written for the side's own slot

  // Choosing.
  std::vector<Candidate> candidates_;
  bool candidatesReady_ = false;
  // What the candidates were searched for (card, method's moves, profile),
  // the search going on and what stops it, and what each search found.
  std::string searchKey() const;
  std::string searching_;
  std::shared_ptr<std::atomic<bool>> candidateStop_;
  std::map<std::string, std::vector<RankedSolution>> searched_;

  QComboBox *methodBox_ = nullptr;
  QToolButton *leaveButton_ = nullptr;
  QWidget *leaveItem_ = nullptr;
  QWidget *chooser_ = nullptr;
  QLabel *chooserList_ = nullptr;
  QLineEdit *ownField_ = nullptr;
  QLabel *ownNote_ = nullptr;

  void loadChoices();
  void saveChoices() const;
  void configChanged();
  AnalysisOptions analysisOptions() const;
  double costOf(const Alg &moves) const;
  // The cost a solution is compared by: without the U turns at its ends.
  double compareCost(const Alg &moves) const;
  void requestPair();
  void requestRanks(std::size_t segment, const CubieCube &before);
  void arrived(const Analysis &a);
  bool initiallySolvedKept(const CubieCube &c) const;
  bool wholeReady() const;
  void updateProgress();
  QString pairLine(std::size_t segment) const;
  QString solutionsHtml(const std::vector<RankedSolution> &solutions) const;
  QString chosenHtml() const;
  QString wholeHtml() const;
  // The chosen solution as done from the state on the cube.
  std::optional<Alg> chosenHere(const Alg &chosen) const;
  // Choosing.
  void buildChooser();
  void requestCandidates();
  void fillCandidates(const std::vector<RankedSolution> &searched);
  void showCandidates();
  void candidateLink(const QString &link);
  // The sheet's algs, read once into the mode's notes.
  void importSheet();
  bool sheetLooked_ = false; // and none found, since the sets last changed
  std::optional<std::string> sheetAlg(const std::string &card) const;
};

} // namespace vcube
