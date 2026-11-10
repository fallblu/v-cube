#include "cube/cubie.hpp"
#include "cube/facelet.hpp"
#include "cube/scramble.hpp"
#include "train/bld.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <set>

using namespace vcube;

namespace {

constexpr std::array<BldPiece, 2> kKinds = {BldPiece::Edge, BldPiece::Corner};

// A facelet cube whose stickers are labelled with the facelet they started
// on, so that cube[f] is where the sticker now at f belongs.
FaceletCube labelled() {
  FaceletCube::Stickers s{};
  for (int i = 0; i < kFaceletCount; ++i) {
    s[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
  }
  return FaceletCube(s);
}

std::array<Alg, kBldStickers> defaultSetups(BldPiece kind) {
  std::array<Alg, kBldStickers> out{};
  for (int s = 0; s < kBldStickers; ++s) {
    if (auto setup = op::defaultSetup(kind, s)) {
      out[static_cast<std::size_t>(s)] = *setup;
    }
  }
  return out;
}

// The facelets of every piece of a type.
std::vector<int> faceletsOf(BldPiece kind) {
  std::vector<int> out;
  if (kind == BldPiece::Edge) {
    for (const auto &e : kEdgeFacelets) {
      out.insert(out.end(), e.begin(), e.end());
    }
  } else {
    for (const auto &c : kCornerFacelets) {
      out.insert(out.end(), c.begin(), c.end());
    }
  }
  return out;
}

bool solves(const CubieCube &cube, const Alg &alg) {
  FaceletCube f = cube.toFacelets();
  f.apply(alg);
  return f.isStandardOrientation() && f.isSolved();
}

// A memo traced with the highest cycle-break sticker instead of the lowest,
// using checkMemo's next targets.
std::vector<int> otherMemo(const CubieCube &cube, BldPiece kind) {
  std::vector<int> targets;
  for (int guard = 0; guard < 100; ++guard) {
    MemoCheck c = checkMemo(cube, kind, targets);
    if (c.solves || c.expected.empty()) {
      break;
    }
    targets.push_back(c.expected.back());
  }
  return targets;
}

} // namespace

TEST_CASE("speffz stickers are the facelets of the pieces they name", "[bld]") {
  const std::array<std::string, kBldStickers> edgeNames = {
      "UB", "UR", "UF", "UL", "LU", "LF", "LD", "LB", "FU", "FR", "FD", "FL",
      "RU", "RB", "RD", "RF", "BU", "BL", "BD", "BR", "DF", "DR", "DB", "DL"};
  const std::array<std::string, kBldStickers> cornerNames = {
      "UBL", "UBR", "UFR", "UFL", "LUB", "LUF", "LDF", "LDB",
      "FUL", "FUR", "FDR", "FDL", "RUF", "RUB", "RDB", "RDF",
      "BUR", "BUL", "BDL", "BDR", "DFL", "DFR", "DBR", "DBL"};
  for (BldPiece kind : kKinds) {
    std::vector<int> all = faceletsOf(kind);
    std::set<int> seen;
    for (int s = 0; s < kBldStickers; ++s) {
      int f = bldFacelet(kind, s);
      CHECK(std::find(all.begin(), all.end(), f) != all.end());
      CHECK(seen.insert(f).second);
      CHECK(bldSticker(kind, f) == s);
      // The sticker is on its named face.
      const auto &names = kind == BldPiece::Edge ? edgeNames : cornerNames;
      CHECK(bldStickerName(kind, s) == names[static_cast<std::size_t>(s)]);
      CHECK("URFDLB"[f / 9] == names[static_cast<std::size_t>(s)][0]);
      std::vector<int> piece = pieceStickers(kind, s);
      REQUIRE(piece.size() == (kind == BldPiece::Edge ? 2u : 3u));
      CHECK(piece.front() == s);
    }
    CHECK(seen.size() == static_cast<std::size_t>(kBldStickers));
  }
  CHECK(bldFacelet(BldPiece::Edge, 2) == 7);            // C: UF
  CHECK(bldFacelet(BldPiece::Corner, 15) == 15);        // P: R7
  CHECK(bldSticker(BldPiece::Edge, 4) == std::nullopt); // a centre
  CHECK(bldSticker(BldPiece::Edge, 0) == std::nullopt); // a corner
  // Corner stickers go clockwise: UFR's U, then R, then F.
  CHECK(pieceStickers(BldPiece::Corner, 2) == std::vector<int>{2, 12, 9});
}

TEST_CASE("letters are parsed and formatted in pairs", "[bld]") {
  LetterScheme s = LetterScheme::speffz();
  CHECK(s.letter(BldPiece::Edge, 0) == "A");
  CHECK(s.letter(BldPiece::Corner, 23) == "X");
  CHECK(s.sticker(BldPiece::Edge, "c") == 2);
  CHECK(s.sticker(BldPiece::Edge, "Z") == std::nullopt);
  CHECK(s.problems(BldPiece::Edge).empty());

  auto p = s.parse(BldPiece::Edge, "AB cd, e-f");
  CHECK(!p.errorAt);
  CHECK(p.stickers == std::vector<int>{0, 1, 2, 3, 4, 5});
  CHECK(s.format(BldPiece::Edge, p.stickers) == "AB CD EF");
  CHECK(s.format(BldPiece::Edge, {0, 1, 2}) == "AB C");
  CHECK(s.format(BldPiece::Edge, {}).empty());

  auto bad = s.parse(BldPiece::Edge, "AB ZC");
  CHECK(bad.errorAt == 3u);
  // Offsets count characters, not bytes.
  CHECK(s.parse(BldPiece::Edge, "A\xC3\xA9").errorAt == 1u);

  // Labels of more than one character match longest first.
  LetterScheme multi = s;
  multi.letter(BldPiece::Edge, 0) = "Ch";
  auto m = multi.parse(BldPiece::Edge, "chC");
  CHECK(m.stickers == std::vector<int>{0, 2});
  // Shared and empty letters are problems.
  multi.letter(BldPiece::Edge, 1) = "c";
  multi.letter(BldPiece::Edge, 5) = "";
  CHECK(multi.sticker(BldPiece::Edge, "C") == std::nullopt);
  auto problems = multi.problems(BldPiece::Edge);
  CHECK(problems == std::vector<std::string>{"", "C"});
  // Letters fold as the scheme compares them: A-Z only.
  CHECK(foldLetter("Ch") == "ch");
  CHECK(foldLetter("\xC3\x89") == "\xC3\x89"); // É stays as it is
}

TEST_CASE("a setup in use is the user's own only when it does the job",
          "[bld]") {
  // Edge C (UF): the computed setup, an own one that works, one that can't
  // be read and one that moves the buffer.
  auto computed = op::defaultSetup(BldPiece::Edge, 2);
  REQUIRE(computed);
  CHECK(op::setupInUse(BldPiece::Edge, 2, "") == computed);
  Alg own = parseAlgOrEmpty("M2 D L2");
  REQUIRE(op::isValidSetup(BldPiece::Edge, 2, own));
  CHECK(op::setupInUse(BldPiece::Edge, 2, "M2 D L2") == own);
  CHECK(op::setupInUse(BldPiece::Edge, 2, "R Q") == computed);
  CHECK(op::setupInUse(BldPiece::Edge, 2, "U'") == computed);
  CHECK(op::setupInUse(BldPiece::Edge, 1, "") == std::nullopt);
}

TEST_CASE("the swap algs swap the buffer with the swap spot", "[bld]") {
  // T-perm: UR and UL (U stickers B and D), and the UFR and UBR corners.
  FaceletCube t = labelled();
  t.apply(op::swapAlg(BldPiece::Edge));
  CHECK(t[bldFacelet(BldPiece::Edge, op::kEdgeBuffer)] ==
        bldFacelet(BldPiece::Edge, op::kEdgeSwap));
  CHECK(t[bldFacelet(BldPiece::Edge, op::kEdgeSwap)] ==
        bldFacelet(BldPiece::Edge, op::kEdgeBuffer));
  CHECK(t[8] == 2);
  CHECK(t[2] == 8);
  // Modified Y-perm: UBL and RDF (A and P), and the UL and UB edges.
  FaceletCube y = labelled();
  y.apply(op::swapAlg(BldPiece::Corner));
  CHECK(y[0] == 15);
  CHECK(y[15] == 0);
  CHECK(y[3] == 1);
  CHECK(y[1] == 3);
  // Parity: UFR <-> UBR and UL <-> UB, as the T-perm and Y-perm do them,
  // and nothing else.
  FaceletCube parity = labelled();
  parity.apply(op::parityAlg());
  for (int f = 0; f < kFaceletCount; ++f) {
    bool corner = f == 8 || f == 9 || f == 20 || f == 2 || f == 45 || f == 11;
    bool edge = f == 3 || f == 37 || f == 1 || f == 46;
    int expected = corner ? t[f] : edge ? y[f] : f;
    CHECK(parity[f] == expected);
  }
}

TEST_CASE("default setups are short and valid", "[bld]") {
  for (BldPiece kind : kKinds) {
    std::size_t longest = 0;
    for (int s = 0; s < kBldStickers; ++s) {
      auto setup = op::defaultSetup(kind, s);
      if (!op::isTarget(kind, s)) {
        CHECK(!setup);
        CHECK(!op::isValidSetup(kind, s, {}));
        continue;
      }
      REQUIRE(setup);
      CHECK(op::isValidSetup(kind, s, *setup));
      longest = std::max(longest, setup->size());
      if (s == op::swapSpot(kind)) {
        CHECK(setup->empty());
      }
    }
    // Only RD (O) needs four moves: D l' D' L2.
    CHECK(longest == (kind == BldPiece::Edge ? 4u : 2u));
  }
  // Buffers: UR's stickers for edges, UBL's for corners.
  CHECK(!op::isTarget(BldPiece::Edge, 1));
  CHECK(!op::isTarget(BldPiece::Edge, 12)); // M: RU
  CHECK(!op::isTarget(BldPiece::Corner, 0));
  CHECK(!op::isTarget(BldPiece::Corner, 4));  // E: LUB
  CHECK(!op::isTarget(BldPiece::Corner, 17)); // R: BUL
  // A setup that moves the buffer, or the T-perm's corners, doesn't count.
  CHECK(!op::isValidSetup(BldPiece::Edge, 2, parseAlgOrEmpty("U'")));
  CHECK(!op::isValidSetup(BldPiece::Edge, 9, parseAlgOrEmpty("R")));
  // Any moves may make a setup, as long as the target gets there.
  CHECK(op::isValidSetup(BldPiece::Edge, 20, parseAlgOrEmpty("D' L2")));
  CHECK(!op::isValidSetup(BldPiece::Corner, 2, parseAlgOrEmpty("U")));
  CHECK(op::isValidSetup(BldPiece::Corner, 15, {}));
  CHECK(op::isValidSetup(BldPiece::Corner, 21, parseAlgOrEmpty("y y' R D'")) ==
        op::isValidSetup(BldPiece::Corner, 21, parseAlgOrEmpty("R D'")));
}

TEST_CASE("setup, swap alg and undo swap the buffer with the target", "[bld]") {
  for (BldPiece kind : kKinds) {
    // The side effect is the swap alg's own on the other piece type.
    FaceletCube bare = labelled();
    bare.apply(op::swapAlg(kind));
    std::vector<int> mine = faceletsOf(kind);
    for (int s = 0; s < kBldStickers; ++s) {
      if (!op::isTarget(kind, s)) {
        continue;
      }
      Alg setup = *op::defaultSetup(kind, s);
      FaceletCube real = labelled();
      real.apply(setup);
      real.apply(op::swapAlg(kind));
      real.apply(invert(setup));

      // The swap as checkMemo does it: the buffer's stickers trade places
      // with the target's, in the same cyclic order.
      FaceletCube::Stickers expected = labelled().stickers();
      std::vector<int> b = pieceStickers(kind, op::buffer(kind));
      std::vector<int> t = pieceStickers(kind, s);
      for (std::size_t j = 0; j < b.size(); ++j) {
        std::swap(expected[static_cast<std::size_t>(bldFacelet(kind, b[j]))],
                  expected[static_cast<std::size_t>(bldFacelet(kind, t[j]))]);
      }
      for (int f = 0; f < kFaceletCount; ++f) {
        bool ofKind = std::find(mine.begin(), mine.end(), f) != mine.end();
        int want = ofKind ? expected[static_cast<std::size_t>(f)] : bare[f];
        INFO("sticker " << s << " facelet " << f);
        CHECK(real[f] == want);
      }
      // The memo checker agrees: the one target solves the swapped cube.
      FaceletCube colours = real;
      for (int f = 0; f < kFaceletCount; ++f) {
        colours[f] = static_cast<std::uint8_t>(real[f] / 9);
      }
      auto cube = CubieCube::fromFacelets(colours);
      REQUIRE(cube);
      CHECK(checkMemo(*cube, kind, {s}).solves);
    }
  }
}

TEST_CASE("a T-perm traces as one edge and three corner targets", "[bld]") {
  CubieCube c = cubieOf(op::swapAlg(BldPiece::Edge));
  BldMemo memo = traceMemo(c);
  CHECK(memo.edges == std::vector<int>{3});         // D
  CHECK(memo.corners == std::vector<int>{1, 2, 1}); // B C B
  CHECK(memo.parity());
  CHECK(traceMemo(CubieCube{}).edges.empty());
  CHECK(traceMemo(CubieCube{}).corners.empty());
  CHECK(!traceMemo(CubieCube{}).parity());
  CHECK(unsolvedCount(c, BldPiece::Edge) == 2);
  CHECK(unsolvedCount(c, BldPiece::Corner) == 2);

  // A flipped edge in place: a cycle break into it, shooting to both of its
  // stickers. Two edges flipped: UF and UB, from an EO alg.
  CubieCube flips;
  flips.eo[UF] = 1;
  flips.eo[UB] = 1;
  BldMemo flipped = traceMemo(flips);
  CHECK(flipped.edges == std::vector<int>{0, 16, 2, 8}); // A Q C I
  CHECK(!flipped.parity());
  // Twisted corners in place.
  CubieCube twists;
  twists.co[URF] = 1;
  twists.co[DRB] = 2;
  BldMemo twisted = traceMemo(twists);
  CHECK(twisted.corners.size() == 4);
  CHECK(checkMemo(twists, BldPiece::Corner, twisted.corners).solves);
}

TEST_CASE("the old pochmann execution of a traced memo solves the cube",
          "[bld]") {
  Rng rng(7);
  auto edgeSetups = defaultSetups(BldPiece::Edge);
  auto cornerSetups = defaultSetups(BldPiece::Corner);
  int odd = 0, even = 0;
  for (int i = 0; i < 300; ++i) {
    CubieCube c = randomState(PieceMask::all(), rng);
    BldMemo memo = traceMemo(c);
    (memo.parity() ? odd : even) += 1;
    CHECK(memo.edges.size() % 2 == memo.corners.size() % 2);
    CHECK(checkMemo(c, BldPiece::Edge, memo.edges).solves);
    CHECK(checkMemo(c, BldPiece::Corner, memo.corners).solves);
    Alg alg = op::execution(memo.edges, memo.corners, edgeSetups, cornerSetups);
    CHECK(solves(c, alg));
    // So does a memo with other cycle breaks.
    std::vector<int> edges = otherMemo(c, BldPiece::Edge);
    std::vector<int> corners = otherMemo(c, BldPiece::Corner);
    CHECK(checkMemo(c, BldPiece::Edge, edges).solves);
    CHECK(solves(c, op::execution(edges, corners, edgeSetups, cornerSetups)));
  }
  CHECK(odd > 0);
  CHECK(even > 0);
}

TEST_CASE("checking a memo points out the first wrong target", "[bld]") {
  // R U: edges and corners both cycle, with no cycle breaks needed at the
  // start for edges (the buffer UR moves).
  CubieCube c = cubieOf(parseAlgOrEmpty("R U R' F2 D L2 B"));
  BldMemo memo = traceMemo(c);
  REQUIRE(memo.edges.size() >= 3);

  MemoCheck ok = checkMemo(c, BldPiece::Edge, memo.edges);
  CHECK(ok.solves);
  CHECK(!ok.firstWrong);
  CHECK(!ok.tooShort);

  // A wrong forced target: one where the memo so far leaves only one
  // right target.
  std::optional<std::size_t> forced;
  for (std::size_t i = 0; i < memo.edges.size() && !forced; ++i) {
    std::vector<int> prefix(memo.edges.begin(),
                            memo.edges.begin() + static_cast<long>(i));
    if (checkMemo(c, BldPiece::Edge, prefix).expected.size() == 1) {
      forced = i;
    }
  }
  REQUIRE(forced);
  std::vector<int> wrong = memo.edges;
  int right = wrong[*forced];
  wrong[*forced] = right == 0 ? 2 : 0;
  MemoCheck w = checkMemo(c, BldPiece::Edge, wrong);
  CHECK(!w.solves);
  CHECK(w.firstWrong == forced);
  CHECK(w.expected == std::vector<int>{right});

  // Too short, and too long.
  std::vector<int> shortMemo(memo.edges.begin(), memo.edges.end() - 1);
  MemoCheck s = checkMemo(c, BldPiece::Edge, shortMemo);
  CHECK(s.tooShort);
  CHECK(!s.solves);
  CHECK(!s.firstWrong);
  CHECK(std::find(s.expected.begin(), s.expected.end(), memo.edges.back()) !=
        s.expected.end());
  std::vector<int> longMemo = memo.edges;
  longMemo.push_back(5);
  MemoCheck l = checkMemo(c, BldPiece::Edge, longMemo);
  CHECK(l.firstWrong == memo.edges.size());
  CHECK(l.expected.empty());
  CHECK(!l.solves);

  // At a cycle break any sticker of an unsolved piece is right, and the
  // buffer piece and solved pieces are not.
  CubieCube flips;
  flips.eo[UF] = 1;
  flips.eo[UB] = 1;
  CHECK(checkMemo(flips, BldPiece::Edge, {7, 2, 8, 0}).firstWrong == 0u);
  MemoCheck alt = checkMemo(flips, BldPiece::Edge, {2, 8, 0, 16});
  CHECK(alt.solves);
  MemoCheck bufferTarget = checkMemo(flips, BldPiece::Edge, {1});
  CHECK(bufferTarget.firstWrong == 0u);
  CHECK(bufferTarget.expected == std::vector<int>{0, 2, 8, 16});
}
