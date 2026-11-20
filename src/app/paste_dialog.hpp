#pragma once

#include "train/case_logic.hpp"

#include <QDialog>
#include <QStringList>

#include <cstddef>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTableWidget;

namespace vcube {

// The most algs one paste may hold. Every alg is tried on every case of the
// set, on the window's own thread and again after each correction, and an
// alg that solves none may add a case for the next ones to be tried on: the
// work grows with the square of the count.
inline constexpr std::size_t kMaxPastedAlgs = 1000;

// A set's cases as the choices of "Other case…": a label for each, no two
// alike, and the case ids in the same order. Cases may share a name, so one
// whose label is there already gets a number after it.
struct CaseChoices {
  QStringList labels;
  std::vector<std::string> ids;
};
CaseChoices caseChoices(const AlgSet &set);

// Pastes a list of algs into a set in two steps: the text, then a table of
// what each alg would do (the case it goes to, or why it isn't filed),
// where algs that aren't filed can be corrected or given a case before
// anything is changed. Apply keeps plan().
class PasteDialog : public QDialog {
  Q_OBJECT
public:
  // `group`: the group new cases go to at first, or -1 for the last one.
  PasteDialog(const AlgSet &set, int group, const QString &text,
              QWidget *parent = nullptr);

  const PastePlan &plan() const { return plan_; }
  // The algs that aren't filed, as lines to paste again.
  QString leftOver() const;

private:
  const AlgSet &set_;
  std::vector<PasteItem> items_;
  PastePlan plan_;
  bool loading_ = false;

  QStackedWidget *pages_;
  QPlainTextEdit *edit_;
  QCheckBox *create_;
  QComboBox *groupBox_;
  QLabel *summary_;
  QTableWidget *table_;
  QPushButton *backButton_;
  QPushButton *checkButton_;
  QPushButton *applyButton_;

  void check();
  void replan();
  void fillTable();
  void chooseCase(int row, int index);
};

} // namespace vcube
