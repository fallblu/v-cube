#include "app/app_settings.hpp"
#include "app/main_window.hpp"
#include "app/platform.hpp"
#include "app/single_instance.hpp"
#include "app/text_style.hpp"
#include "cube/solver.hpp"
#include "io/data_file.hpp"

#include "vcube_version.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>

int main(int argc, char **argv) {
  vcube::selectQtPlatform();
  QApplication app(argc, argv);
  QApplication::setOrganizationName("v-cube");
  QApplication::setApplicationName("v-cube");
  QApplication::setApplicationVersion(VCUBE_VERSION);
  // Matches the installed v-cube.desktop, so that Wayland shells show its
  // icon and name for the window.
  QGuiApplication::setDesktopFileName("v-cube");
  QIcon icon(":/icons/v-cube.svg");
  icon.addFile(":/icons/v-cube-256.png"); // without the SVG plugin
  QApplication::setWindowIcon(icon);

  // A separate profile (a dev build, say): data, settings and so the
  // single-instance lock all in one folder, away from the real ones.
  QCommandLineParser parser;
  parser.setApplicationDescription(QObject::tr(
      "A Rubik's cube trainer and timer.\n\nThe data folder can also be set "
      "with the VCUBE_HOME environment variable."));
  parser.addHelpOption();
  parser.addVersionOption();
  QCommandLineOption dataDirOption(
      "data-dir",
      QObject::tr("Keep the data files in <folder> and the settings in its "
                  "settings/ subfolder, instead of the usual places."),
      QObject::tr("folder"));
  parser.addOption(dataDirOption);
  parser.process(app);
  QString home = parser.isSet(dataDirOption)
                     ? parser.value(dataDirOption)
                     : qEnvironmentVariable("VCUBE_HOME");
  QString dataDir;
  if (!home.isEmpty()) {
    // A relative folder is taken from the current one, never the usual.
    dataDir = QDir::cleanPath(QDir::current().absoluteFilePath(home));
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       QDir(dataDir).filePath("settings"));
  } else {
    dataDir = QDir(QStandardPaths::writableLocation(
                       QStandardPaths::GenericDataLocation))
                  .filePath("v-cube");
  }
  if (!QDir().mkpath(dataDir)) {
    QMessageBox::critical(nullptr, "v-cube",
                          QObject::tr("The data folder %1 can't be made.")
                              .arg(QDir::toNativeSeparators(dataDir)));
    return 1;
  }
  vcube::initSettingsStorage(home.isEmpty());

  vcube::AppSettings settings = vcube::AppSettings::load();
  vcube::applyTheme(app, settings.darkTheme);
  vcube::setInterfaceTextPercent(settings.interfaceTextPercent);

  // One copy at a time: a second one brings the first to the front.
  vcube::SingleInstance instance(dataDir);
  switch (instance.claim()) {
  case vcube::SingleInstance::Claim::First:
    break;
  case vcube::SingleInstance::Claim::Signalled:
    return 0;
  case vcube::SingleInstance::Claim::Running: {
    // The lock is held but no copy answered: maybe one that is hung, or a
    // lock left by a crash under another host name (a synced folder, a
    // renamed computer), which is never taken for stale.
    QString holder = instance.lockHolder();
    QMessageBox box(QMessageBox::Information, "v-cube",
                    QObject::tr("v-cube is already running. Only one copy "
                                "can run at a time, so that they don't save "
                                "over each other's changes."));
    box.setInformativeText(
        QObject::tr("No running copy answered, though %1 says one is "
                    "running%2. If v-cube isn't running anywhere (it may "
                    "have crashed on another computer), Start anyway takes "
                    "over the lock.\n\nTo run another copy, start it with "
                    "--data-dir <folder>.")
            .arg(QDir::toNativeSeparators(instance.lockPath()),
                 holder.isEmpty() ? QString() : " (" + holder + ")"));
    QPushButton *start =
        box.addButton(QObject::tr("Start anyway"), QMessageBox::AcceptRole);
    QPushButton *quit = box.addButton(QMessageBox::Close);
    box.setDefaultButton(quit);
    box.setEscapeButton(quit);
    box.exec();
    if (box.clickedButton() != start) {
      return 0;
    }
    if (!instance.takeOver()) {
      QMessageBox::warning(
          nullptr, "v-cube",
          QObject::tr("%1 couldn't be taken over. Delete it while v-cube "
                      "is closed, then start v-cube again.")
              .arg(QDir::toNativeSeparators(instance.lockPath())));
      return 1;
    }
    break;
  }
  }

  // Build the solver's tables while the window comes up.
  (void)QtConcurrent::run([] { vcube::TwoPhaseSolver::warmUp(); });

  int result = 0;
  {
    vcube::MainWindow window(settings, dataDir);
    QObject::connect(&instance, &vcube::SingleInstance::activationRequested,
                     &window, &vcube::MainWindow::raiseWindow);
    window.show();
    window.showStartupProblems();
    vcube::quitOnTerminationSignals();
    result = app.exec();
    vcube::finishBackgroundWrites(); // saves still being written
  }
  // Again once the window and its pages are gone, while the application
  // still is: a save queued as they were taken down (none is known) is
  // written now, not reported to an application being destroyed.
  vcube::finishBackgroundWrites();
  return result;
}
