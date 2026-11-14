#pragma once

#include "cube/move.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vcube {

// Fingertricks and regrips: how an alg is best turned with two hands.
//
// Each hand's grip is where its thumb sits on the cube, as the wrist turns
// it: home, up a quarter turn (the right thumb after R from home, the left
// thumb after L'), down a quarter turn, or far up or down (a half turn
// from home, a strain). A trick turns one move with one hand from some
// grips (a right index flick does U with the right thumb home or up) and
// may turn that hand's wrist (R moves the right thumb up a step). Between
// moves a hand can regrip to another grip, which costs less while the
// other hand is turning. planGrips() finds the cheapest way through an
// alg: the grip to start in, a trick for each move and the regrips.

enum class Hand : std::uint8_t { Right, Left, Both, Either };
std::string_view handId(Hand h); // "right", "left", "both", "either"
std::optional<Hand> handFromId(std::string_view id);

enum class Finger : std::uint8_t { Thumb, Index, Middle, Ring, Wrist, Hands };
std::string_view fingerId(Finger f); // "thumb", ..., "wrist", "hands"
std::optional<Finger> fingerFromId(std::string_view id);

inline constexpr int kGripMin = -2; // far down
inline constexpr int kGripMax = 2;  // far up
inline constexpr int kGripCount = kGripMax - kGripMin + 1;

// "far down", "down", "home", "up", "far up".
std::string_view gripName(int grip);
std::optional<int> gripFromName(std::string_view name);

// A bit per grip (bit 0 = far down), for the grips a trick starts from.
using GripMask = std::uint8_t;
constexpr GripMask gripBit(int grip) {
  return static_cast<GripMask>(1u << (grip - kGripMin));
}
inline constexpr GripMask kAnyGrip = (1u << kGripCount) - 1;
// "home, up" and the other way; nullopt for text that isn't grips. An
// empty mask is written "" and read from "" or "any" as every grip.
std::string gripMaskText(GripMask m);
std::optional<GripMask> gripMaskFromText(std::string_view text);

struct GripTrick {
  std::string move; // as written: "U", "R2", "F'"
  Hand hand = Hand::Right;
  Finger finger = Finger::Index;
  std::string how;          // "flick", "push", "pull", ...
  GripMask from = kAnyGrip; // the hand's grips it starts from
  int turn = 0;             // steps the hand's thumb moves (R: +1)
  bool reset = false;       // both hands end home (a rotation)
  double cost = 1.0;
  bool on = true; // off: never chosen
  friend bool operator==(const GripTrick &, const GripTrick &) = default;
};

// "right index flick", "right wrist", "both hands rotate".
std::string trickText(const GripTrick &t);

struct GripModel {
  std::vector<GripTrick> tricks;
  double otherMove = 2.5;    // a move no trick turns
  double regrip = 1.2;       // a hand regrips while it would turn
  double hiddenRegrip = 0.4; // a hand regrips while the other one turns
  double regripStep = 0.3;   // each step of a regrip past the first
  double strain = 1.5;       // each move ending with a hand far up or down
  double start = 0.25;       // each step from home of the starting grips
  double sameFinger = 0.4;   // a finger turning two moves in a row
  friend bool operator==(const GripModel &, const GripModel &) = default;
};

// v-cube's standard two-handed catalogue: flicks, pushes and pulls, wrist
// turns, ring-finger D moves, thumb and index F moves, and rotations as a
// regrip of both hands.
GripModel standardGripModel();

struct Grip {
  int right = 0;
  int left = 0;
  friend bool operator==(Grip, Grip) = default;
};

// One move of a plan.
struct GripStep {
  Move move;
  int trick = -1; // index in the model's tricks; -1: no trick turns it
  Grip before;    // after any regrip, as the move starts
  Grip after;
  bool regripRight = false; // the hand regrips before this move
  bool regripLeft = false;
  double cost = 0.0; // the trick, its regrips and penalties
};

struct GripPlan {
  Grip start;
  std::vector<GripStep> steps;
  double total = 0.0;
  double tricks = 0.0; // the tricks' own costs
  double extras = 0.0; // regrips, strain, the start, a finger reused
  int regrips = 0;     // hand regrips (two hands at once count two)
  int untricked = 0;   // moves no trick turns
};

// For each move, every trick that can turn it and the cheapest whole plan
// that uses it there, best first.
struct GripOption {
  int trick = -1;
  double total = 0.0;
};

// Constraints for planGrips: forced[i] is the trick move i must use (-1
// for any). Shorter than the alg leaves the rest free.
struct GripChoice {
  std::vector<int> forced;
};

struct GripResult {
  GripPlan plan;
  std::vector<std::vector<GripOption>> options; // per move, when asked
};

GripResult planGrips(const Alg &alg, const GripModel &model,
                     const GripChoice &choice = {}, bool withOptions = false);

// The plan's cost alone (what a ranking needs, without the steps).
double gripCost(const Alg &alg, const GripModel &model);

// The plan as text, a line per move with its regrips, for copying.
std::string gripPlanText(const GripPlan &plan, const GripModel &model);

} // namespace vcube
