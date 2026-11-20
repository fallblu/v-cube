#pragma once

#include "app/app_settings.hpp"

#include <QDialog>

#include <array>
#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QKeySequenceEdit;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTabWidget;
class QLabel;

namespace vcube {

class DrillSettingsTab;

class SettingsDialog : public QDialog {
  Q_OBJECT
public:
  SettingsDialog(const AppSettings &settings, QWidget *parent = nullptr);
  AppSettings result() const { return settings_; }
  // Opens on the colour neutrality tab.
  void showNeutralityTab();

  void accept() override;

signals:
  // Apply was pressed: the settings as the dialog now has them.
  void applied(const AppSettings &settings);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void changeEvent(QEvent *event) override;

private:
  // Reads the controls into the settings; false (with a message) if
  // something can't be read.
  bool collect();
  // Shows `field` in `widget` (a check box, a spin box or a combo box of
  // values), and has collect() read it back.
  template <typename Widget, typename T>
  Widget *bound(Widget *widget, T &field);

private:
  AppSettings settings_;
  std::vector<std::function<void()>> readBacks_;
  QTabWidget *tabs_;
  std::array<QPushButton *, 6> colourButtons_{};
  QComboBox *mainTopBox_;
  QComboBox *mainFrontBox_;
  QComboBox *presetBox_;
  std::array<QCheckBox *, 36> holdBoxes_{}; // [top * 6 + front], or null
  std::array<QLabel *, 12> holdSwatches_{}; // front header, then top rows
  QLabel *holdSummary_;
  QTableWidget *keyTable_;
  QKeySequenceEdit *keyEdit_;
  QLineEdit *moveEdit_;
  QPushButton *changeButton_;
  QPushButton *removeButton_;
  QLineEdit *learningEdit_;
  QLineEdit *relearningEdit_;
  QSpinBox *lapseSpin_;
  QSpinBox *learnAheadSpin_;
  QDoubleSpinBox *slowSpin_;
  DrillSettingsTab *drillTab_;

  void refreshColours();
  void styleColourButtons();
  QWidget *buildNeutralityTab(QWidget *parent);
  void fillMainFronts();
  // Shows settings_.neutrality in the controls.
  void refreshNeutrality();
  void refreshKeys();
  // Binds the key and move entered, to the selected row or as a new one.
  enum class Bind { ChangeSelected, AddNew };
  void setBinding(Bind bind);
  // The selected binding's row, or -1.
  int selectedBinding() const;
  void updateKeyButtons();
};

} // namespace vcube
