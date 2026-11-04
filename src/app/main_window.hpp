#pragma once

#include "app/app_settings.hpp"
#include "io/library.hpp"
#include "io/sessions.hpp"

#include <QMainWindow>

class QTabWidget;

namespace vcube {

class TimerPage;
class TrainerPage;
class LibraryPage;

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  MainWindow(AppSettings &settings, QString dataDir, QWidget *parent = nullptr);

protected:
  bool eventFilter(QObject *obj, QEvent *event) override;
  void closeEvent(QCloseEvent *e) override;

private:
  AppSettings &settings_;
  QString dataDir_;
  AlgLibrary library_;
  SessionStore sessions_;
  QTabWidget *tabs_;
  TimerPage *timerPage_;
  TrainerPage *trainerPage_ = nullptr;
  LibraryPage *libraryPage_ = nullptr;
  struct KeySeen {
    const QEvent *event = nullptr;
    int type = 0;
    int key = 0;
    quint64 timestamp = 0;
    bool operator==(const KeySeen &) const = default;
  };
  KeySeen lastKey_;

  void buildMenus();
  void openDataFolder();
  void openSettings();
  void importCstimer();
  void exportCstimer();
  void importAlgSet();
  void exportAlgSet();
  void showControls();
  void showAbout();
  void showMessage(const QString &text);
};

} // namespace vcube
