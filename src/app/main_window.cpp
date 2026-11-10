#include "app/main_window.hpp"

#include "io/data_file.hpp"

#include "app/attempt_page.hpp"
#include "app/blind_page.hpp"
#include "app/drill_page.hpp"
#include "app/inspection_page.hpp"
#include "app/learn_page.hpp"
#include "app/library_page.hpp"
#include "app/platform.hpp"
#include "app/review_dialog.hpp"
#include "app/settings_dialog.hpp"
#include "app/timer_page.hpp"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace vcube {

namespace {

struct ModeInfo {
  const char *id; // settings value, e.g. "learn"
  const char *label;
};

constexpr std::array<ModeInfo, MainWindow::ModeCount> kModes = {{
    {"learn", QT_TRANSLATE_NOOP("MainWindow", "Learn")},
    {"drill", QT_TRANSLATE_NOOP("MainWindow", "Drill")},
    {"library", QT_TRANSLATE_NOOP("MainWindow", "Library")},
    {"timed", QT_TRANSLATE_NOOP("MainWindow", "Timed")},
    {"untimed", QT_TRANSLATE_NOOP("MainWindow", "Untimed")},
    {"movecount", QT_TRANSLATE_NOOP("MainWindow", "Move Count")},
    {"inspection", QT_TRANSLATE_NOOP("MainWindow", "Inspection")},
    {"blind", QT_TRANSLATE_NOOP("MainWindow", "Blind")},
}};

int modeFromId(const QString &id) {
  for (std::size_t i = 0; i < kModes.size(); ++i) {
    if (id == kModes[i].id) {
      return static_cast<int>(i);
    }
  }
  // Modes of earlier versions: Timer became Timed; Scramble and Virtual
  // became Untimed.
  if (id == "timer") {
    return MainWindow::Timed;
  }
  if (id == "scramble" || id == "virtual") {
    return MainWindow::Untimed;
  }
  return -1;
}

// Settings that earlier versions used and nothing reads any more.
void removeObsoleteSettings(QSettings &q) {
  for (const char *key :
       {"window/tab", "window/focus", "trainer/virtual", "trainer/showScramble",
        "timer/virtual", "timer/cubeMode", "scramble/type", "scramble/showAlgs",
        "virtual/scrambleType", "virtual/showAlgs", "ui/view/virtual"}) {
    q.remove(key);
  }
}

} // namespace

MainWindow::MainWindow(AppSettings &settings, QString dataDir, QWidget *parent)
    : QMainWindow(parent), settings_(settings), dataDir_(std::move(dataDir)),
      library_(QDir(dataDir_).filePath("algsets"), ":/algsets"),
      sessions_(QDir(dataDir_).filePath("sessions.json")),
      untimed_(QDir(dataDir_).filePath("untimed.json"), SolveLog::Untimed),
      moveCounts_(QDir(dataDir_).filePath("movecounts.json")) {
  setWindowTitle("v-cube");
  library_.load();
  QString error;
  // An unreadable file is kept, under another name if it could be moved.
  auto kept = [](const QString &path) {
    return path.isEmpty()
               ? tr("It is left where it is, and nothing will be saved "
                    "over it.")
               : tr("It was kept as %1.").arg(QDir::toNativeSeparators(path));
  };
  if (!sessions_.load(&error)) {
    QMessageBox::warning(this, "v-cube",
                         tr("Could not read your timer sessions (%1), so the "
                            "timer starts with an empty session.")
                                 .arg(error) +
                             " " + kept(sessions_.setAsidePath()));
  }
  if (!untimed_.load(&error)) {
    QMessageBox::warning(this, "v-cube",
                         tr("Could not read your untimed solves (%1), so "
                            "Untimed starts with an empty session.")
                                 .arg(error) +
                             " " + kept(untimed_.setAsidePath()));
  }
  if (!moveCounts_.load(&error)) {
    QMessageBox::warning(this, "v-cube",
                         tr("Could not read your move counts (%1), so Move "
                            "Count starts with the default sessions.")
                                 .arg(error) +
                             " " + kept(moveCounts_.setAsidePath()));
  }

  stack_ = new QStackedWidget(this);
  learnPage_ = new LearnPage(settings_, library_, stack_);
  drillPage_ = new DrillPage(settings_, library_, stack_);
  libraryPage_ = new LibraryPage(settings_, library_, stack_);
  timedPage_ = new TimerPage(settings_, sessions_, library_, stack_);
  untimedPage_ = new AttemptPage(settings_, untimed_, library_, stack_);
  moveCountPage_ = new AttemptPage(settings_, moveCounts_, library_, stack_);
  inspectionPage_ = new InspectionPage(settings_, dataDir_, stack_);
  blindPage_ = new BlindPage(settings_, dataDir_, stack_);
  pages_ = {learnPage_,   drillPage_,     libraryPage_,    timedPage_,
            untimedPage_, moveCountPage_, inspectionPage_, blindPage_};
  for (ModePage *page : pages_) {
    stack_->addWidget(page);
    connect(page, &ModePage::message, this, &MainWindow::showMessage);
  }
  connect(libraryPage_, &LibraryPage::setsChanged, this,
          [this](const QString &setId) {
            learnPage_->reloadSets();
            drillPage_->reloadSets();
            timedPage_->refreshScrambleTypes(setId);
            untimedPage_->refreshScrambleTypes(setId);
          });
  connect(inspectionPage_, &InspectionPage::reviewRequested, this,
          [this](const QString &scramble, const QString &solution, int step,
                 int index) {
            ReviewedSolve solve;
            auto s = static_cast<SolveStep>(step);
            solve.title = tr("Inspection: %1 #%2")
                              .arg(qs(std::string(stepName(s))))
                              .arg(index + 1);
            solve.scramble = parseAlgOrEmpty(ss(scramble));
            solve.solution = parseAlgOrEmpty(ss(solution));
            solve.step = s;
            solve.dnf = !stepReached(
                [&] {
                  FaceletCube f;
                  f.apply(solve.scramble);
                  f.apply(solve.solution);
                  return f;
                }(),
                s);
            ReviewDialog dialog(solve, settings_, &library_, this);
            dialog.exec();
          });
  for (CasePage *page : {static_cast<CasePage *>(learnPage_),
                         static_cast<CasePage *>(drillPage_)}) {
    connect(page, &CasePage::editCaseRequested, this,
            [this](const QString &setId, const QString &caseId) {
              if (setMode(Library)) {
                libraryPage_->selectCase(setId, caseId);
              }
            });
  }

  auto *central = new QWidget(this);
  auto *layout = new QHBoxLayout(central);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  sidebar_ = buildSidebar();
  layout->addWidget(sidebar_);
  layout->addWidget(stack_, 1);
  setCentralWidget(central);

  focusHint_ = new QLabel(central);
  focusHint_->setAttribute(Qt::WA_TransparentForMouseEvents);
  focusHint_->setStyleSheet(
      "QLabel { background: rgba(0, 0, 0, 170); color: white; "
      "border-radius: 6px; padding: 6px 14px; }");
  focusHint_->hide();
  focusHintTimer_ = new QTimer(this);
  focusHintTimer_->setSingleShot(true);
  connect(focusHintTimer_, &QTimer::timeout, focusHint_, &QWidget::hide);

  buildMenus();
  qApp->installEventFilter(this);

  if (!library_.errors().isEmpty()) {
    QMessageBox::warning(this, "v-cube",
                         tr("Some alg set files had problems:") + "\n\n" +
                             library_.errors().join("\n"));
  }
  QSettings q;
  restoreGeometry(q.value("window/geometry").toByteArray());
  if (q.value("window/geometry").isNull()) {
    resize(1280, 800);
  }
  // The app always opens out of full screen and focus mode, however it was
  // closed; the window keeps its size, position and maximised state.
  setWindowState(windowState() & ~Qt::WindowFullScreen);
  int mode = modeFromId(q.value("window/mode").toString());
  if (mode < 0) {
    // Earlier versions saved a tab: Timer, Trainer or Library.
    constexpr std::array<int, 3> fromTab = {Timed, Drill, Library};
    int tab = q.value("window/tab", -1).toInt();
    mode = tab >= 0 && tab < 3 ? fromTab[static_cast<std::size_t>(tab)] : Learn;
  }
  removeObsoleteSettings(q);
  setMode(mode);
}

QWidget *MainWindow::buildSidebar() {
  auto *bar = new QWidget(this);
  bar->setObjectName("modeSidebar");
  bar->setAutoFillBackground(true);
  QPalette pal = bar->palette();
  pal.setColor(QPalette::Window, palette().color(QPalette::Base));
  bar->setPalette(pal);
  bar->setFocusPolicy(Qt::NoFocus);
  bar->setStyleSheet(
      "QToolButton { text-align: left; padding: 6px 18px 6px 14px; border: "
      "none; border-radius: 4px; }"
      "QToolButton:hover { background: palette(button); }"
      "QToolButton:checked { background: palette(highlight); color: "
      "palette(highlighted-text); }");
  auto *layout = new QVBoxLayout(bar);
  layout->setContentsMargins(8, 10, 8, 10);
  layout->setSpacing(2);
  modeButtons_ = new QButtonGroup(this);
  modeButtons_->setExclusive(true);
  auto header = [&](const QString &text) {
    auto *label = new QLabel(text, bar);
    QFont f = label->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 0.85);
    label->setFont(f);
    label->setForegroundRole(QPalette::PlaceholderText);
    label->setContentsMargins(6, 10, 0, 4);
    layout->addWidget(label);
  };
  for (int m = 0; m < ModeCount; ++m) {
    if (m == Learn) {
      header(tr("ALGS"));
    } else if (m == Timed) {
      header(tr("SOLVES"));
    } else if (m == Inspection) {
      header(tr("PRACTICE"));
    }
    auto *b = new QToolButton(bar);
    b->setText(tr(kModes[static_cast<std::size_t>(m)].label));
    b->setToolTip(tr("%1 (Ctrl+%2)").arg(b->text()).arg(m + 1));
    b->setCheckable(true);
    b->setFocusPolicy(Qt::NoFocus);
    b->setToolButtonStyle(Qt::ToolButtonTextOnly);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    modeButtons_->addButton(b, m);
    layout->addWidget(b);
  }
  layout->addStretch();
  connect(modeButtons_, &QButtonGroup::idClicked, this,
          [this](int m) { setMode(m); });
  return bar;
}

ModePage *MainWindow::currentPage() const {
  return mode_ >= 0 ? pages_[static_cast<std::size_t>(mode_)] : nullptr;
}

bool MainWindow::setMode(int mode) {
  mode = std::clamp(mode, 0, ModeCount - 1);
  ModePage *old = currentPage();
  if (mode == mode_) {
    return true;
  }
  if (old && old->isBusy()) {
    // Keep the sidebar on the running mode.
    modeButtons_->button(mode_)->setChecked(true);
    showMessage(tr("Finish or cancel the solve (Esc) before switching modes"));
    return false;
  }
  if (old) {
    // A text field on the old page must not keep the keyboard.
    if (QWidget *focus = QApplication::focusWidget();
        focus && old->isAncestorOf(focus)) {
      focus->clearFocus();
    }
    old->deactivated();
  }
  mode_ = mode;
  modeButtons_->button(mode)->setChecked(true);
  stack_->setCurrentWidget(currentPage());
  currentPage()->activated();
  QSettings().setValue("window/mode",
                       kModes[static_cast<std::size_t>(mode)].id);
  return true;
}

void MainWindow::setFullScreen(bool on) {
  setWindowState(on ? windowState() | Qt::WindowFullScreen
                    : windowState() & ~Qt::WindowFullScreen);
}

void MainWindow::setFocusMode(bool on) {
  focusMode_ = on;
  sidebar_->setVisible(!on);
  menuBar()->setVisible(!on);
  statusBar()->setVisible(!on);
  for (ModePage *page : pages_) {
    page->setFocusMode(on);
  }
  if (on) {
    showFocusHint();
  } else {
    focusHint_->hide();
  }
}

void MainWindow::showFocusHint() {
  showOverlay(tr("Focus mode: F10 or right-click to show everything again"),
              3500);
}

void MainWindow::showOverlay(const QString &text, int ms) {
  focusHint_->setText(text);
  focusHint_->adjustSize();
  // Placed once the layout has settled (at start-up, after the window is
  // shown).
  QTimer::singleShot(0, focusHint_, [this] {
    QWidget *area = centralWidget();
    focusHint_->move((area->width() - focusHint_->width()) / 2, 12);
    focusHint_->raise();
    focusHint_->show();
  });
  focusHintTimer_->start(ms);
}

void MainWindow::contextMenuEvent(QContextMenuEvent *e) {
  QMenu menu(this);
  menu.addAction(fullScreenAction_);
  menu.addAction(focusAction_);
  menu.exec(e->globalPos());
}

void MainWindow::changeEvent(QEvent *e) {
  if (e->type() == QEvent::WindowStateChange && fullScreenAction_) {
    fullScreenAction_->setChecked(isFullScreen());
  }
  if (e->type() == QEvent::ActivationChange && !isActiveWindow()) {
    if (ModePage *page = currentPage()) {
      page->windowDeactivated();
    }
  }
  QMainWindow::changeEvent(e);
}

void MainWindow::closeEvent(QCloseEvent *e) {
  QSettings q;
  q.setValue("window/geometry", saveGeometry());
  finishBackgroundWrites();
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
  ModePage *page = currentPage();
  return page && page->handleKey(key, event->type() == QEvent::KeyPress);
}

void MainWindow::buildMenus() {
  QMenu *file = menuBar()->addMenu(tr("&File"));
  file->addAction(tr("Import alg set…"), this, &MainWindow::importAlgSet);
  file->addAction(tr("Export alg set…"), this, &MainWindow::exportAlgSet);
  file->addSeparator();
  file->addAction(tr("Open data folder"), this, &MainWindow::openDataFolder);
  file->addSeparator();
  file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

  QMenu *edit = menuBar()->addMenu(tr("&Edit"));
  edit->addAction(tr("&Settings…"), QKeySequence(Qt::CTRL | Qt::Key_Comma),
                  this, [this] { openSettings(); });
  edit->addAction(tr("&Colour neutrality…"),
                  QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), this,
                  [this] { openSettings(true); });

  QMenu *view = menuBar()->addMenu(tr("&View"));
  fullScreenAction_ = view->addAction(tr("&Full screen"));
  fullScreenAction_->setCheckable(true);
  fullScreenAction_->setChecked(isFullScreen());
  QList<QKeySequence> fullScreenKeys = {QKeySequence(Qt::Key_F11)};
  for (const QKeySequence &k :
       QKeySequence::keyBindings(QKeySequence::FullScreen)) {
    if (!fullScreenKeys.contains(k)) {
      fullScreenKeys << k;
    }
  }
  fullScreenAction_->setShortcuts(fullScreenKeys);
  connect(fullScreenAction_, &QAction::toggled, this,
          &MainWindow::setFullScreen);
  focusAction_ = view->addAction(tr("F&ocus mode"));
  focusAction_->setCheckable(true);
  focusAction_->setShortcut(QKeySequence(Qt::Key_F10));
  focusAction_->setToolTip(
      tr("Hide everything but the scramble, timer, cube and algs"));
  connect(focusAction_, &QAction::toggled, this, &MainWindow::setFocusMode);

  QMenu *modes = menuBar()->addMenu(tr("&Mode"));
  for (int m = 0; m < ModeCount; ++m) {
    modes->addAction(
        tr(kModes[static_cast<std::size_t>(m)].label),
        QKeySequence(Qt::CTRL | static_cast<Qt::Key>(Qt::Key_1 + m)), this,
        [this, m] { setMode(m); });
    if (m == Library || m == MoveCount) {
      modes->addSeparator();
    }
  }

  QMenu *help = menuBar()->addMenu(tr("&Help"));
  help->addAction(tr("Keyboard controls"), this, &MainWindow::showControls);
  help->addAction(tr("About v-cube"), this, &MainWindow::showAbout);

  // The shortcuts must keep working while focus mode hides the menu bar.
  for (QAction *menu : menuBar()->actions()) {
    addActions(menu->menu()->actions());
  }
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
  if (focusMode_) {
    showOverlay(text, 5000); // the status bar is hidden
  }
}

void MainWindow::openSettings(bool neutrality) {
  if (currentPage() && currentPage()->isBusy()) {
    return;
  }
  SettingsDialog dialog(settings_, this);
  if (neutrality) {
    dialog.showNeutralityTab();
  }
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  bool themeChanged = dialog.result().darkTheme != settings_.darkTheme;
  settings_ = dialog.result();
  settings_.save();
  if (themeChanged) {
    applyTheme(*qApp, settings_.darkTheme);
  }
  for (ModePage *page : pages_) {
    page->applySettings();
  }
}

void MainWindow::importAlgSet() {
  if (setMode(Library)) {
    libraryPage_->importSetFile();
  }
}

void MainWindow::exportAlgSet() {
  if (setMode(Library)) {
    libraryPage_->exportSetFile();
  }
}

void MainWindow::showControls() {
  QString text = tr(
      "<h3>Modes</h3>"
      "<p><b>Ctrl+1</b> to <b>Ctrl+8</b> switch between Learn, Drill, "
      "Library, Timed, Untimed, Move Count, Inspection and Blind. Switching "
      "is blocked while a solve or inspection is running.</p>"
      "<h3>Window</h3>"
      "<p><b>F11</b> toggles full screen. <b>F10</b> toggles focus mode, "
      "which hides everything but the scramble, timer, cube and algs; "
      "right-click shows both options. <b>Ctrl+Shift+N</b> opens the colour "
      "neutrality settings.</p>"
      "<h3>Learn (virtual cube, untimed)</h3>"
      "<p>Solve the case on the virtual cube from memory. <b>Tab</b> shows the "
      "algs (grades the case Again), <b>Shift+Space</b> gives up, <b>Esc</b> "
      "resets the case. After the case: <b>Space/Enter</b> next case, "
      "<b>1–4</b> change the grade (Again/Hard/Good/Easy), <b>Esc</b> resets "
      "the cube to practise. Guided learning: a new case's walkthrough steps "
      "with <b>→</b>/<b>←</b> (<b>Home</b> back to the start) and "
      "<b>Space</b> tries it with each next move shown; in the learning "
      "steps <b>Tab</b> first shows only the next move. In context: "
      "<b>Tab</b> shows the set's cheapest order, <b>Esc</b> starts again, "
      "<b>Shift+Space</b> gives up, then <b>Space</b> for another.</p>"
      "<h3>Drill (timed)</h3>"
      "<p>Hold <b>Space</b> until the time turns green, release to start. "
      "<b>Esc</b> cancels. Virtual cube: until the timer starts only "
      "rotations turn the cube; <b>Space</b> stops it, and a case whose step "
      "isn't done then (solved for PLL, oriented for OLL, ...) is a DNF. "
      "Physical cube: any key stops. <b>Backspace</b> before or during "
      "the solve gives the case up (a DNF). After the case: <b>Space</b> "
      "next case, <b>Backspace</b> toggles DNF (virtual) or \"wrong alg\" "
      "(physical), <b>Esc</b> tries the case again.</p>"
      "<p>After a DNF the algs are shown and <b>Space</b> starts practice "
      "of the same case, untimed and unrecorded: first with the algs "
      "shown, then (<b>Space</b>) without them, where <b>Tab</b> shows "
      "them again. Once it is solved without help, <b>Space</b> moves on. "
      "<b>Esc</b> resets the cube and <b>Shift+Space</b> skips to the next "
      "case.</p>"
      "<h3>Timed</h3>"
      "<p>Hold <b>Space</b> until the time turns green, release to start. "
      "With inspection on, tap Space to start inspecting first. <b>Esc</b> "
      "cancels. Physical cube: any key stops. Virtual cube: until the timer "
      "starts only rotations turn the cube; <b>Space</b> stops it, and a "
      "cube that isn't solved then is a DNF.</p>"
      "<h3>Untimed</h3>"
      "<p>Virtual cube: rotations only until the solve starts (hold "
      "<b>Space</b> until it turns green, then release); "
      "<b>Space</b> ends it (DNF if the cube isn't solved) and the cube then "
      "turns freely; <b>Space</b> again gives the next scramble. <b>Esc</b> "
      "starts the scramble again during a solve, or resets the cube to "
      "solved after it. In the alg field, <b>Enter</b> applies the alg and "
      "<b>Esc</b> returns the keyboard to the cube. Physical cube: "
      "<b>Space</b> or <b>→</b> keeps the scramble as done and shows the "
      "next, <b>Backspace</b> or <b>←</b> goes back.</p>"
      "<h3>Move Count</h3>"
      "<p>Virtual cube: rotations only until the solve starts (hold "
      "<b>Space</b> until it turns green, then release); "
      "<b>Space</b> ends it, counting the moves (HTM) if the session's step "
      "is done and DNF if not. <b>Esc</b> starts the scramble again. "
      "Physical cube: solve the step, then type the number of moves your "
      "solution took: <b>0–9</b> type the count, <b>Backspace</b> corrects "
      "it, <b>Esc</b> clears it and <b>Enter</b> records it and shows the "
      "next scramble. Double-click a solve to review it (<b>←</b>/<b>→</b>, "
      "<b>Home</b>/<b>End</b> step the replay).</p>"
      "<h3>Inspection</h3>"
      "<p><b>Space</b> starts inspection (the cube only turns as a whole), "
      "<b>Space</b> again hides the cube to execute your plan with the "
      "turning keys, and <b>Space</b> reveals it; <b>Space</b> then gives "
      "the next scramble. <b>Esc</b> puts the scramble back. Bad edges: type "
      "the count with <b>0–9</b> (<b>Backspace</b> corrects) and press "
      "<b>Enter</b>; afterwards <b>Tab</b> rings the other axis's bad "
      "edges.</p>"
      "<h3>Blind</h3>"
      "<p>Letter quiz: type the ringed sticker's letter (<b>Enter</b> for a "
      "longer label) or click the sticker named; <b>Space</b> next. Memo "
      "tracing: <b>Space</b> new scramble, <b>Enter</b> to type the edge "
      "letters, <b>Enter</b> for the corners and <b>Enter</b> to check. "
      "Setup moves: turn the cube until the ringed sticker reaches ◎; "
      "<b>Esc</b> starts again, <b>Space</b> skips. Blind solves: "
      "<b>Space</b> starts the memo, <b>Space</b> hides the cube for the "
      "execution and <b>Space</b> stops (physical cube: any key stops, "
      "<b>Backspace</b> toggles DNF); <b>Esc</b> cancels.</p>"
      "<h3>Alg set scrambles (Timed and Untimed)</h3>"
      "<p><b>Tab</b> shows or hides the algs for the case the scramble "
      "sets up. Nothing is graded.</p>"
      "<h3>Turning keys (Learn and the virtual cube)</h3><p>");
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
