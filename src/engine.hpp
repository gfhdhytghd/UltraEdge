#pragma once
#include <algorithm>
#include <cmath>
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
enum class Mode { Enter, Dwell, Push };
enum class Action { None, Enter, Exit };
struct Rule {
  std::string monitor = "*", target = "left";
  Mode mode = Mode::Push;
  double band = 2, pause = 180, push = 8, noise = .5, exit = 80, cooldown = 400,
         dwell = 300, burst = 160, holdWait = 0;
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
struct Machine {
  bool candidate = false, armed = false, active = false;
  double lastMotion = 0, entered = 0, lastPush = 0, until = 0, activated = 0;
  bool sawHold = false;
  Point sum{};
  std::string monitor;
  void reset() {
    candidate = armed = active = false;
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
      candidate = armed = false;
      sum = {};
      return Action::None;
    }
    if (!candidate) {
      candidate = true;
      entered = lastMotion = now;
      sum = {};
      if (r.mode != Mode::Enter)
        return Action::None;
    }
    bool fire = r.mode == Mode::Enter ||
                (r.mode == Mode::Dwell && now - entered >= r.dwell);
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
          bool towards = (d.x == 0 || delta.x * d.x > r.noise) &&
                         (d.y == 0 || delta.y * d.y > r.noise);
          if (d.x == 0)
            towards = towards && delta.y * d.y >= std::abs(delta.x);
          if (d.y == 0)
            towards = towards && delta.x * d.x >= std::abs(delta.y);
          // Sideways / reverse movement breaks the second push and requires a
          // stop.
          if (!towards) {
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
      active = true;
      activated = now;
      sawHold = false;
      candidate = armed = false;
      sum = {};
      return Action::Enter;
    }
    return Action::None;
  }
};
} // namespace edge
