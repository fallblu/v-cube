#include "app/eo_cross_check.hpp"

#include "train/move_count.hpp"
#include "train/solve_review.hpp"
#include "train/step_solver.hpp"

#include <QtConcurrent/QtConcurrentRun>

#include <utility>

namespace vcube {

std::vector<Hold> optimalHolds(const AppSettings &settings) {
  if (settings.optimalAnyHold) {
    return {allHolds().begin(), allHolds().end()};
  }
  return withBothEoAxes(settings.neutrality.holds);
}

EoCrossCheck::EoCrossCheck(const AppSettings &settings, QObject *parent)
    : QObject(parent), settings_(settings) {
  connect(&watcher_, &QFutureWatcher<std::optional<int>>::finished, this,
          [this] {
            std::optional<int> best = watcher_.result();
            std::optional<Job> job = std::exchange(running_, std::nullopt);
            if (std::optional<Job> next = std::exchange(next_, std::nullopt)) {
              run(std::move(*next));
            } else if (job && best) {
              QString yours =
                  job->yours ? QString::number(*job->yours) : tr("not done");
              emit found(
                  tr("EO Cross: yours %1, optimal %2").arg(yours).arg(*best));
            }
          });
}

EoCrossCheck::~EoCrossCheck() { watcher_.waitForFinished(); }

void EoCrossCheck::start(const std::string &scramble,
                         const std::string &solution) {
  ParseResult parsed = parseAlg(scramble);
  ParseResult moves = parseAlg(solution);
  if (!parsed || !moves) {
    return;
  }
  Job job;
  job.scramble = parsed.alg;
  FaceletCube start;
  start.apply(parsed.alg);
  for (const StepMark &m : stepMarks(start, moves.alg, {SolveStep::EOCross},
                                     settings_.simplifiedCounts)) {
    job.yours = m.htm;
  }
  if (watcher_.isRunning()) {
    next_ = std::move(job); // the one running is no longer the last solve
  } else {
    run(std::move(job));
  }
}

void EoCrossCheck::cancel() {
  running_.reset();
  next_.reset();
}

void EoCrossCheck::run(Job job) {
  watcher_.setFuture(
      QtConcurrent::run([alg = job.scramble, holds = optimalHolds(settings_)] {
        return heldDistance(alg, SolveStep::EOCross, holds);
      }));
  running_ = std::move(job);
}

} // namespace vcube
