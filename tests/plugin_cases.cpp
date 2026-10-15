
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
int main() {
 auto* L=luaL_newstate();assert(L);luaL_openlibs(L);
 lua_newtable(L);lua_pushcfunction(L,addRule);lua_setfield(L,-2,"rule");
 lua_setglobal(L,"edge");
 lua_pushcfunction(L,tick);lua_setglobal(L,"tick");
 lua_pushcfunction(L,hold);lua_setglobal(L,"hold");
 coroutineCase(L);
 rules.clear();lua_close(L);
}
