#pragma once

#include "app/app_settings.hpp"
#include "io/library.hpp"
#include "train/move_count.hpp"
#include "train/solve_review.hpp"
#include "train/step_solver.hpp"

#include <QDialog>
#include <QFutureWatcher>

#include <optional>
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
};

// Reviews a solve on the virtual cube: a replay to step through (the arrow
// keys, Home and End, or the slider) with the points where each step was
// done marked; for EO, EO+1, EO+2 and EO Cross the optimal solutions, in
// the holds of the colour neutrality (with both EO axes) and over every
// colour and axis; and for a solve that reached EO Cross the F2L pairs one
// by one, against an alg set of pair cases (such as a ZZ F2L set): which
// case each pair was, your moves against the set's alg, and the pair that
// was cheapest at that point. "Retry" asks to solve the scramble again.
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

protected:
  void keyPressEvent(QKeyEvent *e) override;

private:
  struct Optima {
    std::vector<HeldOptimum> neutral; // the neutrality's holds, both axes
    std::vector<HeldOptimum> all;     // every hold
  };

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

  CubeView *cube_;
  QSlider *slider_;
  QLabel *positionLabel_;
  QLabel *movesLabel_;
  QLabel *marksLabel_;
  QLabel *optimalLabel_;
  QWidget *pairsBox_;
  QComboBox *pairSetBox_;
  QLabel *pairsLabel_;
  QPushButton *retryButton_;

  void buildPairSets();
  void showPosition(std::size_t at);
  void showMoves();
  void showMarks();
  void showOptima();
  void showPairs();
  // Replays `alg` (from the scramble) instead of the solution.
  void play(const Alg &alg, const QString &name);
  void startOptima();
};

// "■ yellow top, ■ green front" written as the rotation that turns the
// cube from white top, green front to the hold, e.g. "z2 y", or "" for
// none.
QString holdRotationText(Hold h);

} // namespace vcube
