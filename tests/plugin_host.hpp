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
template<class T> using SP=std::shared_ptr<T>;
template<class T> struct WP:std::weak_ptr<T> {
 using std::weak_ptr<T>::weak_ptr;
 T* get() const {return this->lock().get();}
};
namespace Config {using INTEGER=long;}
static void* handle=reinterpret_cast<void*>(1);
namespace Config {
struct IConfigManager {virtual ~IConfigManager()=default;};
static SP<IConfigManager> manager;
static auto& mgr(){return manager;}
namespace Lua {
struct SLuaStateLifetime {lua_State* state=nullptr;};
struct CConfigManager:IConfigManager {
 SP<SLuaStateLifetime> lifetime=std::make_shared<SLuaStateLifetime>();
 std::map<std::string,int(*)(lua_State*)> functions;
 auto luaStateLifetime(){return lifetime;}
 static CConfigManager* fromLuaState(lua_State* L) {
  lua_getfield(L,LUA_REGISTRYINDEX,"hl_lua_manager");
  auto* m=static_cast<CConfigManager*>(lua_touserdata(L,-1));lua_pop(L,1);return m;
 }
};
}
}
static int registrations=0;
namespace HyprlandAPI {
static bool addLuaFunction(void*,const std::string& ns,const std::string& name,int(*fn)(lua_State*)) {
 ++registrations;
 auto m=std::static_pointer_cast<Config::Lua::CConfigManager>(Config::mgr());
 return m->functions.emplace(ns+"."+name,fn).second;
}
}
static auto installManager(lua_State* L) {
 auto m=std::make_shared<Config::Lua::CConfigManager>();m->lifetime->state=L;
 Config::manager=m;
 lua_pushlightuserdata(L,m.get());lua_setfield(L,LUA_REGISTRYINDEX,"hl_lua_manager");
 return m;
}
static lua_State* closedState=nullptr;
static unsigned closedUnrefs=0;
static void checkedUnref(lua_State* L,int table,int ref) {
 if(L==closedState){++closedUnrefs;return;}
 luaL_unref(L,table,ref);
}
#define luaL_unref checkedUnref
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
struct CInputManager {
 void onMouseMoved(IPointer::SMotionEvent);
 void onMouseWheel(IPointer::SAxisEvent, SP<IPointer>);
};
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
