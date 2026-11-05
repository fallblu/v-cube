#pragma once

#include "app/app_settings.hpp"
#include "app/case_filter.hpp"
#include "app/colours.hpp"
#include "app/mode_page.hpp"
#include "io/library.hpp"
#include "train/case_logic.hpp"

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
  };

  AppSettings &settings_;
  AlgLibrary &library_;
  const QString mode_;
  std::string setId_;
  Rng rng_;
  std::map<std::string, CaseInfo> info_;

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
  const CaseInfo *infoOf(const std::string &caseId) const;
  // Has at least one passing alg, so it can be offered.
  bool eligible(const std::string &caseId) const;
  // Ticked cases that can be offered, in list order.
  std::vector<std::string> tickedEligible() const;
  int tickedCount() const;
  // Cases without a passing alg.
  int excludedCount() const;

  QTreeWidgetItem *itemFor(const std::string &caseId) const;
  void selectInTree(const std::string &caseId);
  void updateRow(const std::string &caseId);
  void updateAllRows();

  // The hold for the next case, picked from the colour neutrality.
  Hold pickHold();
  // Whether the colour neutrality allows more than one hold.
  bool isNeutral() const { return !settings_.neutrality.isFixed(); }

  // The passing algs with the AUFs they need from `state`, main alg first
  // (without the rotations and U turns the display orientation took over),
  // as rich text under the case's name.
  // With `hold`, a line saying which hold the AUFs are for follows.
  QString algsHtml(const std::string &caseId, const CubieCube &state,
                   std::optional<Hold> hold = std::nullopt);
  QPixmap caseDiagram(const std::string &caseId, const CubieCube &state,
                      int size, const ColourMap &colours);
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
// Timer, Scramble and Virtual, which have no case list.
QString caseAlgsHtml(const AlgSet &set, const std::string &caseId,
                     const CubieCube &state);

// When a case is due: "now", "10 min", "3 h" or "4 d", or "today" for a
// review due before `dayEnd` (reviews are due all day).
QString dueText(const CaseProgress *p, std::int64_t now,
                std::int64_t dayEnd = 0);
// "1 case without a passing alg is skipped." and the plural.
QString skippedText(int cases);
// Seconds with two decimals, or "-".
QString secondsText(std::optional<double> ms);

} // namespace vcube
