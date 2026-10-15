// Offline host for literal plugin callback methods. No compositor session.

#include <optional>
#include <chrono>
#include <cassert>
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
static double fakeTime=1000; static double now() { return fakeTime; }
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

template<class T> using SP=std::shared_ptr<T>;
template<class T> using WP=std::weak_ptr<T>;
namespace Config {using INTEGER=long;}
template<class T> struct CConfigValue {CConfigValue(const char*){} T operator*()const{return 0;}};
struct FakeAQ {void* getLibinputHandle(){return this;}};
struct IPointer {
 bool m_isTouchpad=true,m_flipX=false,m_flipY=false;
 FakeAQ device;FakeAQ* aq(){return &device;}
 struct SMotionEvent {Vec delta{},unaccel{};SP<IPointer> device;};
 struct SAxisEvent {double delta=0;int axis=0,source=1;};
 struct SSwipeBeginEvent {int fingers=3;};
 struct SSwipeUpdateEvent {Vec delta{};};
 struct SSwipeEndEvent {bool cancelled=false;};
};
constexpr int WL_POINTER_AXIS_SOURCE_FINGER=1,WL_POINTER_AXIS_VERTICAL_SCROLL=0,LIBINPUT_CONFIG_SCROLL_2FG=2;
static int libinput_device_config_scroll_get_method(void*){return LIBINPUT_CONFIG_SCROLL_2FG;}
namespace Event {struct SCallbackInfo{bool cancelled=false;};}
struct CInputManager {};
struct CFunctionHook {void* m_original=nullptr;};
static CFunctionHook motionHook, wheelHook;
static CFunctionHook* hook=&motionHook;
static CFunctionHook* axisHook=&wheelHook;
namespace PROTO {struct Capture {bool active=false;bool isCaptured(){return active;}};static Capture capture;static Capture* inputCapture=&capture;}
static int captureScans=0,captureLookups=0,captureQueries=0;
static bool viewflowCaptured=false,viewflowExport=true;
static bool viewflowQuery(){++captureQueries;return viewflowCaptured;}
struct FakePlugin {std::string m_name="viewflow-hyprland";void* m_handle=this;};
struct FakePlugins {FakePlugin plugin;bool loaded=true;std::vector<FakePlugin*> getAllPlugins(){++captureScans;return loaded?std::vector<FakePlugin*>{&plugin}:std::vector<FakePlugin*>{};}};
static FakePlugins plugins;
static FakePlugins* g_pPluginSystem=&plugins;
static void* dlsym(void*,const char*){++captureLookups;return viewflowExport?reinterpret_cast<void*>(viewflowQuery):nullptr;}
struct CEventLoopTimer {void updateTimeout(std::chrono::milliseconds){}};
