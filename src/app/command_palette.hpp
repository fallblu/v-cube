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
// drop-downs' items, buttons, tick boxes and button menus). Typing filters
// it; Enter runs the selected command.
class CommandPalette : public QDialog {
  Q_OBJECT
public:
  struct Command {
    QString text;
    QString shortcut; // shown on the right, may be empty
    std::function<void()> run;
  };

  CommandPalette(std::vector<Command> commands, QWidget *parent);

  // The menu bar's enabled commands, as "Menu: Command".
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
};

} // namespace vcube
