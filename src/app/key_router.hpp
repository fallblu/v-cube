#pragma once

#include <QObject>

#include <functional>
#include <set>

class QKeyEvent;

namespace vcube {

class ModePage;

// Sends the keyboard of a window to its active mode page, installed on
// the application as an event filter: only keys aimed at the window (not
// at dialogs, menus or text fields), each event once although Qt offers an
// unused one to every parent in turn. A key held down reaches the page
// once: its auto-repeats go to the page only when the page accepts them
// (ModePage::acceptsAutoRepeat()), and are swallowed if the page used the
// press, so that no page acts on them twice and no widget gets them.
class KeyRouter : public QObject {
  Q_OBJECT
public:
  KeyRouter(QWidget *window, std::function<ModePage *()> page,
            QObject *parent = nullptr);

protected:
  bool eventFilter(QObject *obj, QEvent *event) override;

private:
  QWidget *window_;
  std::function<ModePage *()> page_;
  struct KeySeen {
    const QEvent *event = nullptr;
    int type = 0;
    int key = 0;
    quint64 timestamp = 0;
    bool operator==(const KeySeen &) const = default;
  };
  KeySeen lastKey_;
  // Where Qt offers lastKey_ next if nobody uses it (only compared).
  const QWidget *nextOffer_ = nullptr;
  // Keys whose press the page used and that haven't been let go.
  std::set<int> used_;

  bool route(QKeyEvent *e, bool press);
};

// A key that only modifies others (Shift, Ctrl, Alt, the lock keys ...):
// pressing one never stops a timer.
bool isModifierKey(int key);

} // namespace vcube
