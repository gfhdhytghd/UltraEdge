#include "engine.hpp"
#include <chrono>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/managers/SessionLockManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/pointer/PointerManager.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}
using Json = nlohmann::json;
static HANDLE handle;
static CFunctionHook *hook;
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
  std::string enter, exit, enterArgs, exitArgs, regionCommand;
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
  else if (mode != "push")
    throw std::runtime_error("unknown mode");
  r.band = j.value("band", 2.);
  r.pause = j.value("pause_ms", 180.);
  r.push = j.value("push", 8.);
  r.noise = j.value("noise", .5);
  r.exit = j.value("exit_distance", 80.);
  r.cooldown = j.value("cooldown_ms", 400.);
  r.dwell = j.value("dwell_ms", 300.);
  r.burst = j.value("burst_ms", 160.);
  r.holdWait = j.value("hold_wait_ms", 0.);
  if (!edge::validTarget(r.target) || r.monitor.empty())
    throw std::runtime_error("invalid target/monitor");
  for (double v : {r.band, r.pause, r.push, r.noise, r.exit, r.cooldown,
                   r.dwell, r.burst, r.holdWait})
    if (!std::isfinite(v) || v < 0)
      throw std::runtime_error("threshold must be finite and nonnegative");
  if (r.band <= 0 || r.push <= r.noise || r.exit < r.band || r.pause <= 0 ||
      r.burst <= 0)
    throw std::runtime_error("require band > 0, push > noise, exit_distance >= "
                             "band, pause/burst > 0");
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
    if (!b.regionCommand.empty())
      return rect(Json::parse(
          HyprlandAPI::invokeHyprctlCommand(b.regionCommand, b.state.monitor)));
  } catch (const std::exception &e) {
    failure(e.what());
  }
  return {};
}
static bool action(Binding &b, edge::Action a) {
  if (b.L) {
    int ref = a == edge::Action::Enter ? b.on : b.off;
    if (ref == LUA_NOREF)
      return true;
    if (!luaCall(b, ref, 1, b.state.monitor))
      return false;
    bool ok = !lua_isboolean(b.L, -1) || lua_toboolean(b.L, -1);
    lua_pop(b.L, 1);
    return ok;
  }
  auto name = a == edge::Action::Enter ? b.enter : b.exit;
  if (name.empty())
    return true;
  auto it = g_pKeybindManager->m_dispatchers.find(name);
  if (it == g_pKeybindManager->m_dispatchers.end()) {
    failure("unknown native dispatcher: " + name);
    return false;
  }
  auto result = it->second(a == edge::Action::Enter ? b.enterArgs : b.exitArgs);
  if (!result.success)
    failure(result.error);
  return result.success;
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
    bool dynamic = b->region != LUA_NOREF || !b->regionCommand.empty();
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
  reinterpret_cast<void (*)(CInputManager *, IPointer::SMotionEvent)>(
      hook->m_original)(self, e);
  process({d.x, d.y}, true);
}
static int addRule(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  try {
    Json j = Json::object();
    for (auto key : {"target", "monitor", "mode"}) {
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
    for (auto key : {"band", "pause_ms", "push", "noise", "exit_distance",
                     "cooldown_ms", "dwell_ms", "burst_ms", "hold_wait_ms"}) {
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
  rules.clear();
  return 0;
}
static int pauseLua(lua_State *L) {
  paused = lua_toboolean(L, 1);
  for (auto &b : rules)
    b->state.reset();
  return 0;
}
static void loadNative() {
  if (Config::mgr()->type() == Config::CONFIG_LUA)
    return;
  try {
    static auto value =
        CConfigValue<Config::STRING>("plugin:edge_trigger:rules");
    auto array = Json::parse(*value);
    if (!array.is_array())
      throw std::runtime_error("rules must be JSON array");
    std::vector<std::shared_ptr<Binding>> next;
    for (auto &j : array) {
      auto b = std::make_shared<Binding>();
      b->rule = parse(j);
      b->enter = j.at("on_enter");
      b->exit = j.value("on_exit", "");
      b->enterArgs = j.value("enter_args", "");
      b->exitArgs = j.value("exit_args", "");
      b->regionCommand = j.value("hold_command", "");
      next.push_back(b);
    }
    rules = std::move(next);
    error.clear();
  } catch (const std::exception &e) {
    rules.clear();
    failure(e.what());
  }
}
static SP<SHyprCtlCommand> status;
APICALL EXPORT std::string PLUGIN_API_VERSION() { return HYPRLAND_API_VERSION; }
APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE h) {
  handle = h;
  if (std::string(__hyprland_api_get_hash()) !=
      __hyprland_api_get_client_hash())
    throw std::runtime_error("Hyprland header/runtime hash mismatch");
  if (!HyprlandAPI::addConfigValueV2(
          h, makeShared<Config::Values::CStringValue>(
                 "plugin:edge_trigger:rules", "JSON rule array", "[]")))
    throw std::runtime_error("config registration failed");
  if (Config::mgr()->type() != Config::CONFIG_LUA) {
    if (!HyprlandAPI::addDispatcherV2(
            h, "edge_trigger:pause", [](std::string arg) {
              if (arg != "0" && arg != "1")
                return SDispatchResult{.success = false,
                                       .error = "expected 0 or 1"};
              paused = arg == "1";
              for (auto &b : rules)
                b->state.reset();
              return SDispatchResult{};
            }))
      throw std::runtime_error("pause dispatcher registration failed");
  }
  if (Config::mgr()->type() == Config::CONFIG_LUA) {
    if (!HyprlandAPI::addLuaFunction(h, "edge_trigger", "rule", addRule) ||
        !HyprlandAPI::addLuaFunction(h, "edge_trigger", "clear", clear) ||
        !HyprlandAPI::addLuaFunction(h, "edge_trigger", "pause", pauseLua))
      throw std::runtime_error("Lua registration failed");
  }
  void *address = nullptr;
  for (auto &f : HyprlandAPI::findFunctionsByName(h, "onMouseMoved"))
    if (f.demangled == "CInputManager::onMouseMoved(IPointer::SMotionEvent)")
      address = f.address;
  if (!address)
    throw std::runtime_error("missing relative motion ABI");
  hook = HyprlandAPI::createFunctionHook(h, address,
                                         reinterpret_cast<void *>(mouse));
  if (!hook || !hook->hook())
    throw std::runtime_error("relative motion hook failed");
  // Animation ticks stop on idle desktops. Dwell/hold must keep working there.
  timer = makeShared<CEventLoopTimer>(
      std::chrono::milliseconds(16),
      [](SP<CEventLoopTimer> self, void *) {
        if (!rules.empty())
          process();
        self->updateTimeout(std::chrono::milliseconds(16));
      },
      nullptr);
  g_pEventLoopManager->addTimer(timer);
  listeners.push_back(Event::bus()->m_events.exit.listen([] {
    rules.clear();
    if (timer)
      timer->cancel();
  }));
  listeners.push_back(Event::bus()->m_events.config.preReload.listen([] {
    rules.clear();
    paused = false;
    error.clear();
  }));
  listeners.push_back(
      Event::bus()->m_events.config.reloaded.listen([] { loadNative(); }));
  listeners.push_back(Event::bus()->m_events.monitor.layoutChanged.listen([] {
    for (auto &b : rules)
      b->state.reset();
  }));
  listeners.push_back(
      Event::bus()->m_events.monitor.added.listen([](PHLMONITOR) {
        for (auto &b : rules)
          b->state.reset();
      }));
  listeners.push_back(
      Event::bus()->m_events.monitor.removed.listen([](PHLMONITOR) {
        for (auto &b : rules)
          b->state.reset();
      }));
  status = HyprlandAPI::registerHyprCtlCommand(
      h, {.name = "edge-trigger-state",
          .exact = true,
          .fn = [](eHyprCtlOutputFormat, std::string) {
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
  return {"hypr-edge-trigger", "Exposed edge/corner triggers", "wilf", "0.1.0"};
}
APICALL EXPORT void PLUGIN_EXIT() {
  listeners.clear();
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
