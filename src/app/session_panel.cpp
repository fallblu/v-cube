#include "app/session_panel.hpp"

#include "app/page_parts.hpp"
#include "app/review_dialog.hpp"
#include "app/text_style.hpp"
#include "app/ui_state.hpp"
#include "app/widgets.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

namespace vcube {

// --- AttemptTableModel -----------------------------------------------------

void AttemptTableModel::setColumns(std::vector<AttemptColumn> columns) {
  beginResetModel();
  columns_ = std::move(columns);
  endResetModel();
}

void AttemptTableModel::setRows(std::vector<AttemptRow> rows) {
  beginResetModel();
  rows_ = std::move(rows);
  endResetModel();
}

void AttemptTableModel::prependRow(AttemptRow row) {
  beginInsertRows({}, 0, 0);
  rows_.insert(rows_.begin(), std::move(row));
  endInsertRows();
}

int AttemptTableModel::attemptAt(int row) const {
  if (row < 0 || row >= static_cast<int>(rows_.size())) {
    return -1;
  }
  return rows_[static_cast<std::size_t>(row)].index;
}

int AttemptTableModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int AttemptTableModel::columnCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : static_cast<int>(columns_.size());
}

QVariant AttemptTableModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() >= static_cast<int>(rows_.size())) {
    return {};
  }
  const AttemptRow &row = rows_[static_cast<std::size_t>(index.row())];
  if (role == Qt::UserRole) {
    return row.index;
  }
  if (index.column() >= static_cast<int>(row.cells.size())) {
    return {};
  }
  const AttemptCell &cell = row.cells[static_cast<std::size_t>(index.column())];
  switch (role) {
  case Qt::DisplayRole:
    return cell.text;
  case Qt::ForegroundRole:
    return cell.colour.isValid() ? QVariant(cell.colour) : QVariant();
  case Qt::TextAlignmentRole:
    return cell.alignRight
               ? QVariant(Qt::Alignment(Qt::AlignRight | Qt::AlignVCenter))
               : QVariant(Qt::Alignment(Qt::AlignLeft | Qt::AlignVCenter));
  default:
    return {};
  }
}

QVariant AttemptTableModel::headerData(int section, Qt::Orientation orientation,
                                       int role) const {
  if (orientation != Qt::Horizontal || section < 0 ||
      section >= static_cast<int>(columns_.size())) {
    return QAbstractTableModel::headerData(section, orientation, role);
  }
  const AttemptColumn &c = columns_[static_cast<std::size_t>(section)];
  if (role == Qt::DisplayRole) {
    return c.title;
  }
  return {};
}

// --- SessionPanel ------------------------------------------------------------

SessionPanel::SessionPanel(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  sessionButton_ = new QToolButton(this);
  sessionButton_->setFocusPolicy(Qt::NoFocus);
  sessionButton_->setPopupMode(QToolButton::InstantPopup);
  sessionButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  sessionButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  sessionButton_->setToolTip(tr("Switch, add, rename or delete sessions"));
  menu_ = new QMenu(sessionButton_);
  sessionButton_->setMenu(menu_);
  layout->addWidget(sessionButton_);

  stats_ = new QLabel(this);
  stats_->setTextFormat(Qt::RichText);
  useTabularFigures(stats_); // the figures don't shift as they change
  stats_->setWordWrap(true);
  stats_->setFocusPolicy(Qt::NoFocus);
  stats_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(stats_);

  model_ = new AttemptTableModel(this);
  table_ = new QTableView(this);
  table_->setModel(model_);
  table_->verticalHeader()->setVisible(false);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setFocusPolicy(Qt::NoFocus);
  table_->setContextMenuPolicy(Qt::CustomContextMenu);
  table_->setWordWrap(false);
  table_->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
  // Columns sharing the width keep room for a time (a narrow panel
  // scrolls rather than cutting them short).
  table_->horizontalHeader()->setMinimumSectionSize(
      table_->fontMetrics().horizontalAdvance(QStringLiteral("88.88")) + 12);
  layout->addWidget(table_, 1);

  deleteRow_ = new QWidget(this);
  auto *row = new QHBoxLayout(deleteRow_);
  row->setContentsMargins(0, 0, 0, 0);
  deleteButton_ = flatButton(tr("Delete last"), deleteRow_);
  deleteButton_->setToolTip(tr("Delete the last solve (Delete)"));
  undoButton_ = flatButton(tr("Undo delete"), deleteRow_);
  undoButton_->setToolTip(tr("Put back the solve last deleted (Ctrl+Z)"));
  undoButton_->setVisible(false);
  row->addWidget(deleteButton_);
  row->addWidget(undoButton_);
  row->addStretch();
  deleteRow_->setVisible(false);
  layout->addWidget(deleteRow_);

  chart_ = new TrendChart(this);
  chart_->setVisible(false); // until the page lays it out

  connect(menu_, &QMenu::aboutToShow, this, &SessionPanel::menuAboutToShow);
  connect(stats_, &QLabel::linkActivated, this, &SessionPanel::statActivated);
  connect(table_, &QTableView::doubleClicked, this,
          [this](const QModelIndex &index) {
            emit attemptActivated(model_->attemptAt(index.row()));
          });
  connect(table_, &QWidget::customContextMenuRequested, this,
          [this](const QPoint &pos) {
            QModelIndex index = table_->indexAt(pos);
            if (index.isValid()) {
              emit attemptMenuRequested(model_->attemptAt(index.row()),
                                        table_->viewport()->mapToGlobal(pos));
            }
          });
  connect(table_->horizontalHeader(), &QWidget::customContextMenuRequested,
          this, &SessionPanel::showColumnMenu);
  connect(deleteButton_, &QPushButton::clicked, this,
          &SessionPanel::deleteLastClicked);
  connect(undoButton_, &QPushButton::clicked, this, &SessionPanel::undoClicked);
}

void SessionPanel::setSessionName(const QString &name) {
  sessionButton_->setText(tr("Session: %1").arg(name));
}

void SessionPanel::fillSessionMenu(const std::vector<MenuSession> &sessions,
                                   std::size_t current) {
  // clear() deletes the actions, which leave the group as they go: the
  // one group serves every filling.
  menu_->clear();
  if (!sessionGroup_) {
    sessionGroup_ = new QActionGroup(menu_);
  }
  for (std::size_t i = 0; i < sessions.size(); ++i) {
    // In one pass, so that a "%1" in a session's name stays as it is.
    QAction *a = menu_->addAction(
        tr("%1 (%2)").arg(sessions[i].name, QString::number(sessions[i].count)),
        this, [this, i] { emit sessionChosen(i); });
    a->setCheckable(true);
    a->setChecked(i == current);
    sessionGroup_->addAction(a);
  }
  menu_->addSeparator();
  menu_->addAction(tr("New session…"), this,
                   &SessionPanel::newSessionRequested);
  menu_->addAction(tr("Rename…"), this, &SessionPanel::renameSessionRequested);
  menu_->addAction(tr("Delete…"), this, &SessionPanel::deleteSessionRequested);
}

void SessionPanel::setMenuEnabled(bool on) {
  for (QAction *a : menu_->actions()) {
    a->setEnabled(on);
  }
}

void SessionPanel::setStatsClickable(bool on) {
  statsClickable_ = on;
  stats_->setWordWrap(!on);
  stats_->setTextInteractionFlags(on ? Qt::TextSelectableByMouse |
                                           Qt::LinksAccessibleByMouse
                                     : Qt::TextSelectableByMouse);
  stats_->setToolTip(on ? tr("Click a statistic to see its solves")
                        : QString());
}

void SessionPanel::setStatsHtml(const QString &html) { stats_->setText(html); }

void SessionPanel::setColumns(std::vector<AttemptColumn> columns) {
  model_->setColumns(std::move(columns));
  applyColumns();
}

void SessionPanel::setRows(std::vector<AttemptRow> rows) {
  model_->setRows(std::move(rows));
}

void SessionPanel::prependRow(AttemptRow row) {
  model_->prependRow(std::move(row));
}

void SessionPanel::setHiddenColumnsKey(const QString &key) {
  hiddenKey_ = key;
  table_->horizontalHeader()->setToolTip(
      key.isEmpty() ? QString() : tr("Right-click to choose the columns"));
  applyColumns();
}

void SessionPanel::applyColumns() {
  QStringList hidden;
  if (!hiddenKey_.isEmpty()) {
    hidden = saved::hiddenSessionColumns(hiddenKey_).get();
  }
  QHeaderView *header = table_->horizontalHeader();
  const auto &columns = model_->columns();
  for (std::size_t i = 0; i < columns.size(); ++i) {
    const AttemptColumn &c = columns[i];
    int section = static_cast<int>(i);
    header->setSectionResizeMode(section, c.fit ? QHeaderView::ResizeToContents
                                                : QHeaderView::Stretch);
    table_->setColumnHidden(section, !c.available ||
                                         (c.hideable && hidden.contains(c.id)));
  }
}

void SessionPanel::showColumnMenu(const QPoint &pos) {
  if (hiddenKey_.isEmpty()) {
    return;
  }
  QMenu menu(this);
  fillColumnMenu(menu);
  menu.exec(table_->horizontalHeader()->mapToGlobal(pos));
}

void SessionPanel::fillMenu(QMenu &menu) {
  if (statsClickable_) {
    // The links of the statistics, as "Current ao5", "Best mo3" ...
    QMenu *stats = menu.addMenu(tr("See the solves of"));
    static const QRegularExpression link("href='([a-z]+):([^']+)'");
    for (auto it = link.globalMatch(stats_->text()); it.hasNext();) {
      auto m = it.next();
      QString kind = m.captured(1), id = m.captured(2);
      QString text = kind == "best"  ? tr("Best %1").arg(id)
                     : kind == "cur" ? tr("Current %1").arg(id)
                                     : tr("Session mean");
      QString href = kind + ":" + id;
      stats->addAction(text, this, [this, href] { emit statActivated(href); });
    }
    stats->setEnabled(!stats->isEmpty());
  }
  if (!hiddenKey_.isEmpty()) {
    fillColumnMenu(*menu.addMenu(tr("Columns")));
  }
}

void SessionPanel::fillColumnMenu(QMenu &menu) {
  QStringList hidden = saved::hiddenSessionColumns(hiddenKey_).get();
  for (const AttemptColumn &c : model_->columns()) {
    if (!c.hideable) {
      continue;
    }
    QAction *a = menu.addAction(c.title);
    a->setCheckable(true);
    a->setChecked(!hidden.contains(c.id));
    a->setEnabled(c.available);
    connect(a, &QAction::toggled, this, [this, id = c.id](bool on) {
      QStringList h = saved::hiddenSessionColumns(hiddenKey_).get();
      h.removeAll(id);
      if (!on) {
        h << id;
      }
      saved::hiddenSessionColumns(hiddenKey_).set(h);
      applyColumns();
    });
  }
}

void SessionPanel::setDeleteRowVisible(bool on) { deleteRow_->setVisible(on); }

void SessionPanel::setDeleteEnabled(bool on) { deleteButton_->setEnabled(on); }

void SessionPanel::setUndoVisible(bool on) { undoButton_->setVisible(on); }

void SessionPanel::setUndoEnabled(bool on) { undoButton_->setEnabled(on); }

// --- shared pieces -----------------------------------------------------------

QString statLinkHtml(const QString &href, StatValue v, const QString &text,
                     const QString &colour) {
  if (!v || colour.isEmpty()) {
    return text;
  }
  return QString("<a href='%1' style='text-decoration:none; color:%3'>%2</a>")
      .arg(href, text, colour);
}

QString statsTableHtml(
    const SessionStats &stats,
    const std::function<QString(const std::string &id)> &label,
    const std::function<QString(const std::string &id, StatValue v)> &format,
    const QString &linkColour) {
  QString html =
      "<table cellspacing='4' width='100%'><tr><th></th><th align='right'>" +
      QObject::tr("current") + "</th><th align='right'>" + QObject::tr("best") +
      "</th></tr>";
  for (const StatLine &line : stats.lines) {
    QString id = QString::fromStdString(line.id);
    html += QString("<tr><td>%1</td><td align='right'>%2</td><td "
                    "align='right'><b>%3</b></td></tr>")
                .arg(label(line.id),
                     statLinkHtml("cur:" + id, line.current,
                                  format(line.id, line.current), linkColour),
                     statLinkHtml("best:" + id, line.best,
                                  format(line.id, line.best), linkColour));
  }
  return html + "</table>";
}

void openStatLink(QWidget *parent, const QString &link,
                  std::span<const Solve> solves, const StatLinkView &view) {
  QStringList parts = link.split(':');
  if (parts.size() != 2 || solves.empty()) {
    return;
  }
  bool best = parts[0] == "best";
  QString id = parts[1];
  auto results = [&](std::size_t from) {
    return [&view, from](std::size_t i) { return view.result(from + i); };
  };
  if (parts[0] == "mean") {
    showTextDialog(
        parent, QObject::tr("Session mean"),
        statListing(QObject::tr("Mean of %1: %2")
                        .arg(solves.size())
                        .arg(view.format(summarize(solves, view.unit).mean)),
                    solves, 1, false, results(0)));
    return;
  }
  std::optional<StatId> stat = parseStatId(id.toStdString());
  if (!stat) {
    return;
  }
  std::optional<StatSpan> span = statSpan(
      solves, *stat, best ? StatPick::Best : StatPick::Current, view.unit);
  if (!span) {
    return;
  }
  if (stat->kind == StatId::Kind::Single) {
    view.details(span->from);
    return;
  }
  bool mean = stat->kind == StatId::Kind::Mean;
  auto part = solves.subspan(span->from, span->count);
  StatValue value = mean ? meanOf(part, view.unit) : averageOf(part, view.unit);
  QString heading =
      (mean ? QObject::tr("Mean of %1: %2") : QObject::tr("Average of %1: %2"))
          .arg(span->count)
          .arg(view.format(value));
  showTextDialog(parent,
                 best ? QObject::tr("Best %1").arg(id)
                      : QObject::tr("Current %1").arg(id),
                 statListing(heading, part, view.number(span->from), !mean,
                             results(span->from)));
}

std::optional<QString> askSessionName(QWidget *parent, const QString &title,
                                      const QString &label,
                                      const QString &suggested) {
  bool ok = false;
  QString name = QInputDialog::getText(parent, title, label, QLineEdit::Normal,
                                       suggested, &ok);
  if (!ok || name.trimmed().isEmpty()) {
    return std::nullopt;
  }
  return name.trimmed();
}

bool confirmDeleteSession(QWidget *parent, const QString &name,
                          std::size_t count, const QString &noun) {
  return QMessageBox::question(
             parent, QObject::tr("Delete session"),
             QObject::tr("Delete session \"%1\" and its %2 %3?")
                 .arg(name)
                 .arg(count)
                 .arg(noun)) == QMessageBox::Yes;
}

std::optional<QString> askComment(QWidget *parent, const QString &label,
                                  const QString &comment) {
  bool ok = false;
  QString text = QInputDialog::getText(parent, QObject::tr("Comment"), label,
                                       QLineEdit::Normal, comment, &ok);
  if (!ok) {
    return std::nullopt;
  }
  return text;
}

QAction *ActionMenu::add(const QString &text, std::function<void()> run) {
  QAction *a = addAction(text);
  runs_.emplace_back(a, std::move(run));
  return a;
}

void ActionMenu::run(const QPoint &globalPos,
                     const std::function<bool()> &stillOk) {
  QAction *chosen = exec(globalPos);
  if (!chosen || (stillOk && !stillOk())) {
    return;
  }
  for (const auto &[action, f] : runs_) {
    if (action == chosen && f) {
      f();
      return;
    }
  }
}

void addAttemptActions(ActionMenu &menu, const AttemptMenuActions &actions) {
  if (actions.details) {
    menu.add(QObject::tr("Details…"), actions.details);
  }
  if (actions.comment) {
    menu.add(QObject::tr("Comment…"), actions.comment);
  }
  if (actions.copyApart) {
    menu.addSeparator();
  }
  QString scramble = actions.scramble;
  menu.add(QObject::tr("Copy scramble"),
           [scramble] { QApplication::clipboard()->setText(scramble); });
  if (!actions.solution.isEmpty()) {
    QString solution = actions.solution;
    menu.add(QObject::tr("Copy solution"),
             [solution] { QApplication::clipboard()->setText(solution); });
  }
  if (actions.remove) {
    menu.addSeparator();
    menu.add(QObject::tr("Delete"), actions.remove);
  }
}

} // namespace vcube
