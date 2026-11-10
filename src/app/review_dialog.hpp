#pragma once

#include "app/app_settings.hpp"
#include "io/library.hpp"
#include "train/move_count.hpp"
#include "train/solve_review.hpp"
#include "train/step_solver.hpp"

#include <QDialog>
#include <QFutureWatcher>
#include <QList>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;

namespace vcube {

class CubeView;

// A solve done on the virtual cube, to review.
struct ReviewedSolve {
  QString title;                 // e.g. "EO Cross #12"
  Alg scramble;                  // applied holding white top, green front
  Alg solution;                  // the moves as done, rotations included
  std::optional<SolveStep> step; // the step it was for; none: a whole solve
  bool dnf = false;
  // A timed solve: when each move of the solution was turned (ms from the
  // start), and the solve's time. Without them there are no step times.
  std::vector<std::int64_t> times;
  std::int64_t totalMs = 0;
  // The move count recorded for the solve, when it has one: all the moves
  // up to the end, which can be more than the step took.
  std::optional<int> recorded;
};

// Reviews a solve on the virtual cube: a replay to step through (the arrow
// keys, Home and End, or the slider) with the points where each step was
// done marked; for a whole solve its splits (the cross, each pair and the
// last layer, with times and TPS when the solve was timed); for the steps
// with a solver the optimal solutions, in the holds of the colour
// neutrality (with both EO axes) and over every colour and axis; and for
// a solve that reached EO Cross the F2L pairs one by one, against an alg
// set of pair cases (such as a ZZ F2L set): which case each pair was, your
// moves against the set's alg and the optimal <R, U, L> pair, and the pair
// that was cheapest at that point. The reconstruction can be copied or
// opened on alg.cubing.net. "Retry" asks to solve the scramble again.
class ReviewDialog : public QDialog {
  Q_OBJECT
public:
  ReviewDialog(ReviewedSolve solve, const AppSettings &settings,
               AlgLibrary *library, QWidget *parent = nullptr);
  ~ReviewDialog() override;

  // Shows the Retry button.
  void setRetryEnabled(bool on);

signals:
  void retryRequested();

private:
  struct Optima {
    std::vector<HeldOptimum> neutral; // the neutrality's holds, both axes
    std::vector<HeldOptimum> all;     // every hold
  };
  // The optimal <R, U, L> solution of each pair segment's slot (nullopt
  // for a segment that solved several slots, or none found).
  using PairOptima = std::vector<std::optional<Alg>>;

  ReviewedSolve solve_;
  const AppSettings &settings_;
  AlgLibrary *library_;
  FaceletCube start_;
  std::vector<StepMark> marks_;
  std::size_t at_ = 0; // moves of the replayed alg shown
  Alg replay_;         // what the replay steps through
  QString replayName_; // "Your solution" or an optimal one
  QFutureWatcher<Optima> optimaWatcher_;
  std::optional<Optima> optima_;
  QFutureWatcher<PairOptima> pairWatcher_;
  std::optional<PairOptima> pairOptima_;
  SolveSplits splits_;

  CubeView *cube_;
  QSlider *slider_;
  QLabel *positionLabel_;
  QLabel *movesLabel_;
  QLabel *marksLabel_;
  QLabel *splitsLabel_;
  QLabel *optimalLabel_;
  QLabel *compareLabel_;
  QWidget *pairsBox_;
  QComboBox *pairSetBox_;
  QLabel *pairsLabel_;
  QPushButton *retryButton_;

  void buildPairSets();
  void showPosition(std::size_t at);
  void showMoves();
  void showMarks();
  void showSplits();
  void showOptima();
  void showPairs();
  // Replays `alg` (from the scramble) instead of the solution.
  void play(const Alg &alg, const QString &name);
  void startOptima();
  void startPairOptima();
  void step(int by);
};

// A small dialog with what is kept for a solve, a row per item ("Date",
// "Scramble", ...), with Copy buttons for the scramble and solution rows
// and, with `review`, a Review button that closes it and calls `review`.
void showSolveDetails(QWidget *parent, const QString &title,
                      const QList<QPair<QString, QString>> &rows,
                      const std::function<void()> &review = {});

// Plain text in a dialog with a Copy button, e.g. the solves of an
// average.
void showTextDialog(QWidget *parent, const QString &title, const QString &text);

// The solves of a statistic as cstimer lists them: "ao5: 12.34", then a
// line per solve with its number, its result (in brackets when the
// average drops it) and scramble. `first` is the number of the first.
// `results` gives each solve's text.
QString statListing(const QString &heading, std::span<const Solve> solves,
                    std::size_t first, bool trimmed,
                    const std::function<QString(std::size_t)> &result);

// "■ yellow top, ■ green front" written as the rotation that turns the
// cube from white top, green front to the hold, e.g. "z2 y", or "" for
// none.
QString holdRotationText(Hold h);

} // namespace vcube
