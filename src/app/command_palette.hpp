#pragma once

#include <QDialog>

#include <functional>
#include <vector>

class QLineEdit;
class QListWidget;
class QMenuBar;

namespace vcube {

// A searchable list of everything the window can do from the keyboard
// (Ctrl+K): the menu commands, and the choices of the page shown (its
// drop-downs' items, buttons, tick boxes and button menus), under a
// heading per group (Go, this page, the other menus) with their keys on
// the right. With nothing typed the commands last run come first. Typing
// filters it; Enter runs the selected command.
class CommandPalette : public QDialog {
  Q_OBJECT
public:
  struct Command {
    QString text;
    QString shortcut; // shown on the right, may be empty
    std::function<void()> run;
    // The heading it is listed under: for a menu command, the menu's
    // name until the window says otherwise.
    QString group = {};
  };

  CommandPalette(std::vector<Command> commands, QWidget *parent);

  // The menu bar's enabled commands, as "Menu: Command", each in the
  // group of its menu's name.
  static std::vector<Command> fromMenus(QMenuBar *bar);
  // The visible, enabled controls in `page`.
  static std::vector<Command> fromControls(QWidget *page);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  std::vector<Command> commands_;
  QLineEdit *search_;
  QListWidget *list_;

  void filter();
  void runSelected();
  // The next selectable row from `row` going `step` (a heading is
  // skipped), or `row` if there is none.
  int nextRow(int row, int step) const;
  // How many of the last commands run are remembered.
  static constexpr int kRecent = 5;
};

} // namespace vcube
