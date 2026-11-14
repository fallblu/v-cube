#include "train/ergonomics.hpp"
#include "train/grips.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <string>

using namespace vcube;

namespace {

GripPlan plan(const std::string &alg, const GripChoice &choice = {}) {
  return planGrips(parseAlgOrEmpty(alg), standardGripModel(), choice).plan;
}

// "right index flick" ... for each move.
std::vector<std::string> tricks(const GripPlan &p) {
  GripModel m = standardGripModel();
  std::vector<std::string> out;
  for (const GripStep &s : p.steps) {
    out.push_back(s.trick >= 0
                      ? trickText(m.tricks[static_cast<std::size_t>(s.trick)])
                      : "none");
  }
  return out;
}

} // namespace

TEST_CASE("grip names and masks", "[grips]") {
  CHECK(gripName(0) == "home");
  CHECK(gripName(-2) == "far down");
  CHECK(gripFromName(" up ") == 1);
  CHECK(gripFromName("+2") == 2);
  CHECK_FALSE(gripFromName("sideways"));
  CHECK(gripMaskText(gripBit(0) | gripBit(1)) == "home, up");
  CHECK(gripMaskText(kAnyGrip) == "any");
  CHECK(gripMaskFromText("home, up") == (gripBit(0) | gripBit(1)));
  CHECK(gripMaskFromText("") == kAnyGrip);
  CHECK(gripMaskFromText("-1,0") == (gripBit(-1) | gripBit(0)));
  CHECK_FALSE(gripMaskFromText("home, nowhere"));
  CHECK(handFromId("left") == Hand::Left);
  CHECK(fingerFromId("ring") == Finger::Ring);
}

TEST_CASE("the sexy move and sune need no regrips", "[grips]") {
  GripPlan p = plan("R U R' U'");
  CHECK(p.regrips == 0);
  CHECK(p.start == Grip{0, 0});
  CHECK(tricks(p) ==
        std::vector<std::string>{"right wrist", "right index flick",
                                 "right wrist", "left index flick"});
  CHECK(p.steps[0].after.right == 1);
  CHECK(p.total == Catch::Approx(4.0));

  GripPlan sune = plan("R U R' U R U2 R'");
  CHECK(sune.regrips == 0);
  CHECK(sune.steps[5].trick >= 0);
  CHECK(tricks(sune)[5] == "right index double flick");
}

TEST_CASE("T perm: F from the top after R', F' with the thumb", "[grips]") {
  GripPlan p = plan("R U R' U' R' F R2 U' R' U' R U R' F'");
  CHECK(p.regrips == 0);
  auto t = tricks(p);
  CHECK(t[5] == "right index push from the top");
  CHECK(t[6] == "right wrist up");
  CHECK(t[13] == "right thumb push");
  CHECK(p.untricked == 0);
}

TEST_CASE("R2-heavy algs start with the thumb down or up", "[grips]") {
  // R2 from home would go far up: starting a step off home is cheaper.
  GripPlan p = plan("R2 U R U R' U' R' U' R' U R'");
  CHECK(p.start.right != 0);
  CHECK(std::abs(p.start.right) < 2);
}

TEST_CASE("regrips hide behind the other hand's moves", "[grips]") {
  GripModel m = standardGripModel();
  // F after R needs the thumb down (or a thumb or reach trick): the right
  // hand can regrip while the left flicks U'.
  GripPlan p = planGrips(parseAlgOrEmpty("R U' F"), m).plan;
  CHECK(p.total < 1.0 + 1.0 + 1.3 + m.regrip);
}

TEST_CASE("forced tricks and the options for each move", "[grips]") {
  GripModel m = standardGripModel();
  Alg alg = parseAlgOrEmpty("R U R' D R U' R' D'");
  GripResult r = planGrips(alg, m, {}, true);
  REQUIRE(r.options.size() == alg.size());
  // The best option of each move is the plan's own trick and total.
  for (std::size_t i = 0; i < alg.size(); ++i) {
    REQUIRE_FALSE(r.options[i].empty());
    CHECK(r.options[i].front().total == Catch::Approx(r.plan.total));
    CHECK(r.options[i].front().trick == r.plan.steps[i].trick);
  }
  // D can be turned by either hand; forcing the other one costs more and
  // the forced plan's total is that option's.
  REQUIRE(r.options[3].size() >= 2);
  const GripOption &other = r.options[3][1];
  GripChoice c;
  c.forced = {-1, -1, -1, other.trick};
  GripPlan forced = planGrips(alg, m, c).plan;
  CHECK(forced.steps[3].trick == other.trick);
  CHECK(forced.total == Catch::Approx(other.total));
  CHECK(forced.total >= r.plan.total);
}

TEST_CASE("moves with no trick, rotations and turned-off tricks", "[grips]") {
  GripModel m = standardGripModel();
  GripPlan p = planGrips(parseAlgOrEmpty("R E R'"), m).plan;
  CHECK(p.untricked == 1);
  CHECK(p.steps[1].trick == -1);
  CHECK(p.total >= m.otherMove + 2.0);

  // A rotation sets both hands home.
  GripPlan rot = planGrips(parseAlgOrEmpty("R y R"), m).plan;
  CHECK(rot.steps[1].after == Grip{0, 0});

  // With the left flick off, U' goes to the right index push.
  for (auto &t : m.tricks) {
    if (t.move == "U'" && t.hand == Hand::Left) {
      t.on = false;
    }
  }
  GripPlan off = planGrips(parseAlgOrEmpty("U'"), m).plan;
  REQUIRE(off.steps[0].trick >= 0);
  CHECK(m.tricks[static_cast<std::size_t>(off.steps[0].trick)].hand ==
        Hand::Right);

  CHECK(planGrips({}, m).plan.total == 0.0);
  CHECK(gripCost({}, m) == 0.0);
}

TEST_CASE("gripCost matches the plan and grip profiles score with it",
          "[grips]") {
  GripModel m = standardGripModel();
  for (const char *a :
       {"R U R' U'", "R2 D' R U2 R' D R U2 R", "F R U R' U' F'",
        "R U2 R2 F R F' U2 R' F R F'", "r U R' U' r' F R F'", "M' U M U2"}) {
    Alg alg = parseAlgOrEmpty(a);
    CHECK(gripCost(alg, m) == Catch::Approx(planGrips(alg, m).plan.total));
  }
  const ErgoProfile &g = builtinErgoProfiles().front();
  REQUIRE(g.grips);
  CHECK(g.name == kGripProfileName);
  Alg sexy = parseAlgOrEmpty("R U R' U'");
  CHECK(ergoTotal(sexy, g) == Catch::Approx(ergoScore(sexy, g).total));
  CHECK(ergoScore(parseAlgOrEmpty("R U R' U'"), g).total <
        ergoScore(parseAlgOrEmpty("B U B' U'"), g).total);
}

TEST_CASE("the plan as text", "[grips]") {
  GripModel m = standardGripModel();
  std::string text = gripPlanText(plan("R U R' U'"), m);
  CHECK(text.rfind("Start: right thumb home, left thumb home\n", 0) == 0);
  CHECK(text.find("R   right wrist (right thumb up)\n") != std::string::npos);
  CHECK(text.find("U   right index flick\n") != std::string::npos);
}
