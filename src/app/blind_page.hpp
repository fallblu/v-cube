#pragma once

#include "app/app_settings.hpp"
#include "app/bld_scheme_dialog.hpp"
#include "app/blind_drill.hpp"
#include "app/mode_page.hpp"
#include "app/scramble_source.hpp"
#include "io/blind_store.hpp"
#include "io/library.hpp"

#include <array>

class QComboBox;
class QLabel;
class QPushButton;
class QTabBar;

namespace vcube {

class CubeView;
class NetPreview;
class TimeDisplay;
class TrendChart;

// Blind mode: blindfolded solving with Old Pochmann, in four drills, each
// on a tab of its own (the tabs stay in focus mode too).
//
// - Letter quiz: a sticker of the solved cube is ringed and its letter
//   typed, or a letter is shown and its sticker clicked on the net.
// - Memo tracing: the memo of a scrambled cube is typed and checked by
//   doing it, so any valid cycle break counts.
// - Setup moves: the setup for a letter is done on the virtual cube (the
//   ringed sticker must reach the swap spot without moving the buffer or
//   the swap alg's other pieces) or typed.
// - Blind solves: memo with the cube shown, then execution with it hidden
//   (virtual cube), or a timer with a memo split (physical cube), in
//   sessions.
//
// The letters belong to the stickers as the cube is held for blind (the
// blind hold, white top and green front unless changed), and the cube is
// shown in that hold. The quiz and setup drills weigh the next letter
// towards the slow and missed ones. Their clocks, and a trace's, stop
// while the page or the window is away.
//
// Each drill is a BlindDrill with its own left column, options and input
// row; the page holds the tabs, the shared data and middle, routes the
// keys and events to the drill picked, and edits the lettering.
class BlindPage : public ModePage {
  Q_OBJECT
public:
  // Sessions and drill statistics are kept in `dataDir`/blind.json.
  BlindPage(AppSettings &settings, const QString &dataDir,
            QWidget *parent = nullptr);

  bool handleKey(QKeyEvent *e, bool press) override;
  void applySettings() override;
  bool isBusy() const override;
  void activated() override;
  void deactivated() override;
  void windowDeactivated() override;
  void setFocusMode(bool on) override;

protected:
  void changeEvent(QEvent *event) override;

private:
  enum class Drill { Quiz, Tracing, Setups, Solves };

  AppSettings &settings_;
  BlindStore store_;
  BlindScheme scheme_;
  AlgLibrary noSets_; // the scramble feed needs one; WCA scrambles only
  ScrambleFeed feed_;
  Rng rng_;
  BlindContext context_;
  std::array<BlindDrill *, 4> drills_{};
  Drill drill_ = Drill::Quiz;

  QTabBar *drillTabs_;
  QWidget *leftPanel_;
  QWidget *optionsRow_;
  QWidget *viewPicker_;
  QComboBox *viewBox_;
  QPushButton *schemeButton_;
  QLabel *scrambleLabel_;
  NetPreview *net_;
  CubeView *cubeView_;
  TrendChart *chart_;

  BlindDrill *current() const;
  void save();
  // Shows the drill's widgets and starts it afresh.
  void selectDrill(Drill d);
  // The cube view's mode: the drill's (see BlindDrill::view()), else the
  // view picked.
  void applyView();
  // The tabs, the lettering and the view are locked during a solve.
  void updateControls();
  void editScheme();
};

} // namespace vcube
