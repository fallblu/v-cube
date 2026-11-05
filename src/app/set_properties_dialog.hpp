#pragma once

#include "train/algset.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QWidget;

namespace vcube {

// Edits an alg set's properties: name, description, the random U turns
// around drilled cases, the target time, and the step settings (what the
// algs may leave unsolved and how diagrams look) for the whole set or as a
// group's own override. Works on a copy; result() has the edited set.
class SetPropertiesDialog : public QDialog {
  Q_OBJECT
public:
  // `group`: the group whose step settings to show first, or -1 for the
  // whole set.
  SetPropertiesDialog(const AlgSet &set, int group, QWidget *parent = nullptr);

  const AlgSet &result() const { return set_; }
  void accept() override;

private:
  AlgSet set_;
  int scope_ = -1; // group being edited, -1 = whole set
  bool loading_ = false;

  QLineEdit *nameEdit_;
  QLineEdit *descriptionEdit_;
  QCheckBox *aufBeforeBox_;
  QCheckBox *aufAfterBox_;
  QDoubleSpinBox *targetSpin_;
  QComboBox *scopeBox_;
  QCheckBox *overrideBox_;
  QWidget *specWidgets_;
  QComboBox *presetBox_;
  QLineEdit *freeEdit_;
  QLabel *freeError_;
  QComboBox *cornerMaskBox_;
  QComboBox *edgeMaskBox_;
  QComboBox *restMaskBox_;
  QCheckBox *byPieceBox_;
  QComboBox *viewBox_;

  StepSpec *scopeSpec(); // nullptr for a group without its own settings
  void loadScope();
  bool storeScope(); // false if the free-piece tokens can't be read
};

} // namespace vcube
