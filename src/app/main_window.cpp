#include "app/main_window.hpp"

#include "app/library_page.hpp"
#include "app/platform.hpp"
#include "app/settings_dialog.hpp"
#include "app/timer_page.hpp"
#include "app/trainer_page.hpp"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>

namespace vcube {

MainWindow::MainWindow(AppSettings &settings, QString dataDir, QWidget *parent)
    : QMainWindow(parent), settings_(settings), dataDir_(std::move(dataDir)),
      library_(QDir(dataDir_).filePath("algsets"), ":/algsets"),
      sessions_(QDir(dataDir_).filePath("sessions.json")) {
  setWindowTitle("v-cube");
  library_.load();
  QString error;
  if (!sessions_.load(&error)) {
    QMessageBox::warning(
        this, "v-cube",
        tr("Could not read your sessions (%1). Starting with an empty "
           "session; the old file is left untouched until you save.")
            .arg(error));
  }

  tabs_ = new QTabWidget(this);
  tabs_->setFocusPolicy(Qt::NoFocus);
  tabs_->tabBar()->setFocusPolicy(Qt::NoFocus);
  timerPage_ = new TimerPage(settings_, sessions_, library_, tabs_);
  tabs_->addTab(timerPage_, tr("Timer"));
  connect(timerPage_, &TimerPage::message, this, &MainWindow::showMessage);
  trainerPage_ = new TrainerPage(settings_, library_, tabs_);
  tabs_->addTab(trainerPage_, tr("Trainer"));
  connect(trainerPage_, &TrainerPage::message, this, &MainWindow::showMessage);
  libraryPage_ = new LibraryPage(settings_, library_, tabs_);
  tabs_->addTab(libraryPage_, tr("Library"));
  connect(libraryPage_, &LibraryPage::message, this, &MainWindow::showMessage);
  connect(libraryPage_, &LibraryPage::setsChanged, this, [this] {
    trainerPage_->reloadSets();
    timerPage_->refreshScrambleTypes();
  });
  connect(trainerPage_, &TrainerPage::editCaseRequested, this,
          [this](const QString &setId, const QString &caseId) {
            tabs_->setCurrentWidget(libraryPage_);
            libraryPage_->selectCase(setId, caseId);
          });
  setCentralWidget(tabs_);

  buildMenus();
  qApp->installEventFilter(this);

  for (const QString &e : library_.errors()) {
    showMessage(e);
  }
  QSettings q;
  restoreGeometry(q.value("window/geometry").toByteArray());
  tabs_->setCurrentIndex(q.value("window/tab", 0).toInt());
  if (q.value("window/geometry").isNull()) {
    resize(1200, 780);
  }
}

void MainWindow::closeEvent(QCloseEvent *e) {
  QSettings q;
  q.setValue("window/geometry", saveGeometry());
  q.setValue("window/tab", tabs_->currentIndex());
  QMainWindow::closeEvent(e);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
  if (event->type() != QEvent::KeyPress &&
      event->type() != QEvent::KeyRelease) {
    return QMainWindow::eventFilter(obj, event);
  }
  // Only keys aimed at this window, not at dialogs, menus or text fields.
  if (QApplication::activeModalWidget() || QApplication::activePopupWidget()) {
    return false;
  }
  auto *w = qobject_cast<QWidget *>(obj);
  if (!w || w->window() != this) {
    return false;
  }
  QWidget *focus = QApplication::focusWidget();
  if (qobject_cast<QLineEdit *>(focus) || qobject_cast<QTextEdit *>(focus) ||
      qobject_cast<QPlainTextEdit *>(focus) ||
      qobject_cast<QAbstractSpinBox *>(focus) ||
      qobject_cast<QKeySequenceEdit *>(focus)) {
    return false;
  }
  auto *key = static_cast<QKeyEvent *>(event);
  // An unhandled key event is offered again to each parent; handle it once.
  KeySeen seen{event, event->type(), key->key(), key->timestamp()};
  if (seen == lastKey_) {
    return false;
  }
  lastKey_ = seen;
  bool press = event->type() == QEvent::KeyPress;
  QWidget *page = tabs_->currentWidget();
  if (page == timerPage_) {
    return timerPage_->handleKey(key, press);
  }
  if (page == trainerPage_) {
    return trainerPage_->handleKey(key, press);
  }
  return false;
}

void MainWindow::buildMenus() {
  QMenu *file = menuBar()->addMenu(tr("&File"));
  file->addAction(tr("Import alg set…"), this, &MainWindow::importAlgSet);
  file->addAction(tr("Export alg set…"), this, &MainWindow::exportAlgSet);
  file->addSeparator();
  file->addAction(tr("Import cstimer sessions…"), this,
                  &MainWindow::importCstimer);
  file->addAction(tr("Export sessions for cstimer…"), this,
                  &MainWindow::exportCstimer);
  file->addSeparator();
  file->addAction(tr("Open data folder"), this, &MainWindow::openDataFolder);
  file->addSeparator();
  file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

  QMenu *edit = menuBar()->addMenu(tr("&Edit"));
  edit->addAction(tr("&Settings…"), QKeySequence(Qt::CTRL | Qt::Key_Comma),
                  this, &MainWindow::openSettings);

  QMenu *help = menuBar()->addMenu(tr("&Help"));
  help->addAction(tr("Keyboard controls"), this, &MainWindow::showControls);
  help->addAction(tr("About v-cube"), this, &MainWindow::showAbout);
}

void MainWindow::openDataFolder() {
  if (openFolder(dataDir_)) {
    return;
  }
  // No file manager could be started: show the paths so they can be opened
  // by hand.
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Open data folder"));
  auto *layout = new QVBoxLayout(&dialog);
  auto *intro = new QLabel(
      tr("Could not open a file manager. Your files are here:"), &dialog);
  layout->addWidget(intro);
  auto *grid = new QGridLayout;
  const std::pair<QString, QString> rows[] = {
      {tr("Data folder"), QDir::toNativeSeparators(dataDir_)},
      {tr("Config file"), QDir::toNativeSeparators(QSettings().fileName())},
  };
  int row = 0;
  for (const auto &[label, path] : rows) {
    auto *edit = new QLineEdit(path, &dialog);
    edit->setReadOnly(true);
    edit->setMinimumWidth(edit->fontMetrics().horizontalAdvance(path) + 24);
    auto *copy = new QPushButton(tr("Copy"), &dialog);
    copy->setAutoDefault(false);
    connect(copy, &QPushButton::clicked, &dialog, [this, path, label] {
      QGuiApplication::clipboard()->setText(path);
      showMessage(tr("%1 path copied").arg(label));
    });
    grid->addWidget(new QLabel(label, &dialog), row, 0);
    grid->addWidget(edit, row, 1);
    grid->addWidget(copy, row, 2);
    ++row;
  }
  layout->addLayout(grid);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  dialog.exec();
}

void MainWindow::showMessage(const QString &text) {
  statusBar()->showMessage(text, 8000);
}

void MainWindow::openSettings() {
  if (timerPage_->isBusy()) {
    return;
  }
  SettingsDialog dialog(settings_, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  bool themeChanged = dialog.result().darkTheme != settings_.darkTheme;
  settings_ = dialog.result();
  settings_.save();
  if (themeChanged) {
    applyTheme(*qApp, settings_.darkTheme);
  }
  timerPage_->applySettings();
  trainerPage_->applySettings();
  libraryPage_->applySettings();
}

void MainWindow::importAlgSet() {
  tabs_->setCurrentWidget(libraryPage_);
  libraryPage_->importSetFile();
}

void MainWindow::exportAlgSet() {
  tabs_->setCurrentWidget(libraryPage_);
  libraryPage_->exportSetFile();
}

void MainWindow::importCstimer() {
  QString path = QFileDialog::getOpenFileName(
      this, tr("Import cstimer export"), QString(),
      tr("cstimer export (*.txt *.json);;All files (*)"));
  if (path.isEmpty()) {
    return;
  }
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    QMessageBox::warning(this, tr("Import"), f.errorString());
    return;
  }
  QString error;
  auto imported = sessionsFromCstimer(
      QJsonDocument::fromJson(f.readAll()).object(), &error);
  if (!imported) {
    QMessageBox::warning(this, tr("Import"),
                         tr("Not a cstimer export: %1").arg(error));
    return;
  }
  std::size_t solves = 0;
  for (auto &s : *imported) {
    solves += s.solves.size();
    s.name += " (cstimer)";
    sessions_.sessions().push_back(std::move(s));
  }
  sessions_.setCurrentIndex(sessions_.sessions().size() - 1);
  sessions_.save(&error);
  timerPage_->reloadSessions();
  showMessage(tr("Imported %1 sessions with %2 solves")
                  .arg(imported->size())
                  .arg(solves));
}

void MainWindow::exportCstimer() {
  QString path = QFileDialog::getSaveFileName(
      this, tr("Export for cstimer"), "v-cube-sessions.txt",
      tr("cstimer import file (*.txt)"));
  if (path.isEmpty()) {
    return;
  }
  QString error;
  QByteArray bytes = QJsonDocument(sessionsToCstimer(sessions_.sessions()))
                         .toJson(QJsonDocument::Compact);
  if (!writeFileAtomically(path, bytes, &error)) {
    QMessageBox::warning(this, tr("Export"), error);
    return;
  }
  showMessage(tr("Exported %1 sessions to %2")
                  .arg(sessions_.sessions().size())
                  .arg(path));
}

void MainWindow::showControls() {
  QString text = tr(
      "<h3>Timer (physical cube)</h3>"
      "<p>Hold <b>Space</b> until the time turns green, release to start, "
      "press any key "
      "to stop. With inspection on, tap Space to start inspecting first. "
      "<b>Esc</b> "
      "cancels.</p>"
      "<h3>Virtual cube</h3>"
      "<p><b>Space</b> scrambles (timer) or gives up (trainer); the first turn "
      "starts "
      "the timer and solving stops it. <b>Esc</b> aborts. Click the cube to "
      "switch "
      "between 3D, flat and hidden.</p>"
      "<h3>Trainer</h3>"
      "<p>After a case: <b>Space/Enter</b> next case, <b>Backspace</b> toggles "
      "\"wrong alg\", <b>1–4</b> override the grade (Again/Hard/Good/Easy), "
      "<b>Esc</b> resets the case to try again. <b>Tab</b> reveals the "
      "algs.</p>"
      "<h3>Turning keys</h3><p>");
  QStringList rows;
  for (const auto &b : settings_.keymap.bindings) {
    rows << QString("<code>%1</code>&nbsp;%2")
                .arg(Keymap::keyText(b.key, b.shift).toHtmlEscaped(),
                     qs(toString(b.move)).toHtmlEscaped());
  }
  text += rows.join(" &nbsp; ") + "</p>";
  QMessageBox box(this);
  box.setWindowTitle(tr("Keyboard controls"));
  box.setTextFormat(Qt::RichText);
  box.setText(text);
  box.exec();
}

void MainWindow::showAbout() {
  QMessageBox::about(
      this, tr("About v-cube"),
      tr("<h3>v-cube %1</h3><p>Create, learn and drill 3x3 alg sets; scramble "
         "and "
         "time solves with a real or a virtual cube.</p>"
         "<p>Random-state scrambles use Kociemba's two-phase algorithm. "
         "Inspired by "
         "cstimer and Alg-Trainer.</p>"
         "<p>Data folder: <code>%2</code><br>Qt %3, platform "
         "<code>%4</code></p>")
          .arg(QApplication::applicationVersion(), dataDir_.toHtmlEscaped(),
               qVersion(), QGuiApplication::platformName()));
}

} // namespace vcube
