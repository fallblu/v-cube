#pragma once

#include "app/app_settings.hpp"
#include "app/bld_scheme_dialog.hpp"
#include "app/cube_view.hpp"
#include "app/mode_page.hpp"
#include "app/scramble_source.hpp"
#include "app/widgets.hpp"
#include "io/blind_store.hpp"
#include "io/library.hpp"
#include "train/move_count.hpp"

#include <QTimer>

#include <optional>

class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QTableWidget;
class QToolButton;

namespace vcube {

// Blind mode: blindfolded solving with Old Pochmann, in four drills.
//
// - Letter quiz: a sticker of the solved cube is ringed and its letter
//   typed, or a letter is shown and its sticker clicked on the net.
// - Memo tracing: the memo of a scrambled cube is typed and checked by
//   doing it, so any valid cycle break counts.
// - Setup moves: the setup for a letter is done on the virtual cube (the
//   ringed sticker must reach the swap spot without moving the buffer or
//   the swap alg's other pieces) or typed.
// - Blind solves: memo with the cube shown, then execution with it hidden
//   (virtual cube), or a timer with a memo split (physical cube), in
//   sessions.
//
// The letters belong to the stickers as the cube is held for blind (the
// blind hold, white top and green front unless changed), and the cube is
// shown in that hold. The quiz and setup drills weigh the next letter
// towards the slow and missed ones.
class BlindPage : public ModePage {
  Q_OBJECT
public:
  // Sessions and drill statistics are kept in `dataDir`/blind.json.
  BlindPage(AppSettings &settings, const QString &dataDir,
            QWidget *parent = nullptr);

  bool handleKey(QKeyEvent *e, bool press) override;
  void applySettings() override;
  bool isBusy() const override;
  void activated() override;
  void deactivated() override;
  void windowDeactivated() override;
  void setFocusMode(bool on) override;

protected:
  bool eventFilter(QObject *obj, QEvent *event) override;

private:
  enum class Drill { Quiz, Tracing, Setups, Solves };
  // Which piece types a drill asks about.
  enum class Pieces { Edges, Corners, Both };
  // Letter quiz: name the ringed sticker's letter, or click the letter's
  // sticker.
  enum class Direction { ToLetter, ToSticker, Mixed };
  // Memo tracing and blind solves: waiting to start, then (tracing)
  // looking and typing or (solves) memo and execution, then the result.
  enum class Phase { Ready, Memo, Executing, Done };

  // A letter asked about in the quiz or the setup drill.
  struct Item {
    BldPiece kind = BldPiece::Edge;
    int sticker = 0;
    bool toLetter = true; // quiz: type the letter (else click the sticker)
    std::int64_t shownAt = 0;
    bool answered = false;
    bool right = false;
  };

  AppSettings &settings_;
  BlindStore store_;
  BlindScheme scheme_;
  AlgLibrary noSets_; // the scramble feed needs one; WCA scrambles only
  ScrambleFeed feed_;
  Rng rng_;
  Drill drill_ = Drill::Quiz;

  // Letter quiz and setup moves.
  std::optional<Item> item_;
  std::optional<Item> last_; // not asked twice in a row
  QString typed_;
  QTimer nextTimer_; // on to the next letter after a right answer
  // Setup moves on the cube: the cube turned from solved, and each
  // sticker's starting facelet at every facelet (to follow the target).
  FaceletCube setupCube_;
  FaceletCube setupIds_;
  Alg setupMoves_;

  // Memo tracing and blind solves.
  Phase phase_ = Phase::Ready;
  FaceletCube heldStart_; // the scramble as held for blind (solver frame)
  CubieCube heldCubie_;
  FaceletCube traceCube_; // turned as a whole while tracing
  VirtualAttempt attempt_;
  std::int64_t startMs_ = 0;  // monotonic, when memo (or tracing) began
  std::int64_t memoMs_ = 0;   // memo time of a blind solve
  bool waitingTrace_ = false; // tracing starts when the scramble is ready
  QTimer tick_;

  QWidget *leftPanel_;
  QToolButton *sessionButton_;
  QMenu *sessionMenu_;
  QLabel *stats_;
  QTableWidget *table_;
  QPushButton *resetStatsButton_;
  QWidget *optionsRow_;
  QComboBox *drillBox_;
  QComboBox *piecesBox_;
  QComboBox *directionBox_;
  QComboBox *setupModeBox_;
  QComboBox *cubeBox_;
  QWidget *viewPicker_;
  QComboBox *viewBox_;
  QPushButton *schemeButton_;
  QLabel *scrambleLabel_;
  NetPreview *net_;
  QLabel *promptLabel_;
  CubeView *cubeView_;
  TimeDisplay *display_;
  QLabel *hintLabel_;
  QWidget *traceRow_;
  QLineEdit *edgesEdit_;
  QLineEdit *cornersEdit_;
  QWidget *setupRow_;
  QLineEdit *setupEdit_;
  QLabel *resultLabel_;
  TrendChart *chart_;

  BlindSession &session() { return store_.current(); }
  bool onVirtualCube() const;
  Pieces pieces() const;
  Direction direction() const;
  bool setupsOnCube() const;
  QString letterOf(BldPiece kind, int sticker) const;
  // "G (LD)": a sticker's letter and name.
  QString stickerText(BldPiece kind, int sticker) const;

  // Shows the widgets and starts what the drill needs.
  void selectDrill(Drill d);
  // The cube view's mode: the net for the quiz, hidden while the cube
  // should be, else the view picked.
  void applyView();
  void updateControls();
  void showHint();
  void refreshLeft();
  void save();

  // Letter quiz and setup moves.
  // A sticker for the drill, weighted towards slow and missed ones.
  Item pickItem(bool setups);
  void nextItem();
  void showItem();
  void answerLetter(const QString &text);
  void answerSticker(int facelet);
  void finishItem(bool right, const QString &feedback);
  void skipItem();
  void resetSetupCube();
  void turnSetup(Move m);
  void checkTypedSetup();
  void showSetupCube();

  // Memo tracing.
  void startTracing();
  void focusTrace();
  void checkTrace();

  // Blind solves.
  void showScramble();
  void startMemo();
  void startExecution();
  void stopSolve();
  void cancelSolve();
  void recordSolve(std::int64_t timeMs, std::int64_t memoMs, bool dnf,
                   const std::string &solution);
  void next();
  void onTick();
  void refreshSessionMenu();
  void selectSession(std::size_t index);
  void showAttemptMenu(const QPoint &pos);
  void addSession();
  void renameSession();
  void deleteSession();

  void editScheme();
};

} // namespace vcube
