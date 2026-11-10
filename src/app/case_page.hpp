#pragma once

#include "app/app_settings.hpp"
#include "app/case_filter.hpp"
#include "app/colours.hpp"
#include "app/mode_page.hpp"
#include "io/library.hpp"
#include "train/case_logic.hpp"
#include "train/families.hpp"
#include "train/reduction.hpp"

#include <functional>
#include <map>

class QComboBox;
class QFormLayout;
class QHBoxLayout;
class QLabel;
class QMenu;
class QPushButton;
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
// app's colour neutrality; thumbnails are drawn in its main hold.
class CasePage : public ModePage {
  Q_OBJECT
public:
  // The library changed. A hidden page catches up when it is shown.
  void reloadSets();
  void applySettings() override;
  void activated() override;
  void deactivated() override;
  void setFocusMode(bool on) override;

signals:
  void editCaseRequested(const QString &setId, const QString &caseId);

protected:
  // `mode` names the page in settings keys ("learn", "drill").
  CasePage(AppSettings &settings, AlgLibrary &library, QString mode,
           QWidget *parent);
  // Builds the case list and selects the last set; call at the end of the
  // derived constructor.
  void init();

  // What the page knows about a case.
  struct CaseInfo {
    std::vector<std::size_t> passing; // indices of passing algs; [0] = main
    CubieCube state;     // the case as it is set up and drawn (oriented for
                         // the main alg)
    std::string mainAlg; // the main alg as done from `state`
    CaseAnalysis analysis;
  };

  AppSettings &settings_;
  AlgLibrary &library_;
  const QString mode_;
  std::string setId_;
  Rng rng_;
  std::map<std::string, CaseInfo> info_;
  // The set's mirror families, when it learns by family (else empty).
  CaseFamilies families_;
  // Steps left from each case by the set's algs (see stepsLeft()).
  std::map<std::string, int> steps_;

  QWidget *left_;
  QVBoxLayout *leftLayout_;
  QFormLayout *form_; // set selector; derived pages add rows
  QComboBox *setBox_;
  CaseFilter *filter_;
  QTreeWidget *tree_;
  QHBoxLayout *tickRow_; // tick all/none; derived pages add buttons
  QLabel *summary_;

  AlgSet *set();
  SetProgress &progress();
  const CaseProgress *progressOf(const std::string &caseId);
  // The set learns each case with its mirror images as one family.
  bool byFamily() const { return !families_.families.empty(); }
  // The case whose card is this case's: the first case of its family when
  // learning by family, else the case itself.
  std::string cardId(const std::string &caseId) const;
  // The progress holding the case's spaced-repetition card (its family's
  // when learning by family); drill statistics stay with each case.
  const CaseProgress *cardOf(const std::string &caseId);
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
  int tickedCount() const;
  // Cases without a passing alg.
  int excludedCount() const;

  QTreeWidgetItem *itemFor(const std::string &caseId) const;
  void selectInTree(const std::string &caseId);
  void updateRow(const std::string &caseId);
  void updateAllRows();

  // The yaw to look at a case's group from in 3D: the corner its diagrams
  // are drawn from, mirrored with the world, so that e.g. a back-left F2L
  // slot can be seen.
  double yawFor(const std::string &caseId, Mirror world);

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
  virtual void fillRow(QTreeWidgetItem *item, const std::string &caseId) = 0;
  virtual void updateSummary() = 0;
  // A set was selected: start practising it.
  virtual void startSet() = 0;
  // The same set was reloaded (e.g. algs edited in the Library).
  virtual void setReloaded() = 0;
  // Practise this case now (from the case list's menu).
  virtual void startCase(const std::string &caseId) = 0;
  virtual QString startCaseLabel(const std::string &caseId) = 0;
  virtual void extendCaseMenu(QMenu &menu, const std::string &caseId) {
    (void)menu;
    (void)caseId;
  }
  virtual void ticksChanged() {}
  // The colour neutrality changed in Settings.
  virtual void neutralityChanged() {}

private:
  QPushButton *tickAllButton_;
  QPushButton *tickNoneButton_;
  bool reloadPending_ = false;  // the library changed while hidden
  bool rebuildPending_ = false; // the look changed while hidden
  bool ticksPending_ = false;   // a tick change waits to be saved

  void populateSets();
  void selectSet(const std::string &id);
  void computeInfo();
  // Builds the case list, keeping its scroll position and collapsed groups.
  void buildTree();
  // Brings the ticks and rows up to date without drawing the thumbnails
  // again, e.g. after the other mode changed them.
  void syncRows();
  void saveTicks();
  // Saves the ticks once a batch of changes (e.g. "Tick all") is done.
  void ticksEdited();
  // Ticks or unticks every case shown.
  void tickShown(bool on);
  // "Tick all" becomes "Tick shown" while the list is filtered.
  void filterChanged();
  ColourNeutrality neutrality_; // as last applied
  void showCaseMenu(const QPoint &pos);
};

// The algs of a case that solve `state` (the case set up in its display
// orientation, possibly with AUFs and free pieces scrambled), with the
// AUFs they need, main alg first, as rich text under the case's name. For
// Timed and Untimed, which have no case list.
QString caseAlgsHtml(const AlgSet &set, const std::string &caseId,
                     const CubieCube &state);

// The algs of the case a cube shows in `world` as they are done from the
// cube (mirrored for a mirrored view), main alg first, under the case's
// name. A step that stops at another case names it.
QString viewAlgsHtml(const AlgSet &set, const CaseView &view,
                     const CubieCube &state, Mirror world);
// Every step from `state` to the end of a reduction set's step by the
// set's main algs, under the first case's name.
QString chainHtml(const AlgSet &set, const CubieCube &state, Mirror world);

// When a case is due: "now", "10 min", "3 h" or "4 d", or "today" for a
// review due before `dayEnd` (reviews are due all day).
QString dueText(const CaseProgress *p, std::int64_t now,
                std::int64_t dayEnd = 0);
// "1 case without a passing alg is skipped." and the plural.
QString skippedText(int cases);
// Seconds with two decimals, or "-".
QString secondsText(std::optional<double> ms);

} // namespace vcube
