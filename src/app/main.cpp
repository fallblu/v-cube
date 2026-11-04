#include "app/app_settings.hpp"
#include "app/main_window.hpp"
#include "app/platform.hpp"
#include "cube/solver.hpp"

#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>

int main(int argc, char **argv) {
  vcube::selectQtPlatform();
  QApplication app(argc, argv);
  QApplication::setOrganizationName("v-cube");
  QApplication::setApplicationName("v-cube");
  QApplication::setApplicationVersion(VCUBE_VERSION);
  vcube::initSettingsStorage();

  // Build the solver's tables while the window comes up.
  (void)QtConcurrent::run([] { vcube::TwoPhaseSolver::warmUp(); });

  vcube::AppSettings settings = vcube::AppSettings::load();
  vcube::applyTheme(app, settings.darkTheme);

  QString dataDir = QDir(QStandardPaths::writableLocation(
                             QStandardPaths::GenericDataLocation))
                        .filePath("v-cube");
  QDir().mkpath(dataDir);

  vcube::MainWindow window(settings, dataDir);
  window.show();
  return app.exec();
}
