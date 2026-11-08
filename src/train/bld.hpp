#pragma once

#include "cube/cubie.hpp"
#include "cube/move.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vcube {

// Blindfolded solving with Old Pochmann (OP): each piece is solved by
// swapping it with a buffer piece, one target sticker at a time. A target
// is brought to the swap spot by setup moves, swapped with the buffer by
// the swap alg, and the setup undone.
//
// Edges: buffer UR (its U sticker), swap spot UL (its U sticker), swapped
// by the T-perm, which also swaps the UFR and UBR corners. Corners: buffer
// UBL (its U sticker), swap spot DFR (its R sticker), swapped by the
// modified Y-perm, which also swaps the UL and UB edges. After an odd
// number of edge targets the parity alg (swapping the UFR and UBR corners
// and the UL and UB edges) goes between the edges and the corners.
//
// Everything is in the solver's frame: the cube held the way blind solves
// are done (the scheme's letters belong to those positions).

enum class BldPiece : std::uint8_t { Edge, Corner };
inline constexpr int kBldStickers = 24; // of each piece type

// Stickers of each type are numbered in Speffz order, A = 0 to X = 23,
// face by face (U L F R B D), clockwise from the top-left of each face as
// it is seen (U with B at the top, D with F at the top):
//   edges:   UB UR UF UL  LU LF LD LB  FU FR FD FL  RU RB RD RF
//            BU BL BD BR  DF DR DB DL
//   corners: UBL UBR UFR UFL  LUB LUF LDF LDB  FUL FUR FDR FDL
//            RUF RUB RDB RDF  BUR BUL BDL BDR  DFL DFR DBR DBL
// (the sticker's own face first).
int bldFacelet(BldPiece kind, int sticker); // facelet index, 0..53
std::optional<int> bldSticker(BldPiece kind, int facelet);
// The sticker's piece and face, e.g. "UF" for edge sticker C, "FU" for I,
// "RDF" for corner sticker P.
std::string bldStickerName(BldPiece kind, int sticker);
// The stickers of a sticker's piece, starting with it; a corner's go
// clockwise (as its facelets do in kCornerFacelets).
std::vector<int> pieceStickers(BldPiece kind, int sticker);

// A lettering scheme: one letter (or short label) per sticker.
struct LetterScheme {
  std::array<std::string, kBldStickers> edges;
  std::array<std::string, kBldStickers> corners;

  static LetterScheme speffz(); // A to X for both types

  const std::string &letter(BldPiece kind, int sticker) const;
  std::string &letter(BldPiece kind, int sticker);
  // The sticker with this letter (ignoring case); nullopt if no sticker or
  // more than one has it.
  std::optional<int> sticker(BldPiece kind, std::string_view letter) const;
  // Letters that are empty or shared by two stickers of a type.
  std::vector<std::string> problems(BldPiece kind) const;

  // Typed letters, e.g. "AB CD E", "abcde" or "a b c": letters are matched
  // longest first, spaces and commas are ignored. On failure, the offset
  // (in characters) of the first text that isn't a letter.
  struct Parsed {
    std::vector<int> stickers;
    std::optional<std::size_t> errorAt;
  };
  Parsed parse(BldPiece kind, std::string_view text) const;
  // Letters in pairs: "AB CD E".
  std::string format(BldPiece kind, const std::vector<int> &stickers) const;

  friend bool operator==(const LetterScheme &, const LetterScheme &) = default;
};

// The Old Pochmann buffers, swap spots and algs.
namespace op {

inline constexpr int kEdgeBuffer = 1;   // B: UR
inline constexpr int kEdgeSwap = 3;     // D: UL
inline constexpr int kCornerBuffer = 0; // A: UBL
inline constexpr int kCornerSwap = 15;  // P: RDF (the R sticker)

int buffer(BldPiece kind);
int swapSpot(BldPiece kind);
// The T-perm for edges, the modified Y-perm for corners.
std::string swapAlgText(BldPiece kind);
const Alg &swapAlg(BldPiece kind);
// Swaps the UFR and UBR corners and the UL and UB edges.
std::string parityAlgText();
const Alg &parityAlg();

// A sticker can be a target unless it is on the buffer piece.
bool isTarget(BldPiece kind, int sticker);

// The shortest setup that brings `sticker` to the swap spot using only
// moves that keep the buffer and the swap alg's other pieces in place
// (edges: L, D, M, E and the wide l and d; corners: R, D and F), preferring
// outer turns to wide turns and wide turns to slices. Empty for the swap
// spot itself; nullopt for a sticker of the buffer piece.
std::optional<Alg> defaultSetup(BldPiece kind, int sticker);

// Whether `setup` (any moves) brings `sticker` to the swap spot and leaves
// the buffer piece and the swap alg's other pieces (edges: the UFR and UBR
// corners; corners: the UL and UB edges) where they were and as they were,
// so that setup, swap alg, undo swaps the buffer with that sticker.
bool isValidSetup(BldPiece kind, int sticker, const Alg &setup);

// The full execution of a memo: each edge target as setup, T-perm, undo;
// the parity alg after an odd number of edge targets; then each corner
// target as setup, Y-perm, undo. Setups are by sticker.
Alg execution(const std::vector<int> &edges, const std::vector<int> &corners,
              const std::array<Alg, kBldStickers> &edgeSetups,
              const std::array<Alg, kBldStickers> &cornerSetups);

} // namespace op

// A cube's memo: the targets, as Speffz stickers, for each piece type.
// Each target swaps the buffer with it. When the buffer holds its own piece
// while pieces are left, a new cycle starts at the unsolved piece with the
// lowest sticker (in Speffz order), shooting to that sticker; a piece that
// is in place but flipped or twisted is such a piece.
struct BldMemo {
  std::vector<int> edges;
  std::vector<int> corners;
  bool parity() const { return edges.size() % 2 == 1; }
};
BldMemo traceMemo(const CubieCube &cube);

// A typed memo checked against a cube, for one piece type, by doing its
// targets as swaps with the buffer in order.
struct MemoCheck {
  bool solves = false; // the targets solve every piece of the type
  // The first target that isn't right at that point: not where the piece
  // in the buffer belongs or, at a cycle break, not a sticker of an
  // unsolved piece; or a target after the pieces were all solved.
  std::optional<std::size_t> firstWrong;
  // What would have been right there: the one sticker the buffer's piece
  // belongs at, or at a cycle break every sticker of the unsolved pieces.
  // Empty when the extra target came after everything was solved. When
  // the memo is too short, the targets that would come next.
  std::vector<int> expected;
  bool tooShort = false; // every target was right but pieces are left
};
MemoCheck checkMemo(const CubieCube &cube, BldPiece kind,
                    const std::vector<int> &targets);

// Pieces of the type not solved (in place and oriented).
int unsolvedCount(const CubieCube &cube, BldPiece kind);

} // namespace vcube
