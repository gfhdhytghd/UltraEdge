#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace edge {
struct Point {
  double x = 0, y = 0;
};
struct Rect {
  double x = 0, y = 0, w = 0, h = 0;
  bool contains(Point p) const {
    return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h;
  }
};
struct Monitor {
  std::string name;
  Rect box;
};
enum class Mode { Enter, Dwell, Push, Move, Swipe };
enum class Action { None, Enter, Exit };
struct Rule {
  std::string monitor = "*", target = "left";
  Mode mode = Mode::Push;
  int fingers = 3;
  std::string swipeDirection = "right";
  double swipeDistance = 40;
  bool consume = true;
  double band = 2, pause = 180, push = 8, noise = .5, exit = 80, cooldown = 400,
         dwell = 300, burst = 160, holdWait = 0, move = 100, movementGap = 80,
         pushAngle = 90;
};
inline Point direction(const std::string &s) {
  return {s.find("left") != std::string::npos    ? -1.
          : s.find("right") != std::string::npos ? 1.
                                                 : 0.,
          s.find("top") != std::string::npos      ? -1.
          : s.find("bottom") != std::string::npos ? 1.
                                                  : 0.};
}
inline bool validTarget(const std::string &s) {
  return s == "left" || s == "right" || s == "top" || s == "bottom" ||
         s == "top-left" || s == "top-right" || s == "bottom-left" ||
         s == "bottom-right";
}
// Full cone width centered on the edge normal (corner diagonal).
inline bool towards(const Rule &r, Point delta) {
  auto d = direction(r.target);
  if ((d.x && delta.x * d.x <= r.noise) ||
      (d.y && delta.y * d.y <= r.noise))
    return false;
  double dot = delta.x * d.x + delta.y * d.y;
  double cross = delta.x * d.y - delta.y * d.x;
  double deviation = std::atan2(std::abs(cross), dot);
  return deviation <= r.pushAngle * std::acos(-1.) / 360. + 1e-12;
}
// Global logical rectangles, no physical pixel or output scale conversion.
inline bool inBand(const Rule &r, const Rect &b, Point p, double width) {
  auto d = direction(r.target);
  return b.contains(p) &&
         (d.x == 0 || (d.x < 0 ? p.x - b.x : b.x + b.w - p.x) <= width) &&
         (d.y == 0 || (d.y < 0 ? p.y - b.y : b.y + b.h - p.y) <= width);
}
inline bool exposed(const Rule &r, const Monitor &m, Point p,
                    const std::vector<Monitor> &all) {
  auto d = direction(r.target);
  for (const auto &n : all) {
    if (n.name == m.name)
      continue;
    auto a = m.box, b = n.box;
    // An adjacent output blocks only its shared segment. Expand tangent test by
    // band at corners so a diagonal push at a shared endpoint is not a hot
    // corner.
    double margin = (d.x != 0 && d.y != 0) ? r.band : 0;
    if (d.x && p.y >= b.y - margin && p.y <= b.y + b.h + margin &&
        (d.x < 0 ? std::abs(b.x + b.w - a.x) < .01
                 : std::abs(b.x - a.x - a.w) < .01))
      return false;
    if (d.y && p.x >= b.x - margin && p.x <= b.x + b.w + margin &&
        (d.y < 0 ? std::abs(b.y + b.h - a.y) < .01
                 : std::abs(b.y - a.y - a.h) < .01))
      return false;
    // Overlapping/mirrored outputs are not exposed in the overlap either.
    Point outside = {d.x < 0   ? a.x - .01
                     : d.x > 0 ? a.x + a.w + .01
                               : p.x,
                     d.y < 0   ? a.y - .01
                     : d.y > 0 ? a.y + a.h + .01
                               : p.y};
    if (b.contains(outside))
      return false;
  }
  return true;
}
inline Point swipeDirection(const std::string &name) {
  std::string d = name;
  if (auto p = d.find("up"); p != std::string::npos)
    d.replace(p, 2, "top");
  if (auto p = d.find("down"); p != std::string::npos)
    d.replace(p, 4, "bottom");
  return direction(d);
}
inline bool validSwipeDirection(const std::string &name) {
  return name == "left" || name == "right" || name == "up" || name == "down" ||
         name == "up-left" || name == "up-right" || name == "down-left" ||
         name == "down-right";
}
struct Machine {
  uint64_t generation = 0;
  bool candidate = false, armed = false, active = false;
  double lastMotion = 0, entered = 0, lastPush = 0, until = 0, activated = 0;
  bool sawHold = false, moving = false;
  double movementStart = 0, movementLast = 0;
  Point sum{};
  std::string monitor;
  Action activate(double now) {
    active = true;
    activated = now;
    sawHold = false;
    candidate = armed = false;
    moving = false;
    sum = {};
    return Action::Enter;
  }
  void reset() {
    ++generation;
    candidate = armed = active = moving = false;
    sum = {};
    monitor.clear();
    until = 0;
    sawHold = false;
  }
  Action step(const Rule &r, const std::vector<Monitor> &all, Point p,
              Point delta, double now, bool motion,
              std::optional<Rect> hold = {}, bool dynamic = false,
              bool paused = false) {
    const Monitor *m = nullptr;
    for (auto &n : all)
      if (n.box.contains(p)) {
        m = &n;
        break;
      }
    if (paused || !m || (r.monitor != "*" && r.monitor != m->name)) {
      reset();
      return Action::None;
    }
    // Crossing outputs cannot transfer candidates or dispatch either action.
    if (!monitor.empty() && monitor != m->name) {
      reset();
      monitor = m->name;
      return Action::None;
    }
    monitor = m->name;
    bool hot = inBand(r, m->box, p, r.band) && exposed(r, *m, p, all);
    if (active) {
      if (hold)
        sawHold = true;
      bool waiting =
          dynamic && !hold && !sawHold && now - activated < r.holdWait;
      bool keep = dynamic && !waiting ? hold && hold->contains(p)
                                      : inBand(r, m->box, p, r.exit);
      if (inBand(r, m->box, p, r.band) && !exposed(r, *m, p, all)) {
        reset();
        return Action::None;
      }
      if (!keep) {
        active = false;
        candidate = armed = false;
        sum = {};
        until = now + r.cooldown;
        return Action::Exit;
      }
      return Action::None;
    }
    if (!hot || now < until) {
      if (candidate)
        ++generation;
      candidate = armed = moving = false;
      sum = {};
      return Action::None;
    }
    if (!candidate) {
      ++generation;
      candidate = true;
      entered = lastMotion = now;
      moving = false;
      sum = {};
      if (r.mode != Mode::Enter)
        return Action::None;
    }
    bool fire = r.mode == Mode::Enter ||
                (r.mode == Mode::Dwell && now - entered >= r.dwell);
    if (r.mode == Mode::Move) {
      // A timer can expire a motion burst, but can never complete one.
      if (moving && now - movementLast > r.movementGap)
        moving = false;
      if (motion && std::hypot(delta.x, delta.y) > r.noise) {
        if (!towards(r, delta))
          moving = false;
        else {
          if (!moving) {
            moving = true;
            movementStart = now;
          }
          movementLast = now;
          fire = now - movementStart >= r.move;
        }
      }
    }
    if (r.mode == Mode::Push) {
      double len = std::hypot(delta.x, delta.y);
      if (!armed && now - lastMotion >= r.pause) {
        armed = true;
        sum = {};
        lastPush = now;
      }
      if (motion && len > r.noise) {
        if (!armed)
          lastMotion = now;
        else {
          auto d = direction(r.target);
          // Sideways / reverse movement breaks the second push and requires a
          // stop.
          if (!towards(r, delta)) {
            armed = false;
            sum = {};
            lastMotion = now;
          } else {
            if (now - lastPush > r.burst)
              sum = {};
            lastPush = now;
            sum.x += delta.x * d.x;
            sum.y += delta.y * d.y;
            fire =
                (d.x == 0 || sum.x >= r.push) && (d.y == 0 || sum.y >= r.push);
          }
        }
      }
    }
    if (fire) {
      return activate(now);
    }
    return Action::None;
  }
};
// Complete on release, so cancelled gestures never dispatch. Each session is
// bound to the candidate/output that existed at begin; reset/crossing
// invalidates it.
struct Swipe {
  uint64_t generation = 0;
  bool begun = false, invalid = false;
  Point travel{};
  std::string monitor;
  bool begin(const Rule &r, const Machine &m, int fingers, double now) {
    begun = r.mode == Mode::Swipe && r.fingers == fingers && m.candidate &&
            !m.active && now >= m.until;
    invalid = false;
    travel = {};
    monitor = m.monitor;
    generation = m.generation;
    return begun;
  }
  void update(const Rule &r, Point delta) {
    if (!begun)
      return;
    auto d = swipeDirection(r.swipeDirection);
    if ((d.x && delta.x * d.x < -r.noise) || (d.y && delta.y * d.y < -r.noise))
      invalid = true;
    travel.x += delta.x;
    travel.y += delta.y;
  }
  Action end(const Rule &r, Machine &m, double now, bool cancelled) {
    bool eligible = begun && !invalid && !cancelled && m.candidate &&
                    !m.active && m.monitor == monitor &&
                    m.generation == generation && now >= m.until;
    auto d = swipeDirection(r.swipeDirection);
    bool enough = (d.x == 0 || travel.x * d.x >= r.swipeDistance) &&
                  (d.y == 0 || travel.y * d.y >= r.swipeDistance);
    if (d.x == 0)
      enough = enough && travel.y * d.y >= std::abs(travel.x);
    if (d.y == 0)
      enough = enough && travel.x * d.x >= std::abs(travel.y);
    begun = false;
    return eligible && enough ? m.activate(now) : Action::None;
  }
};
} // namespace edge
