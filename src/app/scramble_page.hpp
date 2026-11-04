#pragma once

#include "app/app_settings.hpp"
#include "app/mode_page.hpp"
#include "app/scramble_source.hpp"

#include <deque>

class QLabel;

namespace vcube {

// Scrambles for casual solving on a physical cube: no timer, no sessions.
// Space or the right arrow shows the next scramble; Backspace or the left
// arrow goes back through this run's history.
class ScramblePage : public ModePage {
  Q_OBJECT
public:
  ScramblePage(AppSettings &settings, AlgLibrary &library,
               QWidget *parent = nullptr);

  bool handleKey(QKeyEvent *e, bool press) override;
  void applySettings() override;
  void refreshScrambleTypes(); // alg sets changed

  static constexpr std::size_t kHistory = 100;

private:
  AppSettings &settings_;
  ScrambleFeed feed_;
  std::deque<QString> history_;
  std::size_t index_ = 0; // shown entry of history_
  bool wantNext_ = false; // show the feed's scramble when it is ready

  ScrambleTypeBox *typeBox_;
  QLabel *scrambleLabel_;
  QLabel *positionLabel_;
  NetPreview *net_;

  void next();
  void previous();
  void feedChanged();
  void show();
};

} // namespace vcube
