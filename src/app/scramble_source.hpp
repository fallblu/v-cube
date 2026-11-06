#pragma once

#include "app/colours.hpp"
#include "app/widgets.hpp"
#include "io/library.hpp"

#include <QComboBox>
#include <QFutureWatcher>
#include <QObject>

class QPushButton;

#include <functional>
#include <optional>

namespace vcube {

// How to make a random state for a scramble type: a built-in type id
// ("333", "ll", ...) or "set:<alg set id>", which picks one of the set's
// ticked cases with its free pieces and AUFs randomised (and names it).
std::function<ScrambleSetup(Rng &)>
scrambleStateMaker(const AlgLibrary &library, const QString &type);

// The scramble types shared by Timed and Untimed mode: the
// built-in random-state types, then one per alg set with cases. With
// `only`, just those built-in types (by id, in that order) and no alg sets.
class ScrambleTypeBox : public QComboBox {
  Q_OBJECT
public:
  ScrambleTypeBox(const AlgLibrary &library, QWidget *parent = nullptr,
                  QStringList only = {});

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
  QStringList only_;
  QString last_;
  void emitIfChanged();
};

// Generates scrambles of one type on the thread pool, keeping the next one
// ready so that asking for it is instant. Each is meant for a hold picked
// from the colour neutrality.
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
  // Uses `neutrality` from now on, regenerating if it changed.
  void setNeutrality(const ColourNeutrality &neutrality);

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
  ColourNeutrality neutrality_;
  QFutureWatcher<GeneratedScramble> watcher_;
  QString pendingType_;
  ColourNeutrality pendingNeutrality_;
  std::optional<GeneratedScramble> current_;
  std::optional<GeneratedScramble> prefetched_;
  bool wantMore_ = false;
  bool started_ = false;

  void request();
  void ready();
};

// The algs of the alg set case a scramble sets up, for Timed and Untimed
// mode: a checkable "Algs (Tab)" button, offered for alg set
// scramble types, and a label that shows the case's algs (with the AUFs
// the scramble needs) while the button is on. Showing the algs changes nothing
// else. The choice is kept under `settingsKey`.
class ScrambleAlgs : public QObject {
  Q_OBJECT
public:
  ScrambleAlgs(AlgLibrary &library, QString settingsKey, QWidget *parent);

  QPushButton *button() const { return button_; }
  QLabel *label() const { return label_; }

  // The scramble type in use: the button and label are offered for alg
  // set types only.
  void setType(const QString &type);
  // The scramble shown, or nullptr while there is none.
  void setScramble(const GeneratedScramble *scramble);
  // The alg sets changed.
  void refresh() { update(); }
  // Shows or hides the algs (Tab); false if they aren't offered.
  bool toggle();

private:
  AlgLibrary &library_;
  QString settingsKey_;
  QPushButton *button_;
  QLabel *label_;
  bool offered_ = false;
  std::optional<GeneratedScramble> scramble_;

  void update();
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
