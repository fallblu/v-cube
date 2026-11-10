#include "app/main_window.hpp"

#include "io/backup.hpp"
#include "io/data_file.hpp"

#include "app/blind_page.hpp"
#include "app/command_palette.hpp"
#include "app/drill_page.hpp"
#include "app/help_text.hpp"
#include "app/inspection_page.hpp"
#include "app/learn_page.hpp"
#include "app/library_page.hpp"
#include "app/move_count_page.hpp"
#include "app/platform.hpp"
#include "app/review_dialog.hpp"
#include "app/settings_dialog.hpp"
#include "app/sounds.hpp"
#include "app/timer_page.hpp"
#include "app/today_page.hpp"
#include "app/untimed_page.hpp"
#include "app/widgets.hpp"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
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
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTextBrowser>
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
    {"today", QT_TRANSLATE_NOOP("MainWindow", "Today")},
}};

// Sidebar and Mode menu order.
constexpr std::array<int, MainWindow::ModeCount> kModeOrder = {
    MainWindow::Today,     MainWindow::Learn,      MainWindow::Drill,
    MainWindow::Library,   MainWindow::Timed,      MainWindow::Untimed,
    MainWindow::MoveCount, MainWindow::Inspection, MainWindow::Blind};

QString modeLabel(int m) {
  return QCoreApplication::translate("MainWindow",
                                     kModes[static_cast<std::size_t>(m)].label);
}

QKeySequence modeShortcut(int m) {
  return m == MainWindow::Today
             ? QKeySequence(Qt::CTRL | Qt::Key_0)
             : QKeySequence(Qt::CTRL | static_cast<Qt::Key>(Qt::Key_1 + m));
}

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

// Dated copies of the data files kept in the backups folder.
constexpr int kBackupDays = 14;

} // namespace

MainWindow::MainWindow(AppSettings &settings, QString dataDir, QWidget *parent)
    : QMainWindow(parent), settings_(settings), dataDir_(std::move(dataDir)),
      library_(QDir(dataDir_).filePath("algsets"), ":/algsets"),
      sessions_(QDir(dataDir_).filePath("sessions.json")),
      untimed_(QDir(dataDir_).filePath("untimed.json"), SolveLog::Untimed),
      moveCounts_(QDir(dataDir_).filePath("movecounts.json")) {
  setWindowTitle("v-cube");
  setWriteReporter([this](const QString &path, const QString &error) {
    writeReported(path, error);
  });
  // Today's copy of the data files, before anything is changed.
  QString backupError;
  if (!makeDailyBackup(dataDir_, QDate::currentDate(), kBackupDays,
                       &backupError)) {
    failedWrites_[QDir(dataDir_).filePath(kBackupsFolder)] =
        tr("Today's backup failed: %1").arg(backupError);
  }
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
  untimedPage_ = new UntimedPage(settings_, untimed_, library_, stack_);
  moveCountPage_ = new MoveCountPage(settings_, moveCounts_, library_, stack_);
  inspectionPage_ = new InspectionPage(settings_, dataDir_, stack_);
  blindPage_ = new BlindPage(settings_, dataDir_, stack_);
  todayPage_ = new TodayPage(settings_, library_, sessions_, untimed_,
                             moveCounts_, dataDir_, stack_);
  pages_ = {learnPage_,      drillPage_,   libraryPage_,
            timedPage_,      untimedPage_, moveCountPage_,
            inspectionPage_, blindPage_,   todayPage_};
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
            updateBadges();
          });
  connect(todayPage_, &TodayPage::learnSetRequested, this,
          &MainWindow::learnSet);
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
            dialog.setRetryEnabled(true);
            connect(&dialog, &ReviewDialog::retryRequested, inspectionPage_,
                    [this, index] { inspectionPage_->startRetry(index); });
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
  auto *right = new QVBoxLayout;
  right->setContentsMargins(0, 0, 0, 0);
  right->setSpacing(0);
  right->addWidget(buildBanner());
  right->addWidget(stack_, 1);
  layout->addLayout(right, 1);
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

  badgeTimer_ = new QTimer(this);
  badgeTimer_->setInterval(60 * 1000);
  connect(badgeTimer_, &QTimer::timeout, this, &MainWindow::updateBadges);
  badgeTimer_->start();

  buildHelpPanel();
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
  restoreState(q.value("window/state").toByteArray());
  // The app always opens out of full screen and focus mode, however it was
  // closed; the window keeps its size, position and maximised state.
  setWindowState(windowState() & ~Qt::WindowFullScreen);
  fitToScreen();
  restoreLayout();
  setSidebarShown(q.value("window/sidebar", true).toBool());
  int mode = modeFromId(q.value("window/mode").toString());
  if (mode < 0) {
    // Earlier versions saved a tab: Timer, Trainer or Library.
    constexpr std::array<int, 3> fromTab = {Timed, Drill, Library};
    int tab = q.value("window/tab", -1).toInt();
    mode = tab >= 0 && tab < 3 ? fromTab[static_cast<std::size_t>(tab)] : Learn;
  }
  removeObsoleteSettings(q);
  applyWindowSettings();
  setMode(mode);
  updateBadges();
  updateBanner();
}

MainWindow::~MainWindow() { setWriteReporter({}); }

void MainWindow::raiseWindow() {
  if (isMinimized()) {
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
  }
  show();
  raise();
  activateWindow();
}

QWidget *MainWindow::buildSidebar() {
  auto *bar = new QWidget(this);
  bar->setObjectName("modeSidebar");
  bar->setAutoFillBackground(true);
  bar->setBackgroundRole(QPalette::Base); // follows the theme
  bar->setFocusPolicy(Qt::NoFocus);
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
  for (int m : kModeOrder) {
    if (m == Learn) {
      header(tr("ALGS"));
    } else if (m == Timed) {
      header(tr("SOLVES"));
    } else if (m == Inspection) {
      header(tr("PRACTICE"));
    }
    auto *b = new QToolButton(bar);
    b->setText(modeLabel(m));
    b->setToolTip(tr("%1 (%2)").arg(
        modeLabel(m), modeShortcut(m).toString(QKeySequence::NativeText)));
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

void MainWindow::styleSidebar() {
  // Set again after a theme change so that the palette() colours follow.
  sidebar_->setStyleSheet(
      "QToolButton { text-align: left; padding: 6px 18px 6px 14px; border: "
      "none; border-radius: 4px; }"
      "QToolButton:hover { background: palette(button); }"
      "QToolButton:checked { background: palette(highlight); color: "
      "palette(highlighted-text); }");
}

QWidget *MainWindow::buildBanner() {
  banner_ = new QWidget(this);
  banner_->setObjectName("saveBanner");
  banner_->setAttribute(Qt::WA_StyledBackground);
  // The same in both themes: white on a dark red.
  banner_->setStyleSheet("#saveBanner { background: #8e1f1f; }"
                         "#saveBanner QLabel { color: white; }");
  auto *layout = new QHBoxLayout(banner_);
  layout->setContentsMargins(12, 6, 8, 6);
  bannerText_ = new QLabel(banner_);
  bannerText_->setWordWrap(true);
  bannerText_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  auto *folder = new QPushButton(tr("Open data folder"), banner_);
  folder->setFocusPolicy(Qt::NoFocus);
  connect(folder, &QPushButton::clicked, this, &MainWindow::openDataFolder);
  auto *dismiss = new QPushButton(tr("Dismiss"), banner_);
  dismiss->setFocusPolicy(Qt::NoFocus);
  dismiss->setToolTip(tr("Hide this until a file can't be saved again"));
  connect(dismiss, &QPushButton::clicked, this, [this] {
    failedWrites_.clear();
    updateBanner();
  });
  layout->addWidget(bannerText_, 1);
  layout->addWidget(folder);
  layout->addWidget(dismiss);
  banner_->hide();
  return banner_;
}

void MainWindow::writeReported(const QString &path, const QString &error) {
  if (error.isEmpty()) {
    if (failedWrites_.erase(path) > 0) {
      updateBanner();
    }
    return;
  }
  failedWrites_[path] = error;
  updateBanner();
}

void MainWindow::updateBanner() {
  if (!banner_) {
    return;
  }
  if (failedWrites_.empty()) {
    banner_->hide();
    return;
  }
  QStringList lines;
  for (const auto &[path, error] : failedWrites_) {
    lines << QString("<b>%1</b>: %2")
                 .arg(QFileInfo(path).fileName().toHtmlEscaped(),
                      error.toHtmlEscaped());
  }
  bannerText_->setText(
      tr("Your changes are not being saved.") + "<br>" + lines.join("<br>") +
      "<br>" +
      tr("Check that the data folder can be written to; the next save "
         "tries again."));
  banner_->show();
}

void MainWindow::buildHelpPanel() {
  helpDock_ = new QDockWidget(tr("Keyboard controls"), this);
  helpDock_->setObjectName("helpPanel");
  helpDock_->setFeatures(QDockWidget::DockWidgetClosable |
                         QDockWidget::DockWidgetMovable |
                         QDockWidget::DockWidgetFloatable);
  helpText_ = new QTextBrowser(helpDock_);
  helpText_->setFocusPolicy(Qt::NoFocus);
  helpText_->setOpenLinks(false);
  helpText_->setMinimumWidth(220);
  helpDock_->setWidget(helpText_);
  addDockWidget(Qt::RightDockWidgetArea, helpDock_);
  resizeDocks({helpDock_}, {340}, Qt::Horizontal);
  helpDock_->hide();
  connect(helpDock_, &QDockWidget::visibilityChanged, this, [this](bool on) {
    if (on) {
      updateHelp();
    }
  });
}

void MainWindow::updateHelp() {
  if (helpText_ && helpDock_->isVisible()) {
    int scroll = helpText_->verticalScrollBar()->value();
    helpText_->setHtml(controlsHtml(mode_, settings_.keymap));
    helpText_->verticalScrollBar()->setValue(scroll);
  }
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
  bool switching = mode_ >= 0;
  mode_ = mode;
  modeButtons_->button(mode)->setChecked(true);
  // Only the page shown sets the window's minimum size: the stack would
  // otherwise take the largest minimum of all of them.
  for (ModePage *page : pages_) {
    auto policy =
        page == currentPage() ? QSizePolicy::Preferred : QSizePolicy::Ignored;
    page->setSizePolicy(policy, policy);
  }
  stack_->setCurrentWidget(currentPage());
  currentPage()->activated();
  QSettings().setValue("window/mode",
                       kModes[static_cast<std::size_t>(mode)].id);
  if (switching && !sidebar_->isVisible()) {
    // Nothing else shows which mode this is.
    showOverlay(modeLabel(mode), 1500);
  }
  updateHelp();
  updateBadges();
  return true;
}

void MainWindow::updateBadges() {
  TodayPage::Totals t = todayPage_->totals();
  auto label = [&t](int m) {
    QString text = modeLabel(m);
    if (t.due > 0) {
      text += QString("  (%1)").arg(t.due);
    }
    return text;
  };
  for (int m : {static_cast<int>(Today), static_cast<int>(Learn)}) {
    auto *b = modeButtons_->button(m);
    b->setText(label(m));
    b->setToolTip(tr("%1 (%2): %n review(s) due", "", t.due)
                      .arg(modeLabel(m),
                           modeShortcut(m).toString(QKeySequence::NativeText)) +
                  (t.newLeft > 0
                       ? tr(", %n new case(s) left today", "", t.newLeft)
                       : QString()));
  }
}

void MainWindow::learnSet(const QString &setId) {
  if (!setMode(Learn)) {
    return;
  }
  // Pick the set in Learn's set list, as a click would.
  for (QComboBox *box : learnPage_->findChildren<QComboBox *>()) {
    int i = box->findData(setId);
    if (i >= 0) {
      box->setCurrentIndex(i);
      emit box->activated(i);
      return;
    }
  }
}

void MainWindow::setFullScreen(bool on) {
  setWindowState(on ? windowState() | Qt::WindowFullScreen
                    : windowState() & ~Qt::WindowFullScreen);
}

void MainWindow::setFocusMode(bool on) {
  focusMode_ = on;
  sidebar_->setVisible(!on && sidebarAction_->isChecked());
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

void MainWindow::setSidebarShown(bool on) {
  if (sidebarAction_->isChecked() != on) {
    sidebarAction_->setChecked(on); // comes back here
    return;
  }
  sidebar_->setVisible(on && !focusMode_);
  QSettings().setValue("window/sidebar", on);
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
  menu.addAction(sidebarAction_);
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
  if (ModePage *page = currentPage(); page && page->isBusy()) {
    // Ctrl+Q is a slip away from Q, a turning key.
    auto answer = QMessageBox::question(
        this, tr("Quit v-cube"),
        tr("A solve is running. Quit anyway? It won't be recorded."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
      e->ignore();
      return;
    }
  }
  QSettings q;
  q.setValue("window/geometry", saveGeometry());
  q.setValue("window/state", saveState());
  saveLayout();
  finishBackgroundWrites();
  QMainWindow::closeEvent(e);
}

void MainWindow::restoreLayout() {
  QSettings q;
  for (int m = 0; m < ModeCount; ++m) {
    if (m == Learn) {
      continue; // Learn keeps its own splitters
    }
    const auto splitters =
        pages_[static_cast<std::size_t>(m)]->findChildren<QSplitter *>();
    for (qsizetype i = 0; i < splitters.size(); ++i) {
      QByteArray state =
          q.value(QString("window/splitters/%1/%2")
                      .arg(kModes[static_cast<std::size_t>(m)].id)
                      .arg(i))
              .toByteArray();
      if (!state.isEmpty()) {
        splitters[i]->restoreState(state);
      }
    }
  }
}

void MainWindow::saveLayout() {
  QSettings q;
  for (int m = 0; m < ModeCount; ++m) {
    if (m == Learn) {
      continue; // Learn keeps its own splitters
    }
    const auto splitters =
        pages_[static_cast<std::size_t>(m)]->findChildren<QSplitter *>();
    for (qsizetype i = 0; i < splitters.size(); ++i) {
      q.setValue(QString("window/splitters/%1/%2")
                     .arg(kModes[static_cast<std::size_t>(m)].id)
                     .arg(i),
                 splitters[i]->saveState());
    }
  }
}

void MainWindow::fitToScreen() {
  if (isMaximized() || isFullScreen()) {
    return;
  }
  QScreen *screen = QGuiApplication::screenAt(geometry().center());
  if (!screen) {
    screen = QGuiApplication::primaryScreen(); // off every screen
  }
  if (!screen) {
    return;
  }
  QRect avail = screen->availableGeometry();
  QRect g = geometry();
  bool moved = !avail.intersects(g);
  if (g.width() > avail.width() || g.height() > avail.height()) {
    // Saved at another scaling: bigger than the screen now is.
    g.setSize(g.size().boundedTo(avail.size() * 0.9));
    moved = true;
  }
  if (moved) {
    g.moveCenter(avail.center());
    setGeometry(g);
  }
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
  file->addAction(tr("Export all data…"), this, &MainWindow::exportAllData);
  file->addAction(tr("Open data folder"), this, &MainWindow::openDataFolder);
  file->addSeparator();
  file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

  QMenu *edit = menuBar()->addMenu(tr("&Edit"));
  edit->addAction(tr("&Command list…"), QKeySequence(Qt::CTRL | Qt::Key_K),
                  this, &MainWindow::openCommandPalette);
  edit->addSeparator();
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
  sidebarAction_ = view->addAction(tr("&Sidebar"));
  sidebarAction_->setCheckable(true);
  sidebarAction_->setChecked(true);
  sidebarAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
  connect(sidebarAction_, &QAction::toggled, this,
          &MainWindow::setSidebarShown);
  QAction *help = helpDock_->toggleViewAction();
  help->setText(tr("&Keyboard controls"));
  help->setShortcut(QKeySequence(Qt::Key_F1));
  view->addAction(help);

  QMenu *modes = menuBar()->addMenu(tr("&Mode"));
  for (int m : kModeOrder) {
    modes->addAction(modeLabel(m), modeShortcut(m), this,
                     [this, m] { setMode(m); });
    if (m == Today || m == Library || m == MoveCount) {
      modes->addSeparator();
    }
  }

  QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
  helpMenu->addAction(help);
  helpMenu->addAction(tr("About v-cube"), this, &MainWindow::showAbout);

  // The shortcuts must keep working while focus mode hides the menu bar.
  for (QAction *menu : menuBar()->actions()) {
    addActions(menu->menu()->actions());
  }

  // Library's own menus and Undo, shown only in Library mode; their keys
  // stay with the page (they would act on it from other modes).
  QAction *first = edit->actions().value(0);
  edit->insertActions(first, libraryPage_->editActions());
  for (QMenu *menu : libraryPage_->menus()) {
    menuBar()->insertMenu(view->menuAction(), menu);
  }
}

void MainWindow::openCommandPalette() {
  if (currentPage() && currentPage()->isBusy()) {
    return;
  }
  std::vector<CommandPalette::Command> commands =
      CommandPalette::fromMenus(menuBar());
  for (auto &c : CommandPalette::fromControls(currentPage())) {
    commands.push_back(std::move(c));
  }
  CommandPalette palette(std::move(commands), this);
  palette.exec();
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

void MainWindow::exportAllData() {
  QString parent = QFileDialog::getExistingDirectory(
      this, tr("Export all data into a new folder in…"),
      QSettings().value("export/folder", QDir::homePath()).toString());
  if (parent.isEmpty()) {
    return;
  }
  QSettings().setValue("export/folder", parent);
  QString base = QString("v-cube-export-%1")
                     .arg(QDate::currentDate().toString(Qt::ISODate));
  QString target = QDir(parent).filePath(base);
  for (int n = 2; QFileInfo::exists(target); ++n) {
    target = QDir(parent).filePath(QString("%1-%2").arg(base).arg(n));
  }
  QString error;
  if (!vcube::exportAllData(dataDir_, QSettings().fileName(), target, &error)) {
    QMessageBox::warning(this, tr("Export all data"),
                         tr("The export failed: %1").arg(error));
    return;
  }
  QMessageBox::information(
      this, tr("Export all data"),
      tr("Your alg sets, progress, sessions and settings were copied to "
         "%1.\n\nTo move them to another computer, copy the data folder's "
         "contents into that computer's data folder (File > Open data "
         "folder) while v-cube is closed. Dated backups are also kept in "
         "the data folder's backups folder, and deleted sets in "
         "algsets/deleted.")
          .arg(QDir::toNativeSeparators(target)));
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
  applyWindowSettings();
  for (ModePage *page : pages_) {
    page->applySettings();
  }
  updateBadges();
}

void MainWindow::applyWindowSettings() {
  styleSidebar();
  setDisplayFontSizes(settings_.scrambleFontPt, settings_.timerFontPt);
  // The hints under the timer and cube (muted, wrapped labels) and the
  // status bar, which are otherwise small in a large window.
  double scale = settings_.hintTextPercent / 100.0;
  QFont base = QApplication::font();
  QFont hint = base;
  hint.setPointSizeF(base.pointSizeF() * scale);
  for (ModePage *page : pages_) {
    for (QLabel *label : page->findChildren<QLabel *>()) {
      // Labels given a font of their own keep it.
      bool isHint = label->property("vcubeHint").toBool() ||
                    (label->wordWrap() &&
                     label->foregroundRole() == QPalette::PlaceholderText &&
                     !label->testAttribute(Qt::WA_SetFont));
      if (isHint) {
        label->setProperty("vcubeHint", true);
        label->setFont(hint);
      }
    }
  }
  statusBar()->setFont(hint);
  if (settings_.inspectionSounds) {
    prepareSounds();
    setInspectionCallHandler([](int seconds) { playInspectionCall(seconds); });
  } else {
    setInspectionCallHandler({});
  }
  updateHelp();
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

void MainWindow::showAbout() {
  QMessageBox box(this);
  box.setWindowTitle(tr("About v-cube"));
  box.setIconPixmap(windowIcon().pixmap(64, 64));
  box.setTextFormat(Qt::RichText);
  box.setText(
      tr("<h3>v-cube %1</h3><p>Create, learn and drill 3x3 alg sets; "
         "scramble and time solves with a real or a virtual cube.</p>"
         "<p>Random-state scrambles use Kociemba's two-phase algorithm. "
         "Inspired by cstimer and Alg-Trainer.</p>"
         "<p>Data folder: <code>%2</code><br>Qt %3, platform "
         "<code>%4</code></p>")
          .arg(QApplication::applicationVersion().toHtmlEscaped(),
               QDir::toNativeSeparators(dataDir_).toHtmlEscaped(), qVersion(),
               QGuiApplication::platformName()));
  box.exec();
}

} // namespace vcube
