#include "app/app_settings.hpp"
#include "app/main_window.hpp"
#include "app/platform.hpp"
#include "app/single_instance.hpp"
#include "cube/solver.hpp"
#include "io/data_file.hpp"

#include "vcube_version.hpp"

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QMessageBox>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>

int main(int argc, char **argv) {
  vcube::selectQtPlatform();
  QApplication app(argc, argv);
  QApplication::setOrganizationName("v-cube");
  QApplication::setApplicationName("v-cube");
  QApplication::setApplicationVersion(VCUBE_VERSION);
  QIcon icon(":/icons/v-cube.svg");
  icon.addFile(":/icons/v-cube-256.png"); // without the SVG plugin
  QApplication::setWindowIcon(icon);
  vcube::initSettingsStorage();

  vcube::AppSettings settings = vcube::AppSettings::load();
  vcube::applyTheme(app, settings.darkTheme);

  QString dataDir = QDir(QStandardPaths::writableLocation(
                             QStandardPaths::GenericDataLocation))
                        .filePath("v-cube");
  QDir().mkpath(dataDir);

  // One copy at a time: a second one brings the first to the front.
  vcube::SingleInstance instance(dataDir);
  switch (instance.claim()) {
  case vcube::SingleInstance::Claim::First:
    break;
  case vcube::SingleInstance::Claim::Signalled:
    return 0;
  case vcube::SingleInstance::Claim::Running:
    QMessageBox::information(
        nullptr, "v-cube",
        QObject::tr("v-cube is already running. Only one copy can run at a "
                    "time, so that they don't save over each other's "
                    "changes."));
    return 0;
  }

  // Build the solver's tables while the window comes up.
  (void)QtConcurrent::run([] { vcube::TwoPhaseSolver::warmUp(); });

  vcube::MainWindow window(settings, dataDir);
  QObject::connect(&instance, &vcube::SingleInstance::activationRequested,
                   &window, &vcube::MainWindow::raiseWindow);
  window.show();
  int result = app.exec();
  vcube::finishBackgroundWrites(); // saves still being written
  return result;
}
