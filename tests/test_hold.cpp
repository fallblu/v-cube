#include "cube/facelet.hpp"
#include "cube/hold.hpp"
#include "cube/scramble.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace vcube;

namespace {

constexpr int kWhite = 0, kRed = 1, kGreen = 2, kYellow = 3, kOrange = 4,
              kBlue = 5;

// The stickers the solver sees after applying `scramble` holding white top
// and green front, then turning the cube to `h`.
FaceletCube seenAfter(const Alg &scramble, Hold h) {
  FaceletCube f;
  f.apply(scramble);
  f.apply(holdRotation(h));
  return f;
}

// `state` painted in the colours of hold `h`.
FaceletCube painted(const CubieCube &state, Hold h) {
  std::array<int, 6> colours = holdColours(h);
  FaceletCube f = state.toFacelets();
  for (int i = 0; i < kFaceletCount; ++i) {
    f[i] = static_cast<std::uint8_t>(colours[f[i]]);
  }
  return f;
}

} // namespace

TEST_CASE("there are 24 valid holds") {
  CHECK(allHolds().size() == 24);
  std::set<Hold> seen(allHolds().begin(), allHolds().end());
  CHECK(seen.size() == 24);
  CHECK(isValidHold({kWhite, kGreen}));
  CHECK_FALSE(isValidHold({kWhite, kYellow}));
  CHECK_FALSE(isValidHold({kRed, kRed}));
  CHECK_FALSE(isValidHold({-1, 2}));
}

TEST_CASE("hold rotations reach their hold") {
  CHECK(holdRotation(kWcaHold).empty());
  CHECK(toString(holdRotation({kYellow, kBlue})) == "x2");
  for (Hold h : allHolds()) {
    CHECK(holdAfter(holdRotation(h)) == h);
    std::array<int, 6> c = holdColours(h);
    CHECK(c[0] == h.top);
    CHECK(c[2] == h.front);
    CHECK(c[3] == (h.top + 3) % 6);
  }
}

TEST_CASE("held states are conjugates by the hold's rotation") {
  // Held with yellow top and blue front (x2), R U R' U' on the solver's
  // cube is R D R' D' from the WCA hold.
  CHECK(heldState(cubieOf(parseAlgOrEmpty("R U R' U'")), {kYellow, kBlue}) ==
        cubieOf(parseAlgOrEmpty("R D R' D'")));
  // Any hold: a move on the solver's cube is its conjugate by the rotation.
  Hold greenTop{kGreen, kYellow};
  CHECK(heldState(cubieOf(parseAlgOrEmpty("U")), greenTop) ==
        cubieOf(parseAlgOrEmpty(toString(holdRotation(greenTop)) + " U " +
                                toString(invert(holdRotation(greenTop))))));
  CHECK(heldState(CubieCube{}, greenTop).isSolved());
}

TEST_CASE("held states round-trip and show the solver's state") {
  Rng rng(3);
  for (Hold h : allHolds()) {
    CAPTURE(h.top, h.front);
    CubieCube s = randomState(PieceMask::all(), rng);
    CubieCube held = heldState(s, h);
    CHECK(held.isValid());
    CHECK(solverState(held, h) == s);
    // Applying a move sequence that reaches `held`, then turning to the
    // hold, shows `s` in the hold's colours.
    Alg scramble = scrambleToState(held);
    CHECK(seenAfter(scramble, h) == painted(s, h));
  }
}

TEST_CASE("an EOCross scramble held x2 y neutral puts the cross on the "
          "bottom colour") {
  Rng rng(11);
  const ScrambleType *eocross = findScrambleType("eocross");
  REQUIRE(eocross);
  ColourNeutrality n = ColourNeutrality::preset(kWcaHold, Neutrality::X2Y);
  REQUIRE(n.holds.size() == 8);
  std::set<int> crossColours;
  std::set<int> fronts;
  for (Hold h : n.holds) {
    CubieCube s = randomStateOf(*eocross, rng);
    Alg scramble = scrambleToState(heldState(s, h));
    // From the WCA hold, the cross is on the face of the bottom colour.
    FaceletCube f;
    f.apply(scramble);
    int bottom = (h.top + 3) % 6;
    for (int e : {1, 3, 5, 7}) {
      CHECK(f[bottom * 9 + e] == bottom);
    }
    crossColours.insert(bottom);
    fronts.insert(h.front);
    // Turned to the hold, it is the EOCross state.
    FaceletCube seen = seenAfter(scramble, h);
    CHECK(seen == painted(s, h));
  }
  CHECK(crossColours == std::set<int>{kWhite, kYellow});
  CHECK(fronts == std::set<int>{kRed, kGreen, kOrange, kBlue});
}

TEST_CASE("neutrality presets") {
  auto count = [](Hold main, Neutrality n) {
    return neutralHolds(main, n).size();
  };
  for (Hold main : {kWcaHold, Hold{kYellow, kGreen}, Hold{kRed, kWhite}}) {
    CHECK(count(main, Neutrality::Fixed) == 1);
    CHECK(count(main, Neutrality::Y2) == 2);
    CHECK(count(main, Neutrality::Y) == 4);
    CHECK(count(main, Neutrality::X2) == 2);
    CHECK(count(main, Neutrality::X2Y2) == 4);
    CHECK(count(main, Neutrality::X2Y) == 8);
    CHECK(count(main, Neutrality::Full) == 24);
    for (int i = 0; i < kNeutralityCount; ++i) {
      auto n = static_cast<Neutrality>(i);
      std::vector<Hold> hs = neutralHolds(main, n);
      CHECK(std::find(hs.begin(), hs.end(), main) != hs.end());
      CHECK(std::is_sorted(hs.begin(), hs.end()));
    }
  }
  // Yellow top, green front: y keeps yellow on top.
  for (Hold h : neutralHolds({kYellow, kGreen}, Neutrality::Y)) {
    CHECK(h.top == kYellow);
  }
  // y2 from white/green adds white/blue; x2 adds yellow/blue.
  CHECK(neutralHolds(kWcaHold, Neutrality::Y2) ==
        std::vector<Hold>{{kWhite, kGreen}, {kWhite, kBlue}});
  CHECK(neutralHolds(kWcaHold, Neutrality::X2) ==
        std::vector<Hold>{{kWhite, kGreen}, {kYellow, kBlue}});
  for (Hold h : neutralHolds(kWcaHold, Neutrality::X2Y)) {
    CHECK((h.top == kWhite || h.top == kYellow));
  }
}

TEST_CASE("colour neutrality normalizes and matches presets") {
  ColourNeutrality n;
  CHECK(n.isFixed());
  CHECK(n.matchingPreset() == Neutrality::Fixed);
  n = ColourNeutrality::preset({kYellow, kGreen}, Neutrality::X2Y);
  CHECK(n.matchingPreset() == Neutrality::X2Y);
  n.holds.pop_back();
  CHECK_FALSE(n.matchingPreset());
  n.holds = {{kRed, kRed}, {kBlue, kWhite}, {kBlue, kWhite}};
  n.normalize();
  CHECK(n.holds == std::vector<Hold>{{kBlue, kWhite}});
  n.holds.clear();
  n.normalize();
  CHECK(n.holds == std::vector<Hold>{n.main});
  n.main = {kWhite, kYellow};
  n.normalize();
  CHECK(n.main == kWcaHold);

  Rng rng(5);
  ColourNeutrality full = ColourNeutrality::preset(kWcaHold, Neutrality::Full);
  std::set<Hold> picked;
  for (int i = 0; i < 2000; ++i) {
    picked.insert(full.pick(rng));
  }
  CHECK(picked.size() == 24);
}
