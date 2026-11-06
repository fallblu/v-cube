#pragma once

#include "app/app_settings.hpp"
#include "io/library.hpp"
#include "io/move_counts.hpp"
#include "io/sessions.hpp"

#include <QMainWindow>

#include <array>

class QAbstractButton;
class QAction;
class QButtonGroup;
class QLabel;
class QStackedWidget;
class QTimer;

namespace vcube {

class ModePage;
class LearnPage;
class DrillPage;
class LibraryPage;
class TimerPage;
class AttemptPage;

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  MainWindow(AppSettings &settings, QString dataDir, QWidget *parent = nullptr);

  // The modes, in sidebar order (Ctrl+1 to Ctrl+6).
  enum Mode { Learn, Drill, Library, Timed, Untimed, MoveCount, ModeCount };

protected:
  bool eventFilter(QObject *obj, QEvent *event) override;
  void changeEvent(QEvent *e) override;
  void closeEvent(QCloseEvent *e) override;
  void contextMenuEvent(QContextMenuEvent *e) override;

private:
  AppSettings &settings_;
  QString dataDir_;
  AlgLibrary library_;
  SessionStore sessions_;
  MoveCountStore untimed_;
  MoveCountStore moveCounts_;
  QStackedWidget *stack_;
  QWidget *sidebar_;
  QButtonGroup *modeButtons_;
  QAction *fullScreenAction_ = nullptr;
  QAction *focusAction_ = nullptr;
  QLabel *focusHint_;
  QTimer *focusHintTimer_;
  bool focusMode_ = false;
  std::array<ModePage *, ModeCount> pages_{};
  LearnPage *learnPage_;
  DrillPage *drillPage_;
  LibraryPage *libraryPage_;
  TimerPage *timedPage_;
  AttemptPage *untimedPage_;
  AttemptPage *moveCountPage_;
  int mode_ = -1;
  struct KeySeen {
    const QEvent *event = nullptr;
    int type = 0;
    int key = 0;
    quint64 timestamp = 0;
    bool operator==(const KeySeen &) const = default;
  };
  KeySeen lastKey_;

  QWidget *buildSidebar();
  void buildMenus();
  ModePage *currentPage() const;
  // Switches mode unless a solve or inspection is running; false if blocked.
  bool setMode(int mode);
  void setFullScreen(bool on);
  // Hides the sidebar, menu bar, status bar and each page's controls,
  // leaving the central display.
  void setFocusMode(bool on);
  void showFocusHint();
  // Shows `text` over the top of the window for `ms`, e.g. in focus mode.
  void showOverlay(const QString &text, int ms);
  void openDataFolder();
  // On the colour neutrality tab with `neutrality`.
  void openSettings(bool neutrality = false);
  void importAlgSet();
  void exportAlgSet();
  void showControls();
  void showAbout();
  void showMessage(const QString &text);
};

} // namespace vcube
