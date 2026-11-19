#pragma once

#include "app/practice_page.hpp"
#include "train/eo_cross.hpp"

#include <optional>
#include <vector>

class QComboBox;

namespace vcube {

// EO Cross mode: the start of a ZZ solve, from a random-state scramble, in
// the two tabs every practice mode has (see PracticePage). Step picks how
// far a solve goes: EO, EOLine, EO+1, EO+2 or EO Cross.
//
//   Learn  solve the step untimed, then your moves against the optimum.
//          With "Plan, then blind" the cube hides at the first face turn:
//          the step is planned while looking and done unseen, and Space
//          shows how it went.
//   Timed  the step against the clock, kept by step.
//
// A step counts as done in any hold the colour neutrality allows, on
// either EO axis; the optimum is the shortest solution over those holds
// for the state on the cube. The step has no finite list of cases, so
// there are no cards here.
class EoCrossPage : public PracticePage {
  Q_OBJECT
public:
  // One of the optimal solutions listed in the details.
  struct Solution {
    Hold hold;
    Alg alg;
  };

  EoCrossPage(AppSettings &settings, const QString &dataDir,
              QWidget *parent = nullptr);
  ~EoCrossPage() override;

  // What the page shows, for tests and scripts.
  SolveStep step() const { return step_; }
  void setStep(SolveStep step);
  bool blindChosen() const { return blind_; }
  void setBlind(bool on);
  // The state as scrambled (white top, green front) and the hold it is
  // first shown in.
  const CubieCube &scrambled() const { return scrambled_; }
  Hold shownHold() const { return shown_; }
  // The holds a step may be done in.
  std::vector<Hold> allowedHolds() const;
  bool analysisReady() const override { return analysed_; }
  // The optimal solutions listed, best hold first.
  std::vector<Solution> shownSolutions() const;

protected:
  QString modeId() const override { return "eocross"; }
  QString stepName() const override { return "EO Cross"; }
  void addOptions(OptionBar *bar) override;
  void showOptions() override;
  std::optional<Setup> setUp(bool whole, const std::string &card) override;
  void restarted() override;
  bool turned() override;
  Outcome judge() override;
  QString detail() override;
  QString task() const override;
  QString taskNote() const override;
  bool blind() const override { return blind_ && tab() == Tab::Learn; }
  std::vector<ListRow> learnGroups() override;
  std::vector<ListRow> timedGroups() override;
  QString listHelp() const override;
  void groupActivated(const std::string &key) override;

private:
  struct Analysis {
    int request = 0;
    std::vector<HeldOptimum> ranked;
  };

  SolveStep step_ = SolveStep::EOCross;
  bool blind_ = false;
  CubieCube scrambled_;
  Hold shown_ = kWcaHold;
  SolveStep solving_ = SolveStep::EOCross; // the state's step
  std::optional<Hold> solvedIn_;
  std::vector<HeldOptimum> ranked_;
  bool analysed_ = false;
  int requests_ = 0;

  QComboBox *stepBox_ = nullptr;
  QComboBox *blindBox_ = nullptr;
  QWidget *blindItem_ = nullptr;

  void requestAnalysis();
  // Works the optima out now if they haven't arrived.
  void ensureAnalysed();
  // Where a step may be done: "with a yellow or white cross, on either EO
  // axis".
  QString whereText(SolveStep step) const;
  // The moves that solve from the state as shown: the turn to the hold,
  // then the solution.
  Alg fromShown(const Solution &s) const;
  QString breakdownHtml() const;
  QString holdsHtml() const;
};

} // namespace vcube
