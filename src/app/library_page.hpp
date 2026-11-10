#pragma once

#include "app/app_settings.hpp"
#include "app/cube_painter.hpp"
#include "app/mode_page.hpp"
#include "io/library.hpp"
#include "train/case_logic.hpp"

#include <QList>
#include <QTreeWidget>
#include <QVariantMap>

#include <map>
#include <optional>
#include <string>
#include <vector>

class QAction;
class QFormLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QTreeWidgetItem;
class QUndoStack;

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

// The case tree of a set: groups and their cases, several of which can be
// selected and dragged to another place or group. The page moves them.
class CaseTreeWidget : public QTreeWidget {
  Q_OBJECT
public:
  explicit CaseTreeWidget(QWidget *parent = nullptr);

  // Where on an item a drop lands.
  enum Drop { DropOn, DropAbove, DropBelow, DropElsewhere };

signals:
  // The selected items were dropped on `target` (nullptr: below the last
  // item) at `position`, a Drop.
  void dropped(QTreeWidgetItem *target, int position);

protected:
  void dropEvent(QDropEvent *event) override;
};

// Browse and edit alg sets: the set list, each set's case tree, and the
// selected case with its algs. Set properties and step settings live in a
// dialog; set and case actions in the Set and Cases menus of the menu bar
// (shown while the page is) and in context menus. Every change is saved
// at once and can be undone.
class CaseFilter;

class LibraryPage : public ModePage {
  Q_OBJECT
public:
  LibraryPage(AppSettings &settings, AlgLibrary &library,
              QWidget *parent = nullptr);

  void selectCase(const QString &setId, const QString &caseId);
  void applySettings() override;
  void importSetFile();
  void exportSetFile();
  void activated() override;
  void deactivated() override;

  // The Set and Cases menus for the menu bar, and Undo and Redo for the
  // Edit menu; they are shown only while the page is.
  QList<QMenu *> menus() const;
  QList<QAction *> editActions() const;

  // One undoable change of a set: its state before and after (nullopt
  // when it doesn't exist, i.e. it was added or deleted).
  struct Edit {
    std::string setId;
    std::optional<AlgSet> before;
    std::optional<AlgSet> after;
    SetProgress forgotten;  // progress the change dropped, put back by undo
    SetProgress progress;   // a removed set's progress
    QVariantMap settings;   // and its per-set settings
    std::string caseBefore; // the selected case before and after
    std::string caseAfter;
  };
  void applyEdit(Edit &edit, bool undoing, const QString &text);

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
  bool active_ = false;
  // Every case of each set, checked under its step settings; filled when
  // first needed and kept up to date edit by edit.
  std::map<std::string, std::map<std::string, CaseAnalysis>> analysis_;
  QUndoStack *undo_;
  // Pasted algs that weren't filed, offered again by the next paste.
  std::map<std::string, QString> pasteLeftOver_;

  QListWidget *setList_;
  CaseFilter *caseFilter_;
  CaseTreeWidget *caseTree_ = nullptr;
  QGroupBox *caseBox_;
  QLineEdit *caseNameEdit_;
  QLabel *caseDiagram_;
  QLabel *caseGroup_;
  QLabel *caseAlgCount_;
  QLabel *caseProgress_;
  QLabel *caseSummary_;
  AlgListWidget *algList_ = nullptr;
  QLineEdit *newAlgEdit_;

  QMenu *setMenu_;
  QMenu *caseMenu_;
  QMenu *restoreSetMenu_;
  QMenu *moveToMenu_;
  QAction *undoAction_;
  QAction *redoAction_;
  QAction *editSeparator_;
  QAction *newSetAction_;
  QAction *duplicateSetAction_;
  QAction *deleteSetAction_;
  QAction *propertiesAction_;
  QAction *checkAction_;
  QAction *importAction_;
  QAction *exportAction_;
  QAction *restoreCasesAction_;
  QAction *newGroupAction_;
  QAction *newCaseAction_;
  QAction *pasteAction_;
  QAction *renameAction_;
  QAction *deleteAction_;
  QAction *moveUpAction_;
  QAction *moveDownAction_;

  AlgSet *set();
  AlgCase *currentCase();
  const std::map<std::string, CaseAnalysis> &analysisFor(const AlgSet &s);
  const CaseAnalysis *analysisOf(const std::string &caseId);
  int failingIn(const AlgSet &s);

  // An edit of the current set: take begin() before changing it and
  // commit() after. commit() saves it and makes it undoable (or puts the
  // set back if it can't be saved); `changedCases` names the only cases
  // whose algs changed, or is nullopt when anything else may have.
  struct Pending {
    AlgSet before;
    std::string caseId;
  };
  std::optional<Pending> begin();
  bool
  commit(const QString &text, Pending pending,
         std::optional<std::vector<std::string>> changedCases = std::nullopt,
         const std::vector<std::string> &forget = {});
  // Refreshes the page after a set changed.
  void refreshAfter(const std::string &setId,
                    const std::optional<std::vector<std::string>> &cases);
  void pushSetEdit(const QString &text, Edit edit);

  void buildActions();
  void fillContextMenu(QMenu *menu, bool forSets);
  void updateActions();
  void fillRestoreSetMenu();
  void fillMoveToMenu();
  void refreshSetList();
  void loadSet();
  void buildCaseTree();
  void fillCaseItem(QTreeWidgetItem *item, const AlgSet &s, const CaseGroup &g,
                    const AlgCase &c);
  void fillGroupItem(QTreeWidgetItem *item, const AlgSet &s,
                     const CaseGroup &g);
  void loadCase();
  void refreshAlgList();
  void storeAlgs(const QString &text, std::optional<int> edited = {});
  void showAlgMenu(const QPoint &pos);

  // The selected cases in set order, and the selected groups (by index)
  // when no case is selected.
  std::vector<std::string> selectedCases() const;
  std::vector<std::size_t> selectedGroups() const;
  // The group of the current item, if any.
  std::optional<std::size_t> currentGroup() const;
  void selectItems(const std::vector<std::string> &cases,
                   const std::vector<std::size_t> &groups);

  void newSet();
  void duplicateSet();
  void deleteSet();
  void restoreSet();
  void restoreBuiltinSet(const std::string &id);
  void editProperties();
  void checkAlgs();
  void newGroup();
  void renameSelected();
  void deleteSelected();
  void moveSelected(int direction);
  void moveCasesTo(std::size_t group);
  void dropOnTree(QTreeWidgetItem *target, int position);
  void newCaseFromAlg();
  void pasteAlgs();
  void addAlg();
  void removeAlg(int row);
  void moveAlg(int row, int direction);
  void makeMain(int row);
  void warnIfFailing(std::size_t index);

  CubeLook look() const;
};

} // namespace vcube
