#pragma once

#include "app/app_settings.hpp"
#include "app/bld_scheme_dialog.hpp"
#include "app/colours.hpp"
#include "app/cube_view.hpp"
#include "app/session_panel.hpp"
#include "io/blind_store.hpp"
#include "train/blind_flow.hpp"

#include <QObject>
#include <QString>
#include <QStringList>

#include <array>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

class QComboBox;
class QKeyEvent;
class QLabel;
class QPushButton;

namespace vcube {

class NetPreview;
class ScrambleFeed;
class TimeDisplay;

// What the Blind page shares with its drills: the data, the lettering and
// the widgets in the middle of the page, which every drill uses in turn.
struct BlindContext {
  BlindContext(AppSettings &appSettings, BlindStore &blindStore,
               BlindScheme &blindScheme, ScrambleFeed &scrambleFeed,
               Rng &random, QWidget *pageWidget,
               std::function<void()> saveStore)
      : settings(appSettings), store(blindStore), scheme(blindScheme),
        feed(scrambleFeed), rng(random), page(pageWidget),
        save(std::move(saveStore)) {}

  AppSettings &settings;
  BlindStore &store;
  BlindScheme &scheme;
  ScrambleFeed &feed;
  Rng &rng;
  QWidget *page;
  // Writes the store in the background.
  std::function<void()> save;

  // The letter last asked in the quiz or the setup drill (not asked twice
  // in a row).
  std::optional<LetterChoice> lastLetter;

  // The middle of the page.
  CubeView *cube = nullptr;
  QLabel *prompt = nullptr;
  TimeDisplay *display = nullptr;
  QLabel *hint = nullptr;
  QLabel *result = nullptr;
  QLabel *scramble = nullptr; // the scramble's text
  NetPreview *net = nullptr;  // the scramble's net

  // The page or the window is away.
  bool away() const;
};

// One drill of the Blind page, on a tab of its own. It brings its own
// widgets for the left column, the options row and under the hint, which
// the page lays out and shows while it is the one picked, and runs the
// page's middle while it is.
class BlindDrill : public QObject {
  Q_OBJECT
public:
  explicit BlindDrill(BlindContext &context, QObject *parent = nullptr);

  // The drill's widgets for the page to lay out: the left column, the
  // items of the options row, and a row under the hint (or none).
  virtual QWidget *leftPanel() const = 0;
  virtual std::vector<QWidget *> options() const { return {}; }
  virtual QWidget *inputRow() const { return nullptr; }

  // What of the page's middle the drill shows.
  struct Layout {
    bool cube = true;
    bool net = false;      // the scramble's net (in place of the cube)
    bool scramble = false; // the scramble's text
    bool viewPicker = true;
    bool chart = false; // the trend chart (outside focus mode)
  };
  virtual Layout layout() const { return {}; }
  // The cube view's mode, when not the one picked.
  struct View {
    enum class Kind { Chosen, Net, Hidden };
    Kind kind = Kind::Chosen;
    QString hiddenText; // for Hidden
  };
  virtual View view() const { return {}; }

  // Picked, or picked again (an option, the lettering or the session
  // changed): starts afresh. `changed`: another drill was picked before.
  // The page has cleared the prompt, the result and the cube's marks.
  virtual void enter(bool changed) = 0;
  // Another drill was picked.
  virtual void leave() {}
  virtual bool handleKey(QKeyEvent *e, bool press) = 0;
  // A solve is under way: the page's controls are locked.
  virtual bool isBusy() const { return false; }
  // The page was shown or hidden.
  virtual void activated() {}
  virtual void deactivated() {}
  // The page or the window went away, or came back: running clocks stop.
  virtual void pause() {}
  virtual void resume() {}
  // The left column and the hint, afresh.
  virtual void refreshLeft() = 0;
  virtual void showHint() = 0;
  // The feed's scramble changed.
  virtual void scrambleChanged() {}
  // A sticker of the cube view was clicked.
  virtual void stickerClicked(int facelet) { (void)facelet; }

signals:
  void message(const QString &text);
  // The page applies the cube view's mode (see view()) again.
  void viewChanged();
  // isBusy() may have changed: the page locks or unlocks its controls.
  void busyChanged();
  // The page picks the drill again (see enter()).
  void restartRequested();

protected:
  BlindContext &ctx_;

  QString letterOf(BldPiece kind, int sticker) const;
  // "G (LD)": a sticker's letter and name.
  QString stickerText(BldPiece kind, int sticker) const;
  // stickerText(), HTML-escaped for rich text.
  QString stickerHtml(BldPiece kind, int sticker) const;
  // Done: turns the cube shown as a whole, marks and all.
  void look(Move m);
};

// --- shared pieces of the drills -------------------------------------------

inline const StatusColour kBlindRight{Status::Good};
inline const StatusColour kBlindWrong{Status::Bad};
inline constexpr std::array<BldPiece, 2> kBlindKinds = {BldPiece::Edge,
                                                        BldPiece::Corner};

QString blindTimeText(StatValue ms, int decimals = 2);
// A typed or scheme letter as letters are compared (see foldLetter()).
QString foldedLetter(const QString &s);
// A combo box that doesn't take the keyboard focus.
QComboBox *blindComboBox(const QStringList &items, QWidget *parent);
// A label and a widget kept together, so that both can be hidden.
// A column of a drill's table, sharing the width with the others.
AttemptColumn blindColumn(const QString &id, const QString &title);
QWidget *labelledBox(const QString &label, QWidget *w, QWidget *parent);
// "<p style='color:…'>text</p>".
QString colouredHtml(const QColor &colour, const QString &html);

// Which piece types a letter drill asks about.
enum class BlindPieces { Edges, Corners, Both };
QComboBox *blindPiecesBox(QWidget *parent);

// The left column of a drill without sessions: the statistics, a table of
// rows and the Reset statistics button.
class BlindStatsPanel : public QWidget {
  Q_OBJECT
public:
  explicit BlindStatsPanel(bool resetButton, QWidget *parent = nullptr);
  SessionPanel *panel() const { return panel_; }
  QPushButton *resetButton() const { return reset_; }

private:
  SessionPanel *panel_;
  QPushButton *reset_ = nullptr;
};

// Fills a letter drill's table and statistics: a row per letter asked
// (`setups`: those of the setup drill) of the piece types picked.
void showLetterStats(SessionPanel *panel, const LetterStats &stats,
                     BlindPieces pieces, bool setups,
                     const LetterScheme &letters, int decimals,
                     const QColor &muted);

} // namespace vcube
