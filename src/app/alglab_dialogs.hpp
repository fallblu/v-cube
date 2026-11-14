#pragma once

#include "io/library.hpp"
#include "train/alg_lab.hpp"
#include "train/ergonomics.hpp"
#include "train/move_set.hpp"

#include <QDialog>

#include <array>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QTableWidget;

namespace vcube {

struct AppSettings;

// Picks the turns a move set allows, layer by layer (quarter and half
// turns), or as text ("R, U, F2"), and optionally names it to keep as a
// preset.
class MoveSetDialog : public QDialog {
  Q_OBJECT
public:
  MoveSetDialog(const MoveSet &set, QWidget *parent = nullptr);
  MoveSet moveSet() const;
  // The name to keep it under; empty to use it without keeping it.
  QString presetName() const;

private:
  std::array<QCheckBox *, kLayerTypes> quarter_{};
  std::array<QCheckBox *, kLayerTypes> half_{};
  QLineEdit *text_;
  QLineEdit *name_;
  QLabel *error_;
  bool syncing_ = false;
  void fromGrid();
  void fromText();
};

// Edits an ergonomic profile: the cost of each move, of a turn on another
// axis, and the rules for awkward pairs of moves, with a field to try an
// alg against it.
class ProfileDialog : public QDialog {
  Q_OBJECT
public:
  ProfileDialog(const ErgoProfile &profile, QWidget *parent = nullptr);
  ErgoProfile profile() const;

private:
  QLineEdit *name_;
  QTableWidget *moves_;
  QDoubleSpinBox *other_;
  QDoubleSpinBox *axis_;
  QTableWidget *rules_;
  QLineEdit *tryAlg_;
  QLabel *tryScore_;
  void updateTry();
};

class GripView;

// Edits a grip profile: its tricks (which move each turns, with which hand
// and finger, from which grips, how it moves the wrist and what it costs,
// each one on or off) and the costs of regrips and strain, with a field to
// try an alg against it.
class GripProfileDialog : public QDialog {
  Q_OBJECT
public:
  GripProfileDialog(const ErgoProfile &profile, QWidget *parent = nullptr);
  ErgoProfile profile() const;

private:
  ErgoProfile base_;
  QLineEdit *name_;
  QDoubleSpinBox *other_, *regrip_, *hidden_, *step_, *strain_, *start_, *same_;
  QTableWidget *tricks_;
  QLabel *problems_;
  QLineEdit *tryAlg_;
  GripView *tryView_;
  void addTrickRow(const GripTrick &t);
  void updateTry();
};

// Makes or edits a lab case: its name, the setup alg that makes it from a
// solved cube, what its algs must solve (solved, as an alg set's steps do,
// or a free-pieces text such as "LL:p") and the U turns allowed around it.
class LabCaseDialog : public QDialog {
  Q_OBJECT
public:
  LabCaseDialog(const LabCase &c, const AlgLibrary &library,
                const AppSettings &settings, QWidget *parent = nullptr);
  LabCase labCase() const;

private:
  LabCase case_;
  const AlgLibrary &library_;
  const AppSettings &settings_;
  QLineEdit *name_;
  QLineEdit *setup_;
  QComboBox *goal_;
  QLineEdit *free_;
  QCheckBox *before_;
  QCheckBox *after_;
  QLabel *diagram_;
  QLabel *error_;
  QPushButton *ok_ = nullptr;
  void goalChosen();
  void validate();
};

} // namespace vcube
