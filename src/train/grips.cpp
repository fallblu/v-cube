#include "train/grips.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace vcube {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

constexpr std::array<std::string_view, 4> kHandIds = {"right", "left", "both",
                                                      "either"};
constexpr std::array<std::string_view, 6> kFingerIds = {
    "thumb", "index", "middle", "ring", "wrist", "hands"};
constexpr std::array<std::string_view, kGripCount> kGripNames = {
    "far down", "down", "home", "up", "far up"};

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
    s.remove_suffix(1);
  }
  return s;
}

} // namespace

std::string_view handId(Hand h) { return kHandIds[static_cast<int>(h)]; }

std::optional<Hand> handFromId(std::string_view id) {
  for (std::size_t i = 0; i < kHandIds.size(); ++i) {
    if (kHandIds[i] == id) {
      return static_cast<Hand>(i);
    }
  }
  return std::nullopt;
}

std::string_view fingerId(Finger f) { return kFingerIds[static_cast<int>(f)]; }

std::optional<Finger> fingerFromId(std::string_view id) {
  for (std::size_t i = 0; i < kFingerIds.size(); ++i) {
    if (kFingerIds[i] == id) {
      return static_cast<Finger>(i);
    }
  }
  return std::nullopt;
}

std::string_view gripName(int grip) {
  if (grip < kGripMin || grip > kGripMax) {
    return "?";
  }
  return kGripNames[static_cast<std::size_t>(grip - kGripMin)];
}

std::optional<int> gripFromName(std::string_view name) {
  name = trim(name);
  for (int g = kGripMin; g <= kGripMax; ++g) {
    if (gripName(g) == name) {
      return g;
    }
  }
  // Numbers too: -2 .. 2.
  if (name == "-2") {
    return -2;
  }
  if (name == "-1") {
    return -1;
  }
  if (name == "0") {
    return 0;
  }
  if (name == "1" || name == "+1") {
    return 1;
  }
  if (name == "2" || name == "+2") {
    return 2;
  }
  return std::nullopt;
}

std::string gripMaskText(GripMask m) {
  if ((m & kAnyGrip) == kAnyGrip) {
    return "any";
  }
  std::string out;
  for (int g = kGripMin; g <= kGripMax; ++g) {
    if (m & gripBit(g)) {
      if (!out.empty()) {
        out += ", ";
      }
      out += gripName(g);
    }
  }
  return out;
}

std::optional<GripMask> gripMaskFromText(std::string_view text) {
  text = trim(text);
  if (text.empty() || text == "any") {
    return kAnyGrip;
  }
  GripMask m = 0;
  while (!text.empty()) {
    auto comma = text.find(',');
    std::string_view part = trim(text.substr(0, comma));
    auto g = gripFromName(part);
    if (!g) {
      return std::nullopt;
    }
    m |= gripBit(*g);
    if (comma == std::string_view::npos) {
      break;
    }
    text.remove_prefix(comma + 1);
  }
  return m;
}

std::string trickText(const GripTrick &t) {
  std::string s;
  switch (t.hand) {
  case Hand::Right:
    s = "right";
    break;
  case Hand::Left:
    s = "left";
    break;
  case Hand::Both:
    s = "both";
    break;
  case Hand::Either:
    s = "either";
    break;
  }
  s += ' ';
  s += fingerId(t.finger);
  if (!t.how.empty()) {
    s += ' ';
    s += t.how;
  }
  return s;
}

GripModel standardGripModel() {
  constexpr GripMask fd = gripBit(-2), dn = gripBit(-1), hm = gripBit(0),
                     up = gripBit(1), fu = gripBit(2);
  using H = Hand;
  using F = Finger;
  GripModel m;
  auto add = [&](const char *move, H hand, F finger, const char *how,
                 GripMask from, int turn, double cost) {
    GripTrick t;
    t.move = move;
    t.hand = hand;
    t.finger = finger;
    t.how = how;
    t.from = from;
    t.turn = turn;
    t.cost = cost;
    m.tricks.push_back(std::move(t));
  };
  // U layer: flicks and pushes.
  add("U", H::Right, F::Index, "flick", hm | up, 0, 1.0);
  add("U", H::Left, F::Index, "push", hm, 0, 1.7);
  add("U'", H::Left, F::Index, "flick", hm, 0, 1.0);
  add("U'", H::Right, F::Index, "push", dn, 0, 1.4);
  add("U'", H::Right, F::Index, "push", hm, 0, 1.7);
  add("U2", H::Right, F::Index, "double flick", hm, 0, 1.5);
  add("U2", H::Left, F::Index, "double flick", hm, 0, 1.6);
  // R layer: the right wrist.
  add("R", H::Right, F::Wrist, "", fd | dn | hm | up, 1, 1.0);
  add("R'", H::Right, F::Wrist, "", dn | hm | up | fu, -1, 1.0);
  add("R2", H::Right, F::Wrist, "up", fd | dn | hm, 2, 1.5);
  add("R2", H::Right, F::Wrist, "down", hm | up | fu, -2, 1.5);
  // L layer: the left wrist (L moves the left thumb down).
  add("L", H::Left, F::Wrist, "", dn | hm | up | fu, -1, 1.3);
  add("L'", H::Left, F::Wrist, "", fd | dn | hm | up, 1, 1.3);
  add("L2", H::Left, F::Wrist, "down", hm | up | fu, -2, 1.9);
  add("L2", H::Left, F::Wrist, "up", fd | dn | hm, 2, 1.9);
  // D layer: ring fingers.
  add("D", H::Left, F::Ring, "pull", hm, 0, 1.4);
  add("D", H::Right, F::Ring, "push", hm, 0, 1.9);
  add("D'", H::Right, F::Ring, "pull", hm, 0, 1.4);
  add("D'", H::Right, F::Ring, "pull", up, 0, 1.8);
  add("D'", H::Left, F::Ring, "push", hm, 0, 1.9);
  add("D2", H::Left, F::Ring, "double pull", hm, 0, 2.0);
  add("D2", H::Right, F::Ring, "double pull", hm, 0, 2.1);
  // F layer: index from the top with the thumb down, thumbs from the front.
  add("F", H::Right, F::Index, "push from the top", dn, 0, 1.3);
  add("F", H::Left, F::Thumb, "push", hm, 0, 1.6);
  add("F", H::Right, F::Index, "reach over the top", hm, 0, 2.2);
  add("F'", H::Right, F::Thumb, "push", hm, 0, 1.5);
  add("F'", H::Left, F::Index, "push from the top", dn, 0, 1.3);
  add("F'", H::Left, F::Index, "reach over the top", hm, 0, 2.2);
  add("F2", H::Right, F::Index, "double push from the top", dn, 0, 2.0);
  add("F2", H::Right, F::Thumb, "double push", hm, 0, 2.4);
  add("F2", H::Left, F::Thumb, "double push", hm, 0, 2.4);
  // B layer: ring fingers from behind.
  add("B", H::Left, F::Ring, "pull", hm, 0, 2.6);
  add("B'", H::Right, F::Ring, "pull", hm, 0, 2.6);
  add("B2", H::Right, F::Ring, "double pull", hm, 0, 3.4);
  add("B2", H::Left, F::Ring, "double pull", hm, 0, 3.4);
  // Wide R and L turns: the wrist, as R and L.
  add("r", H::Right, F::Wrist, "", fd | dn | hm | up, 1, 1.2);
  add("r'", H::Right, F::Wrist, "", dn | hm | up | fu, -1, 1.2);
  add("r2", H::Right, F::Wrist, "up", fd | dn | hm, 2, 1.8);
  add("r2", H::Right, F::Wrist, "down", hm | up | fu, -2, 1.8);
  add("l", H::Left, F::Wrist, "", dn | hm | up | fu, -1, 1.6);
  add("l'", H::Left, F::Wrist, "", fd | dn | hm | up, 1, 1.6);
  add("l2", H::Left, F::Wrist, "", hm | up | fu, -2, 2.3);
  // M slice: the left ring finger.
  add("M'", H::Left, F::Ring, "push", hm, 0, 1.4);
  add("M", H::Left, F::Ring, "pull", hm, 0, 1.6);
  add("M2", H::Left, F::Ring, "double push", hm, 0, 2.0);
  // Rotations: both hands let go and take the cube again, home.
  for (const char *r : {"x", "x'", "y", "y'", "z", "z'"}) {
    double c = r[0] == 'y' ? 1.5 : r[0] == 'x' ? 1.8 : 2.2;
    add(r, H::Both, F::Hands, "regrip", kAnyGrip, 0, c);
    m.tricks.back().reset = true;
  }
  for (const char *r : {"x2", "y2", "z2"}) {
    double c = r[0] == 'y' ? 2.2 : r[0] == 'x' ? 2.6 : 3.0;
    add(r, H::Both, F::Hands, "regrip", kAnyGrip, 0, c);
    m.tricks.back().reset = true;
  }
  return m;
}

namespace {

// A DP state: both grips and the finger that turned the last move (0:
// none, a wrist or a move no trick turns), for the finger penalty.
constexpr int kSlots = 9; // none, then right and left thumb .. ring

int fingerSlot(const GripTrick &t) {
  if ((t.hand != Hand::Right && t.hand != Hand::Left) ||
      t.finger == Finger::Wrist || t.finger == Finger::Hands) {
    return 0;
  }
  return 1 + (t.hand == Hand::Left ? 4 : 0) + static_cast<int>(t.finger);
}

struct Space {
  static constexpr std::size_t size() {
    return kGripCount * kGripCount * kSlots;
  }
  static std::size_t id(int right, int left, int slot) {
    return static_cast<std::size_t>(
        ((right - kGripMin) * kGripCount + (left - kGripMin)) * kSlots + slot);
  }
  static int right(std::size_t id) {
    return static_cast<int>(id / kSlots / kGripCount) + kGripMin;
  }
  static int left(std::size_t id) {
    return static_cast<int>(id / kSlots % kGripCount) + kGripMin;
  }
  static int slot(std::size_t id) { return static_cast<int>(id % kSlots); }
};

struct Edge {
  std::size_t to = 0;
  int trick = -1;
  int right = 0, left = 0; // grips after any regrip
  double cost = 0.0;
};

class Planner {
public:
  Planner(const Alg &alg, const GripModel &model, const GripChoice &choice)
      : model_(model) {
    for (const Move &m : alg) {
      std::string w = toString(m);
      std::vector<int> cands;
      for (std::size_t t = 0; t < model.tricks.size(); ++t) {
        const GripTrick &k = model.tricks[t];
        if (k.on && k.move == w) {
          cands.push_back(static_cast<int>(t));
        }
      }
      if (cands.empty()) {
        cands.push_back(-1);
      }
      all_.push_back(cands);
    }
    allowed_ = all_;
    for (std::size_t i = 0; i < choice.forced.size() && i < all_.size(); ++i) {
      int f = choice.forced[i];
      if (f >= 0 &&
          std::find(all_[i].begin(), all_[i].end(), f) != all_[i].end()) {
        allowed_[i] = {f};
      }
    }
  }

  std::size_t moves() const { return all_.size(); }
  const Space &space() const { return space_; }
  const std::vector<int> &allowed(std::size_t i) const { return allowed_[i]; }
  const std::vector<int> &all(std::size_t i) const { return all_[i]; }

  // Starting off home costs a little (and a strain is a strain).
  double startCost(std::size_t s) const {
    if (space_.slot(s) != 0) {
      return kInf;
    }
    int r = space_.right(s), l = space_.left(s);
    return model_.start * (std::abs(r) + std::abs(l)) +
           model_.strain *
               ((std::abs(r) == 2 ? 1 : 0) + (std::abs(l) == 2 ? 1 : 0));
  }

  // Every way to turn a move with trick `t` from state `s`.
  template <typename F> void edges(std::size_t s, int t, F &&visit) const {
    const int r0 = space_.right(s), l0 = space_.left(s);
    static const GripTrick kNone{"", Hand::Either, Finger::Hands, "",  kAnyGrip,
                                 0,  false,        0.0,           true};
    const GripTrick &k =
        t >= 0 ? model_.tricks[static_cast<std::size_t>(t)] : kNone;
    const double base = t >= 0 ? k.cost : model_.otherMove;
    const int slot = t >= 0 ? fingerSlot(k) : 0;
    const double same =
        slot != 0 && slot == space_.slot(s) ? model_.sameFinger : 0.0;
    auto regripCost = [&](int from, int to, bool hidden) {
      if (from == to) {
        return 0.0;
      }
      return (hidden ? model_.hiddenRegrip : model_.regrip) +
             model_.regripStep * (std::abs(from - to) - 1);
    };
    for (int r = kGripMin; r <= kGripMax; ++r) {
      if (k.hand == Hand::Right && !(k.from & gripBit(r))) {
        continue;
      }
      if (k.reset && r != r0) {
        continue; // no point regripping before letting go
      }
      double cr = regripCost(r0, r, k.hand == Hand::Left);
      for (int l = kGripMin; l <= kGripMax; ++l) {
        if (k.hand == Hand::Left && !(k.from & gripBit(l))) {
          continue;
        }
        if (k.reset && l != l0) {
          continue;
        }
        double cl = regripCost(l0, l, k.hand == Hand::Right);
        int r1 = r, l1 = l;
        if (k.reset) {
          r1 = l1 = 0;
        } else if (k.hand == Hand::Right) {
          r1 += k.turn;
        } else if (k.hand == Hand::Left) {
          l1 += k.turn;
        }
        if (r1 < kGripMin || r1 > kGripMax || l1 < kGripMin || l1 > kGripMax) {
          continue;
        }
        double strain = model_.strain * ((std::abs(r1) == 2 ? 1 : 0) +
                                         (std::abs(l1) == 2 ? 1 : 0));
        visit(Edge{space_.id(r1, l1, slot), t, r, l,
                   base + cr + cl + strain + same});
      }
    }
  }

private:
  const GripModel &model_;
  Space space_;
  std::vector<std::vector<int>> all_;     // tricks that turn each move
  std::vector<std::vector<int>> allowed_; // with the forced ones
};

struct Back {
  std::size_t from = 0;
  Edge edge;
};

} // namespace

GripResult planGrips(const Alg &alg, const GripModel &model,
                     const GripChoice &choice, bool withOptions) {
  GripResult out;
  Planner pl(alg, model, choice);
  const std::size_t n = pl.moves();
  const std::size_t S = Space::size();

  // Forward: the cheapest way to each state before each move.
  std::vector<std::vector<double>> fwd(n + 1, std::vector<double>(S, kInf));
  std::vector<std::vector<Back>> back(n + 1, std::vector<Back>(S));
  for (std::size_t s = 0; s < S; ++s) {
    fwd[0][s] = pl.startCost(s);
  }
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t s = 0; s < S; ++s) {
      double c0 = fwd[i][s];
      if (c0 == kInf) {
        continue;
      }
      for (int t : pl.allowed(i)) {
        pl.edges(s, t, [&](const Edge &e) {
          double c = c0 + e.cost;
          if (c < fwd[i + 1][e.to]) {
            fwd[i + 1][e.to] = c;
            back[i + 1][e.to] = {s, e};
          }
        });
      }
    }
  }
  std::size_t best = 0;
  for (std::size_t s = 1; s < S; ++s) {
    if (fwd[n][s] < fwd[n][best]) {
      best = s;
    }
  }

  // The plan, from the back.
  GripPlan &plan = out.plan;
  plan.total = fwd[n][best];
  plan.steps.resize(n);
  std::size_t s = best;
  for (std::size_t i = n; i-- > 0;) {
    const Back &b = back[i + 1][s];
    GripStep &st = plan.steps[i];
    st.move = alg[i];
    st.trick = b.edge.trick;
    int r0 = pl.space().right(b.from), l0 = pl.space().left(b.from);
    st.before = {b.edge.right, b.edge.left};
    st.after = {pl.space().right(s), pl.space().left(s)};
    st.regripRight = b.edge.right != r0;
    st.regripLeft = b.edge.left != l0;
    st.cost = b.edge.cost;
    s = b.from;
  }
  plan.start = {pl.space().right(s), pl.space().left(s)};
  for (const GripStep &st : plan.steps) {
    if (st.trick >= 0) {
      plan.tricks += model.tricks[static_cast<std::size_t>(st.trick)].cost;
    } else {
      plan.tricks += model.otherMove;
      ++plan.untricked;
    }
    plan.regrips += (st.regripRight ? 1 : 0) + (st.regripLeft ? 1 : 0);
  }
  plan.extras = plan.total - plan.tricks;
  if (n == 0) {
    plan.total = plan.extras = 0.0;
  }

  if (!withOptions) {
    return out;
  }
  // Backward: the cheapest way to finish from each state before each move.
  std::vector<std::vector<double>> bwd(n + 1, std::vector<double>(S, kInf));
  std::fill(bwd[n].begin(), bwd[n].end(), 0.0);
  for (std::size_t i = n; i-- > 0;) {
    for (std::size_t s0 = 0; s0 < S; ++s0) {
      double c = kInf;
      for (int t : pl.allowed(i)) {
        pl.edges(s0, t, [&](const Edge &e) {
          c = std::min(c, e.cost + bwd[i + 1][e.to]);
        });
      }
      bwd[i][s0] = c;
    }
  }
  out.options.resize(n);
  for (std::size_t i = 0; i < n; ++i) {
    for (int t : pl.all(i)) {
      double c = kInf;
      for (std::size_t s0 = 0; s0 < S; ++s0) {
        if (fwd[i][s0] == kInf) {
          continue;
        }
        pl.edges(s0, t, [&](const Edge &e) {
          c = std::min(c, fwd[i][s0] + e.cost + bwd[i + 1][e.to]);
        });
      }
      if (c < kInf) {
        out.options[i].push_back({t, c});
      }
    }
    auto &opts = out.options[i];
    std::stable_sort(opts.begin(), opts.end(),
                     [](const GripOption &a, const GripOption &b) {
                       return a.total < b.total;
                     });
    // Entries of one trick from different grips: the cheapest only.
    std::vector<GripOption> kept;
    for (const GripOption &o : opts) {
      bool seen = std::any_of(kept.begin(), kept.end(), [&](const auto &k) {
        return k.trick >= 0 && o.trick >= 0 &&
               trickText(model.tricks[static_cast<std::size_t>(k.trick)]) ==
                   trickText(model.tricks[static_cast<std::size_t>(o.trick)]);
      });
      if (!seen) {
        kept.push_back(o);
      }
    }
    opts = std::move(kept);
  }
  return out;
}

double gripCost(const Alg &alg, const GripModel &model) {
  Planner pl(alg, model, {});
  const std::size_t S = Space::size();
  std::vector<double> cur(S), next(S);
  for (std::size_t s = 0; s < S; ++s) {
    cur[s] = pl.startCost(s);
  }
  for (std::size_t i = 0; i < pl.moves(); ++i) {
    std::fill(next.begin(), next.end(), kInf);
    for (std::size_t s = 0; s < S; ++s) {
      if (cur[s] == kInf) {
        continue;
      }
      for (int t : pl.allowed(i)) {
        pl.edges(s, t, [&](const Edge &e) {
          next[e.to] = std::min(next[e.to], cur[s] + e.cost);
        });
      }
    }
    cur.swap(next);
  }
  if (pl.moves() == 0) {
    return 0.0;
  }
  return *std::min_element(cur.begin(), cur.end());
}

std::string gripPlanText(const GripPlan &plan, const GripModel &model) {
  auto grips = [](Grip g) {
    return "right thumb " + std::string(gripName(g.right)) + ", left thumb " +
           std::string(gripName(g.left));
  };
  std::string out = "Start: " + grips(plan.start) + "\n";
  for (const GripStep &st : plan.steps) {
    const GripTrick *t = st.trick >= 0
                             ? &model.tricks[static_cast<std::size_t>(st.trick)]
                             : nullptr;
    auto regrip = [&](const char *hand, int to, Hand mover) {
      out += "  regrip the ";
      out += hand;
      out += " hand to ";
      out += gripName(to);
      if (t && t->hand == mover) {
        out += " (while the other hand turns)";
      }
      out += "\n";
    };
    if (st.regripRight) {
      regrip("right", st.before.right, Hand::Left);
    }
    if (st.regripLeft) {
      regrip("left", st.before.left, Hand::Right);
    }
    std::string m = toString(st.move);
    out += m;
    out += std::string(m.size() < 4 ? 4 - m.size() : 1, ' ');
    if (t) {
      out += trickText(*t);
      if (t->hand == Hand::Right && st.after.right != st.before.right) {
        out += " (right thumb ";
        out += gripName(st.after.right);
        out += ")";
      } else if (t->hand == Hand::Left && st.after.left != st.before.left) {
        out += " (left thumb ";
        out += gripName(st.after.left);
        out += ")";
      }
    } else {
      out += "no trick for this move";
    }
    out += "\n";
  }
  return out;
}

} // namespace vcube
