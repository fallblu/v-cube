#pragma once

#include "app/app_settings.hpp"
#include "app/mode_page.hpp"
#include "app/scramble_source.hpp"
#include "app/widgets.hpp"
#include "io/library.hpp"
#include "io/move_counts.hpp"

class QLabel;
class QMenu;
class QPushButton;
class QTableWidget;
class QToolButton;

namespace vcube {

// Untimed move-efficiency practice for one step at a time (EO, EO Cross,
// ZZ F2L, ...) on a physical cube: for each scramble you type the number of
// moves your solution to the step took. Sessions, statistics and a trend
// chart work as in Timer mode, over move counts instead of times. Only a
// few scramble types are offered (see moveCountScrambleTypes()).
class MoveCountPage : public ModePage {
  Q_OBJECT
public:
  MoveCountPage(AppSettings &settings, MoveCountStore &store,
                AlgLibrary &library, QWidget *parent = nullptr);

  bool handleKey(QKeyEvent *e, bool press) override;
  void applySettings() override;
  void setFocusMode(bool on) override;

private:
  AppSettings &settings_;
  MoveCountStore &store_;
  ScrambleFeed feed_;
  QString entry_; // the digits typed for the current scramble

  QWidget *leftPanel_;
  QWidget *typeRow_;
  QToolButton *sessionButton_;
  QMenu *sessionMenu_;
  ScrambleTypeBox *typeBox_;
  QLabel *scrambleLabel_;
  NetPreview *net_;
  QLabel *stepLabel_;
  TimeDisplay *display_;
  QLabel *hintLabel_;
  QLabel *lastLabel_;
  QTableWidget *table_;
  QLabel *stats_;
  TrendChart *chart_;
  QPushButton *recordButton_;
  QPushButton *dnfButton_;
  QPushButton *deleteButton_;

  MoveCountSession &session() { return store_.current(); }
  void showScramble();
  void showEntry();
  void setEntry(const QString &digits);
  // Records the current scramble with the typed count (or as DNF) and moves
  // on to the next scramble.
  void record(bool dnf);
  void skip();
  void refreshSessionMenu();
  void selectSession(std::size_t index);
  void refreshAttempts();
  void deleteAttempt(int index);
  void showAttemptMenu(const QPoint &pos);
  void addSession();
  void renameSession();
  void deleteSession();
  void save();
};

} // namespace vcube
