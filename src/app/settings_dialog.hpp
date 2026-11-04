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

namespace vcube {

class SettingsDialog : public QDialog {
  Q_OBJECT
public:
  SettingsDialog(const AppSettings &settings, QWidget *parent = nullptr);
  AppSettings result() const { return settings_; }

  void accept() override;

private:
  AppSettings settings_;
  QComboBox *themeBox_;
  QSpinBox *animationSpin_;
  std::array<QPushButton *, 6> colourButtons_{};
  QComboBox *holdBox_;
  QCheckBox *inspectionBox_;
  QSpinBox *decimalsSpin_;
  QTableWidget *keyTable_;
  QKeySequenceEdit *keyEdit_;
  QLineEdit *moveEdit_;
  QSpinBox *newPerDaySpin_;
  QLineEdit *learningEdit_;
  QLineEdit *relearningEdit_;

  void refreshColours();
  void refreshKeys();
  void setBinding();
};

} // namespace vcube
