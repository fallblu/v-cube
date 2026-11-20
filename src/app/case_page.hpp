#pragma once

#include "app/alg_html.hpp"
#include "app/app_settings.hpp"
#include "app/case_filter.hpp"
#include "app/case_list_panel.hpp"
#include "app/colours.hpp"
#include "app/mode_page.hpp"
#include "io/library.hpp"
#include "train/case_logic.hpp"
#include "train/families.hpp"
#include "train/reduction.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QPixmap>
#include <QStringList>

class QComboBox;
class QLabel;
class QMenu;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

namespace vcube {

// What Learn and Drill share: the alg set selector, the case list with
// thumbnails and ticks, and which cases can be offered (those with a
// passing alg). The ticks are stored once per set, so both modes see the
// same choices. The list can be searched and filtered by status; "Tick
// all" and "Tick none" then tick only the cases shown, and double-clicking
// a case practises it now. Cases are set up for a hold picked from the
// app's colour neutrality; thumbnails are drawn in its main hold. Several
// cases (or a group's row) can be selected for the right-click actions,
// and the columns after "Case" can be hidden from the header's menu.
class CasePage : public ModePage {
  Q_OBJECT
public:
  // The library changed. A hidden page catches up when it is shown.
  void reloadSets();
  // Picks a set (or saved::kAcrossSetsId) as the set drop-down does; one the
  // drop-down doesn't offer is left alone.
  void pickSet(const QString &setId);
  // Shows a set and selects one of its cases in the list.
  void showCase(const QString &setId, const QString &caseId);
  void applySettings() override;
  QWidget *sidePanel() const override { return left_; }
  bool hasModeMenu() const override { return true; }
  void fillModeMenu(QMenu &menu) override;
  void activated() override;
  void deactivated() override;

  // Where a row keeps a column's value to sort by (see sortableColumns()).
  static constexpr int kSortRole = Qt::UserRole + 30;

signals:
  void editCaseRequested(const QString &setId, const QString &caseId);
  // An empty state's way out: Library's Paste algs for the set, or the
  // set in Learn.
  void pasteAlgsRequested(const QString &setId);
  void learnSetRequested(const QString &setId);

protected:
  void focusModeChanged() override;
  // `mode` names the page in settings keys ("learn", "drill").
  CasePage(AppSettings &settings, AlgLibrary &library, QString mode,
           QWidget *parent);
  // Builds the case list and selects the last set; call at the end of the
  // derived constructor.
  void init();

  // What the page knows about a case: its algs checked, and `display`, the
  // case as it is set up and drawn (oriented for the main alg).
  using CaseInfo = CaseAnalysis;

  AppSettings &settings_;
  AlgLibrary &library_;
  const QString mode_;
  std::string setId_;
  Rng rng_;

  // The case list on the left; the pointers below are its parts.
  CaseListPanel *panel_;
  QWidget *left_;
  QVBoxLayout *leftLayout_;
  QComboBox *setBox_;
  CaseFilter *filter_;
  QTreeWidget *tree_;
  QLabel *summary_;

  AlgSet *set();
  const AlgSet *set() const;
  SetProgress &progress();
  const SetProgress &progress() const;
  const CaseProgress *progressOf(const std::string &caseId) const;
  // The set learns each case with its mirror images as one family.
  bool byFamily() const { return !families().families.empty(); }
  // The set's mirror families, when it learns by family (else none).
  const CaseFamilies &families() const { return info_->families; }
  // The case whose card is this case's: the first case of its family when
  // learning by family, else the case itself.
  std::string cardId(const std::string &caseId) const;
  // The progress holding the case's spaced-repetition card (its family's
  // when learning by family); drill statistics stay with each case.
  const CaseProgress *cardOf(const std::string &caseId) const;
  const CaseInfo *infoOf(const std::string &caseId) const;
  // Has at least one passing alg, so it can be offered. In a reduction
  // set its steps must also lead to the end.
  bool eligible(const std::string &caseId) const;
  // Steps left from the case, or 0 if its algs don't reach the end.
  int stepsFrom(const std::string &caseId) const;
  // The world to set a case up in: as written or one of the set's mirrors.
  // A case that doesn't depend on the slot is always set up as written.
  Mirror pickWorld(const std::string &caseId);
  // Ticked cases that can be offered, in list order.
  std::vector<std::string> tickedEligible() const;
  // Cases the list shows (search and status filter) that can be offered,
  // ticked or not, in list order.
  std::vector<std::string> shownEligible() const;
  // A group's cases that can be offered, ticked or not, in list order.
  std::vector<std::string> groupEligible(const std::string &groupName) const;
  int tickedCount() const;
  // Cases without a passing alg.
  int excludedCount() const;

  QTreeWidgetItem *itemFor(const std::string &caseId) const;
  // Selects and shows the case's row: only once the case is revealed, so
  // that the list doesn't name a case that is to be recognised.
  void selectInTree(const std::string &caseId);
  void clearCaseSelection();
  void updateRow(const std::string &caseId);
  void updateAllRows();

  // Reviewing the due cases of every set: the set drop-down shows "All
  // sets" and the list shows the set of the case being practised.
  bool acrossSets() const { return acrossSets_; }
  // Switches to another set, or to the reviews across every set, as the
  // set selector does.
  void chooseSet(const std::string &id);
  void chooseAcrossSets();
  virtual bool offersAcrossSets() const { return false; }
  // While reviewing across sets: shows another set's cases (without
  // starting it as selecting it would).
  void showSet(const std::string &id);
  // The set drop-down's text for a set, and for "All sets".
  virtual QString setItemText(const AlgSet &set) const;
  virtual QString acrossItemText() const { return tr("All sets: due reviews"); }
  void refreshSetLabels();

  // The yaw to look at a case's group from in 3D: the corner its diagrams
  // are drawn from (so that e.g. a back-left F2L slot can be seen), moved
  // to the other side in a left-right mirror world. A front-back mirror
  // keeps it, as a back slot is done from the front.
  double yawFor(const std::string &caseId, Mirror world) const;

  // The hold for the next case, picked from the colour neutrality.
  Hold pickHold();

  // The passing algs with the AUFs they need from `state`, main alg first
  // (without the rotations and U turns the display orientation took over),
  // as rich text under the case's name.
  // In a reduction set: every step from `state` to the end. `world` is the
  // mirror world the state was set up in.
  QString algsHtml(const std::string &caseId, const CubieCube &state,
                   Mirror world = {});
  QPixmap caseDiagram(const std::string &caseId, const CubieCube &state,
                      int size, const ColourMap &colours, Mirror world = {});
  void saveProgress();

  // A status the case list can be filtered by.
  struct CaseStatusFilter {
    QString name;
    std::function<bool(const std::string &caseId)> test;
  };
  // The page's own statuses, between "Not ticked" and "No alg".
  virtual std::vector<CaseStatusFilter> statusFilters() { return {}; }

  // The page's case list columns after "Case", and how to fill them.
  virtual QStringList columns() const = 0;
  // Columns hidden until the header's menu shows them.
  virtual QStringList defaultHiddenColumns() const { return {}; }
  virtual void fillRow(QTreeWidgetItem *item, const std::string &caseId) = 0;
  // A group's row, from its cases (e.g. their statistics taken together).
  virtual void fillGroupRow(QTreeWidgetItem *,
                            const std::vector<std::string> &) {}
  // The list can be sorted by the columns after "Case": a click on a
  // column's header sorts by it (largest first, then smallest first, then
  // in list order again, as a click on "Case" does). Cases sort within
  // their groups, and groups by their own rows. fillRow() and
  // fillGroupRow() give each column's value in kSortRole (a number; none
  // sorts as -1).
  virtual bool sortableColumns() const { return false; }
  virtual void updateSummary() = 0;
  // A set was selected: start practising it.
  virtual void startSet() = 0;
  // The same set was reloaded (e.g. algs edited in the Library).
  virtual void setReloaded() = 0;
  // Practise this case now (from the case list's menu).
  virtual void startCase(const std::string &caseId) = 0;
  virtual QString startCaseLabel(const std::string &caseId) const = 0;
  // More actions for the cases right-clicked: the selected cases, or a
  // group's cases when its row was right-clicked.
  virtual void extendCaseMenu(QMenu &, const std::vector<std::string> &) {}
  // More of the mode's menu after the case list's own (e.g. Drill's
  // Reset tally).
  virtual void extendModeMenu(QMenu &) {}
  virtual void ticksChanged() {}
  // The colour neutrality changed in Settings.
  virtual void neutralityChanged() {}

private:
  bool reloadPending_ = false;  // the library changed while hidden
  bool rebuildPending_ = false; // the look changed while hidden
  bool ticksPending_ = false;   // a tick change waits to be saved
  bool acrossSets_ = false;

  // What computeInfo() works out for a set: its cases, its families and
  // the steps left from each case by the set's algs (see stepsLeft()).
  struct SetInfo {
    std::map<std::string, CaseInfo> cases;
    CaseFamilies families;
    std::map<std::string, int> steps;
  };
  std::shared_ptr<const SetInfo> info_ = std::make_shared<SetInfo>();
  // What was worked out for each set, kept until the library changes.
  std::map<std::string, std::shared_ptr<const SetInfo>> infoCache_;

  void populateSets();
  // The cases that can be offered of the rows that pass `test`, in list
  // order.
  std::vector<std::string>
  eligibleRows(const std::function<bool(const QTreeWidgetItem *)> &test) const;
  // Starts the case chosen in the list, giving the keys back first.
  void startCaseFromList(const std::string &id);
  // Works the set's cases out, or reuses what was worked out for it since
  // the library last changed. A family's card kept under one of its cases
  // moves to the family (see adoptFamilyCards()) unless it is Cached and
  // reused, as reviews across sets go from set to set.
  enum class Info { Fresh, Cached };
  void computeInfo(Info info = Info::Fresh);
  // Builds the case list, keeping its scroll position and collapsed groups.
  void buildTree();
  // Brings the ticks and rows up to date without drawing the thumbnails
  // again, e.g. after the other mode changed them.
  void syncRows();
  void saveTicks();
  // Saves the ticks once a batch of changes (e.g. "Tick all") is done.
  void ticksEdited();
  ColourNeutrality neutrality_; // as last applied
  void fillGroupRows();
  void showCaseMenu(const QPoint &pos);
  // Fills `menu` with what can be done with a row of the list (a case, a
  // group, or the selected rows it is one of); false if nothing.
  bool fillCaseMenu(QMenu &menu, QTreeWidgetItem *item);
};

} // namespace vcube
