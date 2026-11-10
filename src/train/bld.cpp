#include "train/bld.hpp"

#include "cube/facelet.hpp"

#include <algorithm>
#include <compare>
#include <cstdint>
#include <map>

namespace vcube {

namespace {

// The facelet of each Speffz sticker, A to X.
constexpr std::array<std::uint8_t, kBldStickers> kEdgeSpeffz = {
    1,  5,  7,  3,  // UB UR UF UL
    37, 41, 43, 39, // LU LF LD LB
    19, 23, 25, 21, // FU FR FD FL
    10, 14, 16, 12, // RU RB RD RF
    46, 50, 52, 48, // BU BL BD BR
    28, 32, 34, 30, // DF DR DB DL
};
constexpr std::array<std::uint8_t, kBldStickers> kCornerSpeffz = {
    0,  2,  8,  6,  // UBL UBR UFR UFL
    36, 38, 44, 42, // LUB LUF LDF LDB
    18, 20, 26, 24, // FUL FUR FDR FDL
    9,  11, 17, 15, // RUF RUB RDB RDF
    45, 47, 53, 51, // BUR BUL BDL BDR
    27, 29, 35, 33, // DFL DFR DBR DBL
};

const std::array<std::uint8_t, kBldStickers> &speffzTable(BldPiece kind) {
  return kind == BldPiece::Edge ? kEdgeSpeffz : kCornerSpeffz;
}

std::size_t index(int i) { return static_cast<std::size_t>(i); }

int pieceCount(BldPiece kind) {
  return kind == BldPiece::Edge ? kEdges : kCorners;
}

int stickersPerPiece(BldPiece kind) { return kind == BldPiece::Edge ? 2 : 3; }

// The facelet at `k` of piece position `piece` (kEdgeFacelets or
// kCornerFacelets order).
int pieceFacelet(BldPiece kind, int piece, int k) {
  return kind == BldPiece::Edge ? kEdgeFacelets[index(piece)][index(k)]
                                : kCornerFacelets[index(piece)][index(k)];
}

// Where a facelet is: its piece position and its place in that piece's
// facelets; piece -1 if it isn't on a piece of the type.
struct PiecePlace {
  int piece = -1;
  int k = 0;
};

PiecePlace placeOf(BldPiece kind, int facelet) {
  for (int p = 0; p < pieceCount(kind); ++p) {
    for (int k = 0; k < stickersPerPiece(kind); ++k) {
      if (pieceFacelet(kind, p, k) == facelet) {
        return {p, k};
      }
    }
  }
  return {};
}

// The facelets of a facelet's piece, starting with it, in table order
// (clockwise for corners).
std::vector<int> pieceFacelets(BldPiece kind, int facelet) {
  PiecePlace place = placeOf(kind, facelet);
  std::vector<int> out;
  if (place.piece < 0) {
    return out;
  }
  int n = stickersPerPiece(kind);
  for (int j = 0; j < n; ++j) {
    out.push_back(pieceFacelet(kind, place.piece, (place.k + j) % n));
  }
  return out;
}

int pieceOfSticker(BldPiece kind, int sticker) {
  return placeOf(kind, bldFacelet(kind, sticker)).piece;
}

// Which sticker sits where: at[f] is the facelet where the sticker now at
// facelet f belongs (centres, which never move here, are their own).
using Stickers = std::array<std::uint8_t, kFaceletCount>;

Stickers stickersOf(const CubieCube &c) {
  Stickers at{};
  for (int i = 0; i < kFaceletCount; ++i) {
    at[index(i)] = static_cast<std::uint8_t>(i);
  }
  for (std::size_t i = 0; i < kCorners; ++i) {
    for (std::size_t k = 0; k < 3; ++k) {
      at[kCornerFacelets[i][(k + c.co[i]) % 3]] = kCornerFacelets[c.cp[i]][k];
    }
  }
  for (std::size_t i = 0; i < kEdges; ++i) {
    for (std::size_t k = 0; k < 2; ++k) {
      at[kEdgeFacelets[i][(k + c.eo[i]) % 2]] = kEdgeFacelets[c.ep[i]][k];
    }
  }
  return at;
}

// Swaps the pieces at two facelets so that their stickers trade places
// facelet for facelet, the others following in the same cyclic order: the
// effect of setup, swap alg and undo for a target.
void swapPieces(Stickers &at, BldPiece kind, int a, int b) {
  std::vector<int> fa = pieceFacelets(kind, a);
  std::vector<int> fb = pieceFacelets(kind, b);
  for (std::size_t j = 0; j < fa.size() && j < fb.size(); ++j) {
    std::swap(at[index(fa[j])], at[index(fb[j])]);
  }
}

bool pieceSolved(const Stickers &at, BldPiece kind, int piece) {
  for (int k = 0; k < stickersPerPiece(kind); ++k) {
    int f = pieceFacelet(kind, piece, k);
    if (at[index(f)] != f) {
      return false;
    }
  }
  return true;
}

bool allSolved(const Stickers &at, BldPiece kind) {
  for (int p = 0; p < pieceCount(kind); ++p) {
    if (!pieceSolved(at, kind, p)) {
      return false;
    }
  }
  return true;
}

// Every sticker of the unsolved pieces other than the buffer, in Speffz
// order: the targets a new cycle may start at.
std::vector<int> cycleBreaks(const Stickers &at, BldPiece kind) {
  int buffer = pieceOfSticker(kind, op::buffer(kind));
  std::vector<int> out;
  for (int s = 0; s < kBldStickers; ++s) {
    int piece = pieceOfSticker(kind, s);
    if (piece != buffer && !pieceSolved(at, kind, piece)) {
      out.push_back(s);
    }
  }
  return out;
}

// The targets that are right next: where the sticker in the buffer
// belongs, or at a cycle break every sticker of an unsolved piece. Empty
// once every piece is solved.
std::vector<int> nextTargets(const Stickers &at, BldPiece kind) {
  if (allSolved(at, kind)) {
    return {};
  }
  int bufferFacelet = bldFacelet(kind, op::buffer(kind));
  int home = at[index(bufferFacelet)];
  int buffer = placeOf(kind, bufferFacelet).piece;
  if (placeOf(kind, home).piece != buffer) {
    return {*bldSticker(kind, home)};
  }
  return cycleBreaks(at, kind);
}

std::vector<int> trace(const CubieCube &cube, BldPiece kind) {
  Stickers at = stickersOf(cube);
  int bufferFacelet = bldFacelet(kind, op::buffer(kind));
  std::vector<int> targets;
  // A real cube needs at most about 1.5 targets per piece; the cap only
  // guards against a cube that isn't one.
  for (int guard = 0; guard < 4 * kBldStickers; ++guard) {
    std::vector<int> next = nextTargets(at, kind);
    if (next.empty()) {
      break;
    }
    targets.push_back(next.front());
    swapPieces(at, kind, bufferFacelet, bldFacelet(kind, next.front()));
  }
  return targets;
}

// Facelets moved by one move: dest[f] is where the sticker at f goes.
using Destinations = std::array<std::uint8_t, kFaceletCount>;

Destinations destinationsOf(Move m) {
  FaceletCube::Stickers labels{};
  for (int i = 0; i < kFaceletCount; ++i) {
    labels[index(i)] = static_cast<std::uint8_t>(i);
  }
  FaceletCube f(labels);
  f.apply(m);
  Destinations dest{};
  for (int i = 0; i < kFaceletCount; ++i) {
    dest[f[i]] = static_cast<std::uint8_t>(i);
  }
  return dest;
}

// The moves setups may use, in the order ties are broken in, with how much
// each is disliked: outer turns 0, wide turns 1, slices 2.
struct SetupMove {
  Move move;
  int rank = 0;
  Destinations dest{};
};

std::vector<SetupMove> setupMoves(BldPiece kind) {
  std::vector<std::pair<MoveType, int>> types;
  if (kind == BldPiece::Edge) {
    types = {{MoveType::L, 0},  {MoveType::D, 0}, {MoveType::Lw, 1},
             {MoveType::Dw, 1}, {MoveType::M, 2}, {MoveType::E, 2}};
  } else {
    types = {{MoveType::R, 0}, {MoveType::D, 0}, {MoveType::F, 0}};
  }
  std::vector<SetupMove> out;
  for (auto [type, rank] : types) {
    for (std::uint8_t amount :
         {std::uint8_t{1}, std::uint8_t{3}, std::uint8_t{2}}) {
      Move m{type, amount, 0};
      out.push_back({m, rank, destinationsOf(m)});
    }
  }
  return out;
}

// How good a setup is, smaller first: length, then how many wide and slice
// turns, then quarter turns (a half turn is two).
struct SetupCost {
  std::size_t length = 0;
  int rank = 0;
  int quarters = 0;
  friend auto operator<=>(const SetupCost &, const SetupCost &) = default;
};

constexpr std::size_t kMaxSetupLength = 4;

std::optional<Alg> searchSetup(BldPiece kind, int sticker) {
  const std::vector<SetupMove> moves = setupMoves(kind);
  const int goal = bldFacelet(kind, op::swapSpot(kind));
  std::optional<Alg> best;
  SetupCost bestCost;
  Alg path;
  std::vector<std::size_t> chosen;
  // Depth-first over every sequence up to kMaxSetupLength moves in tie
  // order, so the first of equally good setups is kept.
  auto search = [&](auto &&self, int at, SetupCost cost) -> void {
    if (best && bestCost < cost) {
      return;
    }
    if (at == goal) {
      if (!best || cost < bestCost) {
        best = path;
        bestCost = cost;
      }
      return;
    }
    if (path.size() == kMaxSetupLength) {
      return;
    }
    for (const SetupMove &m : moves) {
      if (!path.empty() && path.back().type == m.move.type) {
        continue; // two turns of one layer are one turn
      }
      path.push_back(m.move);
      SetupCost next = cost;
      next.length += 1;
      next.rank += m.rank;
      next.quarters += m.move.amount == 2 ? 2 : 1;
      self(self, m.dest[index(at)], next);
      path.pop_back();
    }
  };
  search(search, bldFacelet(kind, sticker), SetupCost{});
  return best;
}

Alg parsed(const std::string &text) { return parseAlgOrEmpty(text); }

} // namespace

// --- stickers
// ------------------------------------------------------------------

int bldFacelet(BldPiece kind, int sticker) {
  if (sticker < 0 || sticker >= kBldStickers) {
    return -1;
  }
  return speffzTable(kind)[index(sticker)];
}

std::optional<int> bldSticker(BldPiece kind, int facelet) {
  const auto &table = speffzTable(kind);
  for (std::size_t s = 0; s < table.size(); ++s) {
    if (table[s] == facelet) {
      return static_cast<int>(s);
    }
  }
  return std::nullopt;
}

std::string bldStickerName(BldPiece kind, int sticker) {
  std::vector<int> facelets = pieceFacelets(kind, bldFacelet(kind, sticker));
  if (facelets.empty()) {
    return {};
  }
  // The sticker's own face, then the others as pieces are named: U or D
  // before F or B before R or L (UBL, LUB, FUL, RDF, DFL).
  auto face = [](int f) { return f / 9; };
  auto axisRank = [](int f) {
    switch (static_cast<Face>(f)) {
    case Face::U:
    case Face::D:
      return 0;
    case Face::F:
    case Face::B:
      return 1;
    default:
      return 2;
    }
  };
  std::vector<int> faces;
  for (int f : facelets) {
    faces.push_back(face(f));
  }
  std::stable_sort(faces.begin() + 1, faces.end(),
                   [&](int a, int b) { return axisRank(a) < axisRank(b); });
  std::string name;
  for (int f : faces) {
    name += "URFDLB"[f];
  }
  return name;
}

std::vector<int> pieceStickers(BldPiece kind, int sticker) {
  std::vector<int> out;
  for (int f : pieceFacelets(kind, bldFacelet(kind, sticker))) {
    out.push_back(*bldSticker(kind, f));
  }
  return out;
}

// --- lettering
// -----------------------------------------------------------------

namespace {

char lower(char c) {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool sameLetter(std::string_view a, std::string_view b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(),
                    [](char x, char y) { return lower(x) == lower(y); });
}

bool isSeparator(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',' ||
         c == '-' || c == ';';
}

// Characters (code points) in the first `bytes` bytes of UTF-8 text.
std::size_t codePoints(std::string_view text, std::size_t bytes) {
  std::size_t n = 0;
  for (std::size_t i = 0; i < bytes && i < text.size(); ++i) {
    if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) {
      ++n;
    }
  }
  return n;
}

} // namespace

std::string foldLetter(std::string_view letter) {
  std::string out(letter);
  for (char &c : out) {
    c = lower(c);
  }
  return out;
}

LetterScheme LetterScheme::speffz() {
  LetterScheme s;
  for (int i = 0; i < kBldStickers; ++i) {
    std::string letter(1, static_cast<char>('A' + i));
    s.edges[index(i)] = letter;
    s.corners[index(i)] = letter;
  }
  return s;
}

const std::string &LetterScheme::letter(BldPiece kind, int sticker) const {
  return (kind == BldPiece::Edge ? edges : corners)[index(sticker)];
}

std::string &LetterScheme::letter(BldPiece kind, int sticker) {
  return (kind == BldPiece::Edge ? edges : corners)[index(sticker)];
}

std::optional<int> LetterScheme::sticker(BldPiece kind,
                                         std::string_view text) const {
  std::optional<int> found;
  for (int s = 0; s < kBldStickers; ++s) {
    const std::string &l = letter(kind, s);
    if (!l.empty() && sameLetter(l, text)) {
      if (found) {
        return std::nullopt; // two stickers share it
      }
      found = s;
    }
  }
  return found;
}

std::vector<std::string> LetterScheme::problems(BldPiece kind) const {
  std::vector<std::string> out;
  std::map<std::string, int> uses;
  bool empty = false;
  for (int s = 0; s < kBldStickers; ++s) {
    const std::string &l = letter(kind, s);
    if (l.empty()) {
      empty = true;
    } else if (++uses[foldLetter(l)] == 2) {
      out.push_back(l);
    }
  }
  if (empty) {
    out.insert(out.begin(), std::string());
  }
  return out;
}

LetterScheme::Parsed LetterScheme::parse(BldPiece kind,
                                         std::string_view text) const {
  Parsed out;
  std::size_t i = 0;
  while (i < text.size()) {
    if (isSeparator(text[i])) {
      ++i;
      continue;
    }
    // The longest letter that starts here; the lowest sticker on a tie.
    std::optional<int> match;
    std::size_t length = 0;
    for (int s = 0; s < kBldStickers; ++s) {
      const std::string &l = letter(kind, s);
      if (l.empty() || l.size() <= length || l.size() > text.size() - i) {
        continue;
      }
      if (sameLetter(l, text.substr(i, l.size()))) {
        match = s;
        length = l.size();
      }
    }
    if (!match) {
      out.errorAt = codePoints(text, i);
      return out;
    }
    out.stickers.push_back(*match);
    i += length;
  }
  return out;
}

std::string LetterScheme::format(BldPiece kind,
                                 const std::vector<int> &stickers) const {
  std::string out;
  for (std::size_t i = 0; i < stickers.size(); ++i) {
    if (i > 0 && i % 2 == 0) {
      out += ' ';
    }
    int s = stickers[i];
    out += s >= 0 && s < kBldStickers ? letter(kind, s) : std::string("?");
  }
  return out;
}

// --- Old Pochmann
// --------------------------------------------------------------

namespace op {

int buffer(BldPiece kind) {
  return kind == BldPiece::Edge ? kEdgeBuffer : kCornerBuffer;
}

int swapSpot(BldPiece kind) {
  return kind == BldPiece::Edge ? kEdgeSwap : kCornerSwap;
}

std::string swapAlgText(BldPiece kind) {
  return kind == BldPiece::Edge ? "R U R' U' R' F R2 U' R' U' R U R' F'"
                                : "R U' R' U' R U R' F' R U R' U' R' F R";
}

const Alg &swapAlg(BldPiece kind) {
  static const Alg edge = parsed(swapAlgText(BldPiece::Edge));
  static const Alg corner = parsed(swapAlgText(BldPiece::Corner));
  return kind == BldPiece::Edge ? edge : corner;
}

std::string parityAlgText() {
  // An Ra-perm with a U' at the end.
  return "R U' R' U' R U R D R' U' R D' R' U2 R' U'";
}

const Alg &parityAlg() {
  static const Alg alg = parsed(parityAlgText());
  return alg;
}

bool isTarget(BldPiece kind, int sticker) {
  return sticker >= 0 && sticker < kBldStickers &&
         pieceOfSticker(kind, sticker) != pieceOfSticker(kind, buffer(kind));
}

std::optional<Alg> defaultSetup(BldPiece kind, int sticker) {
  if (!isTarget(kind, sticker)) {
    return std::nullopt;
  }
  static const auto table = [] {
    std::array<std::array<std::optional<Alg>, kBldStickers>, 2> out{};
    for (BldPiece k : {BldPiece::Edge, BldPiece::Corner}) {
      for (int s = 0; s < kBldStickers; ++s) {
        if (isTarget(k, s)) {
          out[static_cast<std::size_t>(k)][index(s)] = searchSetup(k, s);
        }
      }
    }
    return out;
  }();
  return table[static_cast<std::size_t>(kind)][index(sticker)];
}

bool isValidSetup(BldPiece kind, int sticker, const Alg &setup) {
  if (!isTarget(kind, sticker)) {
    return false;
  }
  // Facelets are labelled with where they started; a label left where it
  // started is a sticker the setup put back.
  FaceletCube::Stickers labels{};
  for (int i = 0; i < kFaceletCount; ++i) {
    labels[index(i)] = static_cast<std::uint8_t>(i);
  }
  FaceletCube f(labels);
  f.apply(setup);
  if (f[bldFacelet(kind, swapSpot(kind))] != bldFacelet(kind, sticker)) {
    return false;
  }
  // The buffer piece, and the pieces the swap alg also swaps, must be
  // where they were and as they were. Centres may move (an M or E turn
  // moves them): the swap alg turns layers, whatever centres they hold.
  auto kept = [&](BldPiece type, int piece) {
    for (int k = 0; k < stickersPerPiece(type); ++k) {
      int facelet = pieceFacelet(type, piece, k);
      if (f[facelet] != facelet) {
        return false;
      }
    }
    return true;
  };
  if (!kept(kind, pieceOfSticker(kind, buffer(kind)))) {
    return false;
  }
  if (kind == BldPiece::Edge) {
    return kept(BldPiece::Corner, URF) && kept(BldPiece::Corner, UBR);
  }
  return kept(BldPiece::Edge, UL) && kept(BldPiece::Edge, UB);
}

std::optional<Alg> setupInUse(BldPiece kind, int sticker,
                              std::string_view own) {
  if (!isTarget(kind, sticker)) {
    return std::nullopt;
  }
  if (!own.empty()) {
    if (ParseResult parsed = parseAlg(own);
        parsed && isValidSetup(kind, sticker, parsed.alg)) {
      return parsed.alg;
    }
  }
  return defaultSetup(kind, sticker);
}

Alg execution(const std::vector<int> &edges, const std::vector<int> &corners,
              const std::array<Alg, kBldStickers> &edgeSetups,
              const std::array<Alg, kBldStickers> &cornerSetups) {
  Alg out;
  auto target = [&out](const Alg &setup, const Alg &swap) {
    out.insert(out.end(), setup.begin(), setup.end());
    out.insert(out.end(), swap.begin(), swap.end());
    Alg undo = invert(setup);
    out.insert(out.end(), undo.begin(), undo.end());
  };
  for (int s : edges) {
    if (s >= 0 && s < kBldStickers) {
      target(edgeSetups[index(s)], swapAlg(BldPiece::Edge));
    }
  }
  if (edges.size() % 2 == 1) {
    const Alg &parity = parityAlg();
    out.insert(out.end(), parity.begin(), parity.end());
  }
  for (int s : corners) {
    if (s >= 0 && s < kBldStickers) {
      target(cornerSetups[index(s)], swapAlg(BldPiece::Corner));
    }
  }
  return out;
}

} // namespace op

// --- memo
// ----------------------------------------------------------------------

BldMemo traceMemo(const CubieCube &cube) {
  return {trace(cube, BldPiece::Edge), trace(cube, BldPiece::Corner)};
}

MemoCheck checkMemo(const CubieCube &cube, BldPiece kind,
                    const std::vector<int> &targets) {
  MemoCheck out;
  Stickers at = stickersOf(cube);
  int bufferFacelet = bldFacelet(kind, op::buffer(kind));
  for (std::size_t i = 0; i < targets.size(); ++i) {
    std::vector<int> next = nextTargets(at, kind);
    if (std::find(next.begin(), next.end(), targets[i]) == next.end()) {
      out.firstWrong = i;
      out.expected = next;
      return out;
    }
    swapPieces(at, kind, bufferFacelet, bldFacelet(kind, targets[i]));
  }
  out.solves = allSolved(at, kind);
  out.tooShort = !out.solves;
  if (out.tooShort) {
    out.expected = nextTargets(at, kind);
  }
  return out;
}

int unsolvedCount(const CubieCube &cube, BldPiece kind) {
  int n = 0;
  if (kind == BldPiece::Edge) {
    for (std::size_t i = 0; i < kEdges; ++i) {
      n += !edgeSolved(cube, static_cast<Edge>(i));
    }
  } else {
    for (std::size_t i = 0; i < kCorners; ++i) {
      n += !cornerSolved(cube, static_cast<Corner>(i));
    }
  }
  return n;
}

} // namespace vcube
