#pragma once

#include "app/app_settings.hpp"
#include "app/cube_view.hpp"
#include "app/mode_page.hpp"
#include "app/scramble_source.hpp"

class QLabel;
class QLineEdit;

namespace vcube {

// A virtual-cube sandbox: turn the cube freely with the keymap, scramble
// it with any scramble type, or type an alg to apply. No timer, statistics
// or sessions.
class VirtualPage : public ModePage {
  Q_OBJECT
public:
  VirtualPage(AppSettings &settings, AlgLibrary &library,
              QWidget *parent = nullptr);

  bool handleKey(QKeyEvent *e, bool press) override;
  void applySettings() override;
  void deactivated() override;
  void setFocusMode(bool on) override;
  void refreshScrambleTypes(); // alg sets changed

protected:
  bool eventFilter(QObject *obj, QEvent *event) override;

private:
  AppSettings &settings_;
  ScrambleFeed feed_;
  ScrambleAlgs *algs_;
  FaceletCube cube_;
  bool wantScramble_ = false; // scramble once the feed has one ready
  bool scrambled_ = false;

  QWidget *topRow_;
  QWidget *algPanel_;
  CubeView *cubeView_;
  ScrambleTypeBox *typeBox_;
  QLabel *scrambleLabel_;
  QLabel *statusLabel_;
  QLineEdit *algEdit_;
  QLabel *algError_;

  void reset();
  void scramble();
  void feedChanged();
  void applyAlg();
  void turn(Move m);
  void returnFocus();
};

} // namespace vcube
