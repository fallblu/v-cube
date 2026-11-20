#pragma once

#include "io/app_data.hpp"

#include "app/app_settings.hpp"
#include "app/mode_table.hpp"

#include <QMainWindow>

#include <array>
#include <functional>
#include <map>
#include <optional>

class QAction;
class QButtonGroup;
class QLabel;
class QPropertyAnimation;
class QSplitter;
class QStackedWidget;
class QTimer;

namespace vcube {

class ModePage;
class LearnPage;
class LibraryPage;
class TodayPage;
class HelpPanel;

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  MainWindow(AppSettings &settings, QString dataDir, QWidget *parent = nullptr);
  ~MainWindow() override;

  // Brings the window to the front, e.g. when v-cube is started again.
  void raiseWindow();

  // What couldn't be read at start-up (data files, alg sets, key
  // bindings), each with what became of it.
  const QStringList &startupProblems() const { return startupProblems_; }
  // Lists them in one window, if there are any; called once the window is
  // shown.
  void showStartupProblems();

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
  QAction *dismissBannerAction_ = nullptr;
  QMenu *modeMenu_ = nullptr;
  QLabel *focusHint_;
  QTimer *focusHintTimer_;
  QPropertyAnimation *focusHintFade_ = nullptr;
  void placeFocusHint();
  void fadeFocusHint(bool in);
  HelpPanel *help_ = nullptr;
  QWidget *banner_ = nullptr;
  QLabel *bannerText_ = nullptr;
  std::array<ModePage *, AppMode::Count> pages_{};
  LearnPage *learnPage_;
  LibraryPage *libraryPage_;
  TodayPage *todayPage_;
  std::optional<AppMode::Id> mode_;
  // Data files that couldn't be written, with the reason, for the banner.
  std::map<QString, QString> failedWrites_;
  // The banner is hidden until the next failed write; failedWrites_ still
  // holds what wasn't saved, for the check when quitting.
  bool bannerDismissed_ = false;
  QStringList startupProblems_;

  QWidget *buildSidebar();
  QWidget *buildBanner();
  void buildMenus();
  ModePage *currentPage() const;
  // Switches mode unless a solve or inspection is running; false if blocked.
  bool setMode(AppMode::Id mode);
  // Hides the sidebar, menu bar, status bar and each page's controls,
  // leaving the central display.
  void setFocusMode(bool on);
  void setSidebarShown(bool on);
  // Shows `text` over the top of the window for `ms`, e.g. in focus mode.
  void showOverlay(const QString &text, int ms);
  void openDataFolder();
  // On the colour neutrality tab with `neutrality`.
  void openSettings(bool neutrality = false);
  void exportAllData();
  void openCommandPalette();
  void updateHelp();
  void showAbout();
  void showMessage(const QString &text);
  // Says so and returns true while a solve or inspection is running.
  bool refuseWhileBusy();
  // The result of a write to a data file (see setWriteReporter()).
  void writeReported(const QString &path, const QString &error);
  void updateBanner();
  void dismissBanner();
  // The due counts on the sidebar's Today and Learn buttons.
  void updateBadges();
  // The alg sets changed (or `setId`'s did): every page that shows them
  // reads them again, whichever page made the change.
  void librarySetsChanged(const QString &setId);
  void learnSet(const QString &setId);
  // Splitter positions, saved per mode.
  void restoreLayout();
  void saveLayout();
  // Calls `f` with each splitter of the pages but Learn's (which keeps its
  // own), the mode's settings id and the splitter's place on its page.
  void forEachSplitter(
      const std::function<void(QSplitter *, const QString &, int)> &f) const;
  // Keeps a restored window on a screen and within its size, e.g. after the
  // screen's scaling changed.
  void fitToScreen();
  // Applies the settings that belong to the window as a whole: the theme's
  // sidebar colours, text sizes and inspection sounds.
  void applyWindowSettings();
};

} // namespace vcube
