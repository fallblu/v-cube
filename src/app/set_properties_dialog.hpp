#pragma once

#include "app/cube_painter.hpp"
#include "train/algset.hpp"

#include <QDialog>

#include <map>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QTimer;
class QWidget;

namespace vcube {

// Edits an alg set's properties: name, description, the random U turns
// around drilled cases, the target time, how Learn teaches the set (guided
// learning, families, a step at a time), the mirror images its cases are
// also set up in, and the step settings (what the algs may leave unsolved
// and how diagrams look) for the whole set or as a group's own override.
// A diagram shows a case of the set as the settings draw it, and a line
// counts the algs that would stop (or start) passing. Works on a copy;
// result() has the edited set.
class SetPropertiesDialog : public QDialog {
  Q_OBJECT
public:
  // `group`: the group whose step settings to show first, or -1 for the
  // whole set. `caseId`: the case to show in the diagram first.
  SetPropertiesDialog(const AlgSet &set, int group, const CubeLook &look,
                      const std::string &caseId, QWidget *parent = nullptr);

  const AlgSet &result() const { return set_; }
  void accept() override;

private:
  AlgSet set_;
  CubeLook look_;
  int scope_ = -1; // group being edited, -1 = whole set
  bool loading_ = false;
  bool targetEdited_ = false;
  // Whether each alg passes now, by case id.
  std::map<std::string, std::vector<bool>> passingNow_;
  std::vector<std::string> previewCases_; // the cases of the scope
  std::size_t previewIndex_ = 0;
  std::string previewCase_;

  QLineEdit *nameEdit_;
  QLineEdit *descriptionEdit_;
  QCheckBox *aufBeforeBox_;
  QCheckBox *aufAfterBox_;
  QDoubleSpinBox *targetSpin_;
  QCheckBox *guidedBox_;
  QCheckBox *familiesBox_;
  QCheckBox *reductionBox_;
  QCheckBox *mirrorLeftRightBox_;
  QCheckBox *mirrorFrontBackBox_;
  QCheckBox *mirrorBothBox_;
  QComboBox *scopeBox_;
  QCheckBox *overrideBox_;
  QWidget *specWidgets_;
  QComboBox *presetBox_;
  QLineEdit *freeEdit_;
  QLabel *freeError_;
  QLabel *freeNote_;
  QComboBox *cornerMaskBox_;
  QComboBox *edgeMaskBox_;
  QComboBox *restMaskBox_;
  QCheckBox *byPieceBox_;
  QComboBox *viewBox_;
  QLabel *preview_;
  QLabel *previewName_;
  QLabel *impact_;
  QTimer *updateTimer_;

  StepSpec *scopeSpec(); // nullptr for a group without its own settings
  void loadScope();
  // Stores the scope's step settings into `into`; false if the free-piece
  // tokens can't be read (shown when `showErrors`).
  bool storeScope(AlgSet &into, bool showErrors);
  // Everything the widgets hold, over `into`.
  bool readWidgets(AlgSet &into, bool showErrors);
  void choosePreviewCases();
  void updatePreview();
};

} // namespace vcube
