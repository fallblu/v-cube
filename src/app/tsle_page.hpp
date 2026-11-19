#pragma once

#include "app/practice_page.hpp"
#include "train/tsle.hpp"

#include <array>
#include <memory>
#include <optional>
#include <vector>

class QPushButton;
class QToolButton;

namespace vcube {

// TSLE mode: ZZ-CT's last-slot step a trigger at a time (see tsle.hpp),
// in three tabs (see PracticePage). From any state every good trigger is
// known, each with the U turn it needs first, so that the one that suits
// the AUF at hand can be chosen rather than one trigger learned per case.
//
//   Learn    a case's card: the state at a random AUF, do the next step.
//            Any good trigger is Good; the verdict says whether it was the
//            best from that AUF.
//   Untimed  a random last-slot state to the end, a line on each trigger
//            as it is done, then compared trigger by trigger.
//   Timed    the whole step, kept by the case it started from.
//
// A chain that ends in an OCLL runs through the OCLL; the OCLL algs can
// be changed and are what the best way is priced with.
class TslePage : public PracticePage {
  Q_OBJECT
public:
  TslePage(AppSettings &settings, const QString &dataDir,
           QWidget *parent = nullptr);
  ~TslePage() override;

  // What the page shows, for tests and scripts.
  Slot slot() const { return slot_; }
  // The state the solve began in and the one the cube is in now (both
  // seen for the front-right slot); the latter nullopt mid-trigger.
  TsleState startState() const { return start_; }
  std::optional<TsleState> currentState() const;
  const TsleSolver &solver(Slot slot) const;
  std::array<bool, kSlots> lastSlots() const { return slots_; }
  void setLastSlots(const std::array<bool, kSlots> &chosen);

protected:
  QString modeId() const override { return "tsle"; }
  QString stepName() const override { return "TSLE"; }
  std::vector<Tab> tabs() const override {
    return {Tab::Learn, Tab::Untimed, Tab::Timed};
  }
  void addOptions(OptionBar *bar) override;
  void addButtons(OptionBar *bar) override;
  void showOptions() override;
  std::vector<std::string> cardPool() const override;
  std::optional<Setup> setUp(Extent extent, const std::string &card) override;
  void restarted() override;
  bool turned() override;
  Outcome judge() override;
  QString detail() override;
  QString hint() override;
  bool hasHints() const override { return true; }
  Alg demoAlg() override;
  QString demoText() override;
  QString task() const override;
  QString taskNote() const override;
  std::vector<ListRow> cardRows() override;
  std::vector<Split> splits() override;
  std::vector<ListRow> untimedGroups() override;
  std::vector<ListRow> timedGroups() override;
  QString listHelp() const override;
  QString cardName(const std::string &card) const override;

private:
  // One step of a solve, and how it compares.
  struct Step {
    std::size_t begin = 0;
    std::size_t end = 0;
    TsleState before;
    TsleState after;
    int moves = 0;     // HTM, as simplified
    bool good = false; // fewer steps left after it (or the OCLL solved)
    int extra = 0;     // moves over the best way, for a good one
    bool hint = false;
  };

  std::array<std::unique_ptr<TsleSolver>, kSlots> solvers_;
  std::array<bool, kSlots> slots_{true, true, false, false};
  Slot slot_ = Slot::FR;
  TsleState start_;
  std::vector<Step> steps_;
  std::size_t stepBegin_ = 0;
  TsleState stepState_;
  bool stepHint_ = false;

  QToolButton *slotsButton_ = nullptr;
  QPushButton *ocllButton_ = nullptr;

  void rebuildSolvers();
  std::vector<OcllAlg> ocllAlgs() const;
  Slot randomSlot();
  Step judged(TsleState before, TsleState after, const Alg &moves) const;
  QString optionsHtml(TsleState state) const;
  QString pathHtml(TsleState state) const;
  QString stepVerdict(const Step &step) const;
  QString caseText(TsleState state) const;
  // The rows of the cases: TSLE's own, or the OCLLs they end in as well.
  enum class Rows { TsleOnly, WithOclls };
  std::vector<ListRow> caseRows(Rows which);
  void editOcllAlgs();
};

} // namespace vcube
