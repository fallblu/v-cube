#pragma once

#include "app/app_settings.hpp"

#include <QDialog>

#include <array>

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

private:
  // Reads the controls into the settings; false (with a message) if
  // something can't be read.
  bool collect();

private:
  AppSettings settings_;
  QTabWidget *tabs_;
  QComboBox *themeBox_;
  QSpinBox *animationSpin_;
  QSpinBox *scrambleFontSpin_;
  QComboBox *interfaceTextBox_;
  QComboBox *hintTextBox_;
  QComboBox *thumbnailBox_;
  std::array<QPushButton *, 6> colourButtons_{};
  QSpinBox *holdSpin_;
  QCheckBox *inspectionBox_;
  QCheckBox *soundsBox_;
  QSpinBox *decimalsSpin_;
  QSpinBox *timerFontSpin_;
  QCheckBox *autoStopBox_;
  QCheckBox *hideCubeBox_;
  QCheckBox *simplifiedBox_;
  QCheckBox *anyHoldBox_;
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
  QSpinBox *newPerDaySpin_;
  QLineEdit *learningEdit_;
  QLineEdit *relearningEdit_;
  QDoubleSpinBox *graduatingSpin_;
  QDoubleSpinBox *easySpin_;
  QSpinBox *lapseSpin_;
  QDoubleSpinBox *easeRecoverySpin_;
  QCheckBox *lateBox_;
  QCheckBox *hardBox_;
  QSpinBox *maxLearningSpin_;
  QSpinBox *learnAheadSpin_;
  QSpinBox *leechSpin_;
  QDoubleSpinBox *slowSpin_;
  DrillSettingsTab *drillTab_;

  void refreshColours();
  QWidget *buildNeutralityTab(QWidget *parent);
  void fillMainFronts();
  // Shows settings_.neutrality in the controls.
  void refreshNeutrality();
  void refreshKeys();
  // Binds the key and move entered, to the selected row or as a new one.
  void setBinding(bool changeSelected);
  // The selected binding's row, or -1.
  int selectedBinding() const;
  void updateKeyButtons();
};

} // namespace vcube
