#pragma once

#include "app/colours.hpp"
#include "cube/hold.hpp"
#include "train/bld.hpp"

#include <QDialog>

#include <array>
#include <optional>
#include <string>

class QComboBox;
class QLabel;
class QTabWidget;
class QTableWidget;

namespace vcube {

class CubeView;

// The user's blind settings, kept in QSettings under "blind/": the
// lettering scheme, their own setup moves (by sticker; empty means the
// computed one) and the hold blind solves are done in, which the letters
// belong to.
struct BlindScheme {
  LetterScheme letters = LetterScheme::speffz();
  std::array<std::string, kBldStickers> edgeSetups{};
  std::array<std::string, kBldStickers> cornerSetups{};
  Hold hold = kWcaHold;

  static BlindScheme load();
  void save() const;

  // The user's own setup for a sticker, empty for the computed one.
  const std::string &ownSetup(BldPiece kind, int sticker) const;
  std::string &ownSetup(BldPiece kind, int sticker);
  // The setup in use: the user's own when it can be read, else the
  // computed one (empty for the swap spot); nullopt for a buffer sticker.
  std::optional<Alg> setup(BldPiece kind, int sticker) const;
  // The setups in use for a piece type, by sticker (empty for buffers).
  std::array<Alg, kBldStickers> setups(BldPiece kind) const;

  friend bool operator==(const BlindScheme &, const BlindScheme &) = default;
};

// Why a setup written as `text` doesn't do for `sticker`, e.g. "brings it
// to UF" or "moves the buffer or the T-perm's corners"; empty when it does.
QString setupProblem(BldPiece kind, int sticker, const std::string &text);

// "Edges" or "Corners" (or the singular), for labels.
QString pieceName(BldPiece kind, bool plural = true);

// Edits the blind lettering scheme, the setup moves and the blind hold:
// a net of the solved cube with every sticker's letter, and a table per
// piece type with each sticker's letter, setup and whether the setup
// works. Clicking a sticker on the net selects its row. Works on a copy;
// result() has the edited settings.
class BldSchemeDialog : public QDialog {
  Q_OBJECT
public:
  BldSchemeDialog(const BlindScheme &scheme, const ColourScheme &colours,
                  QWidget *parent = nullptr);

  const BlindScheme &result() const { return scheme_; }
  // Refuses empty letters and letters shared by two stickers of a type.
  void accept() override;

private:
  enum Column { LetterCol, StickerCol, SetupCol, CheckCol };

  BlindScheme scheme_;
  bool loading_ = false;
  QComboBox *topBox_;
  QComboBox *frontBox_;
  QLabel *holdError_;
  CubeView *net_;
  QTabWidget *tabs_;
  std::array<QTableWidget *, 2> tables_{}; // edges, corners
  std::optional<std::pair<BldPiece, int>> selected_;

  QTableWidget *table(BldPiece kind) const;
  void fillTables();
  void fillRow(BldPiece kind, int sticker);
  // The net in the hold's colours with every letter, and the selected
  // sticker ringed.
  void showNet();
  void selectSticker(BldPiece kind, int sticker);
  void cellChanged(BldPiece kind, int row, int column);
  void holdChanged();
};

} // namespace vcube
