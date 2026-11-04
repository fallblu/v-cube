#pragma once

#include "app/app_settings.hpp"
#include "app/cube_painter.hpp"
#include "io/library.hpp"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace vcube {

// Browse and edit alg sets: set settings, groups, cases and each case's
// algs, plus file import/export and pasting alg lists.
class LibraryPage : public QWidget {
  Q_OBJECT
public:
  LibraryPage(AppSettings &settings, AlgLibrary &library,
              QWidget *parent = nullptr);

  void selectCase(const QString &setId, const QString &caseId);
  void applySettings();
  void importSetFile();
  void exportSetFile();

signals:
  void setsChanged();
  void message(const QString &text);

private:
  AppSettings &settings_;
  AlgLibrary &library_;
  std::string setId_;
  std::string caseId_;
  bool loading_ = false;

  QListWidget *setList_;
  QPushButton *restoreButton_;
  QLineEdit *nameEdit_;
  QLineEdit *descriptionEdit_;
  QComboBox *scopeBox_;
  QCheckBox *overrideBox_;
  QComboBox *presetBox_;
  QLineEdit *freeEdit_;
  QComboBox *cornerMaskBox_;
  QComboBox *edgeMaskBox_;
  QComboBox *restMaskBox_;
  QComboBox *viewBox_;
  QCheckBox *aufBeforeBox_;
  QCheckBox *aufAfterBox_;
  QDoubleSpinBox *targetSpin_;
  QWidget *specWidgets_;
  QTreeWidget *caseTree_;
  QGroupBox *caseBox_;
  QLineEdit *caseNameEdit_;
  QLabel *caseDiagram_;
  QListWidget *algList_;
  QLineEdit *newAlgEdit_;

  AlgSet *set();
  AlgCase *currentCase();
  CaseGroup *scopeGroup(); // group whose settings are being edited
  void changed(bool rebuildTree = false);

  void refreshSetList();
  void loadSet();
  void loadSpec();
  void storeSpec();
  void buildCaseTree();
  void loadCase();
  void refreshAlgList();
  void storeAlgs();

  void newSet();
  void duplicateSet();
  void deleteSet();
  void restoreSet();
  void newGroup();
  void renameGroup();
  void deleteSelected();
  void newCaseFromAlg();
  void pasteAlgs();
  void addAlg();
  void moveAlg(int delta);
  void removeAlg();
  void makeMain();

  CubeLook look() const;
};

} // namespace vcube
