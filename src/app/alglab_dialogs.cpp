#include "app/alglab_dialogs.hpp"
#include "app/text_style.hpp"

#include "app/alglab_common.hpp"
#include "app/app_settings.hpp"
#include "app/grip_view.hpp"
#include "app/page_parts.hpp"
#include "app/ui_text.hpp"
#include "io/algset_json.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace vcube {

namespace {

constexpr std::array<const char *, kLayerTypes> kLayerLabels = {
    "U", "R", "F", "D", "L", "B", "u", "r", "f", "d", "l", "b", "M", "E", "S"};

} // namespace

// --- MoveSetDialog ----------------------------------------------------------

MoveSetDialog::MoveSetDialog(const MoveSet &set, QWidget *parent)
    : QDialog(parent) {
  setWindowTitle(tr("Move set"));
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(new QLabel(
      tr("The turns the generator may use (and the filter allows). Quarter "
         "turns come in both directions."),
      this));
  auto *grid = new QGridLayout;
  grid->addWidget(new QLabel(tr("Layer"), this), 0, 0);
  grid->addWidget(new QLabel(tr("Quarter (R, R')"), this), 0, 1);
  grid->addWidget(new QLabel(tr("Half (R2)"), this), 0, 2);
  grid->addWidget(new QLabel(tr("Layer"), this), 0, 4);
  grid->addWidget(new QLabel(tr("Quarter"), this), 0, 5);
  grid->addWidget(new QLabel(tr("Half"), this), 0, 6);
  grid->setColumnMinimumWidth(3, 24);
  for (std::size_t i = 0; i < kLayerTypes; ++i) {
    // Faces on the left, wide turns and slices on the right.
    int n = static_cast<int>(i);
    int row = n < 6 ? n + 1 : (n < 12 ? n - 5 : n - 5);
    int col = n >= 6 && n < 12 ? 4 : 0;
    grid->addWidget(new QLabel(kLayerLabels[i], this), row, col);
    quarter_[i] = new QCheckBox(this);
    half_[i] = new QCheckBox(this);
    quarter_[i]->setChecked(set.layers[i].quarter);
    half_[i]->setChecked(set.layers[i].half);
    grid->addWidget(quarter_[i], row, col + 1);
    grid->addWidget(half_[i], row, col + 2);
    connect(quarter_[i], &QCheckBox::toggled, this, &MoveSetDialog::fromGrid);
    connect(half_[i], &QCheckBox::toggled, this, &MoveSetDialog::fromGrid);
  }
  layout->addLayout(grid);
  auto *form = new QFormLayout;
  text_ = new QLineEdit(qs(set.toString()), this);
  text_->setPlaceholderText("R, U, F2");
  form->addRow(tr("As text:"), text_);
  name_ = new QLineEdit(this);
  name_->setPlaceholderText(tr("Leave empty to use it without keeping it"));
  form->addRow(tr("Keep as preset:"), name_);
  layout->addLayout(form);
  error_ = new QLabel(this);
  layout->addWidget(error_);
  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);
  connect(text_, &QLineEdit::textEdited, this, &MoveSetDialog::fromText);
  auto check = [this, buttons] {
    buttons->button(QDialogButtonBox::Ok)->setEnabled(!moveSet().empty());
  };
  connect(text_, &QLineEdit::textChanged, this, check);
  check();
}

MoveSet MoveSetDialog::moveSet() const {
  MoveSet s;
  for (std::size_t i = 0; i < kLayerTypes; ++i) {
    s.layers[i] = {quarter_[i]->isChecked(), half_[i]->isChecked()};
  }
  return s;
}

QString MoveSetDialog::presetName() const { return name_->text().trimmed(); }

void MoveSetDialog::fromGrid() {
  if (syncing_) {
    return;
  }
  syncing_ = true;
  text_->setText(qs(moveSet().toString()));
  error_->clear();
  syncing_ = false;
}

void MoveSetDialog::fromText() {
  if (syncing_) {
    return;
  }
  auto s = MoveSet::parse(text_->text().toStdString());
  if (!s) {
    error_->setText(tr("Can't read that: write layers like R, U, F2 (2 for "
                       "half turns only, 1 for quarter turns only)."));
    return;
  }
  error_->clear();
  syncing_ = true;
  for (std::size_t i = 0; i < kLayerTypes; ++i) {
    quarter_[i]->setChecked(s->layers[i].quarter);
    half_[i]->setChecked(s->layers[i].half);
  }
  syncing_ = false;
}

// --- ProfileDialog ----------------------------------------------------------

ProfileDialog::ProfileDialog(const ErgoProfile &profile, QWidget *parent)
    : QDialog(parent) {
  setWindowTitle(tr("Ergonomic profile"));
  resize(560, 640);
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(new QLabel(
      tr("An alg's score is the sum of its moves' costs, plus a cost for "
         "each turn on another axis than the turn before and for each rule "
         "a pair of moves in a row matches. Lower is easier. A rule's side "
         "is a move (R2), a layer with any turn (R*) or any move (*)."),
      this));
  layout->itemAt(0)->widget()->setProperty("wordWrap", true);
  auto *form = new QFormLayout;
  name_ = new QLineEdit(qs(profile.name), this);
  form->addRow(tr("Name:"), name_);
  other_ = new QDoubleSpinBox(this);
  other_->setRange(0, 50);
  other_->setSingleStep(0.1);
  other_->setValue(profile.otherMove);
  form->addRow(tr("A move not in the table:"), other_);
  axis_ = new QDoubleSpinBox(this);
  axis_->setRange(0, 10);
  axis_->setSingleStep(0.05);
  axis_->setValue(profile.axisChange);
  form->addRow(tr("A turn on another axis:"), axis_);
  layout->addLayout(form);

  auto *tables = new QHBoxLayout;
  moves_ = new QTableWidget(static_cast<int>(ergoMoveTokens().size()), 2, this);
  moves_->setHorizontalHeaderLabels({tr("Move"), tr("Cost")});
  moves_->verticalHeader()->hide();
  for (std::size_t i = 0; i < ergoMoveTokens().size(); ++i) {
    const std::string &t = ergoMoveTokens()[i];
    auto *name = new QTableWidgetItem(qs(t));
    name->setFlags(name->flags() & ~Qt::ItemIsEditable);
    moves_->setItem(static_cast<int>(i), 0, name);
    auto it = profile.moves.find(t);
    moves_->setItem(static_cast<int>(i), 1,
                    new QTableWidgetItem(it == profile.moves.end()
                                             ? QString()
                                             : QString::number(it->second)));
  }
  tables->addWidget(moves_, 1);

  auto *ruleBox = new QVBoxLayout;
  rules_ = new QTableWidget(static_cast<int>(profile.rules.size()), 3, this);
  rules_->setHorizontalHeaderLabels({tr("First"), tr("Then"), tr("Cost")});
  rules_->verticalHeader()->hide();
  for (std::size_t i = 0; i < profile.rules.size(); ++i) {
    const ErgoRule &r = profile.rules[i];
    int row = static_cast<int>(i);
    rules_->setItem(row, 0, new QTableWidgetItem(qs(r.first)));
    rules_->setItem(row, 1, new QTableWidgetItem(qs(r.second)));
    rules_->setItem(row, 2, new QTableWidgetItem(QString::number(r.cost)));
  }
  ruleBox->addWidget(rules_, 1);
  auto *ruleButtons = new QHBoxLayout;
  auto *add = new QPushButton(tr("Add rule"), this);
  auto *remove = new QPushButton(tr("Remove rule"), this);
  ruleButtons->addWidget(add);
  ruleButtons->addWidget(remove);
  ruleBox->addLayout(ruleButtons);
  tables->addLayout(ruleBox, 1);
  layout->addLayout(tables, 1);
  connect(add, &QPushButton::clicked, this, [this] {
    int row = rules_->rowCount();
    rules_->insertRow(row);
    rules_->setItem(row, 0, new QTableWidgetItem("*"));
    rules_->setItem(row, 1, new QTableWidgetItem("*"));
    rules_->setItem(row, 2, new QTableWidgetItem("0"));
    rules_->editItem(rules_->item(row, 0));
  });
  connect(remove, &QPushButton::clicked, this, [this] {
    if (rules_->currentRow() >= 0) {
      rules_->removeRow(rules_->currentRow());
      updateTry();
    }
  });

  auto *tryRow = new QHBoxLayout;
  tryAlg_ = new QLineEdit("R U R' U'", this);
  tryScore_ = new QLabel(this);
  tryRow->addWidget(new QLabel(tr("Try an alg:"), this));
  tryRow->addWidget(tryAlg_, 1);
  tryRow->addWidget(tryScore_);
  layout->addLayout(tryRow);
  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);
  connect(tryAlg_, &QLineEdit::textChanged, this, &ProfileDialog::updateTry);
  connect(moves_, &QTableWidget::itemChanged, this, &ProfileDialog::updateTry);
  connect(rules_, &QTableWidget::itemChanged, this, &ProfileDialog::updateTry);
  connect(other_, &QDoubleSpinBox::valueChanged, this,
          &ProfileDialog::updateTry);
  connect(axis_, &QDoubleSpinBox::valueChanged, this,
          &ProfileDialog::updateTry);
  updateTry();
}

ErgoProfile ProfileDialog::profile() const {
  ErgoProfile p;
  p.name = name_->text().trimmed().toStdString();
  p.otherMove = other_->value();
  p.axisChange = axis_->value();
  for (int r = 0; r < moves_->rowCount(); ++r) {
    bool ok = false;
    double v =
        moves_->item(r, 1) ? moves_->item(r, 1)->text().toDouble(&ok) : 0.0;
    if (ok && validCost(v)) {
      p.moves[moves_->item(r, 0)->text().toStdString()] = v;
    }
  }
  for (int r = 0; r < rules_->rowCount(); ++r) {
    auto text = [&](int c) {
      return rules_->item(r, c) ? rules_->item(r, c)->text().trimmed()
                                : QString();
    };
    bool ok = false;
    double cost = text(2).toDouble(&ok);
    if (!text(0).isEmpty() && !text(1).isEmpty() && ok && validCost(cost)) {
      p.rules.push_back({text(0).toStdString(), text(1).toStdString(), cost});
    }
  }
  return p;
}

void ProfileDialog::updateTry() {
  auto parsed = parseAlg(tryAlg_->text().toStdString());
  if (!parsed) {
    tryScore_->setText(tr("can't read it"));
    return;
  }
  ErgoScore s = ergoScore(parsed.alg, profile());
  tryScore_->setText(tr("score %1 (moves %2, pairs %3)")
                         .arg(s.total, 0, 'f', 2)
                         .arg(s.moves, 0, 'f', 2)
                         .arg(s.transitions, 0, 'f', 2));
}

// --- GripProfileDialog ------------------------------------------------------

namespace {

enum TrickCol : int {
  TOn,
  TMove,
  THand,
  TFinger,
  THow,
  TFrom,
  TTurn,
  TCost,
  TReset,
  TColCount
};

} // namespace

GripProfileDialog::GripProfileDialog(const ErgoProfile &profile,
                                     QWidget *parent)
    : QDialog(parent), base_(profile) {
  setWindowTitle(tr("Grip profile"));
  resize(900, 760);
  const GripModel g = profile.grips.value_or(standardGripModel());
  auto *layout = new QVBoxLayout(this);
  auto *intro = new QLabel(
      tr("An alg's score is the cheapest way to turn it with these tricks: "
         "each move gets a trick, and a hand regrips between moves when the "
         "next trick needs another grip. A grip is where the hand's thumb "
         "sits as the wrist turns it: home, up or down a quarter turn (the "
         "right thumb goes up with R, the left thumb down with L), far up "
         "or far down. A trick turns its move from the grips listed under "
         "From and moves its hand's thumb by Turn steps; a rotation lets go "
         "with both hands, which take the cube again home. Lower is "
         "easier."),
      this);
  intro->setWordWrap(true);
  layout->addWidget(intro);

  auto *top = new QHBoxLayout;
  auto *form1 = new QFormLayout;
  auto *form2 = new QFormLayout;
  name_ = new QLineEdit(qs(profile.name), this);
  form1->addRow(tr("Name:"), name_);
  auto cost = [&](QFormLayout *form, const QString &label,
                  double GripModel::*value, double max = 20) {
    auto *b = new QDoubleSpinBox(this);
    b->setRange(0, max);
    b->setSingleStep(0.05);
    b->setDecimals(2);
    b->setValue(g.*value);
    form->addRow(label, b);
    costs_.emplace_back(value, b);
    connect(b, &QDoubleSpinBox::valueChanged, this,
            &GripProfileDialog::updateTry);
  };
  cost(form1, tr("A regrip (a pause):"), &GripModel::regrip);
  cost(form1, tr("A regrip while the other hand turns:"),
       &GripModel::hiddenRegrip);
  cost(form1, tr("Each regrip step past the first:"), &GripModel::regripStep);
  cost(form2, tr("Each move with a hand far up or down:"), &GripModel::strain);
  cost(form2, tr("Each step from home to start in:"), &GripModel::start);
  cost(form2, tr("A finger turning two moves in a row:"),
       &GripModel::sameFinger);
  cost(form2, tr("A move no trick turns:"), &GripModel::otherMove, 50);
  top->addLayout(form1);
  top->addLayout(form2);
  layout->addLayout(top);

  tricks_ = new QTableWidget(0, TColCount, this);
  tricks_->setHorizontalHeaderLabels({tr("On"), tr("Move"), tr("Hand"),
                                      tr("Finger"), tr("How"), tr("From"),
                                      tr("Turn"), tr("Cost"), tr("Rotation")});
  tricks_->horizontalHeaderItem(TFrom)->setToolTip(
      tr("The grips the trick starts from, such as \"home, up\": far down, "
         "down, home, up, far up (or -2 .. 2), or \"any\""));
  tricks_->horizontalHeaderItem(TTurn)->setToolTip(
      tr("Steps the hand's thumb moves: R is +1 for the right hand, L is -1 "
         "for the left"));
  tricks_->horizontalHeaderItem(TReset)->setToolTip(
      tr("Both hands let go and take the cube again home (x, y, z)"));
  tricks_->verticalHeader()->hide();
  tricks_->horizontalHeader()->setSectionResizeMode(THow, QHeaderView::Stretch);
  for (const GripTrick &t : g.tricks) {
    addTrickRow(t);
  }
  tricks_->resizeColumnsToContents();
  layout->addWidget(tricks_, 1);
  // The columns' syntax, which the headers' tooltips alone would hide.
  layout->addWidget(helpLabel(
      tr("From: the grips the trick starts from, such as \"home, up\": far "
         "down, down, home, up, far up (or -2 .. 2), or \"any\". Turn: steps "
         "the hand's thumb moves, R +1 for the right hand, L -1 for the "
         "left. Rotation: both hands let go and take the cube again home."),
      this));
  auto *trickButtons = new QHBoxLayout;
  auto *add = new QPushButton(tr("Add trick"), this);
  auto *remove = new QPushButton(tr("Remove trick"), this);
  auto *standard = new QPushButton(tr("Standard tricks"), this);
  standard->setToolTip(tr("Put back v-cube's standard tricks and costs"));
  problems_ = new QLabel(this);
  problems_->setForegroundRole(QPalette::PlaceholderText);
  trickButtons->addWidget(add);
  trickButtons->addWidget(remove);
  trickButtons->addWidget(standard);
  trickButtons->addWidget(problems_, 1);
  layout->addLayout(trickButtons);
  connect(add, &QPushButton::clicked, this, [this] {
    GripTrick t;
    t.move = "U";
    t.how = "flick";
    t.from = gripBit(0);
    addTrickRow(t);
    tricks_->scrollToBottom();
    tricks_->editItem(tricks_->item(tricks_->rowCount() - 1, TMove));
    updateTry();
  });
  connect(remove, &QPushButton::clicked, this, [this] {
    if (tricks_->currentRow() >= 0) {
      tricks_->removeRow(tricks_->currentRow());
      updateTry();
    }
  });
  connect(standard, &QPushButton::clicked, this, [this] {
    GripModel s = standardGripModel();
    tricks_->setRowCount(0);
    for (const GripTrick &t : s.tricks) {
      addTrickRow(t);
    }
    for (auto [value, box] : costs_) {
      box->setValue(s.*value);
    }
    updateTry();
  });

  auto *tryRow = new QHBoxLayout;
  tryAlg_ = new QLineEdit("R U R' U' R' F R2 U' R' U' R U R' F'", this);
  tryRow->addWidget(new QLabel(tr("Try an alg:"), this));
  tryRow->addWidget(tryAlg_, 1);
  layout->addLayout(tryRow);
  tryView_ = new GripView(this);
  layout->addWidget(tryView_);

  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);

  connect(tryAlg_, &QLineEdit::textChanged, this,
          &GripProfileDialog::updateTry);
  connect(tricks_, &QTableWidget::itemChanged, this,
          &GripProfileDialog::updateTry);
  updateTry();
}

void GripProfileDialog::addTrickRow(const GripTrick &t) {
  QSignalBlocker block(tricks_);
  int row = tricks_->rowCount();
  tricks_->insertRow(row);
  auto check = [](bool on) {
    auto *i = new QTableWidgetItem;
    i->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                Qt::ItemIsUserCheckable);
    i->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    return i;
  };
  tricks_->setItem(row, TOn, check(t.on));
  tricks_->setItem(row, TMove, new QTableWidgetItem(qs(t.move)));
  auto *hand = new QComboBox(tricks_);
  for (Hand h : {Hand::Right, Hand::Left, Hand::Both, Hand::Either}) {
    hand->addItem(qs(handId(h)));
  }
  hand->setCurrentIndex(static_cast<int>(t.hand));
  tricks_->setCellWidget(row, THand, hand);
  auto *finger = new QComboBox(tricks_);
  for (int f = 0; f <= static_cast<int>(Finger::Hands); ++f) {
    finger->addItem(qs(fingerId(static_cast<Finger>(f))));
  }
  finger->setCurrentIndex(static_cast<int>(t.finger));
  tricks_->setCellWidget(row, TFinger, finger);
  for (QComboBox *b : {hand, finger}) {
    connect(b, &QComboBox::currentIndexChanged, this,
            &GripProfileDialog::updateTry);
  }
  tricks_->setItem(row, THow, new QTableWidgetItem(qs(t.how)));
  tricks_->setItem(row, TFrom, new QTableWidgetItem(qs(gripMaskText(t.from))));
  tricks_->setItem(row, TTurn, new QTableWidgetItem(QString::number(t.turn)));
  tricks_->setItem(row, TCost, new QTableWidgetItem(QString::number(t.cost)));
  tricks_->setItem(row, TReset, check(t.reset));
}

ErgoProfile GripProfileDialog::profile() const {
  ErgoProfile p = base_;
  p.name = name_->text().trimmed().toStdString();
  GripModel g;
  for (auto [value, box] : costs_) {
    g.*value = box->value();
  }
  for (int r = 0; r < tricks_->rowCount(); ++r) {
    auto text = [&](int c) {
      return tricks_->item(r, c) ? tricks_->item(r, c)->text().trimmed()
                                 : QString();
    };
    auto combo = [&](int c) {
      auto *b = qobject_cast<QComboBox *>(tricks_->cellWidget(r, c));
      return b ? b->currentIndex() : 0;
    };
    auto parsed = parseAlg(text(TMove).toStdString());
    auto from = gripMaskFromText(text(TFrom).toStdString());
    bool turnOk = false, costOk = false;
    int turn = text(TTurn).toInt(&turnOk);
    double cost = text(TCost).toDouble(&costOk);
    if (!parsed || parsed.alg.size() != 1 || !from || !turnOk || !costOk ||
        turn < -4 || turn > 4 || !validCost(cost)) {
      continue;
    }
    GripTrick t;
    t.move = toString(parsed.alg.front());
    t.hand = static_cast<Hand>(combo(THand));
    t.finger = static_cast<Finger>(combo(TFinger));
    t.how = text(THow).toStdString();
    t.from = *from;
    t.turn = turn;
    t.cost = cost;
    t.on = tricks_->item(r, TOn)->checkState() == Qt::Checked;
    t.reset = tricks_->item(r, TReset)->checkState() == Qt::Checked;
    g.tricks.push_back(std::move(t));
  }
  p.otherMove = g.otherMove;
  p.grips = std::move(g);
  return p;
}

void GripProfileDialog::updateTry() {
  ErgoProfile p = profile();
  int bad = tricks_->rowCount() - static_cast<int>(p.grips->tricks.size());
  problems_->setText(
      bad > 0 ? plural(bad, tr("%n row can't be read and will be left out"),
                       tr("%n rows can't be read and will be left out")) +
                    tr(" (a move such as R2, grips such as \"home, up\", a "
                       "whole number turn and a cost).")
              : QString());
  tryView_->setAlg(tryAlg_->text(), &*p.grips, tr("Type an alg to try."));
}

// --- LabCaseDialog ----------------------------------------------------------

LabCaseDialog::LabCaseDialog(const LabCase &c, const AlgLibrary &library,
                             const AppSettings &settings, QWidget *parent)
    : QDialog(parent), case_(c), settings_(settings) {
  setWindowTitle(c.name.empty() ? tr("New lab case") : tr("Lab case"));
  auto *layout = new QHBoxLayout(this);
  auto *left = new QVBoxLayout;
  auto *form = new QFormLayout;
  name_ = new QLineEdit(qs(c.name), this);
  form->addRow(tr("Name:"), name_);
  setup_ = new QLineEdit(qs(c.setup), this);
  setup_->setPlaceholderText(tr("done to a solved cube, makes the case"));
  form->addRow(tr("Setup alg:"), setup_);
  goal_ = new QComboBox(this);
  goal_->addItem(tr("Solved"), QString());
  goal_->addItem(tr("Last layer oriented (OLL)"), "LL:p");
  goal_->addItem(tr("F2L solved (last layer free)"), "LL:po");
  for (const AlgSet &s : library.sets()) {
    goal_->addItem(tr("As %1 has it").arg(qs(s.name)),
                   qs(freeToString(s.spec.free)));
  }
  goal_->addItem(tr("Custom…"), "custom");
  form->addRow(tr("Algs must leave:"), goal_);
  free_ = new QLineEdit(qs(c.free), this);
  free_->setPlaceholderText(tr("free pieces, e.g. LL:p or LLE:o UF:p"));
  form->addRow(tr("Free pieces:"), free_);
  before_ = new QCheckBox(tr("A U turn may come before the alg"), this);
  before_->setChecked(c.aufBefore);
  after_ = new QCheckBox(tr("A U turn may come after the alg"), this);
  after_->setChecked(c.aufAfter);
  form->addRow(before_);
  form->addRow(after_);
  left->addLayout(form);
  error_ = new QLabel(this);
  error_->setWordWrap(true);
  left->addWidget(error_);
  left->addStretch();
  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  ok_ = buttons->button(QDialogButtonBox::Ok);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  left->addWidget(buttons);
  layout->addLayout(left, 1);
  diagram_ = new QLabel(this);
  diagram_->setFixedSize(scaledToText(kLargeDiagram),
                         scaledToText(kLargeDiagram));
  diagram_->setAlignment(Qt::AlignCenter);
  layout->addWidget(diagram_, 0, Qt::AlignTop);

  int found = goal_->findData(qs(c.free));
  goal_->setCurrentIndex(found >= 0 ? found : goal_->count() - 1);
  free_->setEnabled(goal_->currentData().toString() == "custom");
  connect(goal_, &QComboBox::currentIndexChanged, this,
          &LabCaseDialog::goalChosen);
  connect(name_, &QLineEdit::textChanged, this, &LabCaseDialog::validate);
  connect(setup_, &QLineEdit::textChanged, this, &LabCaseDialog::validate);
  connect(free_, &QLineEdit::textChanged, this, &LabCaseDialog::validate);
  validate();
}

void LabCaseDialog::goalChosen() {
  QString chosen = goal_->currentData().toString();
  bool custom = chosen == "custom";
  free_->setEnabled(custom);
  if (!custom) {
    free_->setText(chosen);
  }
  validate();
}

LabCase LabCaseDialog::labCase() const {
  LabCase c = case_;
  c.name = name_->text().trimmed().toStdString();
  c.setup = cleanAlgText(setup_->text().toStdString());
  auto m = parseFree(free_->text().toStdString());
  c.free = m ? freeToString(*m) : std::string();
  c.aufBefore = before_->isChecked();
  c.aufAfter = after_->isChecked();
  return c;
}

void LabCaseDialog::validate() {
  QString why;
  auto parsed = parseAlg(setup_->text().toStdString());
  if (name_->text().trimmed().isEmpty()) {
    why = tr("Give the case a name.");
  } else if (setup_->text().trimmed().isEmpty()) {
    why = tr("Type the setup alg that makes the case from a solved cube.");
  } else if (!parsed) {
    why = tr("Can't read the setup alg at character %1.")
              .arg(parsed.error->position + 1);
  } else if (!parseFree(free_->text().toStdString())) {
    why = tr("Can't read the free pieces: write e.g. LL:p, LLC:o or F2L:po.");
  }
  error_->setText(why);
  ok_->setEnabled(why.isEmpty());
  if (why.isEmpty()) {
    LabCase c = labCase();
    ResolvedCase r;
    r.state = labCaseState(c);
    r.free = labCaseFree(c);
    r.spec.free = r.free;
    PieceMask ll;
    ll.lastLayer(Free::PermAndOri, Free::PermAndOri);
    r.spec.view =
        withinMask(r.state, ll) ? DiagramView::TopLayer : DiagramView::Iso3D;
    diagram_->setPixmap(labDiagram(r, settings_, scaledToText(kLargeDiagram),
                                   devicePixelRatioF()));
  } else {
    diagram_->clear();
  }
}

} // namespace vcube
