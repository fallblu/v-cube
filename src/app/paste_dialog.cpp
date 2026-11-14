#include "app/paste_dialog.hpp"
#include "app/ui_text.hpp"

#include "app/colours.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace vcube {

namespace {

const auto &kPass = kPassColour;
const auto &kWarn = kWarnColour;
const auto &kFail = kFailColour;

enum Column { LineColumn, AlgColumn, CaseColumn, ResultColumn, ColumnCount };

// Combo entries after the candidates.
constexpr int kNotAdded = -1;
constexpr int kOtherCase = -2;

} // namespace

PasteDialog::PasteDialog(const AlgSet &set, int group, const QString &text,
                         QWidget *parent)
    : QDialog(parent), set_(set) {
  setWindowTitle(tr("Paste algs into %1").arg(qs(set.name)));
  auto *layout = new QVBoxLayout(this);
  pages_ = new QStackedWidget(this);
  layout->addWidget(pages_, 1);

  // --- step 1: the text ---
  auto *first = new QWidget(pages_);
  auto *firstLayout = new QVBoxLayout(first);
  firstLayout->setContentsMargins(0, 0, 0, 0);
  auto *help = new QLabel(
      tr("One case per line: \"Name: alg / alg\", or name and algs in "
         "tab-separated columns as copied from a spreadsheet. Each alg goes "
         "to the case its line names when it solves it; without a name, to "
         "the case it solves (any AUF or angle). Nothing changes until you "
         "check the result and apply it."),
      first);
  help->setWordWrap(true);
  firstLayout->addWidget(help);
  edit_ = new QPlainTextEdit(first);
  edit_->setPlaceholderText(
      "Ua: M2 U M U2 M' U M2 / R U' R U R U R U' R' U' R2\n"
      "T: R U R' U' R' F R2 U' R' U' R U R' F'");
  edit_->setPlainText(text);
  firstLayout->addWidget(edit_, 1);
  auto *createRow = new QHBoxLayout;
  create_ = new QCheckBox(tr("Make new cases for algs that solve no case, in"),
                          first);
  create_->setChecked(!set.builtin && !set.reduction);
  create_->setEnabled(!set.reduction);
  if (set.reduction) {
    create_->setToolTip(
        tr("A set solved a step at a time gets no new cases from pasted "
           "algs: a step doesn't finish its case, so it can't tell which "
           "case it is"));
  }
  groupBox_ = new QComboBox(first);
  for (const auto &g : set.groups) {
    groupBox_->addItem(qs(g.name));
  }
  if (set.groups.empty()) {
    groupBox_->addItem(tr("Cases"));
  }
  groupBox_->setCurrentIndex(group >= 0 && group < groupBox_->count()
                                 ? group
                                 : groupBox_->count() - 1);
  groupBox_->setEnabled(create_->isChecked());
  createRow->addWidget(create_);
  createRow->addWidget(groupBox_);
  createRow->addStretch();
  firstLayout->addLayout(createRow);
  pages_->addWidget(first);

  // --- step 2: the result per alg ---
  auto *second = new QWidget(pages_);
  auto *secondLayout = new QVBoxLayout(second);
  secondLayout->setContentsMargins(0, 0, 0, 0);
  summary_ = new QLabel(second);
  summary_->setWordWrap(true);
  secondLayout->addWidget(summary_);
  table_ = new QTableWidget(second);
  table_->setColumnCount(ColumnCount);
  table_->setHorizontalHeaderLabels(
      {tr("Line"), tr("Alg"), tr("Case"), tr("Result")});
  table_->verticalHeader()->setVisible(false);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setEditTriggers(QAbstractItemView::DoubleClicked |
                          QAbstractItemView::EditKeyPressed |
                          QAbstractItemView::AnyKeyPressed);
  table_->horizontalHeader()->setSectionResizeMode(
      LineColumn, QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setSectionResizeMode(AlgColumn,
                                                   QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(
      CaseColumn, QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setSectionResizeMode(ResultColumn,
                                                   QHeaderView::Stretch);
  secondLayout->addWidget(table_, 1);
  pages_->addWidget(second);

  auto *buttons = new QDialogButtonBox(this);
  backButton_ = buttons->addButton(tr("< Back"), QDialogButtonBox::ActionRole);
  checkButton_ =
      buttons->addButton(tr("Check >"), QDialogButtonBox::ActionRole);
  applyButton_ = buttons->addButton(tr("Apply"), QDialogButtonBox::AcceptRole);
  buttons->addButton(QDialogButtonBox::Cancel);
  layout->addWidget(buttons);

  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(checkButton_, &QPushButton::clicked, this, &PasteDialog::check);
  connect(backButton_, &QPushButton::clicked, this, [this] {
    pages_->setCurrentIndex(0);
    backButton_->setVisible(false);
    checkButton_->setVisible(true);
    applyButton_->setVisible(false);
    checkButton_->setDefault(true);
    edit_->setFocus();
  });
  connect(create_, &QCheckBox::toggled, groupBox_, &QWidget::setEnabled);
  connect(table_, &QTableWidget::itemChanged, this,
          [this](QTableWidgetItem *item) {
            if (loading_ || item->column() != AlgColumn) {
              return;
            }
            auto row = static_cast<std::size_t>(item->row());
            if (row < items_.size()) {
              items_[row].alg = cleanAlgText(item->text().toStdString());
              // Not from inside the signal of an item that replan deletes.
              QMetaObject::invokeMethod(this, &PasteDialog::replan,
                                        Qt::QueuedConnection);
            }
          });

  backButton_->setVisible(false);
  applyButton_->setVisible(false);
  checkButton_->setDefault(true);
  resize(820, 520);
}

void PasteDialog::check() {
  items_ = readPaste(edit_->toPlainText().toStdString());
  pages_->setCurrentIndex(1);
  backButton_->setVisible(true);
  checkButton_->setVisible(false);
  applyButton_->setVisible(true);
  applyButton_->setDefault(true);
  replan();
  table_->scrollToTop();
}

void PasteDialog::replan() {
  plan_ = planPaste(set_, items_, create_->isChecked(),
                    static_cast<std::size_t>(groupBox_->currentIndex()));
  fillTable();
  const ImportReport &r = plan_.report;
  QStringList parts;
  if (r.added) {
    parts << (r.added == 1 ? tr("1 alg goes to its case")
                           : tr("%1 algs go to their cases").arg(r.added));
  }
  if (r.created) {
    parts << counted(r.created, tr("new case"), tr("new cases"));
  }
  if (r.duplicates) {
    parts << tr("%n already there", nullptr, r.duplicates);
  }
  int left = static_cast<int>(r.unmatched.size() + r.ambiguous.size() +
                              r.invalid.size());
  QString text =
      parts.isEmpty() ? tr("Nothing to add.") : parts.join(", ") + ".";
  if (left > 0) {
    text += " " + tr("<b>%1 won't be added</b> as they stand: edit the alg "
                     "(double-click it) or choose its case.")
                      .arg(counted(left, tr("alg"), tr("algs")));
  }
  if (items_.empty()) {
    text = tr("No algs found in the text.");
  }
  summary_->setText(text);
  applyButton_->setEnabled(r.added + r.created > 0);
}

void PasteDialog::fillTable() {
  loading_ = true;
  int scroll = table_->verticalScrollBar()->value();
  table_->clearContents();
  table_->setRowCount(static_cast<int>(items_.size()));
  for (std::size_t i = 0; i < items_.size(); ++i) {
    const PasteItem &item = items_[i];
    const PasteResult &r = plan_.results[i];
    int row = static_cast<int>(i);
    auto *line = new QTableWidgetItem(QString::number(item.line));
    line->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    line->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    if (!item.name.empty()) {
      line->setToolTip(tr("Named %1").arg(qs(item.name)));
    }
    table_->setItem(row, LineColumn, line);
    auto *alg = new QTableWidgetItem(qs(item.alg));
    // Algs that are filed stay as they are; the rest can be corrected.
    bool open = !r.filed() && r.outcome != PasteOutcome::Duplicate;
    alg->setFlags(open ? Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                             Qt::ItemIsEditable
                       : Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    if (open) {
      alg->setToolTip(tr("Double-click to correct the alg"));
    }
    table_->setItem(row, AlgColumn, alg);

    QString result;
    QColor colour;
    switch (r.outcome) {
    case PasteOutcome::Added:
      result = tr("✓ adds to %1").arg(qs(r.reason));
      colour = r.reason.find(", but") != std::string::npos ? kWarn : kPass;
      break;
    case PasteOutcome::NewCase:
      result = tr("✓ %1").arg(qs(r.reason));
      colour = kPass;
      break;
    case PasteOutcome::Duplicate:
      result = tr("= %1").arg(qs(r.reason));
      colour = palette().color(QPalette::PlaceholderText);
      break;
    case PasteOutcome::Ambiguous:
    case PasteOutcome::Mismatch:
      result = tr("? %1: choose a case").arg(qs(r.reason));
      colour = kWarn;
      break;
    case PasteOutcome::Unmatched:
    case PasteOutcome::Invalid:
      result = tr("✗ %1").arg(qs(r.reason));
      colour = kFail;
      break;
    }
    auto *resultItem = new QTableWidgetItem(result);
    resultItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    resultItem->setForeground(colour);
    resultItem->setToolTip(result);
    table_->setItem(row, ResultColumn, resultItem);

    bool choosable =
        r.outcome != PasteOutcome::Invalid && (open || !item.caseId.empty());
    if (!choosable) {
      const AlgCase *c = plan_.set.findCase(r.caseId);
      auto *caseItem = new QTableWidgetItem(c ? qs(c->name) : QString());
      caseItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
      table_->setItem(row, CaseColumn, caseItem);
      continue;
    }
    // A case for it: the ones it solves first, any other on request.
    auto *combo = new QComboBox(table_);
    combo->addItem(tr("(not added)"), kNotAdded);
    std::vector<std::string> ids = r.candidates;
    if (!item.caseId.empty() &&
        std::find(ids.begin(), ids.end(), item.caseId) == ids.end()) {
      ids.insert(ids.begin(), item.caseId);
    }
    for (const std::string &id : ids) {
      if (const AlgCase *c = set_.findCase(id)) {
        combo->addItem(qs(c->name), qs(id));
      }
    }
    combo->addItem(tr("Other case…"), kOtherCase);
    int current = item.caseId.empty() ? 0 : combo->findData(qs(item.caseId));
    combo->setCurrentIndex(std::max(0, current));
    connect(combo, &QComboBox::activated, this,
            [this, row](int index) { chooseCase(row, index); });
    table_->setCellWidget(row, CaseColumn, combo);
  }
  table_->verticalScrollBar()->setValue(scroll);
  loading_ = false;
}

void PasteDialog::chooseCase(int row, int index) {
  auto *combo = qobject_cast<QComboBox *>(table_->cellWidget(row, CaseColumn));
  auto r = static_cast<std::size_t>(row);
  if (!combo || r >= items_.size()) {
    return;
  }
  QVariant choice = combo->itemData(index);
  std::string chosen;
  if (choice.typeId() == QMetaType::QString) {
    chosen = choice.toString().toStdString();
  } else if (choice.toInt() == kOtherCase) {
    QStringList names;
    std::vector<std::string> ids;
    for (const auto &g : set_.groups) {
      for (const auto &c : g.cases) {
        names << tr("%1 (%2)").arg(qs(c.name), qs(g.name));
        ids.push_back(c.id);
      }
    }
    if (names.isEmpty()) {
      QMetaObject::invokeMethod(this, &PasteDialog::replan,
                                Qt::QueuedConnection);
      return;
    }
    bool ok = false;
    QString name = QInputDialog::getItem(
        this, tr("Choose a case"), tr("Add the alg to:"), names, 0, false, &ok);
    if (!ok) { // back to what it was
      QMetaObject::invokeMethod(this, &PasteDialog::replan,
                                Qt::QueuedConnection);
      return;
    }
    chosen = ids[static_cast<std::size_t>(names.indexOf(name))];
  }
  items_[r].caseId = chosen;
  // Rebuilding the table deletes the combo that sent this signal.
  QMetaObject::invokeMethod(this, &PasteDialog::replan, Qt::QueuedConnection);
}

QString PasteDialog::leftOver() const {
  QStringList lines;
  for (std::size_t i = 0; i < items_.size() && i < plan_.results.size(); ++i) {
    const PasteResult &r = plan_.results[i];
    if (r.filed() || r.outcome == PasteOutcome::Duplicate) {
      continue;
    }
    const PasteItem &item = items_[i];
    lines << (item.name.empty() ? qs(item.alg)
                                : qs(item.name) + ": " + qs(item.alg));
  }
  return lines.join("\n");
}

} // namespace vcube
