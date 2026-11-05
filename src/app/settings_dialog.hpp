#pragma once

#include "app/app_settings.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTabWidget;
class QLabel;

namespace vcube {

class SettingsDialog : public QDialog {
  Q_OBJECT
public:
  SettingsDialog(const AppSettings &settings, QWidget *parent = nullptr);
  AppSettings result() const { return settings_; }
  // Opens on the colour neutrality tab.
  void showNeutralityTab();

  void accept() override;

private:
  AppSettings settings_;
  QTabWidget *tabs_;
  QComboBox *themeBox_;
  QSpinBox *animationSpin_;
  std::array<QPushButton *, 6> colourButtons_{};
  QComboBox *holdBox_;
  QCheckBox *inspectionBox_;
  QSpinBox *decimalsSpin_;
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
