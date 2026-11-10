#pragma once

#include "app/app_settings.hpp"
#include "io/library.hpp"
#include "io/move_counts.hpp"
#include "io/sessions.hpp"

#include <QMainWindow>

#include <array>
#include <map>

class QAbstractButton;
class QAction;
class QButtonGroup;
class QDockWidget;
class QLabel;
class QStackedWidget;
class QTextBrowser;
class QTimer;

namespace vcube {

class ModePage;
class LearnPage;
class DrillPage;
class LibraryPage;
class TimerPage;
class UntimedPage;
class MoveCountPage;
class InspectionPage;
class BlindPage;
class TodayPage;

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  MainWindow(AppSettings &settings, QString dataDir, QWidget *parent = nullptr);
  ~MainWindow() override;

  // The modes (Ctrl+1 to Ctrl+8 in this order, Today Ctrl+0). Today comes
  // first in the sidebar.
  enum Mode {
    Learn,
    Drill,
    Library,
    Timed,
    Untimed,
    MoveCount,
    Inspection,
    Blind,
    Today,
    ModeCount
  };

  // Brings the window to the front, e.g. when v-cube is started again.
  void raiseWindow();

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
  QAction *sidebarAction_ = nullptr;
  QLabel *focusHint_;
  QTimer *focusHintTimer_;
  QDockWidget *helpDock_ = nullptr;
  QTextBrowser *helpText_ = nullptr;
  QWidget *banner_ = nullptr;
  QLabel *bannerText_ = nullptr;
  QTimer *badgeTimer_;
  bool focusMode_ = false;
  std::array<ModePage *, ModeCount> pages_{};
  LearnPage *learnPage_;
  DrillPage *drillPage_;
  LibraryPage *libraryPage_;
  TimerPage *timedPage_;
  UntimedPage *untimedPage_;
  MoveCountPage *moveCountPage_;
  InspectionPage *inspectionPage_;
  BlindPage *blindPage_;
  TodayPage *todayPage_;
  int mode_ = -1;
  // Data files that couldn't be written, with the reason, for the banner.
  std::map<QString, QString> failedWrites_;
  struct KeySeen {
    const QEvent *event = nullptr;
    int type = 0;
    int key = 0;
    quint64 timestamp = 0;
    bool operator==(const KeySeen &) const = default;
  };
  KeySeen lastKey_;

  QWidget *buildSidebar();
  void styleSidebar();
  QWidget *buildBanner();
  void buildHelpPanel();
  void buildMenus();
  ModePage *currentPage() const;
  // Switches mode unless a solve or inspection is running; false if blocked.
  bool setMode(int mode);
  void setFullScreen(bool on);
  // Hides the sidebar, menu bar, status bar and each page's controls,
  // leaving the central display.
  void setFocusMode(bool on);
  void setSidebarShown(bool on);
  void showFocusHint();
  // Shows `text` over the top of the window for `ms`, e.g. in focus mode.
  void showOverlay(const QString &text, int ms);
  void openDataFolder();
  // On the colour neutrality tab with `neutrality`.
  void openSettings(bool neutrality = false);
  void importAlgSet();
  void exportAlgSet();
  void exportAllData();
  void openCommandPalette();
  void updateHelp();
  void showAbout();
  void showMessage(const QString &text);
  // The result of a write to a data file (see setWriteReporter()).
  void writeReported(const QString &path, const QString &error);
  void updateBanner();
  // The due counts on the sidebar's Today and Learn buttons.
  void updateBadges();
  void learnSet(const QString &setId);
  // Splitter positions, saved per mode.
  void restoreLayout();
  void saveLayout();
  // Keeps a restored window on a screen and within its size, e.g. after the
  // screen's scaling changed.
  void fitToScreen();
  // Applies the settings that belong to the window as a whole: the theme's
  // sidebar colours, text sizes and inspection sounds.
  void applyWindowSettings();
};

} // namespace vcube
