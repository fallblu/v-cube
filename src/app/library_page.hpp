#pragma once

#include "app/app_settings.hpp"
#include "app/cube_painter.hpp"
#include "app/mode_page.hpp"
#include "io/library.hpp"
#include "train/case_logic.hpp"

#include <QTreeWidget>

#include <map>

class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QTreeWidgetItem;

namespace vcube {

// The alg list of a case: algs in order (the first passing one is the main
// alg) with each one's check result; reordered by drag and drop.
class AlgListWidget : public QTreeWidget {
  Q_OBJECT
public:
  explicit AlgListWidget(QWidget *parent = nullptr);

signals:
  void reordered();

protected:
  void dropEvent(QDropEvent *event) override;
};

// Browse and edit alg sets: the set list, each set's case tree, and the
// selected case with its algs. Set properties and step settings live in a
// dialog; set and case actions in menus and context menus.
class LibraryPage : public ModePage {
  Q_OBJECT
public:
  LibraryPage(AppSettings &settings, AlgLibrary &library,
              QWidget *parent = nullptr);

  void selectCase(const QString &setId, const QString &caseId);
  void applySettings() override;
  void importSetFile();
  void exportSetFile();

signals:
  // The library changed: `setId` names the set whose contents changed, or
  // is empty when sets were added, removed or renamed.
  void setsChanged(const QString &setId);

private:
  AppSettings &settings_;
  AlgLibrary &library_;
  std::string setId_;
  std::string caseTreeSet_; // the set the case tree was last built for
  std::string caseId_;
  bool loading_ = false;
  // Every case of the current set, checked under its step settings.
  std::map<std::string, CaseAnalysis> analysis_;

  QListWidget *setList_;
  QMenu *setMenu_;
  QMenu *editMenu_;
  QTreeWidget *caseTree_;
  QGroupBox *caseBox_;
  QLineEdit *caseNameEdit_;
  QLabel *caseDiagram_;
  QLabel *caseSummary_;
  AlgListWidget *algList_;
  QLineEdit *newAlgEdit_;

  AlgSet *set();
  AlgCase *currentCase();
  const CaseAnalysis *analysisOf(const std::string &caseId) const;
  void changed(bool rebuildTree = false);
  void analyze();

  void fillSetMenu(QMenu *menu);
  void fillEditMenu(QMenu *menu);
  void refreshSetList();
  void loadSet();
  void buildCaseTree();
  void loadCase();
  void refreshAlgList();
  void storeAlgs();
  void showAlgMenu(const QPoint &pos);

  void newSet();
  void duplicateSet();
  void deleteSet();
  void restoreSet();
  void editProperties();
  void checkAlgs();
  void newGroup();
  void renameGroup();
  void deleteSelected();
  void newCaseFromAlg();
  void pasteAlgs();
  void addAlg();
  void removeAlg(int row);
  void makeMain(int row);
  void warnIfFailing(const std::string &alg);

  CubeLook look() const;
};

} // namespace vcube
