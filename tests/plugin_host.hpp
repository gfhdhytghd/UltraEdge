// Offline host for literal plugin callback methods. No compositor session.

#include <optional>
#include <string>
#include <stdexcept>
#include <iostream>
#include <memory>
#include <map>
#include <functional>
#include <nlohmann/json.hpp>
#include "engine.hpp"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
using Json = nlohmann::json;
static bool paused = false, processing = false;
static std::string error;
static double now() { return 1000; }
struct Vec { double x,y; };
struct FakeMonitor { std::string m_name="A"; Vec m_position{0,0},m_size{1000,800}; };
namespace State {
struct Manager {
std::vector<std::shared_ptr<FakeMonitor>> data{std::make_shared<FakeMonitor>()};
auto& monitors() { return data; }
};
static Manager* monitorState() { static Manager m; return &m; }
}
namespace Pointer {
struct Manager { Vec position() { return {0,400}; } };
static Manager* mgr() { static Manager m; return &m; }
}
struct LockManager { bool locked=false; bool isSessionLocked() { return locked; } };
static LockManager lockManager;
static auto* g_pSessionLockManager = &lockManager;
struct Result { bool success=true; std::string error; };
struct Keybinds { std::map<std::string,std::function<Result(std::string)>> m_dispatchers; };
static Keybinds keybinds;
static auto* g_pKeybindManager = &keybinds;
namespace HyprlandAPI {
std::string invokeHyprctlCommand(const std::string&, const std::string&) {
  throw std::runtime_error("offline: native command branch disabled");
}
}
