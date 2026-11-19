#pragma once

#include <QLabel>
#include <QList>
#include <QString>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <functional>

class QButtonGroup;
class QComboBox;
class QFormLayout;
class QPushButton;
class QHBoxLayout;
class QVBoxLayout;

namespace vcube {

class CubeView;
class FlowLayout;
class NetPreview;
class TimeDisplay;
class TrendChart;

// The parts every mode's page is built from, so that the pages look and
// behave alike: the bar of options above the stage and the line of key
// hints below it.

// The row of options at the top of a page: labelled pickers, buttons and
// stretches, wrapping onto further lines in a narrow window. Hidden in
// focus mode by the page.
class OptionBar : public QWidget {
  Q_OBJECT
public:
  explicit OptionBar(QWidget *parent = nullptr);

  // Adds `label` and `control` as one item (the label is the control's
  // buddy, which also names it in the command list); returns the item, to
  // show and hide the two together.
  QWidget *addPicker(const QString &label, QWidget *control, int stretch = 0);
  // "View:" and the drop-down that switches `view` (see
  // createViewSelector).
  QWidget *addViewPicker(CubeView *view, const QString &mode);
  void addWidget(QWidget *w, int stretch = 0);
  void addSpacing(int size = 12);
  void addStretch(int stretch = 1);

private:
  FlowLayout *layout_;
};

// A picker's options (a combo box's items, a SegmentedPicker's) carry the
// enum value they stand for, so that nothing depends on their order in the
// picker.
template <typename Enum> QVariant enumData(Enum value) {
  return static_cast<int>(value);
}
// The chosen option's value; `none` when nothing is chosen.
template <typename Enum, typename Picker>
Enum currentEnum(const Picker *picker, Enum none) {
  QVariant data = picker->currentData();
  return data.isValid() ? static_cast<Enum>(data.toInt()) : none;
}
// Chooses the option that stands for `value`, or the first if none does.
template <typename Enum, typename Picker>
void selectEnum(Picker *picker, Enum value) {
  picker->setCurrentIndex(std::max(0, picker->findData(enumData(value))));
}

// A choice among a few named options shown side by side, the chosen one
// pressed in: for a choice worth seeing at a glance (what Learn practises),
// where a drop-down would hide the alternatives.
class SegmentedPicker : public QWidget {
  Q_OBJECT
public:
  explicit SegmentedPicker(QWidget *parent = nullptr);
  void addOption(const QString &text, const QVariant &value = {});
  int currentIndex() const;
  void setCurrentIndex(int index);
  QVariant currentData() const;
  int findData(const QVariant &value) const;

signals:
  // The user chose an option (not setCurrentIndex).
  void activated(int index);

private:
  QButtonGroup *group_;
  QHBoxLayout *layout_;
  QList<QVariant> data_;
};

// A key and what it does now, e.g. {"Space", "next case"}. With `click`
// the chip can be clicked to do the same (in place of a button for it).
struct KeyHint {
  QString key;
  QString action;
  std::function<void()> click = {};
};

// The keys that do something now, as a line of chips (see keyHintsHtml);
// the chips of keys with a click action can be clicked. Hidden while there
// are none.
class KeyStrip : public QLabel {
  Q_OBJECT
public:
  explicit KeyStrip(QWidget *parent = nullptr);

  void setKeys(const QList<KeyHint> &keys);
  const QList<KeyHint> &keys() const { return keys_; }

protected:
  void changeEvent(QEvent *e) override;

private:
  QList<KeyHint> keys_;
  void render();
};

// The line under the stage that says what is going on and which keys do
// what: a message, then each key as a chip with its action. Follows the
// hint text size of the settings.
class HintBar : public QLabel {
  Q_OBJECT
public:
  explicit HintBar(QWidget *parent = nullptr);

  void setHint(const QString &message, const QList<KeyHint> &keys = {});
  QString message() const { return message_; }
  const QList<KeyHint> &keys() const { return keys_; }

protected:
  void changeEvent(QEvent *e) override;

private:
  QString message_;
  QList<KeyHint> keys_;
  void render();
};

// A short explanation shown under a control. Tooltips are never shown in
// the app (see MainWindow), so what a control needs said is said here.
QLabel *helpLabel(const QString &text, QWidget *parent);

// A button that doesn't take the keyboard focus.
QPushButton *flatButton(const QString &text, QWidget *parent);
// Writes the tooltip of each field of the form under it as a helpLabel.
void showTipsAsHelp(QFormLayout *form);

// The right-hand side of a mode that solves scrambles (Timed, Untimed,
// Move Count): the option bar, then, as one block centred in the height,
// the scramble with its net, the cube, the readout, a hint, the keys, the
// focus-mode statistics and a row of buttons, and the session's chart at
// the bottom. The page fills the
// option bar, adds its buttons and wires everything.
class SolveStage : public QWidget {
  Q_OBJECT
public:
  // `mode` names the saved view of the cube ("timed", "untimed", ...).
  explicit SolveStage(const QString &mode, QWidget *parent = nullptr);

  OptionBar *options;
  QLabel *scramble;
  NetPreview *net;
  CubeView *cube;
  TimeDisplay *display;
  QLabel *hint;
  QLabel *focusStats; // shown by the page in focus mode
  // The keys that do something now, as chips under the hint.
  void setKeys(const QList<KeyHint> &keys);
  QVBoxLayout *centre;
  // The cube's share of the height left in the block, against 1 above
  // and 1 below the block (3 unless set).
  void setCubeStretch(int stretch);

  // The options every solve mode ends its bar with: the session's cube,
  // the view (returned, to hide on a physical cube) and "Skip scramble".
  QComboBox *cubeBox;
  QWidget *viewPicker;
  QPushButton *skipButton;
  void addStandardOptions();

  // A button in the centred row under the hint, after those there or
  // (`first`) before them.
  void addButton(QWidget *button, bool first = false);
  // A widget above the cube.
  void addAboveCube(QWidget *w);
  // A widget under the buttons (still centred with the cube).
  void addUnderButtons(QWidget *w);
  // A widget across the stage between the centre and the chart.
  void addAboveChart(QWidget *w);
  void setChart(TrendChart *chart);

private:
  QVBoxLayout *layout_;
  QHBoxLayout *buttons_;
  KeyStrip *keys_;
  TrendChart *chart_ = nullptr;
  QString mode_;
};

// The rich text of key chips alone, for a label that words its own
// message: "[Space] next · [Esc] reset". A key with a click action is a
// link ("chip:<index>"), its action in the link colour.
QString keyHintsHtml(const QList<KeyHint> &keys, const QPalette &palette);

} // namespace vcube
