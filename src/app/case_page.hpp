#pragma once

#include "app/app_settings.hpp"
#include "app/colours.hpp"
#include "app/mode_page.hpp"
#include "io/library.hpp"
#include "train/case_logic.hpp"

#include <map>

class QComboBox;
class QFormLayout;
class QHBoxLayout;
class QLabel;
class QMenu;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

namespace vcube {

// What Learn and Drill share: the alg set selector, the case list with
// thumbnails and ticks, which cases can be offered (those with a passing
// alg), and the top/front colour options. The ticks and the colour options
// are stored once per set, so both modes (and the Library's diagrams) see
// the same choices.
class CasePage : public ModePage {
  Q_OBJECT
public:
  void reloadSets(); // the library changed
  void applySettings() override;
  void activated() override;
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
  QTreeWidget *tree_;
  QHBoxLayout *tickRow_; // tick all/none; derived pages add buttons
  QLabel *summary_;
  QWidget *orientation_; // top/front colour options, placed by the page
  QComboBox *topBox_;
  QComboBox *frontBox_;
  QComboBox *neutralBox_;

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

  ColourMap baseColours() const;
  // The colours for the next case, following the neutrality option.
  ColourMap caseColours();

  // The passing algs with the AUFs they need from `state`, main alg first
  // (without the rotations and U turns the display orientation took over),
  // as rich text under the case's name.
  QString algsHtml(const std::string &caseId, const CubieCube &state);
  QPixmap caseDiagram(const std::string &caseId, const CubieCube &state,
                      int size, const ColourMap &colours);
  void saveProgress();

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
  virtual QString startCaseLabel() const = 0;
  virtual void extendCaseMenu(QMenu &menu, const std::string &caseId) {
    (void)menu;
    (void)caseId;
  }
  virtual void ticksChanged() {}
  virtual void orientationChanged() {}

private:
  void populateSets();
  void selectSet(const std::string &id);
  void computeInfo();
  void buildTree();
  void saveTicks();
  void loadOrientation();
  void saveOrientation();
  void showCaseMenu(const QPoint &pos);
};

// Text of a duration such as "now", "10 min", "3 h" or "4 d".
QString dueText(const CaseProgress *p, std::int64_t now);
// "1 case without a passing alg is skipped." and the plural.
QString skippedText(int cases);
// Seconds with two decimals, or "-".
QString secondsText(std::optional<double> ms);

} // namespace vcube
