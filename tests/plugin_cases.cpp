
#include <cassert>
static int ticks=0, holds=0;
static int tick(lua_State*) {++ticks;return 0;}
static int hold(lua_State* L) {
 ++holds;lua_createtable(L,0,4);
 for(auto key:{"x","y","width","height"}) {
  lua_pushnumber(L,std::string(key)=="width"?80:std::string(key)=="height"?800:0);
  lua_setfield(L,-2,key);
 }
 return 1;
}
static void run(lua_State* L,const char* code) {
 if(luaL_dostring(L,code)!=LUA_OK) {
  std::cerr<<lua_tostring(L,-1)<<'\n';std::abort();
 }
}
static void coroutineCase(lua_State* L) {
 run(L,R"(
  weak=setmetatable({}, {__mode='v'})
  local co=coroutine.create(function()
   assert(edge.rule{mode='enter',on_enter=tick,on_exit=tick,hold=hold})
  end)
  weak[1]=co;assert(coroutine.resume(co));co=nil
  collectgarbage('collect');collectgarbage('collect')
  assert(weak[1]==nil)
 )");
 // Check identity BEFORE using the saved state. This negative control is safe
 // even when the old implementation retained a freed coroutine pointer.
 assert(rules.size()==1&&rules[0]->L==L);
 process();assert(ticks==1);
 auto box=region(*rules[0]);assert(box&&box->w==80&&holds==1);
 assert(action(*rules[0],edge::Action::Exit));assert(ticks==2);
 rules.clear();assert(lua_gettop(L)==0);
 std::cout<<"coroutine collected: callbacks and ref cleanup use main state PASS\n";
}

static void providerCase(lua_State* L,const char* body,bool expected,const char* expectedError="") {
 rules.clear();error.clear();
 std::string config="assert(edge.rule{mode='enter',on_enter=function()end,hold=function() "+std::string(body)+" end})";
 run(L,config.c_str());
 lua_pushinteger(L,123);int top=lua_gettop(L);
 auto box=region(*rules[0]);assert(bool(box)==expected);
 if(box) assert(box->x==1&&box->y==2&&box->w==80&&box->h==60);
 assert(lua_gettop(L)==top&&lua_tointeger(L,-1)==123);
 assert(error.find(expectedError)!=std::string::npos);
 lua_pop(L,1);rules.clear();
}
static void providerCases(lua_State* L) {
 providerCase(L,"return {x=1,y=2,width=80,height=60}",true);
 providerCase(L,"return setmetatable({x=1,y=2,width=80,height=60},{__index=function() error('must not index') end})",true);
 providerCase(L,"return setmetatable({}, {__index=function() error('must not index') end})",false,"numeric rectangle");
 providerCase(L,"return nil",false);
 providerCase(L,"return 42",false,"rectangle or nil");
 providerCase(L,"return {x='bad',y=2,width=80,height=60}",false,"numeric rectangle");
 providerCase(L,"return {x=1,y=2,width=0,height=60}",false,"invalid hold rectangle");
 providerCase(L,"return {x=1,y=2,width=math.huge,height=60}",false,"invalid hold rectangle");
 providerCase(L,"error('provider failed')",false,"provider failed");
 providerCase(L,"error({})",false,"Lua callback failed");
 assert(lua_gettop(L)==0);
 std::cout<<"hold results: raw fields, errors, invalid values and stack restoration PASS\n";
}

static int originalMotions=0,originalAxes=0;
static void stockMouse(CInputManager*,IPointer::SMotionEvent) {++originalMotions;}
static void stockAxis(CInputManager*,IPointer::SAxisEvent,SP<IPointer>) {++originalAxes;}
static int captureProvider(lua_State* L) {PROTO::capture.active=true;return hold(L);}
static int captureAction(lua_State*) {PROTO::capture.active=true;return 0;}
static void resetCapture() {
 PROTO::capture.active=false;viewflowCaptured=false;viewflowExport=true;plugins.loaded=true;
 nativeSwipe.clear();scrollSwipe.clear();scrollOwner.reset();scrollAxes=0;lastScroll=0;
 rules.clear();paused=false;fakeTime=1000;ticks=holds=originalMotions=originalAxes=0;
 motionHook.m_original=reinterpret_cast<void*>(stockMouse);
 wheelHook.m_original=reinterpret_cast<void*>(stockAxis);
}
static void captureCases(lua_State* L) {
 auto timer=std::make_shared<CEventLoopTimer>();
 for(bool viewflow:{false,true}) {
  auto capture=[&](bool active){if(viewflow)viewflowCaptured=active;else PROTO::capture.active=active;};
  resetCapture();
  run(L,"assert(edge.rule{mode='dwell',dwell_ms=300,on_enter=tick})");
  timerTick(timer,nullptr);assert(ticks==0&&rules[0]->state.candidate);
  capture(true);fakeTime=1301;timerTick(timer,nullptr);
  assert(ticks==0&&!rules[0]->state.candidate&&!rules[0]->state.active);
  capture(false);timerTick(timer,nullptr);assert(ticks==0);
  fakeTime=1602;timerTick(timer,nullptr);assert(ticks==1);
  resetCapture();
  run(L,"assert(edge.rule{mode='enter',on_enter=tick,hold=hold})");
  process();assert(ticks==1&&rules[0]->state.active);
  capture(true);timerTick(timer,nullptr);
  assert(holds==0&&ticks==1&&!rules[0]->state.active);
  assert(!region(*rules[0]));assert(!action(*rules[0],edge::Action::Enter));
  assert(holds==0&&ticks==1);
  resetCapture();
  run(L,"assert(edge.rule{mode='swipe',fingers=2,direction='down',on_enter=tick,consume=true})");
  auto pointer=std::make_shared<IPointer>();
  wheel(nullptr,{60,WL_POINTER_AXIS_VERTICAL_SCROLL,WL_POINTER_AXIS_SOURCE_FINGER},pointer);
  assert(scrollSwipe.captured&&scrollAxes&&originalAxes==0);
  capture(true);
  wheel(nullptr,{5,WL_POINTER_AXIS_VERTICAL_SCROLL,WL_POINTER_AXIS_SOURCE_FINGER},pointer);
  wheel(nullptr,{0,WL_POINTER_AXIS_VERTICAL_SCROLL,WL_POINTER_AXIS_SOURCE_FINGER},pointer);
  mouse(nullptr,{});
  assert(originalAxes==2&&originalMotions==1&&ticks==0);
  assert(!scrollSwipe.captured&&scrollSwipe.entries.empty()&&scrollAxes==0&&scrollOwner.expired());
  // New events while captured must pass through, too.
  wheel(nullptr,{60,WL_POINTER_AXIS_VERTICAL_SCROLL,WL_POINTER_AXIS_SOURCE_FINGER},pointer);
  assert(originalAxes==3&&ticks==0);
  resetCapture();
  run(L,"assert(edge.rule{mode='swipe',fingers=3,direction='right',on_enter=tick,consume=true})");
  Event::SCallbackInfo begin;swipeBegin({3},begin);assert(begin.cancelled&&nativeSwipe.captured);
  Event::SCallbackInfo update;swipeUpdate({{60,0}},update);assert(update.cancelled);
  capture(true);Event::SCallbackInfo end;swipeEnd({false},end);
  assert(!end.cancelled&&ticks==0&&!nativeSwipe.captured&&nativeSwipe.entries.empty());
  Event::SCallbackInfo remoteBegin,remoteUpdate,remoteEnd;
  swipeBegin({3},remoteBegin);swipeUpdate({{60,0}},remoteUpdate);swipeEnd({false},remoteEnd);
  assert(!remoteBegin.cancelled&&!remoteUpdate.cancelled&&!remoteEnd.cancelled&&ticks==0);
  std::cout<<(viewflow?"Viewflow":"native")<<" capture: timer/provider/swipe reset and original mouse/axis forwarding PASS\n";
 }
 resetCapture();
 lua_pushcfunction(L,captureProvider);lua_setglobal(L,"captureProvider");
 lua_pushcfunction(L,captureAction);lua_setglobal(L,"captureAction");
 run(L,"assert(edge.rule{mode='enter',on_enter=tick,on_exit=tick,hold=captureProvider})");
 process();assert(ticks==1);process();
 assert(PROTO::capture.active&&holds==1&&ticks==1&&!rules[0]->state.active&&rules[0]->state.until==0);
 resetCapture();
 run(L,"assert(edge.rule{mode='enter',on_enter=captureAction});assert(edge.rule{mode='enter',on_enter=tick})");
 process();assert(PROTO::capture.active&&ticks==0);
 for(auto& b:rules)assert(!b->state.active&&b->state.until==0);
 resetCapture();viewflowCaptured=true;viewflowExport=false;assert(localInputAllowed());
 viewflowExport=true;plugins.loaded=false;assert(localInputAllowed());
 plugins.loaded=true;assert(!localInputAllowed());
 resetCapture();assert(lua_gettop(L)==0);
 std::cout<<"capture changes during callbacks and Viewflow export lifecycle PASS\n";
}

static void captureQueryCases(lua_State* L) {
 resetCapture();
 run(L,"assert(edge.rule{mode='enter',on_enter=tick,hold=hold});assert(edge.rule{mode='enter',on_enter=tick,hold=hold})");
 auto once=[&](auto call) {
  captureScans=captureLookups=captureQueries=0;
  call();
  assert(captureScans==1&&captureLookups==1&&captureQueries==1);
 };
 once([&]{process();});assert(ticks==2);
 once([&]{process();});assert(holds==2);
 once([&]{mouse(nullptr,{});});
 once([&]{timerTick(std::make_shared<CEventLoopTimer>(),nullptr);});
 once([&]{region(*rules[0]);});
 once([&]{action(*rules[0],edge::Action::Enter);});
 resetCapture();
 run(L,"assert(edge.rule{mode='swipe',fingers=2,direction='down',on_enter=tick,consume=true})");
 auto pointer=std::make_shared<IPointer>();
 once([&]{wheel(nullptr,{60,WL_POINTER_AXIS_VERTICAL_SCROLL,WL_POINTER_AXIS_SOURCE_FINGER},pointer);});
 // One timer invocation calls both process and swipe end on scroll timeout.
 fakeTime+=200;
 once([&]{timerTick(std::make_shared<CEventLoopTimer>(),nullptr);});
 assert(ticks==1);
 resetCapture();
 run(L,"assert(edge.rule{mode='swipe',fingers=3,direction='right',on_enter=tick,consume=true})");
 once([&]{Event::SCallbackInfo info;swipeBegin({3},info);assert(info.cancelled);});
 once([&]{Event::SCallbackInfo info;swipeUpdate({{60,0}},info);assert(info.cancelled);});
 once([&]{Event::SCallbackInfo info;swipeEnd({false},info);assert(info.cancelled);});
 assert(ticks==1);
 once([&]{nativeSwipe.begin(3);});
 once([&]{nativeSwipe.update({60,0});});
 once([&]{nativeSwipe.end(false);});
 // Snapshots must not survive an entry return or cache an optional export.
 resetCapture();viewflowCaptured=true;
 once([&]{process();});
 viewflowCaptured=false;viewflowExport=false;
 captureScans=captureLookups=captureQueries=0;process();
 assert(captureScans==1&&captureLookups==1&&captureQueries==0);
 viewflowExport=true;
 once([&]{process();});
 resetCapture();
 std::cout<<"capture resolution: once per entry across nested rules/process/swipe/timer, no cross-entry cache PASS\n";
}
int main() {
 auto* L=luaL_newstate();assert(L);luaL_openlibs(L);
 lua_newtable(L);lua_pushcfunction(L,addRule);lua_setfield(L,-2,"rule");
 lua_setglobal(L,"edge");
 lua_pushcfunction(L,tick);lua_setglobal(L,"tick");
 lua_pushcfunction(L,hold);lua_setglobal(L,"hold");
 coroutineCase(L);
 providerCases(L);
 captureCases(L);
 captureQueryCases(L);
 rules.clear();lua_close(L);
}
