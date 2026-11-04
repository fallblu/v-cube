#pragma once

#include "app/colours.hpp"
#include "app/widgets.hpp"
#include "io/library.hpp"

#include <QComboBox>
#include <QFutureWatcher>
#include <QObject>

#include <functional>
#include <optional>

namespace vcube {

// How to make a random state for a scramble type: a built-in type id
// ("333", "ll", ...) or "set:<alg set id>", which picks one of the set's
// ticked cases with its free pieces and AUFs randomised.
std::function<CubieCube(Rng &)> scrambleStateMaker(const AlgLibrary &library,
                                                   const QString &type);

// The scramble types shared by Timer, Scramble and Virtual mode: the
// built-in random-state types, then one per alg set with cases.
class ScrambleTypeBox : public QComboBox {
  Q_OBJECT
public:
  ScrambleTypeBox(const AlgLibrary &library, QWidget *parent = nullptr);

  QString currentType() const;
  // Selects `type`, or the first type if it isn't offered.
  void setCurrentType(const QString &type);
  // Re-reads the alg sets, keeping the selection when it still exists.
  void refresh();

signals:
  // The user or refresh() changed the selected type.
  void typeChanged(const QString &type);

private:
  const AlgLibrary &library_;
  QString last_;
  void emitIfChanged();
};

// Generates scrambles of one type on the thread pool, keeping the next one
// ready so that asking for it is instant.
class ScrambleFeed : public QObject {
  Q_OBJECT
public:
  explicit ScrambleFeed(const AlgLibrary &library, QObject *parent = nullptr);

  QString type() const { return type_; }
  // Switches type and starts generating; the current scramble is dropped.
  // Does nothing if the feed already runs with this type.
  void setType(const QString &type);
  // Drops the current and prefetched scrambles and generates new ones
  // (e.g. after the alg sets changed).
  void regenerate();

  // The scramble to show, or nullopt while it is being generated.
  const std::optional<GeneratedScramble> &current() const { return current_; }
  // Moves on to the next scramble.
  void advance();

signals:
  // current() changed, including to or from nullopt.
  void currentChanged();

private:
  const AlgLibrary &library_;
  QString type_ = "333";
  QFutureWatcher<GeneratedScramble> watcher_;
  QString pendingType_;
  std::optional<GeneratedScramble> current_;
  std::optional<GeneratedScramble> prefetched_;
  bool wantMore_ = false;
  bool started_ = false;

  void request();
  void ready();
};

// A small unfolded net of a cube state, in the WCA orientation (white top,
// green front), e.g. to preview a scramble.
class NetPreview : public QWidget {
  Q_OBJECT
public:
  explicit NetPreview(QWidget *parent = nullptr);
  void setScheme(const ColourScheme &scheme);
  void setCube(const std::optional<FaceletCube> &cube);
  // The state `scramble` produces from solved; nullopt if it can't be read.
  void setScramble(const QString &scramble);

  QSize sizeHint() const override { return {156, 117}; }
  QSize minimumSizeHint() const override { return {96, 72}; }

protected:
  void paintEvent(QPaintEvent *) override;

private:
  ColourScheme scheme_ = ColourScheme::standard();
  std::optional<FaceletCube> cube_;
};

} // namespace vcube
