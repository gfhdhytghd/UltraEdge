#include "engine.hpp"
#include <cstdlib>
#include <iostream>
using namespace edge;
static int checks = 0;
static void check(bool b, const char *what) {
  ++checks;
  if (!b) {
    std::cerr << "FAIL: " << what << '\n';
    std::exit(1);
  }
}
static std::vector<Monitor> single = {{"A", {0, 0, 1000, 800}}};
static Action sample(Machine &s, Rule r, Point p, Point d, double t,
                     bool motion = true) {
  return s.step(r, single, p, d, t, motion);
}
int main() {
  Rule r;
  Machine s;
  check(sample(s, r, {0, 400}, {-30, 0}, 0) == Action::None,
        "arrival cannot fire");
  for (int t = 10; t < 300; t += 10)
    check(sample(s, r, {0, 400}, {-10, 0}, t) == Action::None,
          "continuous clamped movement cannot arm");
  check(sample(s, r, {0, 400}, {}, 480, false) == Action::None && s.armed,
        "stationary timer arms");
  check(sample(s, r, {0, 400}, {-.3, .2}, 490) == Action::None && s.armed,
        "small noise ignored");
  check(sample(s, r, {0, 400}, {0, 5}, 500) == Action::None && !s.armed,
        "tangent breaks arm");
  sample(s, r, {0, 400}, {}, 700, false);
  check(sample(s, r, {0, 400}, {2, 0}, 710) == Action::None && !s.armed,
        "reverse breaks arm");
  sample(s, r, {0, 400}, {}, 900, false);
  check(sample(s, r, {0, 400}, {-4, 0}, 910) == Action::None,
        "subthreshold second push");
  check(sample(s, r, {0, 400}, {-4, 0}, 920) == Action::Enter,
        "relative push despite identical clamped position");
  check(sample(s, r, {0, 400}, {-50, 0}, 930) == Action::None,
        "latched no repeats");
  check(sample(s, r, {79, 400}, {80, 0}, 940) == Action::None,
        "inside exit distance");
  check(sample(s, r, {81, 400}, {2, 0}, 950) == Action::Exit, "exit once");
  check(sample(s, r, {82, 400}, {1, 0}, 960) == Action::None, "exit no repeat");
  check(sample(s, r, {0, 400}, {-100, 0}, 970) == Action::None,
        "cooldown suppresses arrival");
  check(sample(s, r, {0, 400}, {-100, 0}, 1360) == Action::None,
        "cooldown requires fresh stop");
  sample(s, r, {0, 400}, {}, 1550, false);
  sample(s, r, {0, 400}, {-4, 0}, 1560);
  check(sample(s, r, {0, 400}, {-4, 0}, 1750) == Action::None,
        "push burst expires");
  check(sample(s, r, {0, 400}, {-4, 0}, 1760) == Action::Enter,
        "fresh burst fires");
  check(s.step(r, single, {0, 400}, {}, 1800, false, {}, false, true) ==
                Action::None &&
            !s.active,
        "pause clears latch without actions");
  for (auto target : {"left", "right", "top", "bottom", "top-left", "top-right",
                      "bottom-left", "bottom-right"}) {
    r.target = target;
    s.reset();
    auto d = direction(target);
    Point p{d.x < 0   ? 0.
            : d.x > 0 ? 999.
                      : 400.,
            d.y < 0   ? 0.
            : d.y > 0 ? 799.
                      : 400.};
    sample(s, r, p, {d.x * 20, d.y * 20}, 0);
    sample(s, r, p, {}, 200, false);
    if (d.x && d.y) {
      check(sample(s, r, p, {d.x * 10, 0}, 210) == Action::None,
            "corner needs both outward axes");
      sample(s, r, p, {}, 410, false);
    }
    check(sample(s, r, p, {d.x * 10, d.y * 10}, 420) == Action::Enter,
          "all edges/corners push");
  }
  r = {};
  r.mode = Mode::Enter;
  s.reset();
  check(sample(s, r, {0, 200}, {}, 0) == Action::Enter, "basic hot edge");
  check(s.step(r, single, {30, 200}, {}, 1, false, Rect{0, 0, 100, 800},
               true) == Action::None,
        "dynamic hold wider than edge");
  check(s.step(r, single, {30, 200}, {}, 2, false, Rect{0, 0, 20, 800}, true) ==
            Action::Exit,
        "dynamic hold shrink exits");
  s.reset();
  sample(s, r, {0, 200}, {}, 10);
  check(s.step(r, single, {0, 200}, {}, 11, false, {}, true) == Action::Exit,
        "missing provider closes latch");
  r.mode = Mode::Dwell;
  s.reset();
  check(sample(s, r, {0, 200}, {}, 0) == Action::None, "dwell begins");
  check(sample(s, r, {0, 200}, {}, 299, false) == Action::None,
        "dwell not early");
  check(sample(s, r, {0, 200}, {}, 300, false) == Action::Enter,
        "dwell timer fires");
  s.reset();
  sample(s, r, {0, 200}, {}, 0);
  sample(s, r, {10, 200}, {}, 100);
  sample(s, r, {0, 200}, {}, 200);
  check(sample(s, r, {0, 200}, {}, 400, false) == Action::None,
        "dwell leave resets timer");
  // Negative global origins and pre-scaled logical output dimensions (e.g. 2x
  // panel).
  std::vector<Monitor> pair = {{"L", {-1000, -200, 1000, 800}},
                               {"R", {0, 0, 1280, 720}}};
  for (auto mode : {Mode::Enter, Mode::Dwell, Mode::Push}) {
    r = {};
    r.mode = mode;
    r.target = "right";
    s.reset();
    for (int t = 0; t < 1000; t += 50)
      check(s.step(r, pair, {-1, 300}, {20, 0}, t, true) == Action::None,
            "shared segment masked all modes");
    s.reset();
    check(s.step(r, pair, {-1, -100}, {20, 0}, 0, true) ==
              (mode == Mode::Enter ? Action::Enter : Action::None),
          "partial shared segment exposed above neighbor");
  }
  r = {};
  r.mode = Mode::Enter;
  r.target = "top-right";
  s.reset();
  auto endpoint = pair;
  endpoint[1].box.y = -200;
  check(s.step(r, endpoint, {-1, -200}, {}, 0, false) == Action::None,
        "corner at shared endpoint masked");
  r.target = "right";
  s.reset();
  s.step(r, pair, {-1, -100}, {}, 0, false);
  check(s.active, "exposed segment latches");
  check(s.step(r, pair, {1, 100}, {2, 200}, 10, true) == Action::None &&
            !s.active && !s.candidate,
        "cross-monitor clears latch no exit");
  r.target = "left";
  s.reset();
  check(s.step(r, pair, {0, 300}, {-20, 0}, 0, true) == Action::None,
        "reciprocal shared edge masked");
  r.monitor = "missing";
  check(s.step(r, pair, {-1000, 0}, {-20, 0}, 300, true) == Action::None,
        "monitor selector");
  r.monitor = "*";
  r.target = "top";
  s.reset();
  std::vector<Monitor> vertical = {{"upper", {-300, -800, 900, 800}},
                                   {"lower", {-100, 0, 900, 600}}};
  check(s.step(r, vertical, {0, 0}, {0, -20}, 0, true) == Action::None,
        "vertical partial shared edge");
  check(s.step(r, vertical, {700, 0}, {0, -20}, 1, true) == Action::Enter,
        "vertical exposed remainder");
  check(s.step(r, vertical, {700, -1}, {0, -1}, 2, true) == Action::None &&
            !s.active,
        "outside all outputs clears silently");
  r = {};
  r.mode = Mode::Enter;
  r.holdWait = 5000;
  s.reset();
  sample(s, r, {0, 400}, {}, 0);
  check(s.step(r, single, {0, 400}, {}, 10, false, {}, true) == Action::None,
        "asynchronous geometry grace");
  check(s.step(r, single, {0, 400}, {}, 100, false, Rect{0, 0, 150, 800},
               true) == Action::None,
        "geometry arrives during grace");
  check(s.step(r, single, {0, 400}, {}, 110, false, {}, true) == Action::Exit,
        "manual hide ends grace after first geometry");
  s.reset();
  sample(s, r, {0, 400}, {}, 0);
  check(s.step(r, single, {0, 400}, {}, 5000, false, {}, true) == Action::Exit,
        "geometry timeout exits");
  r = {};
  s.reset();
  sample(s, r, {0, 400}, {}, 0);
  sample(s, r, {0, 400}, {}, 200, false);
  check(sample(s, r, {0, 400}, {-1, 100}, 210) == Action::None && !s.armed,
        "mostly tangent motion cannot count as push");
  for (int fingers : {2, 3, 4})
    for (auto dir : {"left", "right", "up", "down", "up-left", "down-right"}) {
      r = {};
      r.mode = Mode::Swipe;
      r.fingers = fingers;
      r.swipeDirection = dir;
      s.reset();
      Swipe swipe;
      check(!swipe.begin(r, s, fingers, 0),
            "swipe cannot start without edge arrival");
      sample(s, r, {0, 400}, {}, 0, false);
      check(!swipe.begin(r, s, fingers == 4 ? 3 : 4, 1),
            "wrong finger count rejected");
      check(swipe.begin(r, s, fingers, 2),
            "matching swipe begins at exposed edge");
      auto d = swipeDirection(dir);
      swipe.update(r, {d.x * 45, d.y * 45});
      check(!s.active, "no dispatch before swipe end");
      check(swipe.end(r, s, 10, true) == Action::None,
            "cancelled swipe never activates");
      swipe.begin(r, s, fingers, 20);
      swipe.update(r, {d.x * 45, d.y * 45});
      check(swipe.end(r, s, 30, false) == Action::Enter,
            "all finger counts and directions activate on end");
      check(!swipe.begin(r, s, fingers, 31),
            "active swipe latch cannot repeat");
      check(sample(s, r, {100, 400}, {}, 40, false) == Action::Exit,
            "swipe uses common paired exit");
    }
  r = {};
  r.mode = Mode::Swipe;
  r.swipeDirection = "right";
  s.reset();
  Swipe swipe;
  sample(s, r, {0, 400}, {}, 0, false);
  swipe.begin(r, s, 3, 1);
  swipe.update(r, {39, 0});
  check(swipe.end(r, s, 2, false) == Action::None,
        "subthreshold swipe cannot fire");
  swipe.begin(r, s, 3, 3);
  swipe.update(r, {60, 0});
  swipe.update(r, {-2, 0});
  check(swipe.end(r, s, 4, false) == Action::None,
        "swipe reversal invalidates gesture");
  swipe.begin(r, s, 3, 5);
  swipe.update(r, {50, 100});
  check(swipe.end(r, s, 6, false) == Action::None,
        "wrong dominant swipe direction rejected");
  swipe.begin(r, s, 3, 7);
  swipe.update(r, {60, 0});
  sample(s, r, {50, 400}, {}, 8, false);
  sample(s, r, {0, 400}, {}, 9, false);
  check(swipe.end(r, s, 10, false) == Action::None,
        "leave and return cannot inherit swipe");
  swipe.begin(r, s, 3, 11);
  swipe.update(r, {60, 0});
  s.reset();
  sample(s, r, {0, 400}, {}, 12, false);
  check(swipe.end(r, s, 13, false) == Action::None,
        "pause/reload/layout reset invalidates swipe generation");
  r.target = "right";
  s.reset();
  s.step(r, pair, {-1, 300}, {}, 0, false);
  check(!swipe.begin(r, s, 3, 1), "shared boundary cannot claim swipe");
  s.reset();
  s.step(r, pair, {-1, -100}, {}, 0, false);
  swipe.begin(r, s, 3, 1);
  swipe.update(r, {60, 0});
  s.step(r, pair, {1, 100}, {}, 2, false);
  s.step(r, pair, {-1, -100}, {}, 3, false);
  s.step(r, pair, {-1, -100}, {}, 4, false);
  check(swipe.end(r, s, 5, false) == Action::None,
        "cross output and return cannot inherit swipe");
  std::cout << checks << " state-machine checks passed\n";
}
