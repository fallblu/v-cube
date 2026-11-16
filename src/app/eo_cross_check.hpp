#pragma once

#include "app/app_settings.hpp"
#include "cube/hold.hpp"
#include "cube/move.hpp"

#include <QFutureWatcher>
#include <QObject>
#include <QString>

#include <optional>
#include <string>
#include <vector>

namespace vcube {

// The holds a step's optimum is taken over: every hold, or those of the
// colour neutrality on either EO axis (Settings > Timing and solves).
std::vector<Hold> optimalHolds(const AppSettings &settings);

// After a solve on the virtual cube: its own EO Cross count beside the
// optimal one for its scramble, which is worked out in the background.
class EoCrossCheck : public QObject {
  Q_OBJECT

public:
  explicit EoCrossCheck(const AppSettings &settings, QObject *parent = nullptr);
  ~EoCrossCheck() override;

  // Checks a solve. Only the last solve given is reported: one still
  // being worked out is dropped.
  void start(const std::string &scramble, const std::string &solution);
  // Nothing is reported for the solves given so far.
  void cancel();

signals:
  // "EO Cross: yours 9, optimal 7"
  void found(const QString &line);

private:
  struct Job {
    Alg scramble;
    std::optional<int> yours; // none: the solve never had EO Cross done
  };
  void run(Job job);

  const AppSettings &settings_;
  QFutureWatcher<std::optional<int>> watcher_;
  std::optional<Job> running_; // none once its result isn't wanted
  std::optional<Job> next_;
};

} // namespace vcube
