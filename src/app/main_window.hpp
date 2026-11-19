#pragma once

#include "app/app_data.hpp"
#include "app/app_settings.hpp"
#include "app/mode_table.hpp"

#include <QMainWindow>

#include <array>
#include <map>
#include <optional>

class QAbstractButton;
class QAction;
class QButtonGroup;
class QDockWidget;
class QLabel;
class QPropertyAnimation;
class QStackedWidget;
class QTimer;

namespace vcube {

class ModePage;
class LearnPage;
class DrillPage;
class LibraryPage;
class TimerPage;
class UntimedPage;
class F2LPage;
class TslePage;
class MoveCountPage;
class EoCrossPage;
class AlgLabPage;
class TodayPage;
class HelpPanel;

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  MainWindow(AppSettings &settings, QString dataDir, QWidget *parent = nullptr);
  ~MainWindow() override;

  // Brings the window to the front, e.g. when v-cube is started again.
  void raiseWindow();

protected:
  void changeEvent(QEvent *e) override;
  void closeEvent(QCloseEvent *e) override;
  void contextMenuEvent(QContextMenuEvent *e) override;
  void resizeEvent(QResizeEvent *e) override;

private:
  AppSettings &settings_;
  AppData data_;
  QStackedWidget *stack_;
  QWidget *sidebar_;
  QButtonGroup *modeButtons_;
  QAction *fullScreenAction_ = nullptr;
  QAction *focusAction_ = nullptr;
  QAction *sidebarAction_ = nullptr;
  QAction *pageUndoAction_ = nullptr;
  QAction *sidePanelAction_ = nullptr;
  QMenu *modeMenu_ = nullptr;
  QLabel *focusHint_;
  QTimer *focusHintTimer_;
  QPropertyAnimation *focusHintFade_ = nullptr;
  void placeFocusHint();
  void fadeFocusHint(bool in);
  HelpPanel *help_ = nullptr;
  QWidget *banner_ = nullptr;
  QLabel *bannerText_ = nullptr;
  QTimer *badgeTimer_;
  bool focusMode_ = false;
  std::array<ModePage *, AppMode::Count> pages_{};
  LearnPage *learnPage_;
  DrillPage *drillPage_;
  LibraryPage *libraryPage_;
  TimerPage *timedPage_;
  UntimedPage *untimedPage_;
  MoveCountPage *moveCountPage_;
  F2LPage *f2lPage_;
  TslePage *tslePage_;
  EoCrossPage *eoCrossPage_;
  AlgLabPage *algLabPage_;
  TodayPage *todayPage_;
  std::optional<AppMode::Id> mode_;
  // Data files that couldn't be written, with the reason, for the banner.
  std::map<QString, QString> failedWrites_;

  QWidget *buildSidebar();
  void styleSidebar();
  QWidget *buildBanner();
  void buildMenus();
  ModePage *currentPage() const;
  // Switches mode unless a solve or inspection is running; false if blocked.
  bool setMode(AppMode::Id mode);
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
  // Tells the pages whose settings changed.
  void settingsChanged(SettingsParts parts);
  void showAbout();
  void showMessage(const QString &text);
  // The result of a write to a data file (see setWriteReporter()).
  void writeReported(const QString &path, const QString &error);
  void updateBanner();
  // The due counts on the sidebar's Today and Learn buttons.
  void updateBadges();
  // The alg sets changed (or `setId`'s did): every page that shows them
  // reads them again, whichever page made the change.
  void librarySetsChanged(const QString &setId);
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
