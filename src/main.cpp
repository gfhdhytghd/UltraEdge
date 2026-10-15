#include "engine.hpp"
#include <aquamarine/input/Input.hpp>
#include <chrono>
#include <src/config/ConfigManager.hpp>
#include <src/config/ConfigValue.hpp>
#include <src/config/values/types/StringValue.hpp>
#include <src/event/EventBus.hpp>
#include <src/managers/SessionLockManager.hpp>
#include <src/managers/eventLoop/EventLoopManager.hpp>
#include <src/managers/input/InputManager.hpp>
#include <src/output/Monitor.hpp>
#include <src/plugins/HookSystem.hpp>
#include <src/plugins/PluginAPI.hpp>
#include <src/pointer/PointerManager.hpp>
#include <src/state/MonitorState.hpp>
#include <libinput.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <type_traits>
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}
using Json = nlohmann::json;
static HANDLE handle;
static CFunctionHook *hook;
static CFunctionHook *axisHook;
using MouseFn = void (*)(CInputManager *, IPointer::SMotionEvent);
using WheelFn = void (*)(CInputManager *, IPointer::SAxisEvent, SP<IPointer>);
static_assert(std::is_same_v<decltype(&CInputManager::onMouseMoved),
                             void (CInputManager::*)(IPointer::SMotionEvent)>);
static_assert(std::is_same_v<decltype(&CInputManager::onMouseWheel),
                             void (CInputManager::*)(IPointer::SAxisEvent,
                                                    SP<IPointer>)>);
static SP<CEventLoopTimer> timer;
static std::vector<CHyprSignalListener> listeners;
static bool paused = false, processing = false;
static std::string error;
static double now() {
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
struct Binding {
  edge::Rule rule;
  edge::Machine state;
  lua_State *L = nullptr;
  int on = LUA_NOREF, off = LUA_NOREF, region = LUA_NOREF;
  ~Binding() {
    if (L)
      for (int ref : {on, off, region})
        if (ref != LUA_NOREF)
          luaL_unref(L, LUA_REGISTRYINDEX, ref);
  }
};
static std::vector<std::shared_ptr<Binding>> rules;
static edge::Rule parse(const Json &j) {
  edge::Rule r;
  r.monitor = j.value("monitor", "*");
  r.target = j.value("target", "left");
  auto mode = j.value("mode", "push");
  if (mode == "enter")
    r.mode = edge::Mode::Enter;
  else if (mode == "dwell")
    r.mode = edge::Mode::Dwell;
  else if (mode == "move")
    r.mode = edge::Mode::Move;
  else if (mode == "swipe")
    r.mode = edge::Mode::Swipe;
  else if (mode != "push")
    throw std::runtime_error("unknown mode");
  if (j.contains("fingers") && !j["fingers"].is_number_integer())
    throw std::runtime_error("fingers must be integer");
  r.fingers = j.value("fingers", 3);
  r.swipeDirection = j.value("direction", "right");
  r.swipeDistance = j.value("swipe_distance", 40.);
  r.consume = j.value("consume", true);
  if (r.fingers < 2 || r.fingers > 4 ||
      !edge::validSwipeDirection(r.swipeDirection) ||
      !std::isfinite(r.swipeDistance) || r.swipeDistance <= 0)
    throw std::runtime_error("invalid swipe fingers/direction/distance");
  r.band = j.value("band", 2.);
  r.pause = j.value("pause_ms", 180.);
  r.push = j.value("push", 8.);
  r.noise = j.value("noise", .5);
  r.exit = j.value("exit_distance", 80.);
  r.cooldown = j.value("cooldown_ms", 400.);
  r.dwell = j.value("dwell_ms", 300.);
  r.burst = j.value("burst_ms", 160.);
  r.move = j.value("move_ms", 100.);
  r.movementGap = j.value("movement_gap_ms", 80.);
  r.holdWait = j.value("hold_wait_ms", 0.);
  if (!edge::validTarget(r.target) || r.monitor.empty())
    throw std::runtime_error("invalid target/monitor");
  for (double v : {r.band, r.pause, r.push, r.noise, r.exit, r.cooldown,
                   r.dwell, r.burst, r.holdWait, r.move, r.movementGap})
    if (!std::isfinite(v) || v < 0)
      throw std::runtime_error("threshold must be finite and nonnegative");
  if (r.band <= 0 || r.push <= r.noise || r.exit < r.band || r.pause <= 0 ||
      r.burst <= 0 || r.move <= 0 || r.movementGap <= 0)
    throw std::runtime_error("require band > 0, push > noise, exit_distance >= "
                             "band, pause/burst/move/movement_gap > 0");
  return r;
}
static void failure(const std::string &s) { error = s; }
static bool luaCall(Binding &b, int ref, int results,
                    const std::string &monitor) {
  lua_rawgeti(b.L, LUA_REGISTRYINDEX, ref);
  lua_pushlstring(b.L, monitor.data(), monitor.size());
  if (lua_pcall(b.L, 1, results, 0) != LUA_OK) {
    failure(lua_tostring(b.L, -1) ? lua_tostring(b.L, -1)
                                  : "Lua callback failed");
    lua_pop(b.L, 1);
    return false;
  }
  return true;
}
static std::optional<edge::Rect> rect(const Json &j) {
  if (j.is_null())
    return {};
  edge::Rect b{j.at("x").get<double>(), j.at("y").get<double>(),
               j.at("width").get<double>(), j.at("height").get<double>()};
  if (!std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.w) ||
      !std::isfinite(b.h) || b.w <= 0 || b.h <= 0)
    throw std::runtime_error("invalid hold rectangle");
  return b;
}
static std::optional<edge::Rect> region(Binding &b) {
  try {
    if (b.L && b.region != LUA_NOREF) {
      int top = lua_gettop(b.L);
      if (!luaCall(b, b.region, 1, b.state.monitor))
        return {};
      Json j = nullptr;
      if (lua_istable(b.L, -1)) {
        j = Json::object();
        for (auto key : {"x", "y", "width", "height"}) {
          lua_getfield(b.L, -1, key);
          if (!lua_isnumber(b.L, -1)) {
            lua_settop(b.L, top);
            throw std::runtime_error(
                "hold callback must return numeric rectangle or nil");
          }
          j[key] = lua_tonumber(b.L, -1);
          lua_pop(b.L, 1);
        }
      } else if (!lua_isnil(b.L, -1)) {
        lua_settop(b.L, top);
        throw std::runtime_error("hold callback must return rectangle or nil");
      }
      lua_settop(b.L, top);
      return rect(j);
    }
  } catch (const std::exception &e) {
    failure(e.what());
  }
  return {};
}
static bool action(Binding &b, edge::Action a) {
  // Hyprland development HEAD supports only Lua config; native actions are
  // supplied by the same hl.dsp callbacks as other Lua rules.
  int ref = a == edge::Action::Enter ? b.on : b.off;
  if (ref == LUA_NOREF)
    return true;
  if (!luaCall(b, ref, 1, b.state.monitor))
    return false;
  bool ok = !lua_isboolean(b.L, -1) || lua_toboolean(b.L, -1);
  lua_pop(b.L, 1);
  return ok;
}
static void processImpl(edge::Point delta, bool motion) {
  if (processing)
    return;
  processing = true;
  struct Guard {
    ~Guard() { processing = false; }
  } guard;
  auto copy = rules;
  std::vector<edge::Monitor> monitors;
  for (auto &m : State::monitorState()->monitors())
    monitors.push_back(
        {m->m_name,
         {m->m_position.x, m->m_position.y, m->m_size.x, m->m_size.y}});
  auto p = Pointer::mgr()->position();
  double t = now();
  for (auto &b : copy) {
    bool dynamic = b->region != LUA_NOREF;
    auto hold =
        b->state.active && dynamic ? region(*b) : std::optional<edge::Rect>{};
    auto a = b->state.step(b->rule, monitors, {p.x, p.y}, delta, t, motion,
                           hold, dynamic,
                           paused || g_pSessionLockManager->isSessionLocked());
    if (a != edge::Action::None && !action(*b, a)) {
      b->state.reset();
      b->state.until = t + b->rule.cooldown;
    }
  }
}
static void process(edge::Point delta = {}, bool motion = false) {
  try {
    processImpl(delta, motion);
  } catch (const std::exception &e) {
    failure(e.what());
  }
}
static void mouse(CInputManager *self, IPointer::SMotionEvent e) {
  // Preserve compositor semantics including acceleration choice and touchpad
  // flips.
  static auto noAccel = CConfigValue<Config::INTEGER>("input:force_no_accel");
  auto d = *noAccel == 1 ? e.unaccel : e.delta;
  if (e.device && e.device->m_isTouchpad) {
    if (e.device->m_flipX)
      d.x = -d.x;
    if (e.device->m_flipY)
      d.y = -d.y;
  }
  reinterpret_cast<MouseFn>(hook->m_original)(self, e);
  process({d.x, d.y}, true);
}
struct SwipeEntry {
  std::shared_ptr<Binding> binding;
  edge::Swipe swipe;
};
struct SwipeSession {
  std::vector<SwipeEntry> entries;
  bool captured = false;
  void clear() {
    entries.clear();
    captured = false;
  }
  void begin(int fingers) {
    clear();
    process();
    double t = now();
    for (auto &b : rules) {
      edge::Swipe swipe;
      if (swipe.begin(b->rule, b->state, fingers, t)) {
        entries.push_back({b, swipe});
        captured = captured || b->rule.consume;
      }
    }
  }
  void update(edge::Point d) {
    process();
    for (auto &e : entries)
      e.swipe.update(e.binding->rule, d);
  }
  void end(bool cancelled) {
    process();
    auto copy = std::move(entries);
    bool wasProcessing = processing;
    processing = true;
    struct Guard {
      bool previous;
      ~Guard() { processing = previous; }
    } guard{wasProcessing};
    for (auto &e : copy) {
      auto &b = *e.binding;
      auto a = e.swipe.end(b.rule, b.state, now(), cancelled);
      if (a != edge::Action::None) {
        try {
          if (!action(b, a)) {
            b.state.reset();
            b.state.until = now() + b.rule.cooldown;
          }
        } catch (const std::exception &ex) {
          failure(ex.what());
          b.state.reset();
          b.state.until = now() + b.rule.cooldown;
        }
      }
    }
    captured = false;
  }
};
static SwipeSession nativeSwipe, scrollSwipe;
static WP<IPointer> scrollOwner;
static unsigned scrollAxes = 0;
static double lastScroll = 0;
static void clearGestures() {
  // Release Lua references, but swallow the remainder of an already captured
  // stream. No new gesture may inherit the tail after pause/reload/layout
  // changes.
  nativeSwipe.entries.clear();
  scrollSwipe.entries.clear();
}
static void wheel(CInputManager *self, IPointer::SAxisEvent e,
                  SP<IPointer> pointer) {
  bool eligible = false;
  if (pointer && pointer->m_isTouchpad &&
      e.source == WL_POINTER_AXIS_SOURCE_FINGER && pointer->aq())
    if (auto *dev = pointer->aq()->getLibinputHandle())
      eligible = libinput_device_config_scroll_get_method(dev) ==
                 LIBINPUT_CONFIG_SCROLL_2FG;
  bool consumed = false;
  if (eligible) {
    if (scrollOwner.lock() != pointer) {
      scrollSwipe.end(true);
      scrollOwner = pointer;
      scrollAxes = 0;
      scrollSwipe.clear();
    }
    if (!scrollAxes && e.delta != 0)
      scrollSwipe.begin(2);
    unsigned axis = e.axis == WL_POINTER_AXIS_VERTICAL_SCROLL ? 1 : 2;
    if (e.delta != 0) {
      scrollAxes |= axis;
      lastScroll = now();
      scrollSwipe.update(e.axis == WL_POINTER_AXIS_VERTICAL_SCROLL
                             ? edge::Point{0, e.delta}
                             : edge::Point{e.delta, 0});
    }
    consumed = scrollSwipe.captured;
    if (e.delta == 0) {
      scrollAxes &= ~axis;
      if (!scrollAxes)
        scrollSwipe.end(false);
    }
  }
  if (!consumed)
    reinterpret_cast<WheelFn>(axisHook->m_original)(self, e, pointer);
}
static_assert(std::is_same_v<decltype(&mouse), MouseFn>);
static_assert(std::is_same_v<decltype(&wheel), WheelFn>);
static int addRule(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  try {
    Json j = Json::object();
    for (auto key : {"target", "monitor", "mode", "direction"}) {
      lua_getfield(L, 1, key);
      if (!lua_isnil(L, -1)) {
        if (lua_type(L, -1) != LUA_TSTRING) {
          lua_pop(L, 1);
          throw std::runtime_error("expected string");
        }
        j[key] = lua_tostring(L, -1);
      }
      lua_pop(L, 1);
    }
    for (auto key :
         {"band", "pause_ms", "push", "noise", "exit_distance", "cooldown_ms",
          "dwell_ms", "burst_ms", "hold_wait_ms", "swipe_distance",
          "move_ms", "movement_gap_ms"}) {
      lua_getfield(L, 1, key);
      if (!lua_isnil(L, -1)) {
        if (!lua_isnumber(L, -1)) {
          lua_pop(L, 1);
          throw std::runtime_error("expected number");
        }
        j[key] = lua_tonumber(L, -1);
      }
      lua_pop(L, 1);
    }
    lua_getfield(L, 1, "consume");
    if (!lua_isnil(L, -1)) {
      if (!lua_isboolean(L, -1)) {
        lua_pop(L, 1);
        throw std::runtime_error("consume must be boolean");
      }
      j["consume"] = bool(lua_toboolean(L, -1));
    }
    lua_pop(L, 1);
    lua_getfield(L, 1, "fingers");
    if (!lua_isnil(L, -1)) {
      if (!lua_isinteger(L, -1)) {
        lua_pop(L, 1);
        throw std::runtime_error("fingers must be integer");
      }
      j["fingers"] = lua_tointeger(L, -1);
    }
    lua_pop(L, 1);
    lua_getfield(L, 1, "swipe_distance");
    if (!lua_isnil(L, -1)) {
      if (!lua_isnumber(L, -1)) {
        lua_pop(L, 1);
        throw std::runtime_error("swipe_distance must be numeric");
      }
      j["swipe_distance"] = lua_tonumber(L, -1);
    }
    lua_pop(L, 1);
    auto b = std::make_shared<Binding>();
    b->rule = parse(j);
    b->L = L;
    for (auto pair :
         {std::pair{"on_enter", &b->on}, std::pair{"on_exit", &b->off},
          std::pair{"hold", &b->region}}) {
      lua_getfield(L, 1, pair.first);
      if (lua_isfunction(L, -1))
        *pair.second = luaL_ref(L, LUA_REGISTRYINDEX);
      else {
        bool invalid = !lua_isnil(L, -1);
        lua_pop(L, 1);
        if (invalid)
          throw std::runtime_error("actions/hold must be Lua functions");
      }
    }
    if (b->on == LUA_NOREF)
      throw std::runtime_error("on_enter callback required");
    rules.push_back(b);
    lua_pushboolean(L, true);
    return 1;
  } catch (const std::exception &e) {
    error = e.what();
  }
  lua_pushnil(L);
  lua_pushlstring(L, error.data(), error.size());
  return 2;
}
static int clear(lua_State *) {
  clearGestures();
  rules.clear();
  return 0;
}
static int pauseLua(lua_State *L) {
  paused = lua_toboolean(L, 1);
  clearGestures();
  for (auto &b : rules)
    b->state.reset();
  return 0;
}
static SP<IPC::Socket1::SCommand> status;
APICALL EXPORT std::string PLUGIN_API_VERSION() { return HYPRLAND_API_VERSION; }
APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE h) {
  handle = h;
  // Keep the client ABI computation local: the API's exported inline helper
  // may be ELF-interposed by another already loaded plugin.
  const auto abiVersionPrefix = [](const char *version) {
    std::string value(version);
    return value.substr(0, value.find_last_of('.'));
  };
  const std::string clientABI = std::string(GIT_COMMIT_HASH) + "_aq_" +
                                abiVersionPrefix(AQUAMARINE_VERSION) + "_hu_" +
                                abiVersionPrefix(HYPRUTILS_VERSION) + "_hg_" +
                                abiVersionPrefix(HYPRGRAPHICS_VERSION) +
                                "_hc_" + abiVersionPrefix(HYPRCURSOR_VERSION) +
                                "_hlg_" + abiVersionPrefix(HYPRLANG_VERSION);
  const std::string runtimeABI = __hyprland_api_get_hash();
  if (runtimeABI != clientABI)
    throw std::runtime_error("Hyprland header/runtime ABI mismatch: runtime=" +
                             runtimeABI + "; client=" + clientABI);
  if (!HyprlandAPI::addConfigValueV2(
          h, makeShared<Config::Values::CStringValue>("plugin:ultraedge:rules",
                                                      "JSON rule array", "[]")))
    throw std::runtime_error("config registration failed");
  if (Config::mgr()->type() == Config::CONFIG_LUA) {
    if (!HyprlandAPI::addLuaFunction(h, "ultraedge", "rule", addRule) ||
        !HyprlandAPI::addLuaFunction(h, "ultraedge", "clear", clear) ||
        !HyprlandAPI::addLuaFunction(h, "ultraedge", "pause", pauseLua))
      throw std::runtime_error("Lua registration failed");
  }
  void *address = nullptr;
  size_t matches = 0;
  for (auto &f : HyprlandAPI::findFunctionsByName(h, "onMouseMoved"))
    if (f.demangled == "CInputManager::onMouseMoved(IPointer::SMotionEvent)") {
      address = f.address;
      ++matches;
    }
  if (!address || matches != 1)
    throw std::runtime_error("expected exactly one relative motion ABI target");
  hook = HyprlandAPI::createFunctionHook(h, address,
                                         reinterpret_cast<void *>(mouse));
  if (!hook || !hook->hook())
    throw std::runtime_error("relative motion hook failed");
  void *wheelAddress = nullptr;
  matches = 0;
  for (auto &f : HyprlandAPI::findFunctionsByName(h, "onMouseWheel"))
    if (f.demangled == "CInputManager::onMouseWheel(IPointer::SAxisEvent, "
                       "Hyprutils::Memory::CSharedPointer<IPointer>)") {
      wheelAddress = f.address;
      ++matches;
    }
  if (!wheelAddress || matches != 1)
    throw std::runtime_error("expected exactly one touchpad axis ABI target");
  axisHook = HyprlandAPI::createFunctionHook(h, wheelAddress,
                                             reinterpret_cast<void *>(wheel));
  if (!axisHook || !axisHook->hook())
    throw std::runtime_error("touchpad axis hook failed");
  listeners.push_back(Event::bus()->m_events.gesture.swipe.begin.listen(
      [](IPointer::SSwipeBeginEvent e, Event::SCallbackInfo &info) {
        if (info.cancelled) {
          nativeSwipe.clear();
          return;
        }
        nativeSwipe.begin(e.fingers);
        if (nativeSwipe.captured)
          info.cancelled = true;
      }));
  listeners.push_back(Event::bus()->m_events.gesture.swipe.update.listen(
      [](IPointer::SSwipeUpdateEvent e, Event::SCallbackInfo &info) {
        if (info.cancelled)
          for (auto &entry : nativeSwipe.entries)
            entry.swipe.invalid = true;
        nativeSwipe.update({e.delta.x, e.delta.y});
        if (nativeSwipe.captured)
          info.cancelled = true;
      }));
  listeners.push_back(Event::bus()->m_events.gesture.swipe.end.listen(
      [](IPointer::SSwipeEndEvent e, Event::SCallbackInfo &info) {
        bool captured = nativeSwipe.captured;
        nativeSwipe.end(e.cancelled || info.cancelled);
        if (captured)
          info.cancelled = true;
      }));
  // Animation ticks stop on idle desktops. Dwell/hold must keep working there.
  timer = makeShared<CEventLoopTimer>(
      std::chrono::milliseconds(16),
      [](SP<CEventLoopTimer> self, void *) {
        if (!rules.empty())
          process();
        if (scrollAxes && now() - lastScroll >= 140) {
          scrollSwipe.end(false);
          scrollAxes = 0;
          scrollOwner.reset();
        }
        self->updateTimeout(std::chrono::milliseconds(16));
      },
      nullptr);
  g_pEventLoopManager->addTimer(timer);
  listeners.push_back(Event::bus()->m_events.exit.listen([] {
    clearGestures();
    rules.clear();
    if (timer)
      timer->cancel();
  }));
  listeners.push_back(Event::bus()->m_events.config.preReload.listen([] {
    clearGestures();
    rules.clear();
    paused = false;
    error.clear();
  }));
  listeners.push_back(Event::bus()->m_events.monitor.layoutChanged.listen([] {
    clearGestures();
    for (auto &b : rules)
      b->state.reset();
  }));
  listeners.push_back(
      Event::bus()->m_events.monitor.added.listen([](PHLMONITOR) {
        clearGestures();
        for (auto &b : rules)
          b->state.reset();
      }));
  listeners.push_back(
      Event::bus()->m_events.monitor.removed.listen([](PHLMONITOR) {
        clearGestures();
        for (auto &b : rules)
          b->state.reset();
      }));
  status = HyprlandAPI::registerHyprCtlCommand(
      h, {.name = "ultraedge-state",
          .match = IPC::Socket1::COMMAND_MATCH_EXACT,
          .handler = [](const IPC::Socket1::SRequest &) -> IPC::Socket1::SResponse {
            Json j = {
                {"paused", paused}, {"error", error}, {"rules", Json::array()}};
            for (auto &b : rules)
              j["rules"].push_back({{"target", b->rule.target},
                                    {"monitor", b->state.monitor},
                                    {"active", b->state.active},
                                    {"armed", b->state.armed}});
            return j.dump();
          }});
  HyprlandAPI::reloadConfig();
  return {"UltraEdge", "Exposed edge/corner triggers", "wilf", "0.1.0"};
}
APICALL EXPORT void PLUGIN_EXIT() {
  listeners.clear();
  clearGestures();
  if (axisHook) {
    axisHook->unhook();
    axisHook = nullptr;
  }
  if (timer) {
    timer->cancel();
    if (g_pEventLoopManager)
      g_pEventLoopManager->removeTimer(timer);
    timer.reset();
  }
  rules.clear();
  if (hook) {
    hook->unhook();
    hook = nullptr;
  }
  if (status) {
    HyprlandAPI::unregisterHyprCtlCommand(handle, status);
    status.reset();
  }
}
